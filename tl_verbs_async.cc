#include <infiniband/peer_ops.h>
#include <gdsync.h>
#include <gdsync/tools.h>
#include <gdsync/core.h>

#include "tl_verbs.hpp"

struct verbs_gds_client : verbs_client {
	struct gds_qp *qp_async;
	struct gds_cq *send_cq_async;
	struct gds_cq *recv_cq_async;
}
typedef struct verbs_client_async * verbs_client_async_t;

struct verbs_request_async : verbs_request {
	union
	{
		struct ibv_recv_wr rr;
		gds_send_wr sr;
	} in;
	union
	{
		gds_send_wr* bad_sr;
		struct ibv_recv_wr* bad_rr;
	} out;

	struct CUstream_st *stream;
	gds_send_request_t gds_send_info;
	gds_wait_request_t gds_wait_info;
}
typedef struct verbs_request_async * verbs_request_async_t;

namespace TL
{
	class Verbs_Async : public Verbs {
		protected:
			int gpu_id;
			gds_send_request_t *gds_send_info_region = NULL;
			gds_wait_request_t *gds_wait_info_region = NULL;
			int use_event_sync, use_dbrec_gpu, use_wq_gpu, use_rx_cq_gpu, use_tx_cq_gpu;

			int verbs_check_gpu_error()
			{
				int ret = MP_SUCCESS;
				cudaError_t error = cudaGetLastError();
				if (error != cudaSuccess) {
					CUDA_CHECK(error);
					ret = MP_FAILURE;
				}
				return ret;
			}

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
			
			int verbs_query_print_qp(struct gds_qp *qp, verbs_request_t req, int async)
			{
			    assert(qp);
			    struct ibv_qp_attr qp_attr;
			    struct ibv_qp_init_attr qp_init_attr;

			    memset(&qp_init_attr, 0, sizeof(struct ibv_qp_init_attr));
			    memset(&qp_attr, 0, sizeof(struct ibv_qp_attr));

			    if (ibv_query_qp(qp->qp, &qp_attr, IBV_QP_STATE | IBV_QP_PATH_MTU | IBV_QP_CAP, &qp_init_attr))
			    {
			        mp_err_msg(oob_rank, "client query qp attr fail\n");
			        return MP_FAILURE;
			    }
			   
			    mp_warn_msg(oob_rank, "Init QP attr: max_send_wr=%d, max_recv_wr=%d, max_inline_data=%d, qp_type=%d\nCurrent QP attr: QP State=%d QP Cur State=%d Access Flags=%d max_send_wr=%d, max_recv_wr=%d, max_inline_data=%d\n",
			                    qp_init_attr.cap.max_send_wr,
			                    qp_init_attr.cap.max_recv_wr,
			                    qp_init_attr.cap.max_inline_data,
			                    qp_init_attr.qp_type,
			                    qp_attr.qp_state,
			                    qp_attr.cur_qp_state,
			                    qp_attr.qp_access_flags,
			                    qp_attr.cap.max_send_wr,
			                    qp_attr.cap.max_recv_wr,
			                    qp_attr.cap.max_inline_data
			    );

				#if 0
				    if(req != NULL)
				    {
				        if(req->in.sr.exp_opcode == IBV_EXP_WR_SEND)
				            mp_warn_msg("This is an IBV_EXP_WR_SEND\n");
				            
				        if(req->in.sr.exp_opcode == IBV_EXP_WR_RDMA_WRITE)
				            mp_warn_msg("This is an IBV_EXP_WR_RDMA_WRITE\n");
				    }
				#endif
			    return MP_SUCCESS;
			}

			int verbs_gds_progress_posted_list (mp_flow_t flow)
			{
			    int i, ret = 0;
			    verbs_request_t req = NULL;

			    if (!use_event_sync) 
				return ret;

			    for (i=0; i<peer_count; i++) {
			        client_t *client = &clients[i];

			        req = client->posted_stream_req[flow];

			        while (req != NULL) { 
				    if (req->id > (int)client->last_trigger_id[flow]) break;

			            assert(req->status == MP_PREPARED);
			            assert(req->type == MP_SEND || req->type == MP_RDMA);

			            mp_dbg_msg(oob_rank, "posting req id %d from posted_stream_req list trigger id :%d \n", req->id, client->last_trigger_id[flow]);

			            ret = gds_post_send(client->qp, &req->in.sr, &req->out.bad_sr);
			            if (ret) {
			              fprintf(stderr, "posting send failed: %s \n", strerror(errno));
			              goto out;
			            }

			            req->status = MP_PENDING;

			            // remove request from waited list
			            mp_dbg_msg(oob_rank, "removing req %p from posted_stream_req list\n", req);

				    //delink the request
			            if (req->next != NULL) {
				        req->next->prev = req->prev;
				    }
				    if (req->prev != NULL) {
				        req->prev->next = req->next; 
				    }	

				    //adjust head and tail
			            if (client->posted_stream_req[flow] == req)
			 	        client->posted_stream_req[flow] = req->next;
			            if (client->last_posted_stream_req[flow] == req)
			                client->last_posted_stream_req[flow] = req->prev;

				    //clear request links
				    req->prev = req->next = NULL;

				    req = client->posted_stream_req[flow];
			        }
			    }

			out:
			    return ret;
			}

