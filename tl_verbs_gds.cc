#include "tl_verbs.cc"

namespace TL
{
	class Verbs_GDS : public Verbs {
		private:
			uint32_t *verbs_gds_client_last_tracked_id_ptr(client_t *client, verbs_request_t req)
			{
			    return &client->last_tracked_id[verbs_type_to_flow((mp_req_type_t)req->type)]; //mp_req_to_flow
			}

			void verbs_gds_client_track_posted_stream_req(client_t *client, verbs_request_t req, mp_flow_t flow)
			{    
			    mp_dbg_msg(oob_rank,"[%d] queuing request: %d req: %p \n", oob_rank, req->id, req);
			    if (!client->posted_stream_req[flow]) {
			        mp_dbg_msg(oob_rank,"setting client[%d]->posted_stream_req[%s]=req=%p req->id=%d\n", 
			                   client->oob_rank, flow==TX_FLOW?"TX":"RX", req, req->id);
			        assert(client->last_posted_stream_req[flow] == NULL);
			        client->posted_stream_req[flow] = client->last_posted_stream_req[flow] = req;
			    } else {
			        // append req to stream list
			        client->last_posted_stream_req[flow]->next = req;

				req->prev = client->last_posted_stream_req[flow];
			        assert(req->next == NULL);

			        client->last_posted_stream_req[flow] = req;
			    }
			}

			void verbs_gds_client_track_waited_stream_req(client_t *client, verbs_request_t req, mp_flow_t flow)
			{
			    const char *flow_str = flow==TX_FLOW?"TX":"RX";
			    // init 1st pending req
			    mp_dbg_msg(oob_rank,"client[%d] req=%p req->id=%d flow=%s\n", client->oob_rank, req, req->id, flow_str);
			    if (!client->waited_stream_req[flow]) {
			        mp_dbg_msg(oob_rank,"setting client[%d]->waited_stream_req[%s]=req=%p req->id=%d\n", 
			                   client->oob_rank, flow_str, req, req->id);
			        assert(client->last_waited_stream_req[flow] == NULL);
			        client->waited_stream_req[flow] = client->last_waited_stream_req[flow] = req;
			    } else {
			        // append req to stream list
			        client->last_waited_stream_req[flow]->next = req;

				req->prev = client->last_waited_stream_req[flow];
				assert(req->next == NULL);

			        client->last_waited_stream_req[flow] = req;
			    }
			}

			//Progress TX_FLOW fix:
			//progress (remove) some requests on the TX flow if is not possible to queue a send request
			int verbs_gds_post_send_async(cudaStream_t stream, client_t *client, verbs_request_t req)
			{
			    int progress_retry=0;
			    int ret = MP_SUCCESS;
			    int ret_progress = MP_SUCCESS;

			    if(!client || !req)
			        return MP_FAILURE;

			    us_t start = mp_get_cycles();
			    us_t tmout = MP_PROGRESS_ERROR_CHECK_TMOUT_US;

			    do
			    {
			        ret = gds_stream_queue_send(stream, client->qp, &req->in.sr, &req->out.bad_sr);
			        if(ret == ENOMEM)
			        {
			            if(qp_query == 0)
			            {
			                verbs_query_print_qp(client->qp, req, 1);
			                qp_query=1;
			            }

			            ret_progress = verbs_progress_single_flow(TX_FLOW);
			            if(ret_progress != MP_SUCCESS)
			            {
			                mp_err_msg(oob_rank, "verbs_progress_single_flow failed. Error: %d\n", ret_progress);
			                ret = ret_progress;
			                break;
			            }
			            ret_progress = verbs_progress_single_flow(RX_FLOW);
			            if(ret_progress != MP_SUCCESS)
			            {
			                mp_err_msg(oob_rank, "verbs_progress_single_flow failed. Error: %d\n", ret_progress);
			                ret = ret_progress;
			                break;
			            }
			            us_t now = mp_get_cycles();
			            long long dt = (long long)now-(long long)start;
			            if (dt > (long long)tmout) {
			                start = now;
			                mp_warn_msg(oob_rank, "TX_FLOW has been blocked for %lld secs, checking for GPU errors\n", dt/1000000);
			                int retcode = verbs_check_gpu_error();
			                if (retcode) {
			                    mp_err_msg(oob_rank, "GPU error %d while progressing TX_FLOW\n", retcode);
			                    ret = retcode;
			                    //mp_enable_dbg(1);
			                    goto out;
			                }        
			                ++progress_retry;
			            }
			        }
			    } while(ret == ENOMEM);
			out:
			    qp_query=0;
			    return ret;
			}

			verbs_request_t verbs_new_request_async(client_t *client, mp_req_type_t type, mp_state_t state) //, struct CUstream_st *stream)
			{
			  verbs_request_t req = verbs_get_request();
			  //mp_dbg_msg(oob_rank, "new req=%p\n", req);
			  if (req) {
			      req->peer = client->oob_rank;
			      req->sgv = NULL;
			      req->next = NULL;
			      req->prev = NULL;
			      req->trigger = 0;
			      req->type = type;
			      req->status = state;
			      req->id = verbs_get_request_id(client, type);
			  }

			  return req;
			}

		public:

			Verbs_GDS() : Verbs() {
#ifndef HAVE_GDSYNC
				fprintf(stderr, "Verbs GDS extension cannot work without LibGDSync library\n");
				exit(EXIT_FAILURE);
#endif
			}
			
