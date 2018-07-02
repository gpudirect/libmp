/****
 * Copyright (c) 2011-2018, NVIDIA CORPORATION. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *  * Neither the name of NVIDIA CORPORATION nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 ****/

#include "mpi.h"
#include "mp.h"
#include "cuda.h"
#include "cuda_runtime.h"
#include <string.h>
#include <stdio.h>
#include "assert.h"
#include <limits.h>
#include <sys/types.h>
#include <unistd.h>
#include "prof.h"
#include <cuda_profiler_api.h>
#include <mp/device.cuh>
#include "nvToolsExt.h"

#define MPI_CHECK(stmt)                                             \
do {                                                                \
    int result = (stmt);                                            \
    if (MPI_SUCCESS != result) {                                    \
        char string[MPI_MAX_ERROR_STRING];                          \
        int resultlen = 0;                                          \
        MPI_Error_string(result, string, &resultlen);               \
        fprintf(stderr, " (%s:%d) MPI check failed with %d (%*s)\n",    \
                   __FILE__, __LINE__, result, resultlen, string);  \
        exit(-1);                                                   \
    }                                                               \
} while(0)


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

int enable_debug_prints = 0;
#define mp_dbg_msg(FMT, ARGS...)  do                                    \
{                                                                       \
    if (enable_debug_prints)  {                                              \
        fprintf(stderr, "[%d] [%d] MP DBG  %s() " FMT, getpid(),  my_rank, __FUNCTION__ , ## ARGS); \
        fflush(stderr);                                                 \
    }                                                                   \
} while(0)

#define MAX_SIZE 4096 //128*1024 
#define ITER_COUNT_SMALL (2*1024)
#define ITER_COUNT_LARGE 256

//-------------------------------- NVTX -----------------------------------------
const uint32_t colors[] = { 0x0000ff00, 0x000000ff, 0x00ffff00, 0x00ff00ff, 0x0000ffff, 0x00ff0000, 0x00ffffff };
const int num_colors = sizeof(colors)/sizeof(uint32_t);

#define PUSH_RANGE(name,cid) { \
    int color_id = cid; \
    color_id = color_id%num_colors;\
    nvtxEventAttributes_t eventAttrib = {0}; \
    eventAttrib.version = NVTX_VERSION; \
    eventAttrib.size = NVTX_EVENT_ATTRIB_STRUCT_SIZE; \
    eventAttrib.colorType = NVTX_COLOR_ARGB; \
    eventAttrib.color = colors[color_id]; \
    eventAttrib.messageType = NVTX_MESSAGE_TYPE_ASCII; \
    eventAttrib.message.ascii = name; \
    nvtxRangePushEx(&eventAttrib); \
}
#define POP_RANGE nvtxRangePop();
//-------------------------------------------------------------------------------

struct prof prof_async;
int prof_start = 0;
int prof_idx = 0;

int comm_size, my_rank, peer;
int steps_per_batch = 16, batches_inflight = 4;

mp::mlx5::send_desc_t *tx;
mp::mlx5::send_desc_t *tx_d;
mp::mlx5::wait_desc_t *tx_wait;
mp::mlx5::wait_desc_t *tx_wait_d;
mp::mlx5::wait_desc_t *rx_wait;
mp::mlx5::wait_desc_t *rx_wait_d;

__device__ int counter;
__device__ int clockrate;

__device__ void dummy_kernel(double time)
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

__global__ void exchange_kernel(int my_rank, 
                            mp::mlx5::send_desc_t *tx_d, 
                            mp::mlx5::wait_desc_t *tx_wait_d, 
                            mp::mlx5::wait_desc_t *rx_wait_d, 
                            int iter_number, double kernel_time)
{
    int i;
    assert(gridDim.x == 1);
    /*
     *   dummy_kernel + threadfence: simulate some work after wait
     */
    for (i=0; i<iter_number; ++i) {
        if (!my_rank) {
            
            if (0 == threadIdx.x) {    
                //Kernel send
                mp::device::mlx5::send(tx_d[i]);
                mp::device::mlx5::wait(tx_wait_d[i]);
                mp::device::mlx5::signal(tx_wait_d[i]);

                //Kernel receive
                mp::device::mlx5::wait(rx_wait_d[i]);
                mp::device::mlx5::signal(rx_wait_d[i]);
            }
            //Must be sure that data have been correctly received
            __syncthreads();

            dummy_kernel(kernel_time);
            __threadfence();
            
        } else {
            if (0 == threadIdx.x) {
                //Kernel send
                mp::device::mlx5::wait(rx_wait_d[i]);
                mp::device::mlx5::signal(rx_wait_d[i]);
            }
            //Must be sure that data have been correctly received
            dummy_kernel(kernel_time);
            __threadfence();
            // make sure NIC can fetch coherent data

            if (0 == threadIdx.x) {
                mp::device::mlx5::send(tx_d[i]);
                mp::device::mlx5::wait(tx_wait_d[i]);
                mp::device::mlx5::signal(tx_wait_d[i]);
            }
            __syncthreads();
        }
    }
}

