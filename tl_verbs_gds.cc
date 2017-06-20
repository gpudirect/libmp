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
			int verbs_post_send_on_stream(cudaStream_t stream, client_t *client, verbs_request_t req)
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
			                mp_err_msg(oob_rank, "mp_progress_single_flow failed. Error: %d\n", ret_progress);
			                ret = ret_progress;
			                break;
			            }
			            ret_progress = verbs_progress_single_flow(RX_FLOW);
			            if(ret_progress != MP_SUCCESS)
			            {
			                mp_err_msg(oob_rank, "mp_progress_single_flow failed. Error: %d\n", ret_progress);
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


		public:

			int funzione_casuale() {
				return 1;
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
        ret = gds_prepare_send(client->qp, &req->in.sr, &req->out.bad_sr, &req->gds_send_info);
        if(ret == ENOMEM)
        {
            ret_progress = mp_progress_single_flow(TX_FLOW);
            if(ret_progress != MP_SUCCESS)
            {
                mp_err_msg("mp_progress_single_flow failed. Error: %d\n", ret_progress);
                ret = ret_progress;
                break;
            }
            ret_progress = mp_progress_single_flow(RX_FLOW);
            if(ret_progress != MP_SUCCESS)
            {
                mp_err_msg("mp_progress_single_flow failed. Error: %d\n", ret_progress);
                ret = ret_progress;
                break;
            }
            us_t now = mp_get_cycles();
            long long dt = (long long)now-(long long)start;
            if (dt > (long long)tmout) {
                start = now;
                mp_warn_msg("TX_FLOW has been blocked for %lld secs, checking for GPU errors\n", dt/1000000);
                int retcode = mp_check_gpu_error();
                if (retcode) {
                    mp_err_msg("GPU error %d while progressing TX_FLOW\n", retcode);
                    ret = retcode;
                    mp_enable_dbg(1);
                    goto out;
                }        
                ++progress_retry;
            }
        }
    } while(ret == ENOMEM);
out:
    return ret;
}

int mp_send_on_stream (void *buf, int size, int peer, mp_reg_t *reg_t, 
		mp_request_t *req_t, cudaStream_t stream)
{
    int ret = 0;
    verbs_request_t req = NULL;
    struct mp_reg *reg = (struct mp_reg *)*reg_t;
    client_t *client = &clients[client_index[peer]];

    if (use_event_sync) {
        req = new_stream_request(client, MP_SEND, MP_PREPARED, stream);
        assert(req);

        req->in.sr.next = NULL;
        req->in.sr.exp_send_flags = IBV_EXP_SEND_SIGNALED;
        req->in.sr.exp_opcode = IBV_EXP_WR_SEND;
        req->in.sr.wr_id = (uintptr_t) req;
        req->in.sr.num_sge = 1;
        req->in.sr.sg_list = &req->sg_entry;

        if (mp_enable_ud) {
            req->in.sr.wr.ud.ah = client->ah;
            req->in.sr.wr.ud.remote_qpn = client->qpn;
            req->in.sr.wr.ud.remote_qkey = 0;
        }

        req->sg_entry.length = size;
        req->sg_entry.lkey = reg->key;
        req->sg_entry.addr = (uintptr_t)(buf);

	client->last_posted_trigger_id[mp_type_to_flow(req->type)] = req->id;

	/*delay posting until stream has reached this id*/
        ret = gds_stream_post_poke_dword(stream,
                &client->last_trigger_id[mp_type_to_flow(req->type)],
                req->id,
                GDS_MEMORY_HOST);
        if (ret) {
            mp_err_msg(oob_rank, "gds_stream_queue_send failed: %s \n", strerror(ret));
            // BUG: leaking req ??
            goto out;
        }
        
        client_track_posted_stream_req(client, req, TX_FLOW);

	/*block stream on completion of this req*/
        if (client->last_posted_tracked_id[TX_FLOW] < req->id) {
            req->trigger = 1;

            client->last_posted_tracked_id[TX_FLOW] = req->id;

            ret = gds_stream_post_poll_dword(stream,
                &client->last_tracked_id[mp_type_to_flow(req->type)],
                req->id,
                GDS_WAIT_COND_GEQ,
                GDS_MEMORY_HOST);
            if (ret) {
                mp_err_msg(oob_rank, "gds_stream_queue_send failed: %s \n", strerror(ret));
                goto out;
            }
        }
    } else {
        req = new_stream_request(client, MP_SEND, MP_PENDING, stream);

        assert(req);
        req->in.sr.next = NULL;
        req->in.sr.exp_send_flags = IBV_EXP_SEND_SIGNALED;
        req->in.sr.exp_opcode = IBV_EXP_WR_SEND;
        req->in.sr.wr_id = (uintptr_t) req;
        req->in.sr.num_sge = 1;
        req->in.sr.sg_list = &(req->sg_entry);

        if (mp_enable_ud) {
            req->in.sr.wr.ud.ah = client->ah;
            req->in.sr.wr.ud.remote_qpn = client->qpn;
            req->in.sr.wr.ud.remote_qkey = 0;
        }

        req->sg_entry.length = size;
        req->sg_entry.lkey = reg->key;
        req->sg_entry.addr = (uintptr_t)(buf);

        ret = gds_stream_queue_send(stream, client->qp, &req->in.sr,
                                    &req->out.bad_sr);
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

    *req_t = req; 

out:
    return ret; 
}

#endif