			int pt2pt_nb_send_async(void * buf, size_t size, int peer, mp_request_t * mp_req, mp_key_t * mp_key, asyncStream stream)
			{
			    int ret = 0;
				verbs_request_t req = NULL;
				verbs_region_t reg = (verbs_region_t) *mp_key;
			    client_t *client = &clients[client_index[peer]];
			    
			    if (use_event_sync) {
			        req = verbs_new_request_async(client, MP_SEND, MP_PREPARED); //, stream);
			        assert(req);

			        req->in.sr.next = NULL;
			        req->in.sr.exp_send_flags = IBV_EXP_SEND_SIGNALED;
			        req->in.sr.exp_opcode = IBV_EXP_WR_SEND;
			        req->in.sr.wr_id = (uintptr_t) req;
			        req->in.sr.num_sge = 1;
			        req->in.sr.sg_list = &req->sg_entry;

			        if (verbs_enable_ud) {
			            req->in.sr.wr.ud.ah = client->ah;
			            req->in.sr.wr.ud.remote_qpn = client->qpn;
			            req->in.sr.wr.ud.remote_qkey = 0;
			        }

			        req->sg_entry.length = size;
			        req->sg_entry.lkey = reg->key;
			        req->sg_entry.addr = (uintptr_t)(buf);

			        client->last_posted_trigger_id[verbs_type_to_flow((mp_req_type_t)req->type)] = req->id;

			        ret = gds_stream_post_poke_dword(stream, &client->last_trigger_id[verbs_type_to_flow((mp_req_type_t)req->type)], req->id, GDS_MEMORY_HOST);
			        if (ret) {
			            mp_err_msg(oob_rank, "gds_stream_queue_send failed: %s \n", strerror(ret));
			            // BUG: leaking req ??
			            goto out;
			        }

			        verbs_gds_client_track_posted_stream_req(client, req, TX_FLOW);
			    } else {
			        req = verbs_new_request_async(client, MP_SEND, MP_PENDING_NOWAIT); //, stream);
			        assert(req);

			        mp_dbg_msg(oob_rank, "req=%p id=%d\n", req, req->id);

			        req->in.sr.next = NULL;
			        req->in.sr.exp_send_flags = IBV_EXP_SEND_SIGNALED;
			        req->in.sr.exp_opcode = IBV_EXP_WR_SEND;
			        req->in.sr.wr_id = (uintptr_t) req;
			        req->in.sr.num_sge = 1;
			        req->in.sr.sg_list = &(req->sg_entry);

			        if (verbs_enable_ud) {
			            req->in.sr.wr.ud.ah = client->ah;
			            req->in.sr.wr.ud.remote_qpn = client->qpn;
			            req->in.sr.wr.ud.remote_qkey = 0;
			        }

			        req->sg_entry.length = size;
			        req->sg_entry.lkey = reg->key;
			        req->sg_entry.addr = (uintptr_t)(buf);

			        ret = verbs_gds_post_send_async(stream, client, req);
			        if (ret) {
			            mp_err_msg(oob_rank, "verbs_gds_post_send_on_stream failed: %s \n", strerror(ret));
			            // BUG: leaking req ??
			            goto out;
			        }

			        ret = gds_prepare_wait_cq(client->send_cq, &req->gds_wait_info, 0);
			        if (ret) {
			            mp_err_msg(oob_rank, "gds_prepare_wait_cq failed: %s \n", strerror(ret));
			            // BUG: leaking req ??
			            goto out;
			        }
			    }

				*mp_req = (mp_request_t) req; 

			out:
			    return ret;
			}

