#include "tl_verbs.h"

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
			int oob_size, oob_rank;
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

			int verbs_enable_ud;

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

				verbs_enable_ud = 0;

				clients=NULL;
				client_index=NULL;

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
					fprintf(stderr, "ibv_get_device_list returned NULL \n");
					return MP_FAILURE;
				}

				ib_dev = dev_list[0];
				if (ib_req_dev != NULL) {
					for (i=0; i<num_devices; i++) {
					  select_dev = ibv_get_device_name(dev_list[i]);
					  if (strstr(select_dev, ib_req_dev) != NULL) {
					    ib_dev = dev_list[i];
					    fprintf(stderr, "using IB device: %s \n", ib_req_dev);
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
					fprintf(stderr, "ib_ctx allocation failed \n");
					return MP_FAILURE;
				}

				ib_ctx->context = ibv_open_device(ib_dev);
				if (ib_ctx->context == NULL) {
					fprintf(stderr, "ibv_open_device failed \n");
					return MP_FAILURE;
				}

				/*get device attributes and check relevant leimits*/
				if (ibv_query_device(ib_ctx->context, &dev_attr)) {
					fprintf(stderr, "query_device failed \n"); 	 
					return MP_FAILURE;	
				}

				if (ib_max_sge > dev_attr.max_sge) {
					fprintf(stderr, "warning!! requested sgl length longer than supported by the adapter, reverting to max, requested: %d max: %d \n", ib_max_sge, dev_attr.max_sge);
					ib_max_sge = dev_attr.max_sge;
				}

				ib_ctx->pd = ibv_alloc_pd (ib_ctx->context);
				if (ib_ctx->pd == NULL) {
					fprintf(stderr ,"ibv_alloc_pd failed \n");
					return MP_FAILURE;
				}

				ibv_query_port (ib_ctx->context, ib_port, &ib_port_attr);


				return MP_SUCCESS;
			}

			int createEndpoints() {
				int ret, i;
				/*establish connections*/
				client_index = (int *)calloc(oob_size, sizeof(int));
				if (client_index == NULL) {
					fprintf(stderr, "allocation failed \n");
					return MP_FAILURE;
				}
				memset(client_index, -1, sizeof(int)*oob_size);

				clients = (client_t *)calloc(peer_count, sizeof(client_t));
				if (clients == NULL) {
					fprintf(stderr, "allocation failed \n");
					return MP_FAILURE;
				}
				memset(clients, 0, sizeof(client_t)*peer_count);

				qpinfo_all =(qpinfo_t *)calloc(oob_size, sizeof(qpinfo_t));
				if (qpinfo_all == NULL) {
					fprintf(stderr, "qpinfo allocation failed \n");
					return MP_FAILURE;
				}
				//client_count = peer_count
				/*creating qps for all peers_list*/
				for (i=0; i<peer_count; i++) {
					// MPI rank of i-th peer
					peer = peers_list[i];
					/*rank to peer id mapping */
					client_index[peer] = i;
					printf("Creating client %d, peer %d, client_index[peer]: %d\n", i, peer, client_index[peer]);
					/*peer id to rank mapping */
					clients[i].mpi_rank = peer;
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
//CPU Synchronous case

/*
					gds_create_cq(struct ibv_context *context, int cqe,
						              void *cq_context, struct ibv_comp_channel *channel,
						              int comp_vector, int gpu_id, gds_alloc_cq_flags_t flags)
*/

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
			                fprintf(stderr, "error %d in ibv_exp_create_cq, old errno %d\n", errno, old_errno);
			        }
   					printf("ibv_exp_create_cq recv_cq\n");

			        recv_cq = ibv_exp_create_cq(ib_ctx->context, ib_qp_init_attr.cap.max_recv_wr /*num cqe*/, NULL /* cq_context */, NULL /* channel */, 0 /*comp_vector*/, &cq_attr);
			        if (!recv_cq) {
			                fprintf(stderr, "error %d in ibv_exp_create_cq, old errno %d\n", errno, old_errno);
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

   					clients[i].qp = (struct gds_qp*)calloc(1, sizeof(struct gds_qp));
			        if (!clients[i].qp) {
			                fprintf(stderr, "cannot allocate memory\n");
			                return NULL;
			        }
			        printf("ibv_exp_create_qp qp\n");

			        clients[i].qp->qp = ibv_exp_create_qp(ib_ctx->context, &ib_qp_init_attr);
			        if (!clients[i].qp->qp)  {
			                ret = EINVAL;
			                fprintf(stderr, "error in ibv_exp_create_qp\n");
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
			        	fprintf(stderr, "error %d destroying RX CQ\n", ret);

				err_free_tx_cq:
			        printf("destroying TX CQ\n");
					ret = ibv_destroy_cq(recv_cq);
			        if (ret)
			        	fprintf(stderr, "error %d destroying TX CQ\n", ret);

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
					  fprintf(stderr, "Failed to modify QP to INIT: %d, %s\n", ret, strerror(errno));
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
/*
				if (mp_enable_ud) {
					int result = mp_register(ud_padding, UD_ADDITION, &ud_padding_reg);
					assert(result == MP_SUCCESS);
				}
*/
				oob_comm->sync();

				return MP_SUCCESS;
			}

			void cleanupInit() {
				if(qpinfo_all)
					free(qpinfo_all);
			}


			int finalize() {
				int i;
				mem_region_t *mem_region = NULL;

printf("IBV finalize\n");

				oob_comm->sync();

				/*destroy IB resources*/
				for (i=0; i<peer_count; i++) {
					printf("peer %d\n", i);
#ifdef GDSYNC
				  	gds_destroy_qp (clients[i].qp);
#else
					int retcode = 0;
			        int ret;
			        assert(clients[i].qp);

			        assert(clients[i].qp->qp);
			        ret = ibv_destroy_qp(clients[i].qp->qp);
			        if (ret) {
			                fprintf(stderr, "error %d in destroy_qp\n", ret);
			                retcode = ret;
			        }

			        assert(clients[i].qp->send_cq.cq);
			        ret = ibv_destroy_cq(clients[i].qp->send_cq.cq);
			        if (ret) {
			                fprintf(stderr, "error %d in destroy_cq send_cq\n", ret);
			                retcode = ret;
			        }

			        assert(clients[i].qp->recv_cq.cq);
			        ret = ibv_destroy_cq(clients[i].qp->recv_cq.cq);
			        if (ret) {
			                fprintf(stderr, "error %d in destroy_cq recv_cq\n", ret);
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

			int send() {
				return MP_SUCCESS;
			}

			int receive() {
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
