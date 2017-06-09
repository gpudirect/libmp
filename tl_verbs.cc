#include "tl_verbs.h"

int oob_size, oob_rank;
int mp_dbg_is_enabled, mp_warn_is_enabled;

namespace TL
{
	class Verbs : public Communicator {
		private:
			OOB::Communicator * oob_comm;

			struct ibv_device **dev_list;
			//struct ibv_qp_attr ib_qp_attr;
			//struct ibv_ah_attr ib_ah_attr;
			struct ibv_device_attr dev_attr;
			struct ibv_port_attr ib_port_attr;
			struct ibv_device *ib_dev;
			int ib_port;
			ib_context_t *ib_ctx;

			int num_devices;
			const char *select_dev;
			char *ib_req_dev;
			int peer;
			int peer_count;
			qpinfo_t *qpinfo_all;

			//int smp_depth;
			int ib_tx_depth;
			int ib_rx_depth;
			int num_cqes; // it gets actually rounded up to 512
			int ib_max_sge; 
			int ib_inline_size; 

			int peers_list[MAX_PEERS];
			client_t *clients;
			int *client_index;
			int cq_poll_count;

			int verbs_enable_ud;
			struct verbs_request *mp_request_free_list;
			mem_region_t *mem_region_list;
			char ud_padding[UD_ADDITION];
			mp_key_t ud_padding_reg;
			int mp_request_active_count;
			int verbs_request_limit;
			struct ibv_wc *wc;

			/*to enable opaque requests*/
			void verbs_allocate_requests()
			{
			  int i;
			  mem_region_t *mem_region;
			  struct verbs_request *mp_requests;

			  assert (mp_request_free_list == NULL);

			  mem_region = (mem_region_t *) malloc (sizeof (mem_region_t));
			  if (mem_region == NULL) {
			    mp_err_msg("memory allocation for mem_region failed \n");
			    exit(-1);
			  }
			  if (mem_region_list == NULL) {
			    mem_region_list = mem_region;
			    mem_region->next = NULL;
			  } else {
			    mem_region->next = mem_region_list;
			  }

			  mem_region->region = malloc (sizeof(struct verbs_request)*verbs_request_limit);
			  if (mem_region == NULL) {
			    mp_err_msg("memory allocation for request_region failed \n");
			    exit(-1);
			  }

			  mp_requests = (struct verbs_request *) mem_region->region;

			  mp_request_free_list = mp_requests;
			  for (i=0; i<verbs_request_limit-1; i++) {
			    mp_requests[i].next = mp_requests + i + 1;
			  }
			  mp_requests[i].next = NULL;
			}

			struct verbs_request *verbs_get_request()
			{
			  struct verbs_request *req = NULL;

			  if (mp_request_free_list == NULL) {
			    verbs_allocate_requests();
			    assert(mp_request_free_list != NULL);
			  }

			  req = mp_request_free_list;
			  mp_request_free_list = mp_request_free_list->next;

			  req->next = NULL;
			  req->prev = NULL;

			  return req;
			}

			struct verbs_request *verbs_new_request(client_t *client, mp_req_type_t type, mp_state_t state) //, struct CUstream_st *stream)
			{
			  struct verbs_request *req = verbs_get_request();
			  //mp_dbg_msg("new req=%p\n", req);
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

			int verbs_get_request_id(client_t *client, mp_req_type_t type)
			{
			    assert(client->last_req_id < UINT_MAX);
			    return ++client->last_req_id;
			}

			int verbs_post_recv(client_t *client, struct verbs_request *req)
			{
			    int progress_retry=0, ret=0, ret_progress=0;

			    if(!client || !req)
			        return MP_FAILURE;
			    do
			    {

			    	ret = ibv_post_recv(client->qp->qp, &req->in.rr, &req->out.bad_rr);
			        if(ret == ENOMEM)
			        {
			            ret_progress = verbs_progress_single_flow(RX_FLOW);
			            if(ret_progress != MP_SUCCESS)
			            {
			                mp_err_msg("mp_progress_single_flow failed. Error: %d\n", ret_progress);
			                break;
			            }
			            mp_warn_msg("RX_FLOW was full. mp_progress_single_flow called %d times (ret=%d)\n", (progress_retry+1), ret);
			            progress_retry++;
			        }
			    } while(ret == ENOMEM && progress_retry <= MP_MAX_PROGRESS_FLOW_TRY);

			    return ret;
			}