/*application and pack buffers*/
int sreq_max_inflight = 0, rreq_max_inflight = 0, prepost_depth = 0;
size_t buf_size;
int gpu_id = -1;
int wait_key = 0;
double time_start, time_stop;
cudaStream_t stream;
/*mp specific objects*/
mp_request_t *sreq = NULL;
mp_request_t *rreq = NULL;

double sr_exchange (MPI_Comm comm, int size, int iter_count, int validate, double kernel_time, struct prof *prof)
{
    int i, j, cycle_index, buf_index;
    double latency;
    double time_start, time_stop;
    int sreq_idx = 0, rreq_idx = 0, complete_sreq_idx = 0, complete_rreq_idx = 0;
    int sreq_inflight = 0, rreq_inflight = 0;
    /*application and pack buffers*/
    void *buf = NULL, *sbuf_d = NULL, *rbuf_d = NULL;
    mp_reg_t sreg, rreg; 

    CUDA_CHECK( cudaHostAlloc( (void**)&tx, rreq_max_inflight*sizeof(mp::mlx5::send_desc_t), cudaHostAllocMapped ) );
    CUDA_CHECK( cudaHostGetDevicePointer ( &tx_d, tx, 0 )); 
    CUDA_CHECK( cudaHostAlloc( (void**)&tx_wait, rreq_max_inflight*sizeof(mp::mlx5::wait_desc_t), cudaHostAllocMapped ) );
    CUDA_CHECK( cudaHostGetDevicePointer ( &tx_wait_d, tx_wait, 0 ));
    CUDA_CHECK( cudaHostAlloc( (void**)&rx_wait, rreq_max_inflight*sizeof(mp::mlx5::wait_desc_t), cudaHostAllocMapped ) );
    CUDA_CHECK( cudaHostGetDevicePointer ( &rx_wait_d, rx_wait, 0 ));

/*
    size_t limitValue, freeValue, totalValue;
    CUDA_CHECK(cudaDeviceGetLimit ( &limitValue, cudaLimitMallocHeapSize ));
    CUDA_CHECK(cudaMemGetInfo( &freeValue, &totalValue)); 
  //if(my_rank) fprintf(stdout, "*** GPU, limit: %zd, total: %zd, free: %zd\n", limitValue, totalValue, freeValue);
  */  

    CUDA_CHECK(cudaMalloc((void **)&sbuf_d, size*iter_count));
    CUDA_CHECK(cudaMemset(sbuf_d, 0, size*iter_count)); 

    CUDA_CHECK(cudaMalloc((void **)&rbuf_d, size*iter_count));
    CUDA_CHECK(cudaMemset(rbuf_d, 0, size*iter_count)); 
 
    MP_CHECK(mp_register(sbuf_d, size*iter_count, &sreg, 0));
    MP_CHECK(mp_register(rbuf_d, size*iter_count, &rreg, 0));

    if (validate) {
        mp_dbg_msg("initializing the buffer \n");
        CUDA_CHECK(cudaMemset(sbuf_d, (size + 1)%CHAR_MAX, size*iter_count));
        CUDA_CHECK(cudaMemset(rbuf_d, 0, size*iter_count));
        CUDA_CHECK(cudaDeviceSynchronize());
        buf = (char *) calloc(size*iter_count, sizeof(char));
    }

    time_start = MPI_Wtime();

    for (j=0; j<prepost_depth; j++) {
        mp_dbg_msg("[%d] posted recv request: %d \n", my_rank, rreq_idx);
        MP_CHECK(mp_irecv ((void *)((uintptr_t)rbuf_d + (size*j)), size, peer, &rreg, &rreq[rreq_idx]));
        //Kernel
        MP_CHECK(mp::mlx5::get_descriptors(&rx_wait[rreq_idx], &rreq[rreq_idx]));
        rreq_idx = (rreq_idx + 1)%rreq_max_inflight;
        rreq_inflight++;
    }
    
    prof_idx = 0;
    assert(!(iter_count%steps_per_batch));

    for (j = 0; j < iter_count; j += steps_per_batch) {

	   mp_dbg_msg("[%d] iteration :%d \n", my_rank, j);

       for(cycle_index=0; cycle_index < steps_per_batch; cycle_index++)
        {
            sreq_idx = (j+cycle_index)%sreq_max_inflight;
            MP_CHECK(mp_send_prepare((void *)((uintptr_t)sbuf_d + ((j+cycle_index)*size)), size, peer, &sreg, &sreq[sreq_idx]));
            MP_CHECK(mp::mlx5::get_descriptors(&tx[sreq_idx],      &sreq[sreq_idx]));
            MP_CHECK(mp::mlx5::get_descriptors(&tx_wait[sreq_idx], &sreq[sreq_idx]));
            sreq_inflight++;
        }
  
        //It's the same for both Rank0 and Rank1
        exchange_kernel<<<1,16,0,stream>>>(my_rank, tx_d+(j%sreq_max_inflight), 
            tx_wait_d+(j%sreq_max_inflight), rx_wait_d+(j%rreq_max_inflight), 
            steps_per_batch, kernel_time);

        CUDA_CHECK(cudaGetLastError());
        
        //if (prof && !my_rank) PROF(prof, prof_idx++);

        mp_dbg_msg("[%d] posted send request: %d \n", my_rank, sreq_idx);
        mp_dbg_msg("[%d] requests inflight: %d \n", my_rank, sreq_inflight);

        //Post others recv
        if ((j + prepost_depth) < iter_count) {

            for(cycle_index=0; cycle_index < steps_per_batch; cycle_index++)
            {
                if(rreq_inflight >= rreq_max_inflight)
                    break;

                mp_dbg_msg("[%d] posted recv request: %d \n", my_rank, rreq_idx);
                MP_CHECK(mp_irecv ((void *)((uintptr_t)rbuf_d + ((j + prepost_depth + cycle_index) * size)), size, peer, &rreg, &rreq[rreq_idx]));
                MP_CHECK(mp::mlx5::get_descriptors(&rx_wait[rreq_idx], &rreq[rreq_idx]));
                rreq_idx = (rreq_idx + 1)%rreq_max_inflight;
                rreq_inflight++;
            }
        }
        
        /*synchronize on oldest batch*/
        if (sreq_inflight == sreq_max_inflight)
        {
            mp_dbg_msg("[%d] after waiting on recv, rreq_inflight: %d \n", my_rank, rreq_inflight);

            for (cycle_index=0; cycle_index<steps_per_batch; cycle_index++) {
                mp_dbg_msg("[%d]  waiting on send request: %d \n", my_rank, complete_sreq_idx);
                MP_CHECK(mp_wait(&sreq[complete_sreq_idx]));
                mp_dbg_msg("[%d]  completed send request: %d \n", my_rank, complete_sreq_idx);
                complete_sreq_idx = (complete_sreq_idx + 1)%sreq_max_inflight;
                sreq_inflight--;
            }
        }

        //The final number of wait will be always the same
        if (rreq_inflight == rreq_max_inflight)
        {
            for (cycle_index=0; cycle_index<steps_per_batch; cycle_index++) {
                mp_dbg_msg("[%d] waiting on recv request: %d \n", my_rank, complete_rreq_idx);
                MP_CHECK(mp_wait(&rreq[complete_rreq_idx]));
                mp_dbg_msg("[%d]  completed recv request: %d \n", my_rank, complete_rreq_idx);
                complete_rreq_idx = (complete_rreq_idx + 1)%rreq_max_inflight;
                rreq_inflight--;
            }
        }

        if (j == (iter_count - steps_per_batch))
        {
            while (rreq_inflight > 0) {
                MP_CHECK(mp_wait(&rreq[complete_rreq_idx]));
                mp_dbg_msg("[%d]  completed recv request: %d \n", my_rank, complete_rreq_idx);
                complete_rreq_idx = (complete_rreq_idx + 1)%rreq_max_inflight;
                rreq_inflight--;
            }

            while (sreq_inflight > 0) {
                MP_CHECK(mp_wait(&sreq[complete_sreq_idx]));
                mp_dbg_msg("[%d]  completed send request: %d \n", my_rank, complete_sreq_idx);
                complete_sreq_idx = (complete_sreq_idx + 1)%sreq_max_inflight;
                sreq_inflight--;
            }
        }
/*
        if (!my_rank && prof)  {
            PROF(prof, prof_idx++);
            prof_update(prof);
            prof_idx = 0;
        }
*/
    }

    // TODO: move validate after timing
    if (validate) {
        CUDA_CHECK(cudaMemcpy((void *)((uintptr_t)buf), (void *)((uintptr_t)rbuf_d), size*iter_count, cudaMemcpyDefault));
	   //CUDA_CHECK(cudaDeviceSynchronize());

        char *value = (char *)((uintptr_t)buf);
        for (i=0; i<size*iter_count; i++) {
             if (value[i] != (size + 1)%CHAR_MAX) {
                 mp_dbg_msg("[%d] validation check failed index: %d expected: %d actual: %d \n", 
        		my_rank, i, (size + 1)%CHAR_MAX, value[i]);
                 exit(-1);
             }
        }

        free(buf);
    }
    
    
    MPI_Barrier(comm);
    CUDA_CHECK(cudaStreamSynchronize(stream));

    time_stop = MPI_Wtime();
    latency = (((time_stop - time_start)*1e6)/(iter_count*2));
    
    CUDA_CHECK(cudaDeviceSynchronize());

    mp_deregister(&sreg);
    mp_deregister(&rreg);
    CUDA_CHECK(cudaFree(sbuf_d));
    CUDA_CHECK(cudaFree(rbuf_d));
    
    return latency;
}