			//mp_send_on_stream
			int pt2pt_b_send_async(void *buf, int size, int peer, mp_key_t *mp_key,  mp_request_t *mp_req, asyncStream stream)
			{
			    int ret = 0;
				verbs_request_t req = NULL;
				verbs_region_t reg = (verbs_region_t) *mp_key;
				client_t *client = &clients[client_index[peer]];

			    if (use_event_sync) {
			        req = verbs_new_request_async(client, MP_SEND, MP_PREPARED); //, stream);
			        assert(req);

			        req->in.sr.next = NULL;
			        req->in.sr.exp_send_flags = IBV_EXP_SEND_SIGNALED;
			        req->in.sr.exp_opcode = IBV_EXP_WR_SEND;
			        req->in.sr.wr_id = (uintptr_t) req;
			        req->in.sr.num_sge = 1;
			        req->in.sr.sg_list = &req->sg_entry;

			        if (verbs_enable_ud) {
			            req->in.sr.wr.ud.ah = client->ah;
			            req->in.sr.wr.ud.remote_qpn = client->qpn;
			            req->in.sr.wr.ud.remote_qkey = 0;
			        }

			        req->sg_entry.length = size;
			        req->sg_entry.lkey = reg->key;
			        req->sg_entry.addr = (uintptr_t)(buf);
					client->last_posted_trigger_id[verbs_type_to_flow(req->type)] = req->id;

					/*delay posting until stream has reached this id*/
			        ret = gds_stream_post_poke_dword(stream,
			                &client->last_trigger_id[verbs_type_to_flow(req->type)],
			                req->id,
			                GDS_MEMORY_HOST);
			        if (ret) {
			            mp_err_msg(oob_rank, "gds_stream_queue_send failed: %s \n", strerror(ret));
			            // BUG: leaking req ??
			            goto out;
			        }
			        
			        verbs_gds_client_track_posted_stream_req(client, req, TX_FLOW);

					/*block stream on completion of this req*/
			        if ((int)client->last_posted_tracked_id[TX_FLOW] < req->id) {
			            req->trigger = 1;
			            client->last_posted_tracked_id[TX_FLOW] = req->id;
			            ret = gds_stream_post_poll_dword(stream,
			                &client->last_tracked_id[verbs_type_to_flow(req->type)],
			                req->id,
			                GDS_WAIT_COND_GEQ,
			                GDS_MEMORY_HOST);
			            if (ret) {
			                mp_err_msg(oob_rank, "gds_stream_queue_send failed: %s \n", strerror(ret));
			                goto out;
			            }
			        }
			    } else {
			        req = verbs_new_request_async(client, MP_SEND, MP_PENDING); //, stream);

			        assert(req);
			        req->in.sr.next = NULL;
			        req->in.sr.exp_send_flags = IBV_EXP_SEND_SIGNALED;
			        req->in.sr.exp_opcode = IBV_EXP_WR_SEND;
			        req->in.sr.wr_id = (uintptr_t) req;
			        req->in.sr.num_sge = 1;
			        req->in.sr.sg_list = &(req->sg_entry);

			        if (verbs_enable_ud) {
			            req->in.sr.wr.ud.ah = client->ah;
			            req->in.sr.wr.ud.remote_qpn = client->qpn;
			            req->in.sr.wr.ud.remote_qkey = 0;
			        }

			        req->sg_entry.length = size;
			        req->sg_entry.lkey = reg->key;
			        req->sg_entry.addr = (uintptr_t)(buf);

			        ret = gds_stream_queue_send(stream, client->qp, &req->in.sr, &req->out.bad_sr);
			        if (ret) {
			            mp_err_msg(oob_rank, "gds_stream_queue_rsend failed: %s \n", strerror(ret));
			            // BUG: leaking req ??
			            goto out;
			        }

			        ret = gds_stream_wait_cq(stream, client->send_cq, 0);
			        if (ret) {
			            mp_err_msg(oob_rank, "gds_stream_wait_cq failed: %s \n", strerror(ret));
			            // BUG: leaking req ??
			            goto out;
			        }
			    }

				*mp_req = (mp_request_t) req; 

			out:
			    return ret; 
			}

			//mp_send_prepare
			int pt2pt_send_prepare(void *buf, int size, int peer, mp_key_t *mp_key, mp_request_t *mp_req)
			{
			    int ret = 0;
			    verbs_request_t req;
			    verbs_region_t reg = (verbs_region_t )*mp_key;
			    client_t *client = &clients[client_index[peer]];
			  
			    req = verbs_new_request_async(client, MP_SEND, MP_PREPARED); //, stream);
			    assert(req);

			    mp_dbg_msg(oob_rank, "Preparing send message, req->id=%d \n", req->id);

			    if (use_event_sync) {
			        req->in.sr.next = NULL;
			        req->in.sr.exp_send_flags = IBV_EXP_SEND_SIGNALED;
			        req->in.sr.exp_opcode = IBV_EXP_WR_SEND;
			        req->in.sr.wr_id = (uintptr_t) req;
			        req->in.sr.num_sge = 1;
			        req->in.sr.sg_list = &req->sg_entry;

			        if (verbs_enable_ud) {
			            req->in.sr.wr.ud.ah = client->ah;
			            req->in.sr.wr.ud.remote_qpn = client->qpn;
			            req->in.sr.wr.ud.remote_qkey = 0;
			        }

			        req->sg_entry.length = size;
			        req->sg_entry.lkey = reg->key;
			        req->sg_entry.addr = (uintptr_t)(buf);
			    } else {
			        req->in.sr.next = NULL;
			        req->in.sr.exp_send_flags = IBV_EXP_SEND_SIGNALED;
			        req->in.sr.exp_opcode = IBV_EXP_WR_SEND;
			        req->in.sr.wr_id = (uintptr_t) req;
			        req->in.sr.num_sge = 1;
			        req->in.sr.sg_list = &(req->sg_entry);

			        if (verbs_enable_ud) {
			            req->in.sr.wr.ud.ah = client->ah;
			            req->in.sr.wr.ud.remote_qpn = client->qpn;
			            req->in.sr.wr.ud.remote_qkey = 0;

			            mp_err_msg(oob_rank, "[%d] posintg send to qpn %d size: %d req: %p \n", oob_rank, req->in.sr.wr.ud.remote_qpn, size, req);
			        }

			        req->sg_entry.length = size;
			        req->sg_entry.lkey = reg->key;
			        req->sg_entry.addr = (uintptr_t)(buf);
			        
			        ret = gds_prepare_send(client->qp, &req->in.sr,
			                               &req->out.bad_sr, &req->gds_send_info);
			        //ret = mp_prepare_send(client, req);
			        if (ret) {
			            mp_err_msg(oob_rank, "mp_prepare_send failed: %s \n", strerror(ret));
			            // BUG: leaking req ??
			            goto out;
			        }

			        ret = gds_prepare_wait_cq(client->send_cq, &req->gds_wait_info, 0);
			        if (ret) {
			            mp_err_msg(oob_rank, "gds_prepare_wait_cq failed: %s \n", strerror(ret));
			            // BUG: leaking req ??
			            goto out;
			        }
			    }

				*mp_req = (mp_request_t) req; 
			out:
			    return ret;
			}

