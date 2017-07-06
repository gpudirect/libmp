#include "tl.hpp"
#include "tl_verbs_common.hpp"
//#include "tl_verbs_types.hpp"
#include <typeinfo>

struct verbs_request : mp_request {
  int peer;
  mp_req_type_t type;
  int status;
  int trigger;
  int id;
  //uint32_t id;
  int flags;
  union
  {
    struct ibv_recv_wr rr;
    verbs_send_wr sr;
  } in;
  union
  {
    verbs_send_wr* bad_sr;
    struct ibv_recv_wr* bad_rr;
  } out;

  struct ibv_sge sg_entry;
  struct ibv_sge ud_sg_entry[2];
  struct ibv_sge *sgv;

  struct verbs_request *next;
  struct verbs_request *prev;
};
typedef struct verbs_request * verbs_request_t;

struct verbs_client {
    //void *region;
    //int region_size;
    int oob_rank;
    uint32_t last_req_id;
    uint32_t last_done_id;
    uint32_t last_posted_trigger_id[N_FLOWS];
    uint32_t last_posted_tracked_id[N_FLOWS];
    uint32_t last_trigger_id[N_FLOWS]; //has to be moved to device
    uint32_t last_tracked_id[N_FLOWS];

    /*ib related*/
    struct ibv_qp *qp;
    struct ibv_cq *send_cq;
    uint32_t send_cq_curr_offset;
    struct ibv_cq *recv_cq;
    uint32_t recv_cq_curr_offset;
    
    struct ibv_mr *region_mr;
    //UD info
    struct ibv_ah *ah;
    uint32_t qpn;

  #ifdef HAVE_IPC
    //ICP
    int is_local;
    int local_rank;
    int can_use_ipc;
    //   smp_channel_t smp;
    //   ipc_handle_cache_entry_t *ipc_handle_cache;
    struct verbs_request *last_posted_ipc_rreq;
    struct verbs_request *posted_ipc_rreq;
    struct verbs_request *last_processed_ipc_rreq;
    struct verbs_request *processed_ipc_rreq;
  #endif
};
typedef struct verbs_client * verbs_client_t;

inline int verbs_req_type_rx(mp_req_type_t type)
{
    assert(type > MP_NULL && type < MP_N_TYPES);
    return (type == MP_RECV);
}

inline int verbs_req_type_tx(mp_req_type_t type)
{
    assert(type > MP_NULL && type < MP_N_TYPES);
    return (type == MP_SEND) || (type == MP_RDMA);
}

inline const char * verbs_flow_to_str(mp_flow_t flow) {
    return flow==TX_FLOW?"TX":"RX";
}

inline int verbs_req_valid(verbs_request_t req)
{
    return (req->type   > MP_NULL  && req->type   < MP_N_TYPES ) && 
           (req->status > MP_UNDEF && req->status < MP_N_STATES);
}

inline mp_flow_t verbs_type_to_flow(mp_req_type_t type)
{
	return verbs_req_type_tx(type) ? TX_FLOW : RX_FLOW;
}

inline int verbs_req_can_be_waited(verbs_request_t req)
{
    assert(verbs_req_valid(req));
    return verbs_req_type_rx(req->type) || (
        verbs_req_type_tx(req->type) && !(req->flags & MP_PUT_NOWAIT));
}


namespace TL
{
	class Verbs : public Communicator {
		protected:
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
			verbs_client_t clients;
			int *client_index;
			int cq_poll_count;

			int verbs_enable_ud;
			verbs_request_t mp_request_free_list;
			mem_region_t *mem_region_list;
			char ud_padding[UD_ADDITION];
			mp_region_t ud_padding_reg;
			int mp_request_active_count;
			int verbs_request_limit;
			struct ibv_wc *wc;