			int verbs_progress_single_flow(mp_flow_t flow)
			{
			    int i, ne = 0, ret = 0;
			    struct verbs_cq *cq = NULL; 
			    int cqe_count = 0;

			    if (!wc) {
			        wc = malloc(sizeof(struct ibv_wc)*cq_poll_count);
			    }

			    const char *flow_str = verbs_flow_to_str(flow);

			    //printf("flow=%s\n", flow_str);

			    //useful only for sync_event
			    //progress_posted_list(flow);

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
			            mp_err_msg("error %d(%d) in ibv_poll_cq\n", ne, errno);
			            ret = MP_FAILURE;
			            goto out;
			        } else if (ne) {
			            int j;
			            for (j=0; j<ne; j++) {
			                struct ibv_wc *wc_curr = wc + j;
			                printf("client:%d wc[%d]: status=%x(%s) opcode=%x byte_len=%d wr_id=%"PRIx64"\n",
			                           client->oob_rank, j,
			                           wc_curr->status, ibv_wc_status_str(wc_curr->status), 
			                           wc_curr->opcode, wc_curr->byte_len, wc_curr->wr_id);

			                struct verbs_request *req = (struct verbs_request *) wc_curr->wr_id;

			                if (wc_curr->status != IBV_WC_SUCCESS) {
			                    mp_err_msg("ERROR!!! completion error, status:'%s' client:%d rank:%d req:%p flow:%s\n",
			                               ibv_wc_status_str(wc_curr->status),
			                               i, client->oob_rank,
			                               req, flow_str);
			                    exit(EXIT_FAILURE);
			                    //continue;
			                }

			                if (req) { 
			                    printf("polled new CQE for req:%p flow:%s id=%d peer=%d type=%d\n", req, flow_str, req->id, req->peer, req->type);

			                    if (!(req->status == MP_PENDING_NOWAIT || req->status == MP_PENDING))
			                        mp_err_msg("status not pending, value: %d \n", req->status);

			                    if (req->status == MP_PENDING_NOWAIT) {
			                    } else if (req->status != MP_PENDING) {
			                        mp_err_msg("status not pending, value: %d \n", req->status);
			                        exit(-1);
			                    }

			                    ACCESS_ONCE(client->last_done_id) = req->id;
			                    verbs_progress_request(req);
			                } else {
			                    printf("received completion with null wr_id \n");
			                }
			            }
			        }
			    }

			out: 
			    return ret;
			}

			int verbs_client_can_poll(client_t *client, mp_flow_t flow)
			{
			    struct verbs_request *pending_req;

			    pending_req = client->waited_stream_req[flow];

			    // no pending stream req
			    // or next non-completed req is at least the 1st pending stream req
			    int ret = 0;

			    while (pending_req) {
			        // re-reading each time as it might have been updated
			        int threshold_id = ACCESS_ONCE(client->last_tracked_id[flow]);
			        if (threshold_id < pending_req->id) {
			            mp_dbg_msg("client[%d] stalling progress flow=%s threshold_id=%d req->id=%d\n", 
			                       client->oob_rank, verbs_flow_to_str(flow), threshold_id, pending_req->id);
			            break;
			        } else {
			            mp_dbg_msg("client[%d] flow=%s threshold_id=%d req->id=%d\n", 
			                       client->oob_rank, verbs_flow_to_str(flow), threshold_id, pending_req->id);
				    ret++;
				    pending_req = pending_req->next;
			        }
			    }
				
			    if (!pending_req) {
			        ret = cq_poll_count;
			    }

			    mp_dbg_msg("pending_req=%p ret=%d\n", pending_req, ret);
			    return ret;
			}


			int verbs_progress_request(struct verbs_request *req)
			{
				switch(req->status) {
					case MP_PREPARED:
					    mp_dbg_msg("req=%p id=%d PREPARED\n", req, req->id);
					    break;
					case MP_PENDING_NOWAIT:
					    mp_dbg_msg("req=%p id=%d NOWAIT\n", req, req->id);
					case MP_PENDING:
					    mp_dbg_msg("req=%p id=%d PENDING->COMPLETE\n", req, req->id);
					    req->status = MP_COMPLETE;
					    cleanup_request(req);
					    break;
					case MP_COMPLETE:
					    mp_warn_msg("attempt at progressing a complete req:%p \n", req);
					    break;
					default:
					    mp_err_msg("invalid status %d for req:%p \n", req->status, req);
					    break;
				}
				return 0;
			}