			//mp_send_post_on_stream
			int pt2pt_b_send_post_async(mp_request_t *mp_req, asyncStream stream)
			{
			    int ret = 0; 
			    verbs_request_t req = (verbs_request_t) *mp_req;

			    assert(req->status == MP_PREPARED);
			    client_t *client = &clients[client_index[req->peer]];
				req->stream = stream;

				if (use_event_sync) {

				    if (req->id <= (int)client->last_posted_trigger_id[verbs_type_to_flow(req->type)]) {
				         mp_err_msg(oob_rank, "postd order is different from prepared order, last posted: %d being posted: %d \n",
				                    client->last_posted_trigger_id[verbs_type_to_flow(req->type)], req->id);
				         ret = MP_FAILURE;
				         goto out;
				    }
				    client->last_posted_trigger_id[verbs_type_to_flow(req->type)] = req->id;

					/*delay posting until stream has reached this id*/
				    ret = gds_stream_post_poke_dword(stream,
				            &client->last_trigger_id[verbs_type_to_flow(req->type)],
				            req->id,
				            GDS_MEMORY_HOST);
				    if (ret) {
				        mp_err_msg(oob_rank, "gds_stream_queue_send failed: %s \n", strerror(ret));
				        // BUG: leaking req ??
				        goto out;
				    }
				    
				    verbs_gds_client_track_posted_stream_req(client, req, TX_FLOW);

					/*block stream on completion of this req*/
				    if ((int)client->last_posted_tracked_id[TX_FLOW] < req->id) {
				        req->trigger = 1;

				        client->last_posted_tracked_id[TX_FLOW] = req->id;

				        ret = gds_stream_post_poll_dword(stream,
				            &client->last_tracked_id[verbs_type_to_flow(req->type)],
				            req->id,
				            GDS_WAIT_COND_GEQ,
				            GDS_MEMORY_HOST);
				        if (ret) {
				            mp_err_msg(oob_rank, "gds_stream_queue_send failed: %s \n", strerror(ret));
				            goto out;
				        }
				    }
				} else { 	
				    req->status = MP_PENDING;

				    ret = gds_stream_post_send(stream, &req->gds_send_info);
				    if (ret) {
				        mp_err_msg(oob_rank, "gds_stream_post_send failed: %s \n", strerror(ret));
				        // BUG: leaking req ??
				        goto out;
				    }

				    ret = gds_stream_post_wait_cq(stream, &req->gds_wait_info);
				    if (ret) {
				        mp_err_msg(oob_rank, "gds_stream_post_wait failed: %s \n", strerror(ret));
				        // bug: leaking req ??
				        goto out;
				    }
				}
			out:
			    return ret;
			}

			//mp_send_post_all_on_stream
			int pt2pt_b_send_post_all_async(int count, mp_request_t *mp_req, asyncStream stream)
			{
			    int i, ret = 0, tot_local_reqs = 8; //??
				if (use_event_sync)
				{
				   for (i=0; i<count; i++) 
				   {
						verbs_request_t req = (verbs_request_t) mp_req[i];
						client_t *client = &clients[client_index[req->peer]];

						/*BUG: can requests passed to post_all be differnt from the order they were prepared?*/
						if (req->id <= (int)client->last_posted_trigger_id[verbs_type_to_flow(req->type)]) {
							mp_err_msg(oob_rank, "postd order is different from prepared order, last posted: %d being posted: %d \n",
							          client->last_posted_trigger_id[verbs_type_to_flow(req->type)], req->id);
							ret = MP_FAILURE;
							goto out;
						}
						client->last_posted_trigger_id[verbs_type_to_flow(req->type)] = req->id;
						mp_dbg_msg(oob_rank, "[%d] posting dword on stream:%p id:%d \n", oob_rank, stream, req->id);
						/*delay posting until stream has reached this id*/
						ret = gds_stream_post_poke_dword(stream,
						                                &client->last_trigger_id[verbs_type_to_flow(req->type)],
						                                req->id,
						                                GDS_MEMORY_HOST);
						if (ret) {
						   mp_err_msg(oob_rank, "gds_stream_queue_send failed: %s \n", strerror(ret));
						   // BUG: leaking req ??
						   goto out;
						}
			           
						verbs_gds_client_track_posted_stream_req(client, req, TX_FLOW);

						/*block stream on completion of this req*/
						if ((int)client->last_posted_tracked_id[TX_FLOW] < req->id) {
						   req->trigger = 1;

						   client->last_posted_tracked_id[TX_FLOW] = req->id;

						   mp_dbg_msg(oob_rank, "[%d] posting poll on stream:%p id:%d \n", oob_rank, stream, req->id);

						   ret = gds_stream_post_poll_dword(stream,
						       &client->last_tracked_id[verbs_type_to_flow(req->type)],
						       req->id,
						       GDS_WAIT_COND_GEQ,
						       GDS_MEMORY_HOST);
						   if (ret) {
						       mp_err_msg(oob_rank, "gds_stream_queue_send failed: %s \n", strerror(ret));
						       goto out;
						   }
						}
					}
			    }
			    else {	
			       gds_send_request_t gds_send_request_local[tot_local_reqs];
			       gds_send_request_t *gds_send_request;
			       gds_wait_request_t gds_wait_request_local[tot_local_reqs];
			       gds_wait_request_t *gds_wait_request;

			       if (count <= tot_local_reqs) {
			           gds_send_request = gds_send_request_local;
			           gds_wait_request = gds_wait_request_local;
			       } else {
			           gds_send_request = (gds_send_request_t *) calloc(count, sizeof(*gds_send_request));
			           gds_wait_request = (gds_wait_request_t *) calloc(count, sizeof(*gds_wait_request));
			       }

			       for (i=0; i<count; i++) {
			           verbs_request_t req = (verbs_request_t) mp_req[i];
			           assert(req->status == MP_PREPARED);

			           gds_send_request[i] = req->gds_send_info;
			           gds_wait_request[i] = req->gds_wait_info;
			       }

			       ret = gds_stream_post_send_all(stream, count, gds_send_request);
			       if (ret) {
			           mp_err_msg(oob_rank, "gds_stream_post_send_all failed: %s \n", strerror(ret));
			           // BUG: leaking req ??
			           goto out;
			       }

			       ret = gds_stream_post_wait_cq_all(stream, count, gds_wait_request);
			       if (ret) {
			           mp_err_msg(oob_rank, "gds_stream_post_wait_all failed: %s \n", strerror(ret));
			           // BUG: leaking req ??
			           goto out;
			       }

			       if (count > tot_local_reqs) {
			           free(gds_send_request);
			           free(gds_wait_request);
			       }
			    }
			out:
			    return ret;
			}

