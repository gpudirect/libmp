#ifndef TL_H
#define TL_H

#include "oob.hpp"
#include "mp_common.hpp"

namespace TL
{
        class Communicator {
        public:

                Communicator(){ }
                virtual ~Communicator()=0;

                virtual int setupOOB(OOB::Communicator * input_comm)=0;
                virtual int setupNetworkDevices()=0;
                virtual int createEndpoints()=0;
                virtual int exchangeEndpoints()=0;
                virtual int updateEndpoints()=0;
                virtual void cleanupInit()=0;
                virtual int finalize()=0;
                virtual int deviceInfo()=0;

// ===== COMMUNICATION
                virtual int register_region_buffer(void * addr, size_t length, mp_region_t * mp_reg)=0;
                virtual int unregister_region(mp_region_t *reg_)=0;
                virtual mp_region_t * create_regions(int number)=0;
                virtual mp_request_t * create_requests(int number)=0;

//====================== WAIT ======================
                virtual int wait(mp_request_t *req)=0;
                virtual int wait_all(int count, mp_request_t *req_)=0;
                virtual int wait_word(uint32_t *ptr, uint32_t value, int flags)=0;

//====================== PT2PT ======================
//point-to-point communications
                virtual int pt2pt_nb_recv(void * buf, size_t size, int peer, mp_region_t * mp_reg, mp_request_t * mp_req)= 0;
                virtual int pt2pt_nb_recvv(struct iovec *v, int nvecs, int peer, mp_region_t * mp_reg, mp_request_t * mp_req)= 0;
                virtual int pt2pt_nb_send(void * buf, size_t size, int peer, mp_region_t * mp_reg, mp_request_t * mp_req)= 0;
                virtual int pt2pt_nb_sendv(struct iovec *v, int nvecs, int peer, mp_region_t * mp_reg, mp_request_t * mp_req)= 0;

//====================== ONE-SIDED ======================
//one-sided operations: window creation, put and get
                virtual int onesided_window_create(void *addr, size_t size, mp_window_t *window_t)=0;
                virtual int onesided_window_destroy(mp_window_t *window_t)=0;
                virtual int onesided_nb_put (void *src, int size, mp_region_t *reg_t, int peer, size_t displ, mp_window_t *window_t, mp_request_t *req_t, int flags)=0;
                virtual int onesided_nb_get(void *dst, int size, mp_region_t *reg_t, int peer, size_t displ, mp_window_t *window_t, mp_request_t *req_t)=0;

//============== GPUDirect Async - Verbs_Async class ==============
                virtual int setup_sublayer(int par1)=0;
                virtual int pt2pt_nb_send_async(void * rBuf, size_t size, int client_id, mp_region_t * mp_reg, mp_request_t * mp_req, asyncStream async_stream)=0;
                virtual int pt2pt_nb_sendv_async (struct iovec *v, int nvecs, int peer, mp_region_t * mp_reg, mp_request_t * mp_req, asyncStream stream)=0;
                virtual int pt2pt_b_send_async(void * rBuf, size_t size, int client_id, mp_region_t * mp_reg, mp_request_t * mp_req, asyncStream async_stream)=0;
                virtual int pt2pt_send_prepare(void *buf, int size, int peer, mp_region_t *reg_t, mp_request_t *req_t)=0;
                virtual int pt2pt_sendv_prepare(struct iovec *v, int nvecs, int peer, mp_region_t * mp_reg, mp_request_t *mp_req)=0;
                virtual int pt2pt_b_send_post_async(mp_request_t *req_t, asyncStream stream)=0;
                virtual int pt2pt_b_send_post_all_async(int count, mp_request_t *req_t, asyncStream stream)=0;
                virtual int pt2pt_nb_send_post_async(mp_request_t *req_t, asyncStream stream)=0;
                virtual int pt2pt_nb_send_post_all_async(int count, mp_request_t *req_t, asyncStream stream)=0;
                virtual int wait_async (mp_request_t *req_t, asyncStream stream)=0;
                virtual int wait_all_async(int count, mp_request_t *req_t, asyncStream stream)=0;
                virtual int wait_word_async(uint32_t *ptr, uint32_t value, int flags, asyncStream stream)=0;
                virtual int onesided_nb_put_async(void *src, int size, mp_region_t *mp_region, int peer, size_t displ, mp_window_t *window_t, mp_request_t *mp_req, int flags, asyncStream stream)=0;
                virtual int onesided_nb_get_async(void *dst, int size, mp_region_t *mp_region, int peer, size_t displ, mp_window_t *window_t, mp_request_t *mp_req, asyncStream stream)=0;
                virtual int onesided_put_prepare (void *src, int size, mp_region_t *mp_region, int peer, size_t displ, mp_window_t *window_t, mp_request_t *req_t, int flags)=0;
                virtual int onesided_nb_put_post_async(mp_request_t *mp_req, asyncStream stream)=0;
                virtual int onesided_nb_put_post_all_async (int count, mp_request_t *mp_req, asyncStream stream)=0;
//useful only with gds??
                virtual int progress_requests (int count, mp_request_t *req_)=0;
//Communication descriptors
                virtual int descriptors_queue_alloc(mp_comm_descriptors_queue_t *pdq)=0;
                virtual int descriptors_queue_free(mp_comm_descriptors_queue_t *pdq)=0;
                virtual int descriptors_queue_add_send(mp_comm_descriptors_queue_t *pdq, mp_request_t *mp_req)=0;
                virtual int descriptors_queue_add_wait_send(mp_comm_descriptors_queue_t *pdq, mp_request_t *mp_req)=0;
                virtual int descriptors_queue_add_wait_recv(mp_comm_descriptors_queue_t *pdq, mp_request_t *mp_req)=0;
                virtual int descriptors_queue_add_wait_value32(mp_comm_descriptors_queue_t *pdq, uint32_t *ptr, uint32_t value, int flags)=0;
                virtual int descriptors_queue_add_write_value32(mp_comm_descriptors_queue_t *pdq, uint32_t *ptr, uint32_t value)=0;
                virtual int descriptors_queue_post_async(asyncStream stream, mp_comm_descriptors_queue_t *pdq, int flags)=0;
//Kernel-model descriptors
                virtual int descriptors_kernel(KERNEL_DESCRIPTOR_SEM *psem, uint32_t *ptr, uint32_t value)=0;
                virtual int descriptors_kernel_send(mp_kernel_desc_send_t * sinfo, mp_request_t *mp_req)=0;
                virtual int descriptors_kernel_wait(mp_kernel_desc_wait_t * winfo, mp_request_t *mp_req)=0;
//================================================================
        };
}

TL::Communicator * getTLObj(int tl_index);
typedef TL::Communicator*(*tl_creator)();
void add_tl_creator(int id, tl_creator c);

#endif