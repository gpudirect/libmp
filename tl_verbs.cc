#include "tl_verbs.h"

namespace TL
{
	class Verbs : public Communicator {
		private:
			OOB::Communicator * oob_comm;
			
			struct ibv_device **dev_list;
			struct ibv_qp_attr ib_qp_attr;
			struct ibv_ah_attr ib_ah_attr;
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
	    			return VERBS_FAILURE;

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
					return VERBS_FAILURE;

				printf("peer_count=%d\n", peer_count);

	    		return VERBS_SUCCESS;
	    	}

			int setupNetworkDevices() {
				int i;

				/*pick the right device*/
				dev_list = ibv_get_device_list (&num_devices);
				if (dev_list == NULL) {
					printf("ibv_get_device_list returned NULL \n");
					return VERBS_FAILURE;
				}

				ib_dev = dev_list[0];
				if (ib_req_dev != NULL) {
				for (i=0; i<num_devices; i++) {
				  select_dev = ibv_get_device_name(dev_list[i]);
				  if (strstr(select_dev, ib_req_dev) != NULL) {
				    ib_dev = dev_list[i];
				    printf("using IB device: %s \n", ib_req_dev);
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
				printf("ib_ctx allocation failed \n");
				return VERBS_FAILURE;
				}

				ib_ctx->context = ibv_open_device(ib_dev);
				if (ib_ctx->context == NULL) {
					printf("ibv_open_device failed \n");
					return VERBS_FAILURE;
				}

				/*get device attributes and check relevant leimits*/
				if (ibv_query_device(ib_ctx->context, &dev_attr)) {
				printf("query_device failed \n"); 	 
				return VERBS_FAILURE;	
				}

				if (ib_max_sge > dev_attr.max_sge) {
				  printf("warning!! requested sgl length longer than supported by the adapter, reverting to max, requested: %d max: %d \n", ib_max_sge, dev_attr.max_sge);
				  ib_max_sge = dev_attr.max_sge;
				}

				return VERBS_SUCCESS;
			}

			int createEndpoints() {
#if 1
				int ret, i;
				/*establish connections*/
				client_index = (int *)calloc(oob_size, sizeof(int));
				if (client_index == NULL) {
					printf("allocation failed \n");
					return VERBS_FAILURE;
				}
				memset(client_index, -1, sizeof(int)*oob_size);

				clients = (client_t *)calloc(peer_count, sizeof(client_t));
				if (clients == NULL) {
					printf("allocation failed \n");
					return VERBS_FAILURE;
				}
				memset(clients, 0, sizeof(client_t)*peer_count);

				qpinfo_all =(qpinfo_t *)calloc(oob_size, sizeof(qpinfo_t));
				if (qpinfo_all == NULL) {
					printf("qpinfo allocation failed \n");
					return VERBS_FAILURE;
				}

				/*creating qps for all peers_list*/
				for (i=0; i<peer_count; i++) {
					// MPI rank of i-th peer
					peer = peers_list[i];
					/*rank to peer id mapping */
					client_index[peer] = i;
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

					gds_qp_init_attr_t ib_qp_init_attr;
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
#if 0
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
					  return VERBS_FAILURE;
					}
#else

#endif
					clients[i].send_cq = &clients[i].qp->send_cq;
					clients[i].recv_cq = &clients[i].qp->recv_cq;

					assert(clients[i].qp);
					assert(clients[i].send_cq);
					assert(clients[i].recv_cq);

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
					  printf("Failed to modify QP to INIT: %d, %s\n", ret, strerror(errno));
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

#endif
				return VERBS_SUCCESS;
			}

			int exchangeEndpoints() {
#if 0

				MPI_CHECK(MPI_Alltoall(MPI_IN_PLACE, sizeof(qpinfo_t),
			                         MPI_CHAR, qpinfo_all, sizeof(qpinfo_t),
			                         MPI_CHAR, oob_comm));
#endif

				return VERBS_SUCCESS;
			}	


			int updateEndpoints() {
#if 0
				int i;

				for (i=0; i<peer_count; i++)
				{
					int flags;
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
					  return VERBS_FAILURE;
					}
				}

				oob_comm.sync();

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
						return VERBS_FAILURE;
					}

					if (verbs_enable_ud) {
					  printf("setting up connection with peer: %d lid: %d qpn: %d \n", peer, qpinfo_all[peer].lid,
					                 qpinfo_all[peer].qpn);

					  memset(&ib_ah_attr, 0, sizeof(ib_ah_attr));
					  ib_ah_attr.is_global     = 0;
					  ib_ah_attr.dlid          = qpinfo_all[peer].lid;
					  ib_ah_attr.sl            = 0;
					  ib_ah_attr.src_path_bits = 0;
					  ib_ah_attr.port_num      = ib_port;

					  clients[i].ah = ibv_create_ah(ib_ctx->pd, &ib_ah_attr);
					  if (!clients[i].ah) {
					      printf("Failed to create AH\n");
					      return VERBS_FAILURE;
					  }

					  clients[i].qpn = qpinfo_all[peer].qpn; 
					}
				}
#endif
				return VERBS_SUCCESS;
			}

			void cleanupInit() {
				if(qpinfo_all)
					free(qpinfo_all);
			}


			int finalize() {
				return VERBS_SUCCESS;			
			}

			int send() {
				return VERBS_SUCCESS;
			}

			int receive() {
				return VERBS_SUCCESS;
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
