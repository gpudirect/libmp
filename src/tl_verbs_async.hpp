#include "tl_verbs.hpp"
#include <infiniband/peer_ops.h>
#include <gdsync.h>
#include <gdsync/tools.h>
#include <gdsync/core.h>
#include <gdsync/device.cuh>
#include <gdsync/mlx5.h>
#include <vector>

#define CHECK_GPU(gid) {					\
	if(gid <= MP_NONE)					\
	{										\
		mp_err_msg(0, "GPU check failed: %d\n", gid);	\
		return MP_FAILURE;	\
	}							\
}

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

	struct verbs_request_async *next;
	struct verbs_request_async *prev;

};
typedef struct verbs_request_async * verbs_request_async_t;

struct verbs_client_async : verbs_client {
	struct gds_qp *gqp;
	struct gds_cq *send_gcq;
	struct gds_cq *recv_gcq;

	//Async only? Move in the child struct??
    verbs_request_async_t last_posted_stream_req[N_FLOWS];
    verbs_request_async_t posted_stream_req[N_FLOWS];
    verbs_request_async_t last_waited_stream_req[N_FLOWS]; //head
    verbs_request_async_t waited_stream_req[N_FLOWS]; //tail
};
typedef struct verbs_client_async * verbs_client_async_t;

struct verbs_comm_desc_queue : mp_comm_descriptors_queue {
	std::vector<gds_descriptor_t> descs;

	verbs_comm_desc_queue(size_t reserved_space = 128) {
	    descs.reserve(reserved_space);
	}

	size_t num_descs() {
	    size_t n_descs;
	    try {
	        n_descs = descs.size();
	    } catch(...) {
	        fprintf(stderr, "got C++ excepton in desc queue num_descs\n");
	        exit(EXIT_FAILURE);
	    }
	    return n_descs;
	}
	    
	gds_descriptor_t *head() {
	    gds_descriptor_t *ptr;
	    if (descs.size() < 1) {
	        fprintf(stderr, "desc_queue is empty\n");
	        return NULL;
	    }
	    try {
	        ptr = &descs.at(0);
	    } catch(...) {
	        fprintf(stderr, "got C++ excepton in desc queue head\n");
	        exit(EXIT_FAILURE);
	    }
	    return ptr;
	}

	void add(gds_descriptor_t &desc) {
	    //fprintf(stderr, "adding entry %lu\n", descs.size());
	    try {
	        descs.push_back(desc);
	    } catch(...) {
	        fprintf(stderr, "got C++ excepton in desc queue add\n");
	        exit(EXIT_FAILURE);
	    }
	}

	void flush() {
	    try {
	        descs.clear();
	    } catch(...) {
	        fprintf(stderr, "got C++ excepton in desc queue flush\n");
	        exit(EXIT_FAILURE);
	    }
	}
};
typedef struct verbs_comm_desc_queue * verbs_comm_desc_queue_t;

#if 0
struct verbs_kernel_send_desc : mp_kernel_send_desc {
	gdsync::isem32_t dbrec;
	gdsync::isem64_t db;
};
typedef struct verbs_kernel_send_desc * verbs_kernel_send_desc_t;

struct verbs_kernel_wait_desc : mp_kernel_wait_desc {
	gdsync::wait_cond_t sema_cond;
	gdsync::isem32_t sema;
	gdsync::isem32_t flag;
};
typedef struct verbs_kernel_wait_desc * verbs_kernel_wait_desc_t;
#endif

#if (GDS_API_MAJOR_VERSION==2 && GDS_API_MINOR_VERSION>=2) || (GDS_API_MAJOR_VERSION>2)
#define HAS_GDS_DESCRIPTOR_API 1
#endif


namespace TL
{
	class Verbs_Async : public Verbs {
		protected:
			int gpu_id;
			gds_send_request_t *gds_send_info_region;
			gds_wait_request_t *gds_wait_info_region;
			int use_event_sync, use_dbrec_gpu, use_wq_gpu, use_rx_cq_gpu, use_tx_cq_gpu;
			verbs_client_async_t clients_async;
			verbs_request_async_t mp_request_free_list;

			int verbs_check_gpu_error();
			uint32_t *verbs_client_last_tracked_id_ptr(verbs_client_async_t client, verbs_request_async_t req);
			void verbs_client_track_posted_stream_req(verbs_client_async_t client, verbs_request_async_t req, mp_flow_t flow);
			void verbs_client_track_waited_stream_req(verbs_client_async_t client, verbs_request_async_t req, mp_flow_t flow);
			int verbs_client_can_poll(verbs_client_async_t client, mp_flow_t flow);
			int verbs_progress_posted_list (mp_flow_t flow);

			//=================================================== OVERRIDE ==============================================================
			int verbs_progress_single_flow(mp_flow_t flow);
			int verbs_post_recv(verbs_client_async_t client, verbs_request_async_t req);
			int verbs_post_send(verbs_client_async_t client, verbs_request_async_t req, cudaStream_t stream, int async, int event_sync, int no_wait);
			void  verbs_allocate_requests();
			verbs_request_async_t  verbs_get_request();
			verbs_request_async_t verbs_new_request(verbs_client_async_t client, mp_req_type_t type, mp_state_t state); //, struct CUstream_st *stream)
			//=========================================================================================================================
			void verbs_env_vars();

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
				gds_send_info_region=NULL;
				gds_wait_info_region=NULL;
				mp_request_free_list=NULL;
				verbs_env_vars();
			}
			