			int cleanup_request(struct verbs_request *req)
			{
			    if (req->sgv) {
			        free(req->sgv);
			        req->sgv = NULL;
			    }

			    return 0;
			}

			void verbs_init_ops(struct peer_op_wr *op, int count)
			{
				int i = count;
				while (--i)
					op[i-1].next = &op[i];
				op[count-1].next = NULL;
			}
			
		public:

			Verbs() {
	        	dev_list = NULL;
	        	ib_req_dev = NULL;
				ib_dev = NULL;
				//todo: can we change it dynamically?
				ib_port = 1;
				ib_ctx = NULL;
				//smp_depth = 256;
				ib_tx_depth = 256*2;
				ib_rx_depth = 256*2;
				num_cqes = 256; // it gets actually rounded up to 512
				ib_max_sge = 30;
				ib_inline_size = 64;
				cq_poll_count = 20;
				verbs_request_limit = 512;
				verbs_enable_ud = 0;
				wc = NULL;

				clients=NULL;
				client_index=NULL;
				mp_request_free_list = NULL;
				mem_region_list = NULL;

				getEnvVars();
	        }


	    	~Verbs() {}

	    	void getEnvVars() {

	    		//Which HCA
	    		char *value = NULL;
				value = getenv("VERBS_IB_HCA"); 
				if (value != NULL) {
					ib_req_dev = value;
				}

				value = getenv("VERBS_ENABLE_UD"); 
				if (value != NULL) {
					verbs_enable_ud = atoi(value);
				}

				value = getenv("MP_ENABLE_DEBUG");
		        if (value) {
		            int en = atoi(value);
		            mp_dbg_is_enabled = !!en;
		            //printf("MP_ENABLE_DEBUG=%s\n", value);
		        } else
		            mp_dbg_is_enabled = 0;

		        value = getenv("MP_ENABLE_WARN");
		        if (value) {
		            int en = atoi(value);
		            mp_warn_is_enabled = !!en;
		            //printf("MP_ENABLE_DEBUG=%s\n", value);
		        } else
		            mp_warn_is_enabled = 0;


		        value = getenv("VERBS_CQ_POLL_COUNT"); 
				if (value != NULL) {
					cq_poll_count = atoi(value);
				}

	    	}

	    	int setupOOB(OOB::Communicator * input_comm) {

	    		int i,j;

	    		if(!input_comm)
	    			return MP_FAILURE;

	    		oob_comm=input_comm;
				oob_size=oob_comm->getSize();
				oob_rank=oob_comm->getMyId();

				printf("Communicator says: %d peers\n", oob_size);

				// init peers_list_list    
				for (i=0, j=0; i<oob_size; ++i) {
				        // self reference is forbidden
				    if (i!=oob_rank) {
				        peers_list[j] = i;
				       // rank_to_peer[i] = j;
				        printf("peers_list[%d]=rank %d\n", j, i);
				        ++j;
				    }
				    /* else {
				        rank_to_peer[i] = bad_peer;
				    }*/
				}
				peer_count = j;
				if(oob_size-1 != peer_count)
					return MP_FAILURE;

				printf("peer_count=%d\n", peer_count);

	    		return MP_SUCCESS;
	    	}

