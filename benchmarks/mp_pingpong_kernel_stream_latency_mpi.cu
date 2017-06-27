/****
 * Copyright (c) 2011-2014, NVIDIA Corporation.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *    * Redistributions of source code must retain the above copyright notice,
 *      this list of conditions and the following disclaimer.
 *    * Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
 *    * Neither the name of the NVIDIA Corporation nor the names of its
 *      contributors may be used to endorse or promote products derived from
 *      this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 ****/

#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <limits.h>
#include <sys/types.h>
#include <unistd.h>
#include <cuda.h>
#include <cuda_runtime.h>
#include <cuda_profiler_api.h>
#include <mpi.h>
#include <gdsync.h>
#include <mp.h>

#include "prof.h"

#define CUDA_CHECK(stmt)                                \
do {                                                    \
    cudaError_t result = (stmt);                        \
    if (cudaSuccess != result) {                        \
        fprintf(stderr, "[%s:%d] cuda failed with %s \n",   \
         __FILE__, __LINE__,cudaGetErrorString(result));\
        exit(-1);                                       \
    }                                                   \
    assert(cudaSuccess == result);                      \
} while (0)

#define CU_CHECK(stmt)                                 \
do {                                                    \
    CUresult result = (stmt);                           \
    if (CUDA_SUCCESS != result) {                        \
        fprintf(stderr, "[%s:%d] cuda failed with %d \n",   \
         __FILE__, __LINE__, result);\
        exit(-1);                                       \
    }                                                   \
    assert(CUDA_SUCCESS == result);                     \
} while (0)

#define CUDA_LAST_ERROR()                                 \
do {                                                    \
    cudaError_t result = cudaGetLastError();                            \
    if (cudaSuccess != result) {                        \
        fprintf(stderr, "[%s:%d] cuda failed with %s \n",   \
         __FILE__, __LINE__,cudaGetErrorString(result));\
        exit(-1);                                       \
    }                                                   \
    assert(cudaSuccess == result);                      \
} while (0)


#define MP_CHECK(stmt)                                  \
do {                                                    \
    int result = (stmt);                                \
    if (0 != result) {                                  \
        fprintf(stderr, "[%s:%d] mp call failed \n",    \
         __FILE__, __LINE__);                           \
        exit(-1);                                       \
    }                                                   \
    assert(0 == result);                                \
} while (0)

#ifdef PROFILE_NVTX_RANGES
#include <nvToolsExt.h>

#define COMM_COL 1
#define SM_COL   2
#define SML_COL  3
#define OP_COL   4
#define COMP_COL 5
#define SOLVE_COL 6
#define WARMUP_COL 7
#define EXEC_COL 8

#define SEND_COL 9
#define WAIT_COL 10
#define KERNEL_COL 11


#define PUSH_RANGE(name,cid)                                            \
  do {                                                                  \
    const uint32_t colors[] = {                                         \
            0x0000ff00, 0x000000ff, 0x00ffff00, 0x00ff00ff, 0x0000ffff, 0x00ff0000, 0x00ffffff, 0xff000000, 0xff0000ff, 0x55ff3300, 0xff660000, 0x66330000  \
    };                                                                  \
    const int num_colors = sizeof(colors)/sizeof(colors[0]);            \
    int color_id = cid%num_colors;                                  \
    nvtxEventAttributes_t eventAttrib = {0};                        \
    eventAttrib.version = NVTX_VERSION;                             \
    eventAttrib.size = NVTX_EVENT_ATTRIB_STRUCT_SIZE;               \
    eventAttrib.colorType = NVTX_COLOR_ARGB;                        \
    eventAttrib.color = colors[color_id];                           \
    eventAttrib.messageType = NVTX_MESSAGE_TYPE_ASCII;              \
    eventAttrib.message.ascii = name;                               \
    nvtxRangePushEx(&eventAttrib);                                  \
  } while(0)