			int oob_size, oob_rank;
			int mp_warn_is_enabled, mp_dbg_is_enabled;
			int qp_query;

#ifdef	HAVE_IPC
			char shm_filename[100];
			int shm_fd;
			int shm_client_bufsize;
			int shm_proc_bufsize;
			int shm_filesize;
			void *shm_mapptr;
			char ud_padding[UD_ADDITION];
#endif
			/*to enable opaque requests*/
			void verbs_allocate_requests();
			verbs_request_t verbs_get_request();
			verbs_request_t verbs_new_request(verbs_client_t client, mp_req_type_t type, mp_state_t state);
			void verbs_release_request(verbs_request_t req);
			int verbs_get_request_id(verbs_client_t client, mp_req_type_t type);
			int verbs_post_recv(verbs_client_t client, verbs_request_t req);
			int verbs_query_print_qp(struct ibv_qp *qp, verbs_request_t req);
			int verbs_post_send(verbs_client_t client, verbs_request_t req);
			int verbs_progress_single_flow(mp_flow_t flow);
			int verbs_client_can_poll(verbs_client_t client, mp_flow_t flow);
			int verbs_progress_request(verbs_request_t req);
			int cleanup_request(verbs_request_t req);
			void verbs_env_vars();
			int verbs_update_qp_rts(verbs_client_t client, int index, struct ibv_qp * qp);
			int verbs_update_qp_rtr(verbs_client_t client, int index, struct ibv_qp * qp);
			exchange_win_info * verbs_window_create(void *addr, size_t size, verbs_window_t *window_t);
			//Common fill requests
			int verbs_fill_send_request(void * buf, size_t size, int peer, verbs_region_t reg, uintptr_t req_id, 
                                      struct ibv_exp_send_wr * sr, struct ibv_sge * sg_entry, 
                                      struct ibv_ah *ah, uint32_t qpn);

			int verbs_fill_recv_request(void * buf, size_t size, int peer, verbs_region_t reg, uintptr_t req_id, 
                                      struct ibv_recv_wr * rr, struct ibv_sge * sg_entry, struct ibv_sge ud_sg_entry[2]);
			
			int verbs_fill_put_request(void * buf, size_t size, int peer, verbs_region_t reg, uintptr_t req_id, 
                                      struct ibv_exp_send_wr * sr, struct ibv_sge * sg_entry, 
                                      int client_id, verbs_window_t window, size_t displ, int * req_flags, int flags);

			int verbs_fill_get_request(void * buf, size_t size, int peer, verbs_region_t reg, uintptr_t req_id, 
                                      struct ibv_exp_send_wr * sr, struct ibv_sge * sg_entry, 
                                      int client_id, verbs_window_t window, size_t displ);

		public:
			~Verbs() {}

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
				qp_query=0;

				verbs_env_vars();
	    	}

	    	int setupOOB(OOB::Communicator * input_comm);
			int setupNetworkDevices();
			int createEndpoints();
			int exchangeEndpoints();
			int updateEndpoints();
			void cleanupInit();
			int finalize();
			// ===== COMMUNICATION
			int register_region_buffer(void * addr, size_t length, mp_region_t * mp_reg);
			int unregister_region(mp_region_t *reg_);
			mp_region_t * create_regions(int number);
			mp_request_t * create_requests(int number);

			//====================== WAIT ======================
			int wait(mp_request_t *req);
			int wait_all(int count, mp_request_t *req_);
			int wait_word(uint32_t *ptr, uint32_t value, int flags);

			//====================== PT2PT ======================
			//point-to-point communications
			int pt2pt_nb_recv(void * buf, size_t size, int peer, mp_region_t * mp_reg, mp_request_t * mp_req);
			int pt2pt_nb_recvv(struct iovec *v, int nvecs, int peer, mp_region_t * mp_reg, mp_request_t * mp_req);
			int pt2pt_nb_send(void * buf, size_t size, int peer, mp_region_t * mp_reg, mp_request_t * mp_req);
			int pt2pt_nb_sendv(struct iovec *v, int nvecs, int peer, mp_region_t * mp_reg, mp_request_t * mp_req);