			int setupNetworkDevices() {
				int i;

				/*pick the right device*/
				dev_list = ibv_get_device_list (&num_devices);
				if (dev_list == NULL) {
					mp_err_msg("ibv_get_device_list returned NULL \n");
					return MP_FAILURE;
				}

				ib_dev = dev_list[0];
				if (ib_req_dev != NULL) {
					for (i=0; i<num_devices; i++) {
					  select_dev = ibv_get_device_name(dev_list[i]);
					  if (strstr(select_dev, ib_req_dev) != NULL) {
					    ib_dev = dev_list[i];
					    mp_err_msg("using IB device: %s \n", ib_req_dev);
					    break;
					  }
					}
					if (i == num_devices) {
					  select_dev = ibv_get_device_name(dev_list[0]);
					  ib_dev = dev_list[0];
					  printf("request device: %s not found, defaulting to %s \n", ib_req_dev, select_dev);
					}
				}
				printf("HCA dev: %s\n", ibv_get_device_name(ib_dev));

				/*create context, pd, cq*/
				ib_ctx = (ib_context_t *) calloc (1, sizeof (ib_context_t));
				if (ib_ctx == NULL) {
					mp_err_msg("ib_ctx allocation failed \n");
					return MP_FAILURE;
				}

				ib_ctx->context = ibv_open_device(ib_dev);
				if (ib_ctx->context == NULL) {
					mp_err_msg("ibv_open_device failed \n");
					return MP_FAILURE;
				}

				/*get device attributes and check relevant leimits*/
				if (ibv_query_device(ib_ctx->context, &dev_attr)) {
					mp_err_msg("query_device failed \n"); 	 
					return MP_FAILURE;	
				}

				if (ib_max_sge > dev_attr.max_sge) {
					mp_err_msg("warning!! requested sgl length longer than supported by the adapter, reverting to max, requested: %d max: %d \n", ib_max_sge, dev_attr.max_sge);
					ib_max_sge = dev_attr.max_sge;
				}

				ib_ctx->pd = ibv_alloc_pd (ib_ctx->context);
				if (ib_ctx->pd == NULL) {
					fprintf(stderr ,"ibv_alloc_pd failed \n");
					return MP_FAILURE;
				}

				ibv_query_port (ib_ctx->context, ib_port, &ib_port_attr);

				/*allocate requests*/
				verbs_allocate_requests();
				assert(mp_request_free_list != NULL);

				return MP_SUCCESS;
			}

			int createEndpoints() {
				int ret, i;
				/*establish connections*/
				client_index = (int *)calloc(oob_size, sizeof(int));
				if (client_index == NULL) {
					mp_err_msg("allocation failed \n");
					return MP_FAILURE;
				}
				memset(client_index, -1, sizeof(int)*oob_size);

				clients = (client_t *)calloc(peer_count, sizeof(client_t));
				if (clients == NULL) {
					mp_err_msg("allocation failed \n");
					return MP_FAILURE;
				}
				memset(clients, 0, sizeof(client_t)*peer_count);

				qpinfo_all =(qpinfo_t *)calloc(oob_size, sizeof(qpinfo_t));
				if (qpinfo_all == NULL) {
					mp_err_msg("qpinfo allocation failed \n");
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
					printf("Creating client %d, peer %d, client_index[peer]: %d\n", i, peer, client_index[peer]);
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

					struct ibv_exp_qp_init_attr ib_qp_init_attr;
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
#ifdef GDSYNC
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
					  printf("qp creation failed \n");
					  return MP_FAILURE;
					}
#else
					printf("Starting CQ creation\n");

					// ================= CQs creation =================
					int ret = 0;
			        struct ibv_cq *send_cq = NULL;
			        struct ibv_cq *recv_cq = NULL;
					struct ibv_exp_cq_init_attr cq_attr;
			        cq_attr.comp_mask = 0; //IBV_CREATE_CQ_ATTR_PEER_DIRECT;

			        //CQE Compressing IBV_EXP_CQ_COMPRESSED_CQE? https://www.mail-archive.com/netdev@vger.kernel.org/msg110152.html
			        cq_attr.flags = 0; // see ibv_exp_cq_create_flags
			        cq_attr.res_domain = NULL;
			        //Not Async case, no need of peer attrs
			        cq_attr.peer_direct_attrs = NULL; //peer_attr;

			        int old_errno = errno;

   					printf("ibv_exp_create_cq send_cq\n");

			        send_cq = ibv_exp_create_cq(ib_ctx->context, ib_qp_init_attr.cap.max_send_wr /*num cqe*/, NULL /* cq_context */, NULL /* channel */, 0 /*comp_vector*/, &cq_attr);
			        if (!send_cq) {
			                mp_err_msg("error %d in ibv_exp_create_cq, old errno %d\n", errno, old_errno);
			        }
   					printf("ibv_exp_create_cq recv_cq\n");

			        recv_cq = ibv_exp_create_cq(ib_ctx->context, ib_qp_init_attr.cap.max_recv_wr /*num cqe*/, NULL /* cq_context */, NULL /* channel */, 0 /*comp_vector*/, &cq_attr);
			        if (!recv_cq) {
			                mp_err_msg("error %d in ibv_exp_create_cq, old errno %d\n", errno, old_errno);
			        }

					// ================= QP creation =================

			        ib_qp_init_attr.send_cq = send_cq;
			        ib_qp_init_attr.recv_cq = recv_cq;

			        ib_qp_init_attr.pd = ib_ctx->pd;
			        ib_qp_init_attr.comp_mask |= IBV_EXP_QP_INIT_ATTR_PD;

			        //todo: No need of IBV_QP_INIT_ATTR_PEER_DIRECT (IBV_EXP_QP_INIT_ATTR_PEER_DIRECT)
			        //ib_qp_init_attr.comp_mask |= IBV_QP_INIT_ATTR_PEER_DIRECT;
			        ib_qp_init_attr.peer_direct_attrs = NULL; //peer_attr;

   					printf("calloc client %d qp\n", i);

   					clients[i].qp = (struct verbs_qp*)calloc(1, sizeof(struct verbs_qp));
			        if (!clients[i].qp) {
			                mp_err_msg("cannot allocate memory\n");
			                return MP_FAILURE;
			        }
			        printf("ibv_exp_create_qp qp\n");

			        clients[i].qp->qp = ibv_exp_create_qp(ib_ctx->context, &ib_qp_init_attr);
			        if (!clients[i].qp->qp)  {
			                ret = EINVAL;
			                mp_err_msg("error in ibv_exp_create_qp\n");
			                goto err_free_cqs;
					}

   					printf("populate client %d qp\n", i);

			        clients[i].qp->send_cq.cq = clients[i].qp->qp->send_cq;
			        clients[i].qp->send_cq.curr_offset = 0;
			        clients[i].qp->recv_cq.cq = clients[i].qp->qp->recv_cq;
			        clients[i].qp->recv_cq.curr_offset = 0;

			        printf("created client %d\n", i); //clients[i].qp);
			        goto ok_qp;

					//======== ERROR CASES ========
			        err_free_qp:
				        printf("destroying QP\n");
				        ibv_destroy_qp(clients[i].qp->qp);

					err_free_cqs:
					    printf("destroying RX CQ\n");
						ret = ibv_destroy_cq(send_cq);
				        if (ret)
				        	mp_err_msg("error %d destroying RX CQ\n", ret);

					err_free_tx_cq:
				        printf("destroying TX CQ\n");
						ret = ibv_destroy_cq(recv_cq);
				        if (ret)
				        	mp_err_msg("error %d destroying TX CQ\n", ret);

				    return ret;
			   		//============================

#endif
  					//======== QP CREATED
			    	ok_qp:
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
						  mp_err_msg("Failed to modify QP to INIT: %d, %s\n", ret, strerror(errno));
						  exit(EXIT_FAILURE);
						}