			int verbs_progress_single_flow(mp_flow_t flow)
			{
			    int i, ne = 0, ret = 0;
			    struct gds_cq *cq = NULL;  
			    int cqe_count = 0;

			    if (!wc) {
			        wc = (struct ibv_wc*)calloc(cq_poll_count, sizeof(struct ibv_wc));
			    }

			    const char *flow_str = verbs_flow_to_str(flow);

			    //printf("flow=%s\n", flow_str);

			    //useful only for sync_event
			    verbs_gds_progress_posted_list(flow);
			    for (i=0; i<peer_count; i++) {
			        client_t *client = &clients[i];
			        cq = (flow == TX_FLOW) ? client->send_cq : client->recv_cq; 

			        // WARNING: can't progress a CQE if it is associated to an RX req
			        // which is dependent upon GPU work which has not been triggered yet
			        cqe_count = verbs_client_can_poll(client, flow);
			        cqe_count = MIN(cqe_count, cq_poll_count);
			        if (!cqe_count) {
			            printf("cannot poll client[%d] flow=%s\n", client->oob_rank, flow_str);
			            continue;
			        }
			        ne = ibv_poll_cq(cq->cq, cqe_count, wc);
			        //printf("client[%d] flow=%s cqe_count=%d nw=%d\n", client->oob_rank, flow_str, cqe_count, ne);
			        if (ne == 0) {
			            //if (errno) printf("client[%d] flow=%s errno=%s\n", client->oob_rank, flow_str, strerror(errno));
			        }
			        else if (ne < 0) {
			            mp_err_msg(oob_rank, "error %d(%d) in ibv_poll_cq\n", ne, errno);
			            ret = MP_FAILURE;
			            goto out;
			        } else if (ne) {
			            int j;
			            for (j=0; j<ne; j++) {
			                struct ibv_wc *wc_curr = wc + j;
			                mp_dbg_msg(oob_rank, "client:%d wc[%d]: status=%x(%s) opcode=%x byte_len=%d wr_id=%"PRIx64"\n",
			                           client->oob_rank, j,
			                           wc_curr->status, ibv_wc_status_str(wc_curr->status), 
			                           wc_curr->opcode, wc_curr->byte_len, wc_curr->wr_id);

			                struct verbs_request *req = (struct verbs_request *) wc_curr->wr_id;

			                if (wc_curr->status != IBV_WC_SUCCESS) {
			                    mp_err_msg(oob_rank, "ERROR!!! completion error, status:'%s' client:%d rank:%d req:%p flow:%s\n",
			                               ibv_wc_status_str(wc_curr->status),
			                               i, client->oob_rank,
			                               req, flow_str);
			                    exit(EXIT_FAILURE);
			                    //continue;
			                }

			                if (req) { 
			                    mp_dbg_msg(oob_rank, "polled new CQE for req:%p flow:%s id=%d peer=%d type=%d\n", req, flow_str, req->id, req->peer, req->type);

			                    if (!(req->status == MP_PENDING_NOWAIT || req->status == MP_PENDING))
			                        mp_err_msg(oob_rank, "status not pending, value: %d \n", req->status);

			                    if (req->status == MP_PENDING_NOWAIT) {
			                    } else if (req->status != MP_PENDING) {
			                        mp_err_msg(oob_rank, "status not pending, value: %d \n", req->status);
			                        exit(-1);
			                    }

			                    ACCESS_ONCE(client->last_done_id) = req->id;
			                    verbs_progress_request(req);
			                } else {
			                    mp_warn_msg(oob_rank, "received completion with null wr_id \n");
			                }
			            }
			        }
			    }

			out: 
			    return ret;
			}