			//====================== ONE-SIDED ======================
			//one-sided operations: window creation, put and get
			int onesided_window_create(void *addr, size_t size, mp_window_t *window_t);
			int onesided_window_destroy(mp_window_t *window_t);
			int onesided_nb_put (void *src, int size, mp_region_t *reg_t, int peer, size_t displ, mp_window_t *window_t, mp_request_t *mp_req, int flags);
			int onesided_nb_get(void *dst, int size, mp_region_t *reg_t, int peer, size_t displ, mp_window_t *window_t, mp_request_t *mp_req);

			//============== GPUDirect Async - Verbs_Async class ==============
			int setup_sublayer(int par1);
			int pt2pt_nb_send_async(void * rBuf, size_t size, int client_id, mp_region_t * mp_reg, mp_request_t * mp_req, asyncStream async_stream);
			int pt2pt_nb_sendv_async (struct iovec *v, int nvecs, int peer, mp_region_t * mp_reg, mp_request_t * mp_req, asyncStream stream);
			int pt2pt_b_send_async(void * rBuf, size_t size, int client_id, mp_region_t * mp_reg, mp_request_t * mp_req, asyncStream async_stream);
			int pt2pt_send_prepare(void *buf, int size, int peer, mp_region_t *reg_t, mp_request_t *mp_req);
			int pt2pt_sendv_prepare(struct iovec *v, int nvecs, int peer, mp_region_t * mp_reg, mp_request_t *mp_req);
			int pt2pt_b_send_post_async(mp_request_t * mp_req, asyncStream stream);
			int pt2pt_b_send_post_all_async(int count, mp_request_t * mp_req, asyncStream stream);
			int pt2pt_nb_send_post_async(mp_request_t * mp_req, asyncStream stream);
			int pt2pt_nb_send_post_all_async(int count, mp_request_t * mp_req, asyncStream stream);
			int wait_async (mp_request_t * mp_req, asyncStream stream);
			int wait_all_async(int count, mp_request_t * mp_req, asyncStream stream);
			int wait_word_async(uint32_t *ptr, uint32_t value, int flags, asyncStream stream);
			int onesided_nb_put_async(void *src, int size, mp_region_t *mp_region, int peer, size_t displ, mp_window_t *window_t, mp_request_t *mp_req, int flags, asyncStream stream);
			int onesided_nb_get_async(void *dst, int size, mp_region_t *mp_region, int peer, size_t displ, mp_window_t *window_t, mp_request_t *mp_req, asyncStream stream);
			int onesided_put_prepare (void *src, int size, mp_region_t *mp_region, int peer, size_t displ, mp_window_t *window_t, mp_request_t * mp_req, int flags);
			int onesided_nb_put_post_async(mp_request_t *mp_req, asyncStream stream);
			int onesided_nb_put_post_all_async (int count, mp_request_t *mp_req, asyncStream stream);
			//useful only with gds??
			int progress_requests (int count, mp_request_t * mp_req);
			int descriptors_queue_alloc(mp_comm_descriptors_queue_t *pdq);
			int descriptors_queue_free(mp_comm_descriptors_queue_t *pdq);
			int descriptors_queue_add_send(mp_comm_descriptors_queue_t *pdq, mp_request_t *mp_req);
			int descriptors_queue_add_wait_send(mp_comm_descriptors_queue_t *pdq, mp_request_t *mp_req);
			int descriptors_queue_add_wait_recv(mp_comm_descriptors_queue_t *pdq, mp_request_t *mp_req);
			int descriptors_queue_add_wait_value32(mp_comm_descriptors_queue_t *pdq, uint32_t *ptr, uint32_t value, int flags);
			int descriptors_queue_add_write_value32(mp_comm_descriptors_queue_t *pdq, uint32_t *ptr, uint32_t value);
			int descriptors_queue_post_async(asyncStream stream, mp_comm_descriptors_queue_t *pdq, int flags);
			//================================================================
	};
}