						qpinfo_all[peer].lid = ib_port_attr.lid;
						qpinfo_all[peer].qpn = clients[i].qp->qp->qp_num;
						qpinfo_all[peer].psn = 0;
						printf("QP lid:%04x qpn:%06x psn:%06x\n", 
						         qpinfo_all[peer].lid,
						         qpinfo_all[peer].qpn,
						         qpinfo_all[peer].psn);
				}

				return MP_SUCCESS;
			}

			int exchangeEndpoints() {
				int ret = oob_comm->alltoall(NULL, 0, MP_CHAR, qpinfo_all, sizeof(qpinfo_t), MP_CHAR);
				return ret;
			}	


			int updateEndpoints() {
				int i, ret,flags;
				struct ibv_qp_attr ib_qp_attr;

				for (i=0; i<peer_count; i++)
				{
					peer = peers_list[i];
					memset(&ib_qp_attr, 0, sizeof(struct ibv_qp_attr));
					if (verbs_enable_ud) { 
					  ib_qp_attr.qp_state       = IBV_QPS_RTR;
					  flags = IBV_QP_STATE;
					} else { 
					  ib_qp_attr.qp_state     			= IBV_QPS_RTR;
					  ib_qp_attr.path_mtu     			= ib_port_attr.active_mtu;
					  ib_qp_attr.dest_qp_num  			= qpinfo_all[peer].qpn;
					  ib_qp_attr.rq_psn       			= qpinfo_all[peer].psn;
					  ib_qp_attr.ah_attr.dlid 			= qpinfo_all[peer].lid;
					  ib_qp_attr.max_dest_rd_atomic     = 1;
					  ib_qp_attr.min_rnr_timer          = 12;
					  ib_qp_attr.ah_attr.is_global      = 0;
					  ib_qp_attr.ah_attr.sl             = 0;
					  ib_qp_attr.ah_attr.src_path_bits  = 0;
					  ib_qp_attr.ah_attr.port_num       = ib_port;
					  flags = IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU
					      | IBV_QP_DEST_QPN | IBV_QP_RQ_PSN
					      | IBV_QP_MIN_RNR_TIMER | IBV_QP_MAX_DEST_RD_ATOMIC;
					}

					ret = ibv_modify_qp(clients[i].qp->qp, &ib_qp_attr, flags);
					if (ret != 0) {
					  printf("Failed to modify RC QP to RTR\n");
					  return MP_FAILURE;
					}
				}

				//Barrier with oob object
				oob_comm->sync();

				for (i=0; i<peer_count; i++) {
					int flags = 0;
					peer = peers_list[i];

					memset(&ib_qp_attr, 0, sizeof(struct ibv_qp_attr));
					if (verbs_enable_ud) { 
					  ib_qp_attr.qp_state       = IBV_QPS_RTS;
					  ib_qp_attr.sq_psn         = 0;
					  flags = IBV_QP_STATE | IBV_QP_SQ_PSN; 
					} else { 
					  ib_qp_attr.qp_state       = IBV_QPS_RTS;
					  ib_qp_attr.sq_psn         = 0;
					  ib_qp_attr.timeout        = 20;
					  ib_qp_attr.retry_cnt      = 7;
					  ib_qp_attr.rnr_retry      = 7;
					  ib_qp_attr.max_rd_atomic  = 1;
					  flags = IBV_QP_STATE | IBV_QP_SQ_PSN | IBV_QP_TIMEOUT
					    | IBV_QP_RETRY_CNT | IBV_QP_RNR_RETRY
					    | IBV_QP_MAX_QP_RD_ATOMIC;
					}

					ret = ibv_modify_qp(clients[i].qp->qp, &ib_qp_attr, flags);
					if (ret != 0)
					{
						printf("Failed to modify RC QP to RTS\n");
						return MP_FAILURE;
					}

					if (verbs_enable_ud) {
					  printf("setting up connection with peer: %d lid: %d qpn: %d \n", peer, qpinfo_all[peer].lid,
					                 qpinfo_all[peer].qpn);

					  struct ibv_ah_attr ib_ah_attr;
					  memset(&ib_ah_attr, 0, sizeof(ib_ah_attr));
					  ib_ah_attr.is_global     = 0;
					  ib_ah_attr.dlid          = qpinfo_all[peer].lid;
					  ib_ah_attr.sl            = 0;
					  ib_ah_attr.src_path_bits = 0;
					  ib_ah_attr.port_num      = ib_port;

					  clients[i].ah = ibv_create_ah(ib_ctx->pd, &ib_ah_attr);
					  if (!clients[i].ah) {
					      printf("Failed to create AH\n");
					      return MP_FAILURE;
					  }

					  clients[i].qpn = qpinfo_all[peer].qpn; 
					}
				}

				if (verbs_enable_ud) {
					int result = register_buffer(ud_padding, UD_ADDITION, &ud_padding_reg);
					assert(result == MP_SUCCESS);
				}

				oob_comm->sync();

				return MP_SUCCESS;
			}

			void cleanupInit() {
				if(qpinfo_all)
					free(qpinfo_all);
			}


			int finalize() {
				int i, ret, retcode=0;
				mem_region_t *mem_region = NULL;

				printf("IBV finalize\n");

				oob_comm->sync();

				/*destroy IB resources*/
				for (i=0; i<peer_count; i++) {
					printf("peer %d\n", i);
#ifdef GDSYNC
				  	gds_destroy_qp (clients[i].qp);
#else
			        assert(clients[i].qp);

			        assert(clients[i].qp->qp);
			        ret = ibv_destroy_qp(clients[i].qp->qp);
			        if (ret) {
			                mp_err_msg("error %d in destroy_qp\n", ret);
			                retcode = ret;
			        }

			        assert(clients[i].qp->send_cq.cq);
			        ret = ibv_destroy_cq(clients[i].qp->send_cq.cq);
			        if (ret) {
			                mp_err_msg("error %d in destroy_cq send_cq\n", ret);
			                retcode = ret;
			        }

			        assert(clients[i].qp->recv_cq.cq);
			        ret = ibv_destroy_cq(clients[i].qp->recv_cq.cq);
			        if (ret) {
			                mp_err_msg("error %d in destroy_cq recv_cq\n", ret);
			                retcode = ret;
			        }

			        free(clients[i].qp);
#endif
				}

				ibv_dealloc_pd (ib_ctx->pd);
				ibv_close_device (ib_ctx->context);
/*
				while (mem_region_list != NULL) {
					mem_region = mem_region_list;
					mem_region_list = mem_region_list->next;
					free(mem_region->region);
					free(mem_region);
				}
*/

				/*free all buffers*/
				free(ib_ctx);
				free(client_index);
				free(clients);

				return MP_SUCCESS;			
			}

			// ===== COMMUNICATION
			int send() {
				return MP_SUCCESS;
			}

			int register_buffer(void * addr, size_t length, mp_key_t * mp_mem_key) {

				int flags;

				struct verbs_reg * reg = (verbs_reg_t)calloc(1, sizeof(struct verbs_reg));
				if (!reg) {
				  mp_err_msg("malloc returned NULL while allocating struct mp_reg\n");
				  return MP_FAILURE;
				}

				if (verbs_enable_ud) {
				  printf("UD enabled, registering buffer for LOCAL_WRITE\n");
				  flags = IBV_ACCESS_LOCAL_WRITE;
				} else { 
				  flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE |
				          IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_ATOMIC;
				}

				printf("ibv_reg_mr addr:%p size:%zu flags=0x%08x\n", addr, length, flags);
				// note: register addr, not base. no advantage in registering the whole buffer as we don't
				// maintain a registration cache yet
				reg->mr = ibv_reg_mr(ib_ctx->pd, addr, length, flags);
				if (!reg->mr) {
					mp_err_msg("ibv_reg_mr returned NULL for addr:%p size:%zu errno=%d(%s)\n", 
				            		addr, length, errno, strerror(errno));

					return MP_FAILURE;
				}

				reg->key = reg->mr->lkey;
				printf("reg=%p key=%x\n", reg, reg->key);
				*mp_mem_key = (mp_key_t)reg;

				return MP_SUCCESS;
			}

			mp_key_t * create_keys(int number) {

				if(number <= 0) {
					mp_err_msg("erroneuos requests number specified (%d)\n", number);
					return NULL;
				}

				struct verbs_reg * reg = (verbs_reg_t)calloc(number, sizeof(struct verbs_reg));
				if (!reg) {
				  mp_err_msg("malloc returned NULL while allocating struct mp_reg\n");
				  return NULL;
				}

				return (mp_key_t *) reg;
			}


			mp_request_t * create_requests(int number) {

				verbs_request_t * req;

				if(number <= 0) {
					mp_err_msg("erroneuos requests number specified (%d)\n", number);
					return NULL;
				}

				req = (verbs_request_t *) calloc(number, sizeof(verbs_request_t));
				if(!req) {
					mp_err_msg("calloc returned NULL while allocating struct verbs_request_t\n");
					return NULL;
				}

				return (mp_request_t *) req;
			}

			int receive(void * buf, size_t size, int peer, verbs_request_t * mp_req, mp_key_t * mp_mem_key) {
				int ret = 0;
				struct verbs_request *req = NULL;
				verbs_reg_t reg = (verbs_reg_t) *mp_mem_key;
				client_t *client = &clients[client_index[peer]];

				req = verbs_new_request(client, MP_RECV, MP_PENDING_NOWAIT);
				assert(req);

				printf("peer=%d req=%p buf=%p size=%zd id=%d reg=%p key=%x\n", peer, req, buf, size, req->id, reg, reg->key);
 
				req->in.rr.next = NULL;
				req->in.rr.wr_id = (uintptr_t) req;


				if (verbs_enable_ud) { 
				  struct verbs_reg *ud_reg = (struct verbs_reg *) ud_padding_reg;

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
					mp_err_msg("posting recv failed ret: %d error: %s peer: %d index: %d \n", ret, strerror(errno), peer, client_index[peer]);
					return MP_FAILURE;
				}

				*mp_req = req; 
				return MP_SUCCESS;
			}
	};
}


static TL::Communicator *create() { return new TL::Verbs(); }

static class update_tl_list {
	public: 
		update_tl_list() {
			add_tl_creator(TL_INDEX_VERBS, create);
		}
} tmp;