			//override
			int verbs_post_recv(client_t *client, struct verbs_request *req)
			{
			    int progress_retry=0, ret=0, ret_progress=0;

			    if(!client || !req)
			        return MP_FAILURE;
			    do
			    {
			    	ret = gds_post_recv(client->qp, &req->in.rr, &req->out.bad_rr);
			        if(ret == ENOMEM)
			        {
			        	if(qp_query == 0)
			            {
			                verbs_query_print_qp(client->qp, req, 1);
			                qp_query=1;
			            }

			            ret_progress = verbs_progress_single_flow(RX_FLOW);
			            if(ret_progress != MP_SUCCESS)
			            {
			                mp_err_msg(oob_rank, "verbs_progress_single_flow failed. Error: %d\n", ret_progress);
			                break;
			            }
			            mp_warn_msg(oob_rank, "RX_FLOW was full. verbs_progress_single_flow called %d times (ret=%d)\n", (progress_retry+1), ret);
			            progress_retry++;
			        }
			    } while(ret == ENOMEM && progress_retry <= MP_MAX_PROGRESS_FLOW_TRY);

		        qp_query=0;
			    return ret;
			}

			int verbs_post_send(client_t *client, struct verbs_request *req)
			{
			    int progress_retry=0, ret=0, ret_progress=0;

			    if(!client || !req)
			        return MP_FAILURE;
			    do
			    {
					ret = gds_post_send (client->qp, &req->in.sr, &req->out.bad_sr);
			        if(ret == ENOMEM)
			        {
			        	if(qp_query == 0)
			            {
			                verbs_query_print_qp(client->qp, req, 0);
			                qp_query=1;
			            }

			            ret_progress = verbs_progress_single_flow(TX_FLOW);
			            if(ret_progress != MP_SUCCESS)
			            {
			                mp_err_msg(oob_rank, "verbs_progress_single_flow failed. Error: %d\n", ret_progress);
			                break;
			            }
			            mp_warn_msg(oob_rank, "TX_FLOW was full. verbs_progress_single_flow called %d times (ret=%d)\n", (progress_retry+1), ret);
			            progress_retry++;
			        }
			    } while(ret == ENOMEM && progress_retry <= MP_MAX_PROGRESS_FLOW_TRY);

		        qp_query=0;
			    return ret;
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

			verbs_request_t verbs_gds_new_request(client_t *client, mp_req_type_t type, mp_state_t state) //, struct CUstream_st *stream)
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

			void verbs_gds_env_vars() {
				char *value = NULL;

				value = getenv("VERBS_GDS_EVENT_ASYNC");
				if (value != NULL) {
					use_event_sync = atoi(value);
				}
				if (use_event_sync) mp_warn_msg(oob_rank, "EVENT_ASYNC enabled\n");
				
				//if (init_flags & VERBS_INIT_RX_CQ_ON_GPU) use_rx_cq_gpu = 1;
				value = getenv("VERBS_GDS_RX_CQ_ON_GPU");
				if (value != NULL) {
					use_rx_cq_gpu = atoi(value);
				}
				if (use_rx_cq_gpu) mp_warn_msg(oob_rank, "RX CQ on GPU memory enabled\n");
			
				//if (init_flags & VERBS_INIT_TX_CQ_ON_GPU) use_tx_cq_gpu = 1;
				value = getenv("VERBS_GDS_TX_CQ_ON_GPU");
				if (value != NULL) {
					use_tx_cq_gpu = atoi(value);
				}
				if (use_tx_cq_gpu) mp_warn_msg(oob_rank, "TX CQ on GPU memory enabled\n");

				//if (init_flags & VERBS_INIT_DBREC_ON_GPU) use_dbrec_gpu = 1;
				value = getenv("VERBS_GDS_DBREC_ON_GPU");
				if (value != NULL) {
					use_dbrec_gpu = atoi(value);
				}
				if (use_dbrec_gpu) mp_warn_msg(oob_rank, "WQ DBREC on GPU memory enabled\n");

				mp_dbg_msg(oob_rank, "libgdsync build version 0x%08x, major=%d minor=%d\n", GDS_API_VERSION, GDS_API_MAJOR_VERSION, GDS_API_MINOR_VERSION);

			}
		public:

			Verbs_Async() : Verbs() {
#ifndef HAVE_GDSYNC
				fprintf(stderr, "Verbs GDS extension cannot work without LibGDSync library\n");
				exit(EXIT_FAILURE);
#endif
				use_event_sync=0;
				use_dbrec_gpu=0;
				use_wq_gpu=0;
				use_rx_cq_gpu=0;
				use_tx_cq_gpu=0;
				gpu_id=0;

				verbs_gds_env_vars();
			}
			
			//=================================================== OVERRIDE ==============================================================
			int createEndpoints() {
				int ret, i;
				int gds_flags;
				gds_qp_init_attr_t ib_qp_init_attr;

				/*establish connections*/
				client_index = (int *)calloc(oob_size, sizeof(int));
				if (client_index == NULL) {
					mp_err_msg(oob_rank, "allocation failed \n");
					return MP_FAILURE;
				}
				memset(client_index, -1, sizeof(int)*oob_size);

				clients = (client_t *)calloc(peer_count, sizeof(client_t));
				if (clients == NULL) {
					mp_err_msg(oob_rank, "allocation failed \n");
					return MP_FAILURE;
				}
				memset(clients, 0, sizeof(client_t)*peer_count);

				qpinfo_all =(qpinfo_t *)calloc(oob_size, sizeof(qpinfo_t));
				if (qpinfo_all == NULL) {
					mp_err_msg(oob_rank, "qpinfo allocation failed \n");
					return MP_FAILURE;
				}
				//client_count = peer_count
				/*creating qps for all peers_list*/
				for (i=0; i<peer_count; i++)
				{
					// MPI rank of i-th peer
					peer = peers_list[i];
					/*rank to peer id mapping */
					client_index[peer] = i;
					mp_dbg_msg(oob_rank, "Creating client %d, peer %d, client_index[peer]: %d\n", i, peer, client_index[peer]);
					/*peer id to rank mapping */
					clients[i].oob_rank = peer;
					clients[i].last_req_id = 0;
					clients[i].last_done_id = 0;
					assert(sizeof(clients[i].last_waited_stream_req) == N_FLOWS*sizeof(void*));

					memset(clients[i].last_posted_trigger_id, 0, sizeof(clients[0].last_posted_trigger_id));
					memset(clients[i].last_posted_tracked_id, 0, sizeof(clients[0].last_posted_tracked_id));
					memset(clients[i].last_tracked_id,        0, sizeof(clients[0].last_tracked_id));
					memset(clients[i].last_trigger_id,        0, sizeof(clients[0].last_trigger_id));
					memset(clients[i].last_waited_stream_req, 0, sizeof(clients[0].last_waited_stream_req));
					memset(clients[i].waited_stream_req,      0, sizeof(clients[0].waited_stream_req));
					memset(clients[i].last_posted_stream_req, 0, sizeof(clients[0].last_posted_stream_req));
					memset(clients[i].posted_stream_req,      0, sizeof(clients[0].posted_stream_req));

					memset(&ib_qp_init_attr, 0, sizeof(ib_qp_init_attr));
					ib_qp_init_attr.cap.max_send_wr  = ib_tx_depth;
					ib_qp_init_attr.cap.max_recv_wr  = ib_rx_depth;
					ib_qp_init_attr.cap.max_send_sge = ib_max_sge;
					ib_qp_init_attr.cap.max_recv_sge = ib_max_sge;

					//create QP, set to INIT state and exchange QPN information
					if (verbs_enable_ud) {
					  ib_qp_init_attr.qp_type = IBV_QPT_UD;
					  ib_qp_init_attr.cap.max_inline_data = ib_inline_size;
					} else {
					  ib_qp_init_attr.qp_type = IBV_QPT_RC;
					  ib_qp_init_attr.cap.max_inline_data = ib_inline_size;
					}

					gds_flags = GDS_CREATE_QP_DEFAULT;
					if (use_wq_gpu)
					  gds_flags |= GDS_CREATE_QP_WQ_ON_GPU;
					if (use_rx_cq_gpu)
					  gds_flags |= GDS_CREATE_QP_RX_CQ_ON_GPU;
					if (use_tx_cq_gpu)
					  gds_flags |= GDS_CREATE_QP_TX_CQ_ON_GPU;
					if (use_dbrec_gpu)
					  gds_flags |= GDS_CREATE_QP_WQ_DBREC_ON_GPU;

					//is the CUDA context already initialized?
					clients[i].qp = gds_create_qp(ib_ctx->pd, ib_ctx->context, &ib_qp_init_attr, gpu_id, gds_flags);
					if (clients[i].qp == NULL) {
					  mp_err_msg(oob_rank, "qp creation failed, errno %d\n", errno);
					  return MP_FAILURE;
					}

  					//======== QP CREATED
		    		clients[i].send_cq = &clients[i].qp->send_cq;
					clients[i].recv_cq = &clients[i].qp->recv_cq;

					assert(clients[i].qp);
					assert(clients[i].send_cq);
					assert(clients[i].recv_cq);
					
					struct ibv_qp_attr ib_qp_attr;
					memset(&ib_qp_attr, 0, sizeof(struct ibv_qp_attr));
					ib_qp_attr.qp_state        = IBV_QPS_INIT;
					ib_qp_attr.pkey_index      = 0;
					ib_qp_attr.port_num        = ib_port;
					int flags = 0;
					if (verbs_enable_ud) { 
					  ib_qp_attr.qkey            = 0;
					  flags                      = IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_QKEY;
					} else {
					  ib_qp_attr.qp_access_flags = IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_LOCAL_WRITE;
					  flags                      = IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS;
					}

					ret = ibv_modify_qp (clients[i].qp->qp, &ib_qp_attr, flags);
					if (ret != 0) {
					  mp_err_msg(oob_rank, "Failed to modify QP to INIT: %d, %s\n", ret, strerror(errno));
					  goto err_free_qps;
					}

					qpinfo_all[peer].lid = ib_port_attr.lid;
					qpinfo_all[peer].qpn = clients[i].qp->qp->qp_num;
					qpinfo_all[peer].psn = 0;
					mp_dbg_msg(oob_rank, "QP lid:%04x qpn:%06x psn:%06x\n", 
					         qpinfo_all[peer].lid,
					         qpinfo_all[peer].qpn,
					         qpinfo_all[peer].psn);
				}

				return MP_SUCCESS;

				//======== ERROR CASES ========
				err_free_qps:
					for (i=0; i<peer_count; i++)
					{
						mp_dbg_msg(oob_rank, "destroying QP client %d\n", i);
						ret = ibv_destroy_qp(clients[i].qp->qp);
						if (ret)
							mp_err_msg(oob_rank, "error %d destroying QP client %d\n", ret, i);
					}

				return MP_FAILURE;
				//============================
			}

			int finalize() {
				int i, retcode=MP_SUCCESS;
				mem_region_t *mem_region = NULL;

				oob_comm->barrier();

				/*destroy IB resources*/
				for (i=0; i<peer_count; i++) {
				  	gds_destroy_qp (clients[i].qp);
				}

				ibv_dealloc_pd (ib_ctx->pd);
				ibv_close_device (ib_ctx->context);

				while (mem_region_list != NULL) {
					mem_region = mem_region_list;
					mem_region_list = mem_region_list->next;
					free(mem_region->region);
					free(mem_region);
				}

				/*free all buffers*/
				free(ib_ctx);
				free(client_index);
				free(clients);

				return retcode;			
			}

			int pt2pt_nb_receive(void * buf, size_t size, int peer, mp_request_t * mp_req, mp_key_t * mp_mem_key) {
				int ret = 0;
				verbs_request_t req = NULL;
				verbs_region_t reg = (verbs_region_t) *mp_mem_key;
				client_t *client = &clients[client_index[peer]];

				assert(reg);
				req = verbs_new_request(client, MP_RECV, MP_PENDING_NOWAIT);
				assert(req);

				mp_dbg_msg(oob_rank, "peer=%d req=%p buf=%p size=%zd req id=%d reg=%p key=%x\n", peer, req, buf, size, req->id, reg, reg->key);

#ifdef HAVE_IPC
				if (client->can_use_ipc)
      				track_ipc_stream_rreq(peer, req);
#else
				req->in.rr.next = NULL;
				req->in.rr.wr_id = (uintptr_t) req;

				if (verbs_enable_ud) { 
				  verbs_region_t ud_reg = (verbs_region_t ) ud_padding_reg;

				  req->in.rr.num_sge = 2;
				  req->in.rr.sg_list = req->ud_sg_entry;
				  req->ud_sg_entry[0].length = UD_ADDITION;
				  req->ud_sg_entry[0].lkey = ud_reg->key;
				  req->ud_sg_entry[0].addr = (uintptr_t)(ud_padding);
				  req->ud_sg_entry[1].length = size;
				  req->ud_sg_entry[1].lkey = reg->key;
				  req->ud_sg_entry[1].addr = (uintptr_t)(buf);	
				} else { 
				  req->in.rr.num_sge = 1;
				  req->in.rr.sg_list = &req->sg_entry;
				  req->sg_entry.length = size;
				  req->sg_entry.lkey = reg->key;
				  req->sg_entry.addr = (uintptr_t)(buf);
				}
				//progress (remove) some request on the RX flow if is not possible to queue a recv request
				ret = verbs_post_recv(client, req);
				if (ret) {
					mp_err_msg(oob_rank, "Posting recv failed: %s \n", strerror(errno));
					goto out;
				}

				if (!use_event_sync) {
					ret = gds_prepare_wait_cq(client->recv_cq, &req->gds_wait_info, 0);
						if (ret) {
						mp_err_msg(oob_rank, "gds_prepare_wait_cq failed: %s \n", strerror(errno));
						goto out;
					}
				}
#endif
				*mp_req = (mp_request_t) req; 
			out:
				return ret;
			}

			int pt2pt_nb_send(void * buf, size_t size, int peer, mp_request_t * mp_req, mp_key_t * mp_mem_key) {
				int ret = 0;
				struct verbs_request *req = NULL;
				verbs_region_t reg = (verbs_region_t) *mp_mem_key;
				client_t *client = &clients[client_index[peer]];

				assert(reg);
				req = verbs_new_request(client, MP_SEND, MP_PENDING_NOWAIT);
				assert(req);

				mp_dbg_msg(oob_rank, "peer=%d req=%p buf=%p size=%zd req id=%d reg=%p key=%x\n", peer, req, buf, size, req->id, reg, reg->key);

#ifdef HAVE_IPC
				if (client->can_use_ipc)
				{
					ipc_handle_cache_entry_t *entry = NULL;
					smp_buffer_t *smp_buffer = NULL;

					//try to find in local handle cache
					ipc_handle_cache_find (buf, size, &entry, oob_rank);
					if (!entry) { 
						entry = malloc(sizeof(ipc_handle_cache_entry_t));
						if (!entry) { 
							mp_err_msg(oob_rank, "cache entry allocation failed \n");	
							ret = MP_FAILURE;
							goto out;
						}

						CU_CHECK(cuMemGetAddressRange((CUdeviceptr *)&entry->base, &entry->size, (CUdeviceptr) buf));
						CU_CHECK(cuIpcGetMemHandle (&entry->handle, (CUdeviceptr)entry->base));

						ipc_handle_cache_insert(entry, oob_rank);
					}

					assert(entry != NULL);
					smp_buffer = client->smp.remote_buffer + client->smp.remote_head;
					assert(smp_buffer->free == 1);	

					memcpy((void *)&smp_buffer->handle, (void *)&entry->handle, sizeof(CUipcMemHandle));  
					smp_buffer->base_addr = entry->base;
					smp_buffer->base_size = entry->size;
					smp_buffer->addr = buf;
					smp_buffer->size = size;
					smp_buffer->offset = (uintptr_t)buf - (uintptr_t)entry->base;
					smp_buffer->sreq = req; 
					smp_buffer->free = 0; 
					smp_buffer->busy = 1;
					client->smp.remote_head = (client->smp.remote_head + 1)%smp_depth;	 
				}
#else
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
				// progress (remove) some request on the TX flow if is not possible to queue a send request
				ret = verbs_post_send(client, req);
				if (ret) {
					mp_err_msg(oob_rank, "posting send failed: %s \n", strerror(errno));
					goto out;
				}

				if (!use_event_sync) {
					ret = gds_prepare_wait_cq(client->send_cq, &req->gds_wait_info, 0);
					if (ret) {
					    mp_err_msg(oob_rank, "gds_prepare_wait_cq failed: %s \n", strerror(errno));
					    goto out;
					}
				}
#endif
				*mp_req = (mp_request_t) req;

			out:
			    return ret;
			}

			int wait_all(int count, mp_request_t *req_)
			{
			    int complete = 0, ret = 0;

			    us_t start = mp_get_cycles();
			    us_t tmout = MP_PROGRESS_ERROR_CHECK_TMOUT_US;
			    /*poll until completion*/
			    while (complete < count) {
			        struct verbs_request *req = (verbs_request_t) req_[complete];
					if (!verbs_req_can_be_waited(req))
					{
					    mp_dbg_msg(oob_rank, "cannot wait req:%p status:%d id=%d peer=%d type=%d flags=%08x\n", req, req->status, req->id, req->peer, req->type, req->flags);
					    ret = EINVAL;
					    goto out;
					}
					if (req->status == MP_PENDING_NOWAIT) {
					    mp_dbg_msg(oob_rank, "PENDING_NOWAIT->PENDING req:%p status:%d id=%d peer=%d type=%d\n", req, req->status, req->id, req->peer, req->type);
					    client_t *client = &clients[client_index[req->peer]];
					    mp_flow_t req_flow = verbs_type_to_flow(req->type);
					    struct gds_cq *cq = (req_flow == TX_FLOW) ? client->send_cq : client->recv_cq;
						// user did not call post_wait_cq()
						// if req->status == WAIT_PENDING && it is a stream request
						//   manually ack the cqe info (NEW EXP verbs API)
						//   req->status = MP_WAIT_POSTED
					    ret = gds_post_wait_cq(cq, &req->gds_wait_info, 0);
					    if (ret) {
					      mp_err_msg(oob_rank, "got %d while posting cq\n", ret);
					      goto out;
					    }
					    req->stream = NULL;
					    req->status = MP_PENDING;
					}

			        complete++;
			    }
			    
			    complete=0;

			    while (complete < count) {
			        struct verbs_request *req = (verbs_request_t) req_[complete];
			        
			        while (req->status != MP_COMPLETE) {
			            ret = verbs_progress_single_flow (TX_FLOW);
			            if (ret) {
			                goto out;
			            }
			            ret = verbs_progress_single_flow (RX_FLOW);
			            if (ret) {
			                goto out;
			            }
			            us_t now = mp_get_cycles();
			            if (((long)now-(long)start) > (long)tmout) {
			                start = now;
			                mp_warn_msg(oob_rank, "checking for GPU errors\n");
			                int retcode = verbs_check_gpu_error();
			                if (retcode) {
			                    ret = MP_FAILURE;
			                    goto out;
			                }
			                mp_warn_msg(oob_rank, "enabling dbg tracing\n");
			                //mp_enable_dbg(1);
			                mp_dbg_msg(oob_rank, "complete=%d req:%p status:%d id=%d peer=%d type=%d\n", complete, req, req->status, req->id, req->peer, req->type);
			            }
			        }
			        
			        complete++;
			    }

			    if(!ret)
			    {
			        complete=0;
			        while (complete < count) {
			        	verbs_request_t req = (verbs_request_t) req_[complete];
			            if (req->status == MP_COMPLETE)
			                verbs_release_request((verbs_request_t) req);
			            else
			                ret = MP_FAILURE;

			            complete++;
			        }
			    }

			out:
			    return ret;
			}

						int onesided_nb_put (void *src, int size, mp_key_t *reg_t, int peer, size_t displ, mp_window_t *window_t, mp_request_t *req_t, int flags) 
			{
				int ret = 0;
				verbs_request_t req;
				verbs_region_t reg = (verbs_region_t) *reg_t;
				verbs_window_t window = (verbs_window_t) *window_t;
				int client_id = client_index[peer];
				client_t *client = &clients[client_id];

				if (verbs_enable_ud) { 
					mp_err_msg(oob_rank, "put/get not supported with UD \n");
					ret = MP_FAILURE;
					goto out;
				}
				assert(displ < window->rsize[client_id]);

				req = verbs_new_request(client, MP_RDMA, MP_PENDING_NOWAIT);
				assert(req);

				req->flags = flags;
				req->in.sr.next = NULL;
				if (flags & MP_PUT_NOWAIT)
				  req->in.sr.exp_send_flags = 0;
				else
				  req->in.sr.exp_send_flags = IBV_EXP_SEND_SIGNALED;
				if (flags & MP_PUT_INLINE)
				  req->in.sr.exp_send_flags |= IBV_EXP_SEND_INLINE;
				req->in.sr.exp_opcode = IBV_EXP_WR_RDMA_WRITE;
				req->in.sr.wr_id = (uintptr_t) req;
				req->in.sr.num_sge = 1;
				req->in.sr.sg_list = &req->sg_entry;

				req->sg_entry.length = size;
				req->sg_entry.lkey = reg->key;
				req->sg_entry.addr = (uintptr_t)src;

				req->in.sr.wr.rdma.remote_addr = ((uint64_t)window->base_ptr[client_id]) + displ;
				req->in.sr.wr.rdma.rkey = window->rkey[client_id];

				ret = verbs_post_send(client, req);
				if (ret) {
					mp_err_msg(oob_rank, "posting send failed: %s \n", strerror(errno));
					goto out;
				}

				if (!(flags & MP_PUT_NOWAIT)) {
					ret = gds_prepare_wait_cq(client->send_cq, &req->gds_wait_info, 0);
					if (ret) {
						mp_err_msg(oob_rank, "gds_prepare_wait_cq failed: %s \n", strerror(errno));
						goto out;
					}
				}
				*req_t = req;

			out:
				return ret;
			}

			int onesided_nb_get(void *dst, int size, mp_key_t *reg_t, int peer, size_t displ, mp_window_t *window_t, mp_request_t *req_t) 
			{
				int ret = 0;
				verbs_request_t req;
				verbs_region_t reg = (verbs_region_t) *reg_t;
				verbs_window_t window = (verbs_window_t) *window_t;
				int client_id = client_index[peer];
				client_t *client = &clients[client_id];

				if (verbs_enable_ud) { 
					mp_err_msg(oob_rank, "put/get not supported with UD \n");
					ret = MP_FAILURE;
					goto out;
				}

				assert(displ < window->rsize[client_id]);

				req = verbs_new_request(client, MP_RDMA, MP_PENDING_NOWAIT);

				req->in.sr.next = NULL;
				req->in.sr.exp_send_flags = IBV_EXP_SEND_SIGNALED;
				req->in.sr.exp_opcode = IBV_EXP_WR_RDMA_WRITE;
				req->in.sr.wr_id = (uintptr_t) req;
				req->in.sr.num_sge = 1;
				req->in.sr.sg_list = &req->sg_entry;

				req->sg_entry.length = size;
				req->sg_entry.lkey = reg->key;
				req->sg_entry.addr = (uintptr_t)dst;

				req->in.sr.wr.rdma.remote_addr = ((uint64_t)window->base_ptr[client_id]) + displ;
				req->in.sr.wr.rdma.rkey = window->rkey[client_id];

				ret = verbs_post_send(client, req);
				if (ret) {
					mp_err_msg(oob_rank, "posting send failed: %s \n", strerror(errno));
					goto out;
				}

				ret = gds_prepare_wait_cq(client->send_cq, &req->gds_wait_info, 0);
				if (ret) {
					mp_err_msg(oob_rank, "gds_prepare_wait_cq failed: %s \n", strerror(errno));
					goto out;
				}

				*req_t = req;

			out:
				return ret;
			}

			int setup_sublayer(int par1) 
			{
				int version, ret;
				ret = gds_query_param(GDS_PARAM_VERSION, &version);
				if (ret) {
					mp_err_msg(oob_rank, "error querying libgdsync version\n");
					return MP_FAILURE;
				}
				mp_dbg_msg(oob_rank, "libgdsync queried version 0x%08x\n", version);
				if (!GDS_API_VERSION_COMPATIBLE(version)) {
					mp_err_msg(oob_rank, "incompatible libgdsync version 0x%08x\n", version);
					return MP_FAILURE;
				}
				
				//TODO: additional checks
				gpu_id = par1;
				mp_dbg_msg(oob_rank, "Using gpu_id %d\n", gpu_id);
				CUDA_CHECK(cudaSetDevice(gpu_id));
				//LibGDSync issue #18
				cudaFree(0);

				return MP_SUCCESS;
			}
			//==========================================================================================================================

			//ASYNC
			int pt2pt_nb_send_async(void * buf, size_t size, int peer, mp_request_t * mp_req, mp_key_t * mp_key, asyncStream stream)
			{
			    int ret = 0;
				verbs_request_t req = NULL;
				verbs_region_t reg = (verbs_region_t) *mp_key;
			    client_t *client = &clients[client_index[peer]];
			    
			    if (use_event_sync) {
			        req = verbs_gds_new_request(client, MP_SEND, MP_PREPARED); //, stream);
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
			        req = verbs_gds_new_request(client, MP_SEND, MP_PENDING_NOWAIT); //, stream);
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
			        req = verbs_gds_new_request(client, MP_SEND, MP_PREPARED); //, stream);
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
			        req = verbs_gds_new_request(client, MP_SEND, MP_PENDING); //, stream);

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
			  
			    req = verbs_gds_new_request(client, MP_SEND, MP_PREPARED); //, stream);
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
				return pt2pt_b_send_post_all_async(1, mp_req, stream);
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
				return pt2pt_nb_send_post_all_async(1, mp_req, stream);
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

			//useful only with gds??
			int progress_requests (int count, mp_request_t *req_)
			{
			  int r = 0, ret = 0;
			  int completed_reqs = 0;
			  /*poll until completion*/
			  while (r < count) {
			    verbs_request_t req = (verbs_request_t)req_[r];
			    
			    if (!verbs_req_valid(req)) {
			        mp_err_msg(oob_rank, "invalid req=%p req->id=%d\n", req, req->id);
			    }

			    ret = verbs_progress_single_flow(TX_FLOW);
			    if (ret) {
			        mp_dbg_msg(oob_rank, "progress error %d\n", ret);
			        goto out;
			    }

			    ret = verbs_progress_single_flow(RX_FLOW);
			    if (ret) {
			        mp_dbg_msg(oob_rank, "progress error %d\n", ret);
			        goto out;
			    }

			    if (req->status == MP_COMPLETE) {
			        completed_reqs++;
			    }

			    r++;
			  }
			  if (completed_reqs)
			      mp_dbg_msg(oob_rank, "%d completed reqs, not being released!\n", completed_reqs);
			  ret = completed_reqs;

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
				req = verbs_gds_new_request(client, MP_RDMA, MP_PENDING_NOWAIT); //, stream);
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

				req = verbs_gds_new_request(client, MP_RDMA, MP_PENDING_NOWAIT); //, stream);
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

			//mp_put_prepare
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
				req = verbs_gds_new_request(client, MP_RDMA, MP_PREPARED); //, NULL);
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

	};
}


static TL::Communicator *create_gds() { return new TL::Verbs_Async(); }

static class update_tl_list_gds {
	public: 
		update_tl_list_gds() {
			add_tl_creator(TL_INDEX_VERBS_ASYNC, create_gds);
		}
} list_tl_gds;

#if 0
static int mp_prepare_send(client_t *client, verbs_request_t req)
int (void *buf, int size, int peer, mp_key_t *mp_key, mp_request_t *req_t)
int mp_isendv_on_stream (struct iovec *v, int nvecs, int peer, mp_key_t *mp_key,
                         mp_request_t *req_t, asyncStream stream)
int mp_sendv_prepare(struct iovec *v, int nvecs, int peer, mp_key_t *mp_key, mp_request_t *req_t)
#endif