#define PUSH_RANGE_STR(cid, FMT, ARGS...)       \
  do {                                          \
    char str[128];                              \
    snprintf(str, sizeof(str), FMT, ## ARGS);   \
    PUSH_RANGE(str, cid);                       \
  } while(0)


#define POP_RANGE do { nvtxRangePop(); } while(0)

#else
#define PUSH_RANGE(name,cid)
#define POP_RANGE
#endif

int enable_debug_prints = 0;
#define mp_dbg_msg(FMT, ARGS...)  do                                    \
{                                                                       \
    if (enable_debug_prints)  {                                              \
        fprintf(stderr, "[%d] [%d] MP DBG  %s() " FMT, getpid(),  my_rank, __FUNCTION__ , ## ARGS); \
        fflush(stderr);                                                 \
    }                                                                   \
} while(0)

#define MAX_SIZE 1*1024*1024 
#define ITER_COUNT_SMALL 1000
#define ITER_COUNT_LARGE 1000

struct prof prof_normal;
struct prof prof_async;
struct prof prof_mpi;
int prof_start = 0;
int prof_idx = 0;

static const int over_sub_factor = 2;
int gpu_num_sm;
int enable_ud = 0;
int gpu_id = -1;

int comm_size, my_rank, peer;
int steps_per_batch = 20, batches_inflight = 4;
int enable_async = 1;
int calc_size = 128*1024;
int use_calc_size = 1;
volatile uint32_t tracking_event = 0;
int use_gpu_buffers=0;

__device__ int counter;
__device__ int clockrate;

struct comm_descs {
    enum { max_n_descs = ITER_COUNT_SMALL };
    mp::mlx5::send_desc_t tx[max_n_descs];
    mp::mlx5::wait_desc_t tx_wait[max_n_descs];
    mp::mlx5::wait_desc_t rx_wait[max_n_descs];
};

__global__ void calc_kernel(int n, float c, float *in, float *out)
{
        const uint tid = threadIdx.x;
        const uint bid = blockIdx.x;
        const uint block_size = blockDim.x;
        const uint grid_size = gridDim.x;
        const uint gid = tid + bid*block_size;
        const uint n_threads = block_size*grid_size;
        for (int i=gid; i<n; i += n_threads)
                out[i] = in[i] * c;
}

int gpu_launch_calc_kernel(size_t size, cudaStream_t stream)
{
        const int nblocks = over_sub_factor * gpu_num_sm;
        const int nthreads = 32*2;
        int n = size / sizeof(float);
        static float *in = NULL;
        static float *out = NULL;
        if (!in) {
                CUDA_CHECK(cudaMalloc((void **)&in, size));
                CUDA_CHECK(cudaMalloc((void **)&out, size));

                CUDA_CHECK(cudaMemset((void *)in, 1, size));
                CUDA_CHECK(cudaMemset((void *)out, 1, size));
        }
        calc_kernel<<<nblocks, nthreads, 0, stream>>>(n, 1.0f, in, out);
        CUDA_CHECK(cudaGetLastError());
        return 0;
}

__global__ void dummy_kernel(double time)
{
    long long int start, stop;
    double usec;

    start = clock64();
    do {
        stop = clock64();
	usec = ((double)(stop-start)*1000)/((double)clockrate); 
	counter = usec;
    } while(usec < time);
}

/*application and pack buffers*/
void *buf = NULL, *sbuf_d = NULL, *rbuf_d = NULL;
cudaStream_t stream;
size_t buf_size; 

/*mp specific objects*/
mp_request_t *sreq = NULL;
mp_request_t *rreq = NULL;
mp_reg_t sreg, rreg; 
double time_start, time_stop;
MPI_Request * sreq_mpi;
MPI_Request * rreq_mpi;

int batch_to_rreq_idx (int batch_idx) { 
     return (batch_idx % (batches_inflight + 1))*steps_per_batch;
}

int batch_to_sreq_idx (int batch_idx) { 
     return (batch_idx % batches_inflight)*steps_per_batch;
}

void post_recv (int size, int batch_index)
{
    int j;
    int req_idx = batch_to_rreq_idx (batch_index);
 
    for (j=0; j<steps_per_batch; j++) {
        MP_CHECK(mp_irecv ((void *)((uintptr_t)rbuf_d), size, peer, &rreg, &rreq[req_idx + j]));
    }
}


void post_recv_kernel (int size, int batch_index)
{
    int j;
    int req_idx = batch_to_rreq_idx (batch_index);
 
    for (j=0; j<steps_per_batch; j++) {
        MP_CHECK(mp_irecv ((void *)((uintptr_t)rbuf_d), size, peer, &rreg, &rreq[req_idx + j]));
        MP_CHECK(mp::mlx5::get_descriptors(&descs.rx_wait[req_idx + j], &rreq[req_idx + j]));
    }
}

for (j = 0; j < iter_count; j++) {
        assert(j < comm_descs::max_n_descs);
        // note: the ordering is not important here, no risk of deadlocks
        if (!my_rank) { 
            MP_CHECK(mp_send_prepare((void *)((uintptr_t)sbuf_d + size*j), size, peer, &sreg, &sreq[j]));
            MP_CHECK(mp::mlx5::get_descriptors(&descs.tx[j],      &sreq[j]));
            MP_CHECK(mp::mlx5::get_descriptors(&descs.tx_wait[j], &sreq[j]));

            MP_CHECK(mp_irecv ((void *)((uintptr_t)rbuf_d + size*j), size, peer, &rreg, &rreq[j]));
            MP_CHECK(mp::mlx5::get_descriptors(&descs.rx_wait[j], &rreq[j]));
        } else {
            MP_CHECK(mp_irecv ((void *)((uintptr_t)rbuf_d + size*j), size, peer, &rreg, &rreq[j]));
            MP_CHECK(mp::mlx5::get_descriptors(&descs.rx_wait[j], &rreq[j]));

            MP_CHECK(mp_send_prepare((void *)((uintptr_t)sbuf_d + size*j), size, peer, &sreg, &sreq[j]));
            MP_CHECK(mp::mlx5::get_descriptors(&descs.tx[j],      &sreq[j]));
            MP_CHECK(mp::mlx5::get_descriptors(&descs.tx_wait[j], &sreq[j]));
        }
    }


void post_recv_mpi (int size, int batch_index)
{
    int j;
    int req_idx = batch_to_rreq_idx (batch_index);
 
    for (j=0; j<steps_per_batch; j++) {
        MP_CHECK(MPI_Irecv ((void *)((uintptr_t)rbuf_d), size, MPI_CHAR, peer, my_rank, MPI_COMM_WORLD, &rreq_mpi[req_idx + j]));
    }
}


void wait_send (int batch_index) 
{
    int j;
    int req_idx = batch_to_sreq_idx (batch_index); 

    for (j=0; j<steps_per_batch; j++) {
        MP_CHECK(mp_wait(&sreq[req_idx + j]));
    }
}

void wait_send_mpi (int batch_index) 
{
    int j;
    int req_idx = batch_to_sreq_idx (batch_index); 

    for (j=0; j<steps_per_batch; j++) {
        MP_CHECK(MPI_Wait(&sreq_mpi[req_idx + j], NULL));
    }
}


void wait_recv (int batch_index) 
{
    int j;
    int req_idx = batch_to_rreq_idx (batch_index);
 
    for (j=0; j<steps_per_batch; j++) {
        MP_CHECK(mp_wait(&rreq[req_idx + j]));
    }
}

void wait_recv_mpi (int batch_index) 
{
    int j;
    int req_idx = batch_to_rreq_idx (batch_index);
 
    for (j=0; j<steps_per_batch; j++) {
        MP_CHECK(MPI_Wait(&rreq_mpi[req_idx + j], NULL));
    }
}

void post_work_kernel (int size, int batch_index, double kernel_size) 
{
    int j;
    int sreq_idx = batch_to_sreq_idx (batch_index);
    int rreq_idx = batch_to_rreq_idx (batch_index);
    
    for (j=0; j<steps_per_batch; j++) {
        MP_CHECK(mp_send_prepare((void *)((uintptr_t)sbuf_d), size, peer, &sreg, &sreq[sreq_idx + j]));
        MP_CHECK(mp::mlx5::get_descriptors(&descs.tx[sreq_idx + j],      &sreq[sreq_idx + j]));
        MP_CHECK(mp::mlx5::get_descriptors(&descs.tx_wait[sreq_idx + j], &sreq[sreq_idx + j]));
    }

    const int nblocks = over_sub_factor * gpu_num_sm;
    const int nthreads = 32*2;
    int n = size / sizeof(float);
    static float *in = NULL;
    static float *out = NULL;
    if (!in) {
            CUDA_CHECK(cudaMalloc((void **)&in, size));
            CUDA_CHECK(cudaMalloc((void **)&out, size));

            CUDA_CHECK(cudaMemset((void *)in, 1, size));
            CUDA_CHECK(cudaMemset((void *)out, 1, size));
    }
    calc_kernel<<<nblocks, nthreads, 0, stream>>>(n, 1.0f, in, out);
    CUDA_CHECK(cudaGetLastError());

    PUSH_RANGE("PingPong Async", 1);
    for (j=0; j<steps_per_batch; j++) {
        if (!my_rank) { 

                kernel_wait_send<<nblocks, nthreads+1, 0, stream>>(n, 1.0f, in, out, my_rank, j, descs);

                PUSH_RANGE("Wait", 2);
                MP_CHECK(mp_wait_on_stream(&rreq[rreq_idx + j], stream));
                POP_RANGE;

                PUSH_RANGE("Launch", 3);
                if (kernel_size > 0) {
                    if (use_calc_size > 0)
                       gpu_launch_calc_kernel(kernel_size, stream);
                    else
                       dummy_kernel <<<1, 1, 0, stream>>> (kernel_size);
                }  
                POP_RANGE;

                PUSH_RANGE("Isend", 4);
                MP_CHECK(mp_isend_on_stream ((void *)((uintptr_t)sbuf_d), size, peer, &sreg, &sreq[sreq_idx + j], stream));
                POP_RANGE;

        } else {

                kernel_send_wait<<>>();
                
                PUSH_RANGE("Isend", 4);
                MP_CHECK(mp_isend_on_stream ((void *)((uintptr_t)sbuf_d), size, peer, &sreg, &sreq[sreq_idx + j], stream));
                POP_RANGE;

                PUSH_RANGE("Wait", 2);
                MP_CHECK(mp_wait_on_stream(&rreq[rreq_idx + j], stream));
                POP_RANGE;

                PUSH_RANGE("Launch", 3);
                if (kernel_size > 0) {
                    if (use_calc_size > 0)
                       gpu_launch_calc_kernel(kernel_size, stream);
                    else
                       dummy_kernel <<<1, 1, 0, stream>>> (kernel_size);
                }
                POP_RANGE;
        }
    }
    POP_RANGE;
}


void post_work_async (int size, int batch_index, double kernel_size) 
{
    int j;
    int sreq_idx = batch_to_sreq_idx (batch_index);
    int rreq_idx = batch_to_rreq_idx (batch_index);
   
    PUSH_RANGE("PingPong Async", 1);
    for (j=0; j<steps_per_batch; j++) {
    	if (!my_rank) { 

                PUSH_RANGE("Wait", 2);
                MP_CHECK(mp_wait_on_stream(&rreq[rreq_idx + j], stream));
                POP_RANGE;

                PUSH_RANGE("Launch", 3);
                if (kernel_size > 0) {
                    if (use_calc_size > 0)
                       gpu_launch_calc_kernel(kernel_size, stream);
                    else
                       dummy_kernel <<<1, 1, 0, stream>>> (kernel_size);
                }  
                POP_RANGE;

                PUSH_RANGE("Isend", 4);
                MP_CHECK(mp_isend_on_stream ((void *)((uintptr_t)sbuf_d), size, peer, &sreg, &sreq[sreq_idx + j], stream));
                POP_RANGE;

    	} else {
                PUSH_RANGE("Isend", 4);
                MP_CHECK(mp_isend_on_stream ((void *)((uintptr_t)sbuf_d), size, peer, &sreg, &sreq[sreq_idx + j], stream));
                POP_RANGE;

                PUSH_RANGE("Wait", 2);
                MP_CHECK(mp_wait_on_stream(&rreq[rreq_idx + j], stream));
                POP_RANGE;

                PUSH_RANGE("Launch", 3);
                if (kernel_size > 0) {
                    if (use_calc_size > 0)
                       gpu_launch_calc_kernel(kernel_size, stream);
                    else
                       dummy_kernel <<<1, 1, 0, stream>>> (kernel_size);
                }
                POP_RANGE;
    	}
    }
    POP_RANGE;
}

void post_work_sync (int size, int batch_index, double kernel_size) 
{
    int j;
    int rreq_idx = batch_to_rreq_idx (batch_index);
    int sreq_idx = batch_to_sreq_idx (batch_index);

    for (j=0; j<steps_per_batch; j++) {
	if (!my_rank) { 
            MP_CHECK(mp_wait(&rreq[rreq_idx + j]));

            if (kernel_size > 0) {
                if (use_calc_size > 0)
                   gpu_launch_calc_kernel(kernel_size, stream);
                else
                   dummy_kernel <<<1, 1, 0, stream>>> (kernel_size);
                CUDA_CHECK(cudaStreamSynchronize(stream));
            }

            MP_CHECK(mp_isend ((void *)((uintptr_t)sbuf_d), size, peer, &sreg, &sreq[sreq_idx + j]));
	} else {
            MP_CHECK(mp_isend ((void *)((uintptr_t)sbuf_d), size, peer, &sreg, &sreq[sreq_idx + j]));

            MP_CHECK(mp_wait(&rreq[rreq_idx + j]));

            if (kernel_size > 0) {
                if (use_calc_size > 0)
                   gpu_launch_calc_kernel(kernel_size, stream);
                else
                   dummy_kernel <<<1, 1, 0, stream>>> (kernel_size);
                CUDA_CHECK(cudaStreamSynchronize(stream));
            }
        }
    }
}

void post_work_mpi (int size, int batch_index, double kernel_size) 
{
    int j;
    int rreq_idx = batch_to_rreq_idx (batch_index);
    int sreq_idx = batch_to_sreq_idx (batch_index);

    PUSH_RANGE("PingPong MPI", 1);

    for (j=0; j<steps_per_batch; j++) {
        if (!my_rank) { 
                PUSH_RANGE("Wait", 2);
                MP_CHECK(MPI_Wait(&rreq_mpi[rreq_idx + j], NULL));
                POP_RANGE;

                PUSH_RANGE("Launch & Sync", 3);
                if (kernel_size > 0) {
                    if (use_calc_size > 0)
                       gpu_launch_calc_kernel(kernel_size, stream);
                    else
                       dummy_kernel <<<1, 1, 0, stream>>> (kernel_size);
                    CUDA_CHECK(cudaStreamSynchronize(stream));
                }
                POP_RANGE;
                PUSH_RANGE("Isend", 4);
                MPI_Isend((void *)(uintptr_t)sbuf_d, size, MPI_CHAR, peer, peer, MPI_COMM_WORLD, &sreq_mpi[sreq_idx + j]);
                POP_RANGE;

        } else {
            PUSH_RANGE("Isend", 4);
            MPI_Isend((void *)(uintptr_t)sbuf_d, size, MPI_CHAR, peer, peer, MPI_COMM_WORLD, &sreq_mpi[sreq_idx + j]);
            POP_RANGE;
            
            PUSH_RANGE("Wait", 2);
            MP_CHECK(MPI_Wait(&rreq_mpi[rreq_idx + j], NULL));
            POP_RANGE;
            
            PUSH_RANGE("Launch & Sync", 3);
            if (kernel_size > 0) {
                if (use_calc_size > 0)
                   gpu_launch_calc_kernel(kernel_size, stream);
                else
                   dummy_kernel <<<1, 1, 0, stream>>> (kernel_size);
                CUDA_CHECK(cudaStreamSynchronize(stream));
            }
            POP_RANGE;
        }
    }

    POP_RANGE;
}

double prepost_latency;


double sr_exchange_kernel (MPI_Comm comm, int size, int iter_count, double kernel_size, int use_async)
{
    int j;
    double latency;
    double time_start, time_stop;
    int batch_count, wait_send_batch = 0, wait_recv_batch = 0;
    struct prof *prof = NULL;

    prof = (use_async) ? &prof_async : &prof_normal;
 
    assert((iter_count%steps_per_batch) == 0);
    batch_count = iter_count/steps_per_batch;
    tracking_event = 0;

    post_recv_kernel (size, 0);

    MPI_Barrier(MPI_COMM_WORLD);

    time_start = MPI_Wtime();

    for (j=0; (j<batches_inflight) && (j<batch_count); j++) { 
        if (j<(batch_count-1)) {
            post_recv (size, j+1);
        }

        post_work_kernel (size, j, kernel_size);
    }

    time_stop = MPI_Wtime();

    prepost_latency = ((time_stop - time_start)*1e6);
    
    time_start = MPI_Wtime();

    wait_send_batch = wait_recv_batch = 0;
    prof_idx = 0;
    while (wait_send_batch < batch_count) { 
        if (!my_rank && prof_start) PROF(prof, prof_idx++);

        if (use_async) {
            wait_recv (wait_recv_batch);
            wait_recv_batch++;
        }

        if (!my_rank && prof_start) PROF(prof, prof_idx++); 

        wait_send (wait_send_batch);
        wait_send_batch++;

        if (!my_rank && prof_start) PROF(prof, prof_idx++);

        if (j < (batch_count-1)) {
            post_recv_kernel (size, j+1);
        }

        if (!my_rank && prof_start) PROF(prof, prof_idx++);

        if (j < batch_count) { 
            post_work_kernel (size, j, kernel_size);
        }

        if (!my_rank && prof_start)  {
            PROF(prof, prof_idx++);
            prof_update(prof);
            prof_idx = 0;
        }

       j++;
    }

    MPI_Barrier(comm);

    time_stop = MPI_Wtime();
    latency = (((time_stop - time_start)*1e6 + prepost_latency)/(iter_count));

    CUDA_CHECK(cudaDeviceSynchronize());

    return latency;
}


double sr_exchange (MPI_Comm comm, int size, int iter_count, double kernel_size, int use_async)
{
    int j;
    double latency;
    double time_start, time_stop;
    int batch_count, wait_send_batch = 0, wait_recv_batch = 0;
    struct prof *prof = NULL;

    prof = (use_async) ? &prof_async : &prof_normal;
 
    assert((iter_count%steps_per_batch) == 0);
    batch_count = iter_count/steps_per_batch;
    tracking_event = 0;

    post_recv (size, 0);

    MPI_Barrier(MPI_COMM_WORLD);

    time_start = MPI_Wtime();

    for (j=0; (j<batches_inflight) && (j<batch_count); j++) { 
        if (j<(batch_count-1)) {
            post_recv (size, j+1);
        }

        if (use_async) { 
            post_work_async (size, j, kernel_size);
        } else {               
            post_work_sync (size, j, kernel_size);
	   }
    }

    time_stop = MPI_Wtime();

    prepost_latency = ((time_stop - time_start)*1e6);
    
    time_start = MPI_Wtime();

    wait_send_batch = wait_recv_batch = 0;
    prof_idx = 0;
    while (wait_send_batch < batch_count) { 
        if (!my_rank && prof_start) PROF(prof, prof_idx++);

    	if (use_async) {
    	    wait_recv (wait_recv_batch);
            wait_recv_batch++;
    	}

        if (!my_rank && prof_start) PROF(prof, prof_idx++); 

        wait_send (wait_send_batch);
        wait_send_batch++;

        if (!my_rank && prof_start) PROF(prof, prof_idx++);

        if (j < (batch_count-1)) {
            post_recv (size, j+1);
        }

        if (!my_rank && prof_start) PROF(prof, prof_idx++);

        if (j < batch_count) { 
            if (use_async) { 
                    post_work_async (size, j, kernel_size);
            } else {
                    post_work_sync (size, j, kernel_size);
            }
        }

        if (!my_rank && prof_start)  {
            PROF(prof, prof_idx++);
            prof_update(prof);
            prof_idx = 0;
        }

	   j++;
    }

    MPI_Barrier(comm);

    time_stop = MPI_Wtime();
    latency = (((time_stop - time_start)*1e6 + prepost_latency)/(iter_count));

    CUDA_CHECK(cudaDeviceSynchronize());

    return latency;
}

double sr_exchange_MPI (MPI_Comm comm, int size, int iter_count, double kernel_size)
{
    int j;
    double latency;
    double time_start, time_stop;
    int batch_count, wait_send_batch = 0, wait_recv_batch = 0;
    struct prof *prof = NULL;

    prof = &prof_mpi;
 
    assert((iter_count%steps_per_batch) == 0);
    batch_count = iter_count/steps_per_batch;
    tracking_event = 0;
    
    post_recv_mpi (size, 0);

    MPI_Barrier(MPI_COMM_WORLD);

    time_start = MPI_Wtime();

    for (j=0; (j<batches_inflight) && (j<batch_count); j++) { 
        if (j<(batch_count-1)) {
            post_recv_mpi (size, j+1);
        }

        post_work_mpi (size, j, kernel_size);
    }

    time_stop = MPI_Wtime();

    prepost_latency = ((time_stop - time_start)*1e6);
    
    time_start = MPI_Wtime();

    wait_send_batch = wait_recv_batch = 0;
    prof_idx = 0;
    while (wait_send_batch < batch_count) 
    { 

        if (!my_rank && prof_start) PROF(prof, prof_idx++);

        //if (!my_rank && prof_start) PROF(prof, prof_idx++); 

        wait_send_mpi (wait_send_batch);
        wait_send_batch++;

        if (!my_rank && prof_start) PROF(prof, prof_idx++);

        if (j < (batch_count-1)) {
            post_recv_mpi (size, j+1);
        }

        if (!my_rank && prof_start) PROF(prof, prof_idx++);


        if (j < batch_count) { 
            post_work_mpi (size, j, kernel_size);
        }

        if (!my_rank && prof_start)  {
            PROF(prof, prof_idx++);
            prof_update(prof);
            prof_idx = 0;
        }

        j++;
    }

    MPI_Barrier(comm);

    time_stop = MPI_Wtime();
    latency = (((time_stop - time_start)*1e6 + prepost_latency)/(iter_count));

    CUDA_CHECK(cudaDeviceSynchronize());

    return latency;
}


int main (int argc, char *argv[])
{
    int iter_count, max_size, size, dev_count, local_rank, dev_id = 0;
    int kernel_size = 20;
    int comm_comp_ratio = 0;
    int validate = 0;

    size = 1;
    max_size = MAX_SIZE;

    char *value = getenv("ENABLE_VALIDATION");
    if (value != NULL) {
	validate = atoi(value);
    }
 
    value = getenv("ENABLE_DEBUG_MSG");
    if (value != NULL) {
	enable_debug_prints = atoi(value);
    }

    value = getenv("KERNEL_TIME");
    if (value != NULL) {
	kernel_size = atoi(value);
    }

    value = getenv("COMM_COMP_RATIO");
    if (value != NULL) {
        comm_comp_ratio = atoi(value);
    }

    value = getenv("CALC_SIZE");
    if (value != NULL) {
        calc_size = atoi(value);
    }

    use_calc_size = 1;
    value = getenv("USE_CALC_SIZE");
    if (value != NULL) {
        use_calc_size = atoi(value);
    }

    value = getenv("STEPS_PER_BATCH");
    if (value != NULL) {
        steps_per_batch = atoi(value);
    }

    value = getenv("BATCHES_INFLIGHT");
    if (value != NULL) {
        batches_inflight = atoi(value);
    }

    value = getenv("SIZE");
    if (value != NULL) {
        size = atoi(value);
    }

    value = getenv("MP_ENABLE_UD");
    if (value != NULL) {
        enable_ud = atoi(value);
    }

    if (enable_ud) {
	if (max_size > 4096) { 
	    max_size = 4096;
        }
    }

    value = getenv("USE_GPU_BUFFERS");
    if (value != NULL) {
        use_gpu_buffers = atoi(value);
    }

    printf("use_gpu_buffers=%d\n", use_gpu_buffers);

    while(1) {
        int c;
        c = getopt(argc, argv, "d:h");
        if (c == -1)
            break;

        switch(c) {
        case 'd':
            gpu_id = strtol(optarg, NULL, 0);
            break;
	case 'h':
            printf("syntax: %s [-d <gpu_id]\n", argv[0]);
	    break;
        default:
            printf("ERROR: invalid option\n");
            exit(EXIT_FAILURE);
        }
    }

    char *tags = "wait_recv|wait_send|post_recv|post_work";

    MPI_Init(&argc, &argv);
    MPI_Comm_size(MPI_COMM_WORLD, &comm_size);
    MPI_Comm_rank(MPI_COMM_WORLD, &my_rank);

    if (comm_size != 2) { 
	fprintf(stderr, "this test requires exactly two processes \n");
        exit(-1);
    }

    CUDA_CHECK(cudaGetDeviceCount(&dev_count));
    if (dev_count <= 0) {
        fprintf(stderr, "no CUDA devices found \n");
        exit(-1);
    }

    if (getenv("MV2_COMM_WORLD_LOCAL_RANK") != NULL) {
        local_rank = atoi(getenv("MV2_COMM_WORLD_LOCAL_RANK"));
    } else if (getenv("OMPI_COMM_WORLD_LOCAL_RANK") != NULL) {
        local_rank = atoi(getenv("OMPI_COMM_WORLD_LOCAL_RANK"));
    } else {
        local_rank = 0;
    }

    if (gpu_id >= 0) {
        dev_id = gpu_id;
    } else if (getenv("USE_GPU")) {
        dev_id = atoi(getenv("USE_GPU"));
    } else {
        dev_id = local_rank%dev_count;
    }
    if (dev_id >= dev_count) {
        fprintf(stderr, "invalid dev_id\n");
        exit(-1);
    }

    fprintf(stdout, "[%d] local_rank: %d dev_count: %d using GPU device: %d\n", my_rank, local_rank, dev_count, dev_id);

    CUDA_CHECK(cudaSetDevice(dev_id));
    CUDA_CHECK(cudaFree(0));

    cudaDeviceProp prop;
    CUDA_CHECK(cudaGetDeviceProperties(&prop, dev_id));
    CUDA_CHECK(cudaMemcpyToSymbol(clockrate, (void *)&prop.clockRate, sizeof(int), 0, cudaMemcpyHostToDevice));
    gpu_num_sm = prop.multiProcessorCount;

    fprintf(stdout, "[%d] GPU %d: %s PCIe %d:%d:%d\n", my_rank, dev_id, prop.name, prop.pciDomainID, prop.pciBusID, prop.pciDeviceID);

    peer = !my_rank;
    MP_CHECK(mp_init (MPI_COMM_WORLD, &peer, 1, MP_INIT_DEFAULT, dev_id));

    iter_count = ITER_COUNT_SMALL;
    if (!my_rank) { 
        fprintf(stdout, "steps_per_batch: %d batches_inflight: %d \n", 
   		steps_per_batch, batches_inflight);
        fprintf(stdout, "WARNING: dumping round-trip latency!!!\n");
    }

    /*allocating requests*/
    sreq = (mp_request_t *) malloc(steps_per_batch*batches_inflight*sizeof(mp_request_t));
    rreq = (mp_request_t *) malloc(steps_per_batch*(batches_inflight + 1)*sizeof(mp_request_t));

    sreq_mpi = (MPI_Request *) malloc(steps_per_batch*batches_inflight*sizeof(MPI_Request));
    rreq_mpi = (MPI_Request *) malloc(steps_per_batch*(batches_inflight + 1)*sizeof(MPI_Request));
   
    CUDA_CHECK(cudaStreamCreateWithFlags(&stream, 0));	

    if (!my_rank) {   
    	if (use_calc_size) { 
    		fprintf(stdout, "%10s \t %10s \t %10s \t %10s \t  %10s \t %10s \t %10s \t %10s\n", "Size", "CalcSize", "No-async", "No-async+Kern", "Async", "Async+Kern", "MPI", "MPI+Kern");
    	} else {
    		fprintf(stdout, "%10s \t %10s \t  %10s \t %10s \t %10s \t  %10s \t %10s \t %10s\n", "Size", "KernelTime", "No-async", "No-async+Kern", "Async", "Async+Kern", "MPI", "MPI+Kern");
    	}
    }

    if (size != 1) size = max_size = size;
    for (; size<=max_size; size*=2) 
    {
        double latency;

        if (size > 1024) {
            iter_count = ITER_COUNT_LARGE;
        }

        buf_size = size;

        buf = malloc (buf_size);
        memset(buf, 0, buf_size); 
        if(use_gpu_buffers == 0)
        {
            CUDA_CHECK(cudaMallocHost((void **)&sbuf_d, buf_size));
            memset(sbuf_d, 0, buf_size);

            CUDA_CHECK(cudaMallocHost((void **)&rbuf_d, buf_size));
            memset(rbuf_d, 0, buf_size);   
        }
        else
        {
            CUDA_CHECK(cudaMalloc((void **)&sbuf_d, buf_size));
            CUDA_CHECK(cudaMemset(sbuf_d, 0, buf_size)); 

            CUDA_CHECK(cudaMalloc((void **)&rbuf_d, buf_size));
            CUDA_CHECK(cudaMemset(rbuf_d, 0, buf_size)); 
        }

        MP_CHECK(mp_register(sbuf_d, buf_size, &sreg));
        MP_CHECK(mp_register(rbuf_d, buf_size, &rreg));

        if (!my_rank) fprintf(stdout, "%10d", size);

#if 0
        if (!my_rank) fprintf(stdout, "sleeping 10s\n");
        sleep(10);
        MPI_Barrier(MPI_COMM_WORLD);
#endif

        /*warmup*/
        latency = sr_exchange(MPI_COMM_WORLD, size, iter_count, 0/*kernel_size*/, 1/*use_async*/);
        MPI_Barrier(MPI_COMM_WORLD);

#if 0
        latency = sr_exchange(MPI_COMM_WORLD, size, iter_count, 0/*kernel_size*/, 0/*use_async*/);
        MPI_Barrier(MPI_COMM_WORLD);
#endif

        latency = sr_exchange_MPI(MPI_COMM_WORLD, size, iter_count, 0/*kernel_size*/);
        MPI_Barrier(MPI_COMM_WORLD);

#if 0
	/*Normal*/
        latency = sr_exchange(MPI_COMM_WORLD, size, iter_count, 0/*kernel_size*/, 0/*use_async*/);
        MPI_Barrier(MPI_COMM_WORLD);
#endif     
     
        if (use_calc_size) kernel_size = calc_size; 
        else  kernel_size = (comm_comp_ratio > 0) ? comm_comp_ratio*(latency/2) : kernel_size;

        if (!my_rank) fprintf(stdout, "\t   %10d", kernel_size);
        if (!my_rank) fprintf(stdout, "\t   %8.2lf", latency, prepost_latency);
        //if (!my_rank) fprintf(stdout, "\t   %8.2lf (%8.2lf)", latency, prepost_latency);

//        cudaProfilerStart();
        if (!my_rank) { 
            if (prof_init(&prof_normal, 10000, 10000, "10us", 100, 1, tags)) {
                    fprintf(stderr, "error in prof_init init.\n");
                    exit(-1);
                }
                if (prof_init(&prof_async, 10000, 10000, "10us", 100, 1, tags)) {
                    fprintf(stderr, "error in prof_init init.\n");
                    exit(-1);
                }
                if (prof_init(&prof_mpi, 10000,  10000, "10us", 100, 1, tags)) {
                    fprintf(stderr, "error in prof_init init.\n");
                    exit(-1);
                }
                prof_start = 1;
        }


#if 0
	   /*Normal + Kernel*/
        latency = sr_exchange(MPI_COMM_WORLD, size, iter_count, kernel_size, 0/*use_async*/);
        MPI_Barrier(MPI_COMM_WORLD);

        if (!my_rank) fprintf(stdout, "\t   %8.2lf ", latency /*, prepost_latency */);
        //if (!my_rank) fprintf(stdout, "\t   %8.2lf (%8.2lf)", latency, prepost_latency);

        if (!my_rank) {
            prof_start = 0;
        }
        cudaProfilerStop();
#endif 

#if 0
	/*Async*/
        latency = sr_exchange(MPI_COMM_WORLD, size, iter_count, 0/*kernel_size*/, 1/*use_async*/);

        MPI_Barrier(MPI_COMM_WORLD);
 
        if (!my_rank) fprintf(stdout, "\t   %8.2lf ", latency /*, prepost_latency */);
        //if (!my_rank) fprintf(stdout, "\t   %8.2lf (%8.2lf)", latency, prepost_latency);

#endif
        cudaProfilerStart();
        if (!my_rank) {
            prof_start = 1;
        }

	/*Async + Kernel*/
        latency = sr_exchange(MPI_COMM_WORLD, size, iter_count, kernel_size, 1/*use_async*/);

        MPI_Barrier(MPI_COMM_WORLD);

        if (!my_rank) fprintf(stdout, "\t   %8.2lf ", latency /*, prepost_latency */);
        //if (!my_rank) fprintf(stdout, "\t   %8.2lf (%8.2lf) \n", latency, prepost_latency);

        if (!my_rank) {
            prof_start = 0;
        }
        cudaProfilerStop();

#if 0
/*MPI*/
        latency = sr_exchange_MPI(MPI_COMM_WORLD, size, iter_count, 0/*kernel_size*/);

        MPI_Barrier(MPI_COMM_WORLD);
 
        if (!my_rank) fprintf(stdout, "\t   %8.2lf ", latency /*, prepost_latency */);
        //if (!my_rank) fprintf(stdout, "\t   %8.2lf (%8.2lf)", latency, prepost_latency);

#endif

        cudaProfilerStart();
        if (!my_rank) {
            prof_start = 1;
        }

        /*MPI + Kernel*/
        latency = sr_exchange_MPI(MPI_COMM_WORLD, size, iter_count, kernel_size);

        MPI_Barrier(MPI_COMM_WORLD);

        if (!my_rank) fprintf(stdout, "\t   %8.2lf \n", latency /*, prepost_latency */);
        //if (!my_rank) fprintf(stdout, "\t   %8.2lf (%8.2lf) \n", latency, prepost_latency);

        prof_start = 0;
        cudaProfilerStop();

        if (!my_rank && validate) fprintf(stdout, "SendRecv test passed validation with message size: %d \n", size);

        if (!my_rank) {
	       prof_dump(&prof_normal);
	       prof_dump(&prof_async);
           prof_dump(&prof_mpi);
        }

        mp_deregister(&sreg);
        mp_deregister(&rreg);

        if(use_gpu_buffers == 0)
        {
            CUDA_CHECK(cudaFreeHost(sbuf_d));
            CUDA_CHECK(cudaFreeHost(rbuf_d));
        }
        else
        {
            CUDA_CHECK(cudaFree(sbuf_d));
            CUDA_CHECK(cudaFree(rbuf_d));            
        }
        free(buf);
    }

    CUDA_CHECK(cudaStreamDestroy(stream));
    free(sreq);
    free(rreq);

    mp_finalize ();

    MPI_Barrier(MPI_COMM_WORLD);

    MPI_Finalize();

    return 0;
}