int main (int argc, char *argv[])
{
    int iter_count = 0, size = 0, dev_count = 0, local_rank = 0, dev_id = 0;
    int kernel_time = 20;
    int comm_comp_ratio = 0;
    int validate = 0;
    int max_size = MAX_SIZE; 

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
        kernel_time = atoi(value);
    }

    value = getenv("COMM_COMP_RATIO");
    if (value != NULL) {
        comm_comp_ratio = atoi(value);
    }

    size = 1;
    value = getenv("SIZE");
    if (value != NULL && atoi(value)) {
        size = atoi(value);
    }

    value = getenv("MAX_SIZE");
    if (value != NULL && atoi(value)) {
        max_size = atoi(value);
    }

    int event_async = 0;
    value = getenv("MP_EVENT_ASYNC");
    if (value != NULL) {
        event_async = atoi(value);
    }

    while(1) {
        int c;
        c = getopt(argc, argv, "d:W:");
        if (c == -1)
            break;

        switch(c) {
        case 'd':
            gpu_id = strtol(optarg, NULL, 0);
            break;
        case 'W':
            wait_key = strtol(optarg, NULL, 0);
            break;
        default:
            printf("ERROR: invalid option\n");
            exit(EXIT_FAILURE);
        }
    }

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
    fprintf(stdout, "[%d] validate=%d event_async=%d\n", my_rank, validate, event_async);
    CUDA_CHECK(cudaSetDevice(dev_id));
    CUDA_CHECK(cudaFree(0));

    cudaDeviceProp prop;
    CUDA_CHECK(cudaGetDeviceProperties(&prop, dev_id));
    CUDA_CHECK(cudaMemcpyToSymbol(clockrate, (void *)&prop.clockRate, sizeof(int), 0, cudaMemcpyHostToDevice));
    fprintf(stdout, "[%d] GPU name=%s\n", my_rank, prop.name);

    peer = !my_rank;
    MP_CHECK(mp_init (MPI_COMM_WORLD, &peer, 1, MP_INIT_DEFAULT, dev_id));

    iter_count = ITER_COUNT_SMALL;
    if (!my_rank) {
        fprintf(stdout, "steps_per_batch: %d batches_inflight: %d \n", steps_per_batch, batches_inflight);
        fprintf(stdout, "WARNING: dumping half round-trip latency!!!\n");
    }
  
    prepost_depth = steps_per_batch*2;
    sreq_max_inflight = steps_per_batch*batches_inflight;
    rreq_max_inflight = (steps_per_batch*batches_inflight + prepost_depth);

    rreq_max_inflight += steps_per_batch*2;
    //sreq_max_inflight += 32;
    /*allocating requests*/
    sreq = (mp_request_t *) malloc(sreq_max_inflight*sizeof(mp_request_t));
    rreq = (mp_request_t *) malloc(rreq_max_inflight*sizeof(mp_request_t));

    CUDA_CHECK(cudaStreamCreateWithFlags(&stream, cudaStreamNonBlocking));

    if (!my_rank) fprintf(stdout, "%10s\t  %10s\n", "Size", "Async+Kernel");
    for (; size<=max_size; size*=2)
    {
        double latency;
        const char *tags = "kernel|send|recv|prepost|wait|";

        if (size > 1024) {
            iter_count = ITER_COUNT_LARGE;
        }
    
        if (!my_rank) {
            if (prof_init(&prof_async, 1000,  1000, "1us", 100, 1, tags)) {
                fprintf(stderr, "error in prof_init init.\n");
                exit(-1);
            }

            prof_start = 1;
        }

        if (!my_rank) fprintf(stdout, "%10d", size);

        /*warmup*/
        latency = sr_exchange(MPI_COMM_WORLD, size, iter_count, validate, 1/*kernel_time*/, NULL/*prof*/);

        /*Async + Kernel*/
        latency = sr_exchange(MPI_COMM_WORLD, size, iter_count, validate, kernel_time, &prof_async);
        if (!my_rank) fprintf(stdout, "\t   %8.2lf \n", latency);

        if (!my_rank && validate) fprintf(stdout, "SendRecv test passed validation with message size: %d \n", size);
    }

    CUDA_CHECK(cudaStreamDestroy(stream));
    free(sreq);
    free(rreq);

    mp_finalize ();

    MPI_Barrier(MPI_COMM_WORLD);

    MPI_Finalize();

    return 0;
}
