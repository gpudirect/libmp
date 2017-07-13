#include "tl.hpp"
#include <typeinfo>
#include <iostream>
#include <inttypes.h>
#include <stdio.h>
#include <psm2.h>     
/* required for core PSM2 functions */
#include <psm2_mq.h>  
/* required for PSM2 MQ functions (send, recv, etc) */
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>

struct psm2_request : mp_request {
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
    psm2_send_wr sr;
  } in;
  union
  {
    psm2_send_wr* bad_sr;
    struct ibv_recv_wr* bad_rr;
  } out;

  struct ibv_sge sg_entry;
  struct ibv_sge ud_sg_entry[2];
  struct ibv_sge *sgv;

  struct psm2_request *next;
  struct psm2_request *prev;
};
typedef struct psm2_request * psm2_request_t;

struct psm2_client {
    //void *region;
    //int region_size;
    int oob_rank;
    psm2_epid_t epid;
    psm2_epaddr_t epaddr;
    psm2_mq_t mq;
};
typedef struct psm2_client * psm2_client_t;

/*exchange info*/
typedef struct {
    psm2_epid_t epid;
} epid_info;
typedef epid_info * epid_info_t;

inline int psm2_req_type_rx(mp_req_type_t type)
{
    assert(type > MP_NULL && type < MP_N_TYPES);
    return (type == MP_RECV);
}

inline int psm2_req_type_tx(mp_req_type_t type)
{
    assert(type > MP_NULL && type < MP_N_TYPES);
    return (type == MP_SEND) || (type == MP_RDMA);
}

inline const char * psm2_flow_to_str(mp_flow_t flow) {
    return flow==TX_FLOW?"TX":"RX";
}

inline int psm2_req_valid(psm2_request_t req)
{
    return (req->type   > MP_NULL  && req->type   < MP_N_TYPES ) && 
           (req->status > MP_UNDEF && req->status < MP_N_STATES);
}

inline mp_flow_t psm2_type_to_flow(mp_req_type_t type)
{
	return psm2_req_type_tx(type) ? TX_FLOW : RX_FLOW;
}

inline int psm2_req_can_be_waited(psm2_request_t req)
{
    assert(psm2_req_valid(req));
    return psm2_req_type_rx(req->type) || (
        psm2_req_type_tx(req->type) && !(req->flags & MP_PUT_NOWAIT));
}


namespace TL
{
	class PSM2 : public Communicator {
		protected:
			OOB::Communicator * oob_comm;
			psm2_epid_t my_epid;

			int num_devices;
			const char *select_dev;
			char *ib_req_dev;
			int peer;
			int peer_count;
			epid_info_t epinfo_all;

			//int smp_depth;
			int ib_tx_depth;
			int ib_rx_depth;
			int num_cqes; // it gets actually rounded up to 512
			int ib_max_sge; 
			int ib_inline_size; 

			int peers_list[MAX_PEERS];
			psm2_client_t clients;
			int *client_index;
			int cq_poll_count;

			int psm2_enable_ud;
			psm2_request_t mp_request_free_list;
			mem_region_t *mem_region_list;
			char ud_padding[UD_ADDITION];
			mp_region_t ud_padding_reg;
			int mp_request_active_count;
			int psm2_request_limit;
			struct ibv_wc *wc;

			int oob_size, oob_rank;
			int mp_warn_is_enabled, mp_dbg_is_enabled;
			int qp_query;

			/*to enable opaque requests*/
			void psm2_allocate_requests();
			psm2_request_t psm2_get_request();
			psm2_request_t psm2_new_request(psm2_client_t client, mp_req_type_t type, mp_state_t state);
			void psm2_release_request(psm2_request_t req);
			int psm2_get_request_id(psm2_client_t client, mp_req_type_t type);
			int psm2_post_recv(psm2_client_t client, psm2_request_t req);
			int psm2_query_print_qp(struct ibv_qp *qp, psm2_request_t req);
			int psm2_post_send(psm2_client_t client, psm2_request_t req);
			int psm2_progress_single_flow(mp_flow_t flow);
			int psm2_client_can_poll(psm2_client_t client, mp_flow_t flow);
			int psm2_progress_request(psm2_request_t req);
			int cleanup_request(psm2_request_t req);
			void psm2_env_vars();
			int psm2_update_qp_rts(psm2_client_t client, int index, struct ibv_qp * qp);
			int psm2_update_qp_rtr(psm2_client_t client, int index, struct ibv_qp * qp);
			exchange_win_info * psm2_window_create(void *addr, size_t size, psm2_window_t *window_t);
			//Common fill requests
			int psm2_fill_send_request(void * buf, size_t size, int peer, psm2_region_t reg, uintptr_t req_id, 
                                      struct ibv_exp_send_wr * sr, struct ibv_sge * sg_entry, 
                                      struct ibv_ah *ah, uint32_t qpn);

			int psm2_fill_recv_request(void * buf, size_t size, int peer, psm2_region_t reg, uintptr_t req_id, 
                                      struct ibv_recv_wr * rr, struct ibv_sge * sg_entry, struct ibv_sge ud_sg_entry[2]);
			
			int psm2_fill_put_request(void * buf, size_t size, int peer, psm2_region_t reg, uintptr_t req_id, 
                                      struct ibv_exp_send_wr * sr, struct ibv_sge * sg_entry, 
                                      int client_id, psm2_window_t window, size_t displ, int * req_flags, int flags);

			int psm2_fill_get_request(void * buf, size_t size, int peer, psm2_region_t reg, uintptr_t req_id, 
                                      struct ibv_exp_send_wr * sr, struct ibv_sge * sg_entry, 
                                      int client_id, psm2_window_t window, size_t displ);

		public:
			~PSM2() {}

	    	PSM2() {
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
				psm2_request_limit = 512;
				psm2_enable_ud = 0;
				wc = NULL;

				clients=NULL;
				client_index=NULL;
				mp_request_free_list = NULL;
				mem_region_list = NULL;
				qp_query=0;

				psm2_env_vars();
	    	}

	    	int setupOOB(OOB::Communicator * input_comm);
			int setupNetworkDevices();
			int createEndpoints();
			int exchangeEndpoints();
			int updateEndpoints();
			void cleanupInit();
			int finalize();
			int deviceInfo();
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

			//============== GPUDirect Async - psm2_Async class ==============
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
#if 1
			int descriptors_kernel(KERNEL_DESCRIPTOR_SEM *psem, uint32_t *ptr, uint32_t value);
			int descriptors_kernel_send(mp_kernel_desc_send_t * sinfo, mp_request_t *mp_req);
			int descriptors_kernel_wait(mp_kernel_desc_wait_t * winfo, mp_request_t *mp_req);
#endif
			//================================================================
	};
}
