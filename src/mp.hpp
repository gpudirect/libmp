#pragma once

#if HAVE_CONFIG_H
#  include <config.h>
#endif /* HAVE_CONFIG_H */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <limits.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <time.h>
#include <errno.h>
#include <memory>

//cast done inside tl layer
struct mp_request;
struct mp_region;
struct mp_window;

typedef struct mp_request * mp_request_t;
typedef struct mp_region * mp_region_t;
typedef struct mp_window * mp_window_t;

#define MP_SUCCESS 0
#define MP_FAILURE 1
#define MP_NONE   -1
#define MP_DEFAULT 0

#define MP_CHECK(stmt)                                  \
do {                                                    \
    int result = (stmt);                                \
    if (MP_SUCCESS != result) {                         \
        fprintf(stderr, "[%s:%d] mp call failed \n",    \
         __FILE__, __LINE__);                           \
        exit(EXIT_FAILURE);                             \
    }                                                   \
    assert(0 == result);                                \
} while (0)
//oob_comm->abort(-1);

#define MP_CHECK_OOB_OBJ()																\
	if(!oob_comm) {																		\
		fprintf(stderr, "[%s:%d] OOB object not initialized \n", __FILE__, __LINE__);	\
		exit(EXIT_FAILURE);																\
	}

#define MP_CHECK_TL_OBJ()																\
	if(!tl_comm) {																		\
		fprintf(stderr, "[%s:%d] TL object not initialized \n", __FILE__, __LINE__);	\
		oob_comm->abort(-1);					                						\
		exit(EXIT_FAILURE);																\
	}

#define MP_CHECK_COMM_OBJ()		\
	MP_CHECK_OOB_OBJ();			\
	MP_CHECK_TL_OBJ();

#define MP_API_MAJOR_VERSION    3
#define MP_API_MINOR_VERSION    0
#define MP_API_VERSION          ((MP_API_MAJOR_VERSION << 16) | MP_API_MINOR_VERSION)

#define MP_API_VERSION_COMPATIBLE(v) \
    ( ((((v) & 0xffff0000U) >> 16) == MP_API_MAJOR_VERSION) &&   \
      ((((v) & 0x0000ffffU) >> 0 ) >= MP_API_MINOR_VERSION) )

//===== INFO
typedef enum mp_param {
    MP_PARAM_VERSION=0,
    MP_NUM_PARAMS,
    MP_OOB_TYPE,
    MP_TL_TYPE,
    MP_NUM_RANKS,
    MP_MY_RANK
} mp_param_t;


#ifndef MP_FLAGS_H
#define MP_FLAGS_H
enum mp_put_flags {
    MP_PUT_INLINE  = 1<<0,
    MP_PUT_NOWAIT  = 1<<1, // don't generate a CQE, req cannot be waited for
};

enum mp_wait_flags {
    MP_WAIT_GEQ = 0,
    MP_WAIT_EQ,
    MP_WAIT_AND
};
#endif

//===== mp.cc
int mp_query_param(mp_param_t param, int *value);
int mp_network_device_info();
int mp_init(int argc, char *argv[], int par1);
void mp_finalize();
void mp_get_envars();
mp_request_t * mp_create_request(int number);
mp_region_t * mp_create_regions(int number);
int mp_free_regions(int number, mp_region_t * mp_reg);
int mp_unregister_regions(int number, mp_region_t * mp_regs);
int mp_register_region_buffer(void * addr, size_t length, mp_region_t * mp_reg);
int mp_create_register_regions(int number, mp_region_t ** mp_regs, void * addr, size_t length);
int mp_window_create(void *addr, size_t size, mp_window_t *window_t);
int mp_window_destroy(mp_window_t *window_t);
void mp_barrier();
void mp_abort();
double mp_time();

int mp_prepare_acks_rdma();
int mp_send_ack_rdma(int dst_rank);
int mp_wait_ack_rdma(int src_rank);
int mp_cleanup_acks_rdma();

//===== mp_comm.cc
int mp_irecv(void * buf, size_t size, int peer, mp_region_t * mp_reg, mp_request_t * mp_req);
int mp_isend(void * buf, size_t size, int peer, mp_region_t * mp_reg, mp_request_t * mp_req);
int mp_isendv(struct iovec *v, int nvecs, int peer, mp_region_t * mp_reg, mp_request_t * mp_req);

int mp_iput(void *buf, int size, mp_region_t * mp_reg, int peer, size_t displ, mp_window_t * mp_win, mp_request_t * mp_req, int flags);
int mp_iget(void *buf, int size, mp_region_t * mp_reg, int peer, size_t displ, mp_window_t * mp_win, mp_request_t * mp_req);

int mp_wait_word(uint32_t *ptr, uint32_t value, int flags);
int mp_wait(mp_request_t * mp_req);
int mp_wait_all(int number, mp_request_t * mp_reqs);

#ifdef HAVE_GDSYNC
#include <cuda.h>
#include <cuda_runtime.h>
#include <gdsync/device.cuh>
#include <gdsync/device.cuh>

