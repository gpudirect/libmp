#include "tl.hpp"
#include "tl_verbs_common.hpp"
#include "tl_verbs_types.hpp"

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
			struct verbs_request *verbs_get_request();
			struct verbs_request *verbs_new_request(client_t *client, mp_req_type_t type, mp_state_t state);
			void verbs_release_request(verbs_request_t req);
			inline const char *verbs_flow_to_str(mp_flow_t flow);
			inline int verbs_req_type_rx(mp_req_type_t type);
			inline int verbs_req_type_tx(mp_req_type_t type);
			inline int verbs_req_valid(struct verbs_request *req);
			inline mp_flow_t verbs_type_to_flow(mp_req_type_t type);
			inline int verbs_req_can_be_waited(struct verbs_request *req);
			int verbs_get_request_id(client_t *client, mp_req_type_t type);
			int verbs_post_recv(client_t *client, struct verbs_request *req);
			int verbs_query_print_qp(struct verbs_qp *qp, verbs_request_t req, int async);
			int verbs_post_send(client_t *client, struct verbs_request *req);
			int verbs_progress_single_flow(mp_flow_t flow);
			int verbs_client_can_poll(client_t *client, mp_flow_t flow);
			int verbs_progress_request(struct verbs_request *req);
			int cleanup_request(struct verbs_request *req);
			void verbs_env_vars();

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
			int register_key_buffer(void * addr, size_t length, mp_key_t * mp_mem_key);
			int unregister_key(mp_key_t *reg_);
			mp_key_t * create_keys(int number);
			mp_request_t * create_requests(int number);

			//====================== WAIT ======================
			int wait(mp_request_t *req);
			int wait_all(int count, mp_request_t *req_);
			int wait_word(uint32_t *ptr, uint32_t value, int flags);

			//====================== PT2PT ======================
			//point-to-point communications
			int pt2pt_nb_receive(void * buf, size_t size, int peer, mp_request_t * mp_req, mp_key_t * mp_mem_key);
			int pt2pt_nb_send(void * buf, size_t size, int peer, mp_request_t * mp_req, mp_key_t * mp_mem_key);

			//====================== ONE-SIDED ======================
			//one-sided operations: window creation, put and get
			int onesided_window_create(void *addr, size_t size, mp_window_t *window_t);
			int onesided_window_destroy(mp_window_t *window_t);
			int onesided_nb_put (void *src, int size, mp_key_t *reg_t, int peer, size_t displ, mp_window_t *window_t, mp_request_t *req_t, int flags);
			int onesided_nb_get(void *dst, int size, mp_key_t *reg_t, int peer, size_t displ, mp_window_t *window_t, mp_request_t *req_t);

			//============== GPUDirect Async - Verbs_Async class ==============
			int setup_sublayer(int par1);
	        int pt2pt_nb_send_async(void * rBuf, size_t size, int client_id, mp_request_t * mp_req, mp_key_t * mp_mem_key, asyncStream async_stream);
	        int pt2pt_b_send_async(void * rBuf, size_t size, int client_id, mp_request_t * mp_req, mp_key_t * mp_mem_key, asyncStream async_stream);
	        int pt2pt_send_prepare(void *buf, int size, int peer, mp_key_t *reg_t, mp_request_t *req_t);
			int pt2pt_b_send_post_async(mp_request_t *req_t, asyncStream stream);
			int pt2pt_b_send_post_all_async(int count, mp_request_t *req_t, asyncStream stream);
			int pt2pt_nb_send_post_async(mp_request_t *req_t, asyncStream stream);
			int pt2pt_nb_send_post_all_async(int count, mp_request_t *req_t, asyncStream stream);
			int wait_async (mp_request_t *req_t, asyncStream stream);
	        int wait_all_async(int count, mp_request_t *req_t, asyncStream stream);
	        int wait_word_async(uint32_t *ptr, uint32_t value, int flags, asyncStream stream);
			int onesided_nb_put_async(void *src, int size, mp_key_t *mp_key, int peer, size_t displ, mp_window_t *window_t, mp_request_t *mp_req, int flags, asyncStream stream);
			int onesided_nb_get_async(void *dst, int size, mp_key_t *mp_key, int peer, size_t displ, mp_window_t *window_t, mp_request_t *mp_req, asyncStream stream);
			int onesided_put_prepare (void *src, int size, mp_key_t *mp_key, int peer, size_t displ, mp_window_t *window_t, mp_request_t *req_t, int flags);
			int onesided_nb_put_post_async(mp_request_t *mp_req, asyncStream stream);
			int onesided_nb_put_post_all_async (int count, mp_request_t *mp_req, asyncStream stream);
			//useful only with gds??
			int progress_requests (int count, mp_request_t *req_);
			//================================================================
	};
}