			//mp_isend_post_on_stream
			int pt2pt_nb_send_post_async(mp_request_t *mp_req, asyncStream stream)
			{
			    int retcode;
			    int ret = 0; 
			    verbs_request_t req = (verbs_request_t) *mp_req;

			    assert(req->status == MP_PREPARED);

			    req->stream = stream;

				if (use_event_sync) {
					client_t *client = &clients[client_index[(int)req->peer]];

					if (req->id <= (int)client->last_posted_trigger_id[verbs_type_to_flow(req->type)]) {
					     mp_err_msg(oob_rank, "postd order is different from prepared order, last posted: %d being posted: %d \n",
					                client->last_posted_trigger_id[verbs_type_to_flow(req->type)], req->id);
					     ret = MP_FAILURE;
					     goto out;
					}
					client->last_posted_trigger_id[verbs_type_to_flow(req->type)] = req->id;

					/*delay posting until stream has reached this id*/
					retcode = gds_stream_post_poke_dword(stream,
					                                     &client->last_trigger_id[verbs_type_to_flow(req->type)],
					                                     req->id,
					                                     GDS_MEMORY_HOST);
					if (retcode) {
					    mp_err_msg(oob_rank, "error %s\n", strerror(retcode));
					    ret = MP_FAILURE;
					    // BUG: leaking req ??
					    goto out;
					}

					verbs_gds_client_track_posted_stream_req(client, req, TX_FLOW);
			    } else {
			        req->status = MP_PENDING_NOWAIT;

			        mp_dbg_msg(oob_rank, " Entering \n");

			        ret = gds_stream_post_send(stream, &req->gds_send_info);
			        if (ret) {
			            mp_err_msg(oob_rank, "gds_stream_post_send failed: %s \n", strerror(ret));
			            goto out;
			        }
			    }

			    mp_dbg_msg(oob_rank, " Leaving \n");

			out:
			    return ret;
			}

			//mp_isend_post_all_on_stream
			int pt2pt_nb_send_post_all_async(int count, mp_request_t *mp_req, asyncStream stream)
			{
			    int ret = 0, i, tot_local_reqs=8;
			    gds_send_request_t gds_send_request_local[tot_local_reqs];
			    gds_send_request_t *gds_send_request;

			    mp_dbg_msg(oob_rank, " Entering \n");

			    if (use_event_sync) {
			       for (i=0; i<count; i++) {
						verbs_request_t req = (verbs_request_t) mp_req[i];
						client_t *client = &clients[client_index[req->peer]];

						/*BUG: can requests passed to post_all be differnt from the order they were prepared?*/
						if (req->id <= (int)client->last_posted_trigger_id[verbs_type_to_flow(req->type)]) {
						    mp_err_msg(oob_rank, "posted order is different from prepared order, last posted: %d being posted: %d \n",
						               client->last_posted_trigger_id[verbs_type_to_flow(req->type)], req->id);
						    ret = MP_FAILURE;
						    goto out;
						}
						client->last_posted_trigger_id[verbs_type_to_flow(req->type)] = req->id;

						mp_dbg_msg(oob_rank, "[%d] posting dword on stream:%p id:%d \n", oob_rank, stream, req->id);

						/*delay posting until stream has reached this id*/
						ret = gds_stream_post_poke_dword(stream,
						       &client->last_trigger_id[verbs_type_to_flow(req->type)],
						       req->id,
						       GDS_MEMORY_HOST);
						if (ret) {
						   mp_err_msg(oob_rank, "error while posting pokes %d/%s \n", ret, strerror(ret));
						   // BUG: leaking req ??
						   ret = MP_FAILURE;
						   goto out;
						}

						verbs_gds_client_track_posted_stream_req(client, req, TX_FLOW);
					}
			    } else { 	
					if (count <= tot_local_reqs) {
					    gds_send_request = gds_send_request_local;
					} else {
					    gds_send_request = (gds_send_request_t *) calloc(count, sizeof(*gds_send_request));
					}

					for (i=0; i<count; i++) {
					    verbs_request_t req = (verbs_request_t) mp_req[i];

					    assert(req->status == MP_PREPARED);

					    req->stream = stream;
					    req->status = MP_PENDING_NOWAIT;

					    gds_send_request[i] = req->gds_send_info;
					}

					ret = gds_stream_post_send_all(stream, count, gds_send_request);
					if (ret) {
					    mp_err_msg(oob_rank, "gds_stream_post_send_all failed: %s \n", strerror(ret));
					    // BUG: leaking req ??
					    goto out;
					}

					if (count > tot_local_reqs) {
					    free(gds_send_request);
					}
			    }

			    mp_dbg_msg(oob_rank, " Leaving \n");
			 
			out:
			    return ret;
			}