//===== mp_comm_async.cc
int mp_send_prepare(void * buf, size_t size, int peer, mp_region_t * mp_reg, mp_request_t * mp_req);
int mp_sendv_prepare(struct iovec *v, int nvecs, int peer, mp_region_t * mp_reg, mp_request_t * mp_req);

int mp_isend_post_async(mp_request_t * mp_req, cudaStream_t stream);
int mp_isend_post_all_async(int number, mp_request_t * mp_req, cudaStream_t stream);
int mp_isend_async(void * buf, size_t size, int peer, mp_region_t * mp_reg, mp_request_t * mp_req, cudaStream_t stream);
int mp_isendv_async(struct iovec *v, int nvecs, int peer, mp_region_t * mp_reg, mp_request_t * mp_req, cudaStream_t stream);
int mp_send_post_async(mp_request_t * mp_req, cudaStream_t stream);
int mp_send_post_all_async(int number, mp_request_t * mp_req, cudaStream_t stream);
int mp_send_async(void * buf, size_t size, int peer, mp_region_t * mp_reg, mp_request_t * mp_req, cudaStream_t stream);

int mp_put_prepare(void *buf, int size, mp_region_t * mp_reg, int peer, size_t displ, mp_window_t * mp_win, mp_request_t * mp_req, int flags);
int mp_iput_post_async(mp_request_t * mp_req, cudaStream_t stream);
int mp_iput_post_all_async(int number, mp_request_t * mp_req, cudaStream_t stream);
int mp_iput_async(void *buf, int size, mp_region_t * mp_reg, int peer, size_t displ, mp_window_t * mp_win, mp_request_t * mp_req, int flags, cudaStream_t stream);
int mp_iget_async(void *buf, int size, mp_region_t * mp_reg, int peer, size_t displ, mp_window_t * mp_win, mp_request_t * mp_req, cudaStream_t stream);

int mp_wait_word_async(uint32_t *ptr, uint32_t value, int flags, cudaStream_t stream);
int mp_wait_async(mp_request_t * mp_req, cudaStream_t stream);
int mp_wait_all_async(int number, mp_request_t * mp_reqs, cudaStream_t stream);
int mp_progress_requests(int number, mp_request_t * mp_reqs);

struct mp_comm_descriptors_queue;
typedef struct mp_comm_descriptors_queue *mp_comm_descriptors_queue_t;

int mp_comm_descriptors_queue_alloc(mp_comm_descriptors_queue_t *dq);
int mp_comm_descriptors_queue_free(mp_comm_descriptors_queue_t *dq);
int mp_comm_descriptors_queue_add_send(mp_comm_descriptors_queue_t *dq, mp_request_t *req);
int mp_comm_descriptors_queue_add_wait_send(mp_comm_descriptors_queue_t *dq, mp_request_t *req);
int mp_comm_descriptors_queue_add_wait_recv(mp_comm_descriptors_queue_t *dq, mp_request_t *req);
int mp_comm_descriptors_queue_add_wait_value32(mp_comm_descriptors_queue_t *dq, uint32_t *ptr, uint32_t value, int flags);
int mp_comm_descriptors_queue_add_write_value32(mp_comm_descriptors_queue_t *dq, uint32_t *ptr, uint32_t value);
int mp_comm_descriptors_queue_post_async(cudaStream_t stream, mp_comm_descriptors_queue_t *dq, int flags);

//================================== ASYNC KERNEL DESCRIPTOR =================================================
#ifndef MP_DESCR_KERNEL_H
#define MP_DESCR_KERNEL_H

typedef struct mp_kernel_desc_send {
    gdsync::isem32_t dbrec;
    gdsync::isem64_t db;
} mp_kernel_desc_send;
typedef mp_kernel_desc_send * mp_kernel_desc_send_t;

typedef struct mp_kernel_desc_wait {
    gdsync::wait_cond_t sema_cond;
    gdsync::isem32_t sema;
    gdsync::isem32_t flag;
} mp_kernel_desc_wait;
typedef mp_kernel_desc_wait * mp_kernel_desc_wait_t;

typedef gdsync::isem32_t mp_kernel_semaphore;
typedef mp_kernel_semaphore * mp_kernel_semaphore_t;

#endif

int mp_kernel_descriptors_kernel(mp_kernel_semaphore_t psem, uint32_t *ptr, uint32_t value);
int mp_kernel_descriptors_send(mp_kernel_desc_send_t info, mp_request_t *mp_req);
int mp_kernel_descriptors_wait(mp_kernel_desc_wait_t info, mp_request_t *mp_req);
int mp_prepare_kernel_send(void * buf, int size, int peer, mp_region_t * mp_reg, mp_request_t * mp_req, mp_kernel_desc_send_t sDesc, mp_kernel_desc_wait_t wDesc);
int mp_prepare_kernel_recv(void * buf, int size, int peer, mp_region_t * mp_reg, mp_request_t * mp_req, mp_kernel_desc_wait_t wDesc);

#if 0
__device__ inline void mp_isend_kernel(mp_kernel_desc_send &info);
__device__ inline int mp_wait_kernel(mp_kernel_desc_wait &info);
__device__ inline void mp_signal_kernel(mp_kernel_desc_wait &info);
#endif


#endif