			//=================================================== OVERRIDE ==============================================================
			//NON DA INCLUDERE DI NUOVO!
			int createEndpoints();
			int updateEndpoints();
			int finalize();
			int pt2pt_nb_recv(void * buf, size_t size, int peer, mp_region_t * mp_reg, mp_request_t * mp_req);
			int pt2pt_nb_recvv(struct iovec *v, int nvecs, int peer, mp_region_t * mp_reg, mp_request_t * mp_req);
			int pt2pt_nb_send(void * buf, size_t size, int peer, mp_region_t * mp_reg, mp_request_t * mp_req);
			int pt2pt_nb_sendv(struct iovec *v, int nvecs, int peer, mp_region_t * mp_reg, mp_request_t * mp_req);
			
			int wait_all(int count, mp_request_t *req_);

			int onesided_window_create(void *addr, size_t size, mp_window_t *window_t);
			int onesided_nb_put (void *buf, int size, mp_region_t *mp_reg, int peer, size_t displ, mp_window_t *window_t, mp_request_t *mp_req, int flags);
			int onesided_nb_get(void *buf, int size, mp_region_t *mp_reg, int peer, size_t displ, mp_window_t *window_t, mp_request_t *mp_req);
			int setup_sublayer(int par1);
			//=========================================================================================================================

			//==================================================== ASYNC ===============================================================
			int pt2pt_nb_send_async(void * buf, size_t size, int peer, mp_region_t * mp_reg, mp_request_t * mp_req, asyncStream stream);
			//mp_isendv_on_stream
			int pt2pt_nb_sendv_async (struct iovec *v, int nvecs, int peer, mp_region_t * mp_reg, mp_request_t * mp_req, asyncStream stream);
			//mp_send_on_stream
			int pt2pt_b_send_async(void *buf, int size, int peer, mp_region_t * mp_reg,  mp_request_t *mp_req, asyncStream stream);
			//mp_send_prepare
			int pt2pt_send_prepare(void *buf, int size, int peer, mp_region_t * mp_reg, mp_request_t *mp_req);
			//mp_sendv_prepare
			int pt2pt_sendv_prepare(struct iovec *v, int nvecs, int peer, mp_region_t * mp_reg, mp_request_t *mp_req);
			//mp_send_post_on_stream
			int pt2pt_b_send_post_async(mp_request_t *mp_req, asyncStream stream);
			//mp_send_post_all_on_stream
			int pt2pt_b_send_post_all_async(int count, mp_request_t *mp_req, asyncStream stream);
			//mp_isend_post_on_stream
			int pt2pt_nb_send_post_async(mp_request_t *mp_req, asyncStream stream);
			//mp_isend_post_all_on_stream
			int pt2pt_nb_send_post_all_async(int count, mp_request_t *mp_req, asyncStream stream);
			//useful only with gds??
			int progress_requests (int count, mp_request_t *req_);
			int wait_word_async(uint32_t *ptr, uint32_t value, int flags, asyncStream stream);
			int wait_async(mp_request_t *mp_req, asyncStream stream);
			int wait_all_async(int count, mp_request_t *mp_req, asyncStream stream);
			int onesided_nb_put_async(void *buf, int size, mp_region_t * mp_reg, int peer, size_t displ,
                       mp_window_t *window_t, mp_request_t *mp_req, int flags, asyncStream stream);
			//mp_put_prepare
			int onesided_put_prepare (void *buf, int size, mp_region_t * mp_reg, int peer, size_t displ,
                    mp_window_t *window_t, mp_request_t *mp_req, int flags);
			int onesided_nb_put_post_async(mp_request_t *mp_req, asyncStream stream);
			int onesided_nb_put_post_all_async (int count, mp_request_t *mp_req, asyncStream stream);
			int onesided_nb_get_async(void *buf, int size, mp_region_t * mp_reg, int peer, size_t displ,
										mp_window_t *window_t, mp_request_t *mp_req, asyncStream stream);
			//=========================================================================================================================

			//================================== ASYNC GDS DESCRIPTOR =================================================
			int descriptors_queue_alloc(mp_comm_descriptors_queue_t *pdq);
			int descriptors_queue_free(mp_comm_descriptors_queue_t *pdq);
			int descriptors_queue_add_send(mp_comm_descriptors_queue_t *pdq, mp_request_t *mp_req);
			int descriptors_queue_add_wait_send(mp_comm_descriptors_queue_t *pdq, mp_request_t *mp_req);
			int descriptors_queue_add_wait_recv(mp_comm_descriptors_queue_t *pdq, mp_request_t *mp_req);
			int descriptors_queue_add_wait_value32(mp_comm_descriptors_queue_t *pdq, uint32_t *ptr, uint32_t value, int flags);
			int descriptors_queue_add_write_value32(mp_comm_descriptors_queue_t *pdq, uint32_t *ptr, uint32_t value);
			int descriptors_queue_post_async(cudaStream_t stream, mp_comm_descriptors_queue_t *pdq, int flags);
#if 1
			//================================== ASYNC KERNEL DESCRIPTOR =================================================
			int descriptors_kernel(KERNEL_DESCRIPTOR_SEM *psem, uint32_t *ptr, uint32_t value);
			int descriptors_kernel_send(mp_kernel_desc_send_t * sinfo, mp_request_t *mp_req);
			int descriptors_kernel_wait(mp_kernel_desc_wait_t * winfo, mp_request_t *mp_req);
#endif
	};
}