			int wait_async(mp_request_t *mp_req, asyncStream stream)
			{
				return wait_all_async(1, mp_req, stream);
			}

			int wait_all_async(int count, mp_request_t *mp_req, asyncStream stream)
			{
			    int i, ret = 0;

			    if (!count)
			        return MP_FAILURE;

			    if (use_event_sync)
			    {
					for (i=0; i<count; i++)
					{
						verbs_request_t req = (verbs_request_t) mp_req[i];
						client_t *client = &clients[client_index[req->peer]];

						assert (req->type > 0);
						assert (req->status <= MP_PENDING);

						if ((int)client->last_posted_tracked_id[verbs_type_to_flow(req->type)] < req->id)
						{
							client->last_posted_tracked_id[verbs_type_to_flow(req->type)] = req->id;
							req->trigger = 1;

							ret = gds_stream_post_poll_dword(stream,
							    &client->last_tracked_id[verbs_type_to_flow(req->type)],
							    req->id,
							    GDS_WAIT_COND_GEQ,
							    GDS_MEMORY_HOST);
							if (ret) {
							    mp_err_msg(oob_rank, "gds_stream_queue_send failed: %s \n", strerror(ret));
							    goto out;
							}
						}
					}
			    } else {
			    	int tot_local_reqs=8;
			        gds_wait_request_t gds_wait_request_local[tot_local_reqs];
			        gds_wait_request_t *gds_wait_request;

			        mp_dbg_msg(oob_rank, "count=%d\n", count);

			        if (count <= tot_local_reqs) {
			            gds_wait_request = gds_wait_request_local;
			        } else {
			           gds_wait_request = (gds_wait_request_t *) calloc(count, sizeof(*gds_wait_request));
			        }

			        for (i=0; i<count; i++) {
			            verbs_request_t req = (verbs_request_t) mp_req[i];
			            mp_dbg_msg(oob_rank, "posting wait cq req=%p id=%d\n", req, req->id);

			            if (!verbs_req_can_be_waited(req)) {
			                mp_dbg_msg(oob_rank, "cannot wait req:%p status:%d id=%d peer=%d type=%d flags=%08x\n", req, req->status, req->id, req->peer, req->type, req->flags);
			                ret = EINVAL;
			                goto out;
			            }

			            // cannot check wait-for-end more than once
			            assert(req->status == MP_PENDING_NOWAIT || req->status == MP_COMPLETE);
			            //assert(req->status == MP_PENDING_NOWAIT);

			            req->stream = stream;
			            req->status = MP_PENDING;
			            gds_wait_request[i] = req->gds_wait_info;
			        }

			        ret = gds_stream_post_wait_cq_all(stream, count, gds_wait_request);
			        if (ret) {
			            mp_err_msg(oob_rank, "gds_stream_post_wait_all failed: %s \n", strerror(ret));
			            goto out;
			        }

			        if (count > tot_local_reqs) {
			            free(gds_wait_request);
			        }
			    }

			out:
			    return ret;
			}

			int onesided_nb_put_async(void *src, int size, mp_key_t *mp_key, int peer, size_t displ,
                       mp_window_t *window_t, mp_request_t *mp_req, int flags, asyncStream stream)
			{
				int ret = 0;
				verbs_request_t req = NULL;
				verbs_region_t reg = (verbs_region_t) *mp_key;
				verbs_window_t window = (verbs_window_t) *window_t;
				int client_id = client_index[peer];
				client_t *client = &clients[client_id];

				if (verbs_enable_ud) { 
					mp_err_msg(oob_rank, "put/get not supported with UD \n");
					ret = MP_FAILURE;
					goto out;
				}

				assert(displ < window->rsize[client_id]);
				req = verbs_new_request_async(client, MP_RDMA, MP_PENDING_NOWAIT); //, stream);
				assert(req);

				mp_dbg_msg(oob_rank, "req=%p id=%d\n", req, req->id);

				req->flags = flags;
				req->in.sr.next = NULL;
				if (flags & MP_PUT_NOWAIT) {
				  mp_dbg_msg(oob_rank, "MP_PUT_NOWAIT set\n");
				  req->in.sr.exp_send_flags = 0;
				} else {
				  req->in.sr.exp_send_flags = IBV_EXP_SEND_SIGNALED;
				}
				if (flags & MP_PUT_INLINE) {
				  mp_dbg_msg(oob_rank, "setting SEND_INLINE flag\n");
				  req->in.sr.exp_send_flags |= IBV_EXP_SEND_INLINE;
				}
				req->in.sr.exp_opcode = IBV_EXP_WR_RDMA_WRITE;
				req->in.sr.wr_id = (uintptr_t) req;
				req->in.sr.num_sge = 1;
				req->in.sr.sg_list = &req->sg_entry;

				req->sg_entry.length = size;
				req->sg_entry.lkey = reg->key;
				req->sg_entry.addr = (uintptr_t)src;

				req->in.sr.wr.rdma.remote_addr = ((uint64_t)window->base_ptr[client_id]) + displ;
				req->in.sr.wr.rdma.rkey = window->rkey[client_id];

				ret = verbs_gds_post_send_async(stream, client, req);
				if (ret) {
				  mp_err_msg(oob_rank, "gds_stream_queue_send failed: err=%d(%s) \n", ret, strerror(ret));
				  // BUG: leaking req ??
				  goto out;
				}

				if (flags & MP_PUT_NOWAIT) {
				  req->status = MP_COMPLETE;
				} else {
				  ret = gds_prepare_wait_cq(client->send_cq, &req->gds_wait_info, 0);
				  if (ret) {
				      mp_err_msg(oob_rank, "gds_prepare_wait_cq failed: %s \n", strerror(ret));
				      // BUG: leaking req ??
				      goto out;
				  }
				}
				*mp_req = req;

			out:
				// free req
				if (ret) { 
					if (req) verbs_release_request((verbs_request_t) req);
				}

				return ret;
			}

			int onesided_nb_get_async(void *dst, int size, mp_key_t *mp_key, int peer, size_t displ,
			        mp_window_t *window_t, mp_request_t *mp_req, asyncStream stream)
			{

				int ret = 0;
				verbs_request_t req = NULL;
				verbs_region_t reg = (verbs_region_t) *mp_key;
				verbs_window_t window = (verbs_window_t) *window_t;
				int client_id = client_index[peer];
				client_t *client = &clients[client_id];

				if (verbs_enable_ud) { 
					mp_err_msg(oob_rank, "put/get not supported with UD \n");
					ret = MP_FAILURE;
					goto out;
				}

				assert(displ < window->rsize[client_id]);

				req = verbs_new_request_async(client, MP_RDMA, MP_PENDING_NOWAIT); //, stream);
				assert(req);

				req->in.sr.next = NULL;
				req->in.sr.exp_send_flags = IBV_EXP_SEND_SIGNALED;
				req->in.sr.exp_opcode = IBV_EXP_WR_RDMA_READ;
				req->in.sr.wr_id = (uintptr_t) req;
				req->in.sr.num_sge = 1;
				req->in.sr.sg_list = &req->sg_entry;

				req->sg_entry.length = size;
				req->sg_entry.lkey = reg->key;
				req->sg_entry.addr = (uintptr_t)dst;

				req->in.sr.wr.rdma.remote_addr = ((uint64_t)window->base_ptr[client_id]) + displ;
				req->in.sr.wr.rdma.rkey = window->rkey[client_id];

				ret = gds_stream_queue_send(stream, client->qp, &req->in.sr, &req->out.bad_sr);
				if (ret) {
				  mp_err_msg(oob_rank, "gds_stream_queue_send failed: %s \n", strerror(ret));
				  goto out;
				}

				ret = gds_prepare_wait_cq(client->send_cq, &req->gds_wait_info, 0);
				if (ret) {
					mp_err_msg(oob_rank, "gds_prepare_wait_cq failed: %s \n", strerror(ret));
					goto out;
				}

				*mp_req = req;

				out:
					return ret;
			}

			int onesided_put_prepare (void *src, int size, mp_key_t *mp_key, int peer, size_t displ,
                    mp_window_t *window_t, mp_request_t *mp_req, int flags)
			{
				int ret = 0;
				verbs_request_t req = NULL;
				verbs_region_t reg = (verbs_region_t) *mp_key;
				verbs_window_t window = (verbs_window_t) *window_t;
				int client_id = client_index[peer];
				client_t *client = &clients[client_id];

				if (verbs_enable_ud) { 
					mp_err_msg(oob_rank, "put/get not supported with UD \n");
					ret = MP_FAILURE;
					goto out;
				}

				assert(displ < window->rsize[client_id]);
				req = verbs_new_request_async(client, MP_RDMA, MP_PREPARED); //, NULL);
				assert(req);

				mp_dbg_msg(oob_rank, "req=%p id=%d\n", req, req->id);

				req->flags = flags;
				req->in.sr.next = NULL;
				if (flags & MP_PUT_NOWAIT) {
					mp_dbg_msg(oob_rank, "MP_PUT_NOWAIT set\n");
					req->in.sr.exp_send_flags = 0;
				} else {
					req->in.sr.exp_send_flags = IBV_EXP_SEND_SIGNALED;
				}

				if (flags & MP_PUT_INLINE) {
					mp_dbg_msg(oob_rank, "setting SEND_INLINE flag\n");
					req->in.sr.exp_send_flags |= IBV_EXP_SEND_INLINE;
				}

				req->in.sr.exp_opcode = IBV_EXP_WR_RDMA_WRITE;
				req->in.sr.wr_id = (uintptr_t) req;
				req->in.sr.num_sge = 1;
				req->in.sr.sg_list = &req->sg_entry;

				req->sg_entry.length = size;
				req->sg_entry.lkey = reg->key;
				req->sg_entry.addr = (uintptr_t)src;

				req->in.sr.wr.rdma.remote_addr = ((uint64_t)window->base_ptr[client_id]) + displ;
				req->in.sr.wr.rdma.rkey = window->rkey[client_id];

				ret = gds_prepare_send(client->qp, &req->in.sr, &req->out.bad_sr, &req->gds_send_info);
				if (ret) {
				  mp_err_msg(oob_rank, "error %d in mp_prepare_send: %s \n", ret, strerror(ret));
				  goto out;
				}

				if (flags & MP_PUT_NOWAIT) {
				  //req->status = MP_COMPLETE;
				} else {
				  ret = gds_prepare_wait_cq(client->send_cq, &req->gds_wait_info, 0);
				  if (ret) {
				      mp_err_msg(oob_rank, "error %d gds_prepare_wait_cq failed: %s \n", ret, strerror(ret));
				      goto out;
				  }
				}

				*mp_req = (mp_request_t) req;

				out:
				if (ret) { 
					// free req
					if (req) verbs_release_request((verbs_request_t) req);
				}
				  
				return ret;
			}

			int onesided_nb_put_post_async(mp_request_t *mp_req, asyncStream stream)
			{
				int ret = 0; 
				verbs_request_t req = (verbs_request_t) *mp_req;

				assert(req->status == MP_PREPARED);

				mp_dbg_msg(oob_rank, "req=%p id=%d\n", req, req->id);

				ret = gds_stream_post_send(stream, &req->gds_send_info);
				if (ret) {
					mp_err_msg(oob_rank, "gds_stream_post_send failed: %s \n", strerror(ret));
					goto out;
				}

				if (req->flags & MP_PUT_NOWAIT) {
					req->status = MP_COMPLETE;
				} else {
					req->status = MP_PENDING_NOWAIT;
				}

			out:
			    return ret;
			}

			int onesided_nb_put_post_all_async (int count, mp_request_t *mp_req, asyncStream stream)
			{
			    int i, ret = 0;
			    gds_send_request_t gds_send_request_local[8];
			    gds_send_request_t *gds_send_request = NULL;

			    mp_dbg_msg(oob_rank, "count=%d\n", count);

			    if (count <= 8) {
			        gds_send_request = gds_send_request_local;
			    } else {
					gds_send_request = (gds_send_request_t *) calloc(count, sizeof(*gds_send_request));

			        if (!gds_send_request) {
			            mp_err_msg(oob_rank, "cannot allocate memory\n");
			            ret = ENOMEM;
			            goto out;
			        }
			    }

			    for (i=0; i<count; i++) {
			        verbs_request_t req = (verbs_request_t) mp_req[i];

			        assert(req->status == MP_PREPARED);
			        req->status = MP_PENDING_NOWAIT;

			        mp_dbg_msg(oob_rank, "posting send for req=%p id=%d\n", req, req->id);

			        gds_send_request[i] = req->gds_send_info;
			    }

			    ret = gds_stream_post_send_all(stream, count, gds_send_request);
			    if (ret) {
			        mp_err_msg(oob_rank, "error %d(%s) in gds_stream_post_send_all\n", ret, strerror(ret));
			    }

			    if (count > 8) {
			        free(gds_send_request);
			    }
			out:
			    return ret;
			}

			int wait_word_async(uint32_t *ptr, uint32_t value, int flags, asyncStream stream)
			{
			    int ret = MP_SUCCESS;
			    gds_wait_cond_flag_t cond_flags = GDS_WAIT_COND_GEQ;

			    mp_dbg_msg(oob_rank, "ptr=%p value=%d\n", ptr, value);

			    switch(flags) {
			    case VERBS_WAIT_EQ:  cond_flags = GDS_WAIT_COND_GEQ; break;
			    case VERBS_WAIT_GEQ: cond_flags = GDS_WAIT_COND_GEQ; break;
			    case VERBS_WAIT_AND: cond_flags = GDS_WAIT_COND_GEQ; break;
			    default: ret = EINVAL; goto out; break;
			    }

			    ret = gds_stream_post_poll_dword(stream, ptr, value, cond_flags, GDS_MEMORY_HOST|GDS_WAIT_POST_FLUSH);
			    if (ret) {
			        mp_err_msg(oob_rank, "error %d while posting poll on ptr=%p value=%08x flags=%08x\n", ret, ptr, value, flags);
			    }
			out:
			    return ret;
			}
	};
}


static TL::Communicator *create_gds() { return new TL::Verbs_GDS(); }

static class update_tl_list_gds {
	public: 
		update_tl_list_gds() {
			add_tl_creator(TL_INDEX_VERBS_GDS, create_gds);
		}
} list_tl_gds;

#if 0
static int mp_prepare_send(client_t *client, verbs_request_t req)
int (void *buf, int size, int peer, mp_key_t *mp_key, mp_request_t *req_t)
int mp_send_on_stream (void *buf, int size, int peer, mp_key_t *mp_key, 
		mp_request_t *req_t, asyncStream stream)
int mp_isendv_on_stream (struct iovec *v, int nvecs, int peer, mp_key_t *mp_key,
                         mp_request_t *req_t, asyncStream stream)
int mp_sendv_prepare(struct iovec *v, int nvecs, int peer, mp_key_t *mp_key, mp_request_t *req_t)
int mp_send_post_on_stream (mp_request_t *req_t, asyncStream stream)
int mp_send_post_all_on_stream (int count, mp_request_t *req_t, asyncStream stream)

int mp_put_prepare (void *src, int size, mp_key_t *mp_key, int peer, size_t displ,
                    mp_window_t *window_t, mp_request_t *req_t, int flags)
#endif