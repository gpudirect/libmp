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
#include "cuda_profiler_api.h"

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
        MPI_Abort(MPI_COMM_WORLD, -1);                  \
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
//64*1024
#define ITER_COUNT_SMALL (2*1024)
#define ITER_COUNT_LARGE 256


struct prof prof_normal;
struct prof prof_async;
int prof_start = 0;
int prof_idx = 0;

int comm_size, my_rank, peer;
int steps_per_batch = 16, batches_inflight = 4;
int enable_async = 1;

__device__ int counter;
__device__ int clockrate;

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
int req_max_inflight = 0, rreq_max_inflight = 0, prepost_depth = 0;
cudaStream_t stream;
size_t buf_size;

int gpu_id = -1;
int wait_key = 0;

/*mp specific objects*/
mp_request_t *sreq = NULL;
mp_request_t *rreq = NULL;
mp_reg_t sreg, rreg;
double time_start, time_stop;

double sr_exchange (MPI_Comm comm, int size, int iter_count, int validate, double kernel_time, int use_async, struct prof *prof)
{
    int i, j;
    double latency;
    double time_start, time_stop;
    int req_idx = 0, rreq_idx = 0, complete_req_idx = 0, complete_rreq_idx = 0;
    int req_inflight = 0, rreq_inflight = 0;

    mp_dbg_msg("size=%d iter_count=%d kernel_time=%f use_async=%d\n", size, iter_count, kernel_time, use_async);

    if (validate) {
        mp_dbg_msg("initializing the buffer \n");
        CUDA_CHECK(cudaMemset(sbuf_d, (size + 1)%CHAR_MAX, buf_size));
        CUDA_CHECK(cudaMemset(rbuf_d, 0, buf_size));
        CUDA_CHECK(cudaDeviceSynchronize());
    }

    time_start = MPI_Wtime();

    for (j=0; j<prepost_depth; j++) {
        mp_dbg_msg("posted recv request: %d \n", rreq_idx);
        MP_CHECK(mp_irecv ((void *)((uintptr_t)rbuf_d + size*j), size, peer, &rreg, &rreq[rreq_idx]));
        rreq_idx = (rreq_idx + 1)%rreq_max_inflight;
        rreq_inflight++;
    }

    uint32_t wait_flag;
    if (use_async && wait_key && (1 == my_rank)) {
        fprintf(stdout, "[%d] waiting enabled, inserting a wait32_on_stream\n", my_rank); fflush(stdout);
        ACCESS_ONCE(wait_flag) = 0;
        MP_CHECK(mp_wait32_on_stream(&wait_flag, 1, MP_WAIT_GEQ, stream));
    }


    prof_idx = 0;
    for (j = 0; j < iter_count; j++) {
	mp_dbg_msg("iteration :%d \n", j);

        if (!my_rank) {
            if (prof) PROF(prof, prof_idx++);
            req_idx = j%rreq_max_inflight;
            if (!use_async) {
                MP_CHECK(mp_wait(&rreq[req_idx]));
            } else {
                MP_CHECK(mp_wait_on_stream(&rreq[req_idx], stream));
            }

            if (prof) PROF(prof, prof_idx++);

            if (kernel_time > 0) {
                dummy_kernel <<<1, 1, 0, stream>>> (kernel_time);
                if (!use_async) {
                    CUDA_CHECK(cudaStreamSynchronize(stream));
                }
            }

            if (prof) PROF(prof, prof_idx++);

            req_idx = j%req_max_inflight;
            if (!use_async) {
                MP_CHECK(mp_isend ((void *)((uintptr_t)sbuf_d + size*j), size, peer, &sreg, &sreq[req_idx]));
            } else {
                MP_CHECK(mp_isend_on_stream ((void *)((uintptr_t)sbuf_d + size*j), size, peer, &sreg, &sreq[req_idx], stream));
            mp_dbg_msg("posted send request: %d \n", req_idx);
            }
        } else {
            req_idx = j%req_max_inflight;

            if (!use_async) {
                MP_CHECK(mp_isend ((void *)((uintptr_t)sbuf_d + size*j), size, peer, &sreg, &sreq[req_idx]));
            } else {
                MP_CHECK(mp_isend_on_stream ((void *)((uintptr_t)sbuf_d + size*j), size, peer, &sreg, &sreq[req_idx], stream));
            }
            mp_dbg_msg("posted send request: %d\n", req_idx);

            req_idx = j%rreq_max_inflight;
            if (!use_async) {
                MP_CHECK(mp_wait(&rreq[req_idx]));
            } else {
                MP_CHECK(mp_wait_on_stream(&rreq[req_idx], stream));
            }

            if (kernel_time > 0) {
                dummy_kernel <<<1, 1, 0, stream>>> (kernel_time);
                if (!use_async) {
                    CUDA_CHECK(cudaStreamSynchronize(stream));
                }
            }
        }

        req_inflight++;
        mp_dbg_msg("requests inflight: %d \n", req_inflight);

        if (!my_rank && prof)  PROF(prof, prof_idx++);

        if ((j + prepost_depth) < iter_count) {
            mp_dbg_msg("posted recv request: %d\n", rreq_idx);
            int buf_idx = (j + prepost_depth);
            MP_CHECK(mp_irecv ((void *)((uintptr_t)rbuf_d + size*buf_idx), size, peer, &rreg, &rreq[rreq_idx]));
            rreq_idx = (rreq_idx + 1)%rreq_max_inflight;
            rreq_inflight++;
        }

        if (!my_rank && prof)  PROF(prof, prof_idx++);

        if (use_async && wait_key && (1 == my_rank)) {
            fprintf(stdout, "[%d] sleeping 15s\n", my_rank);
            sleep(15);
            ACCESS_ONCE(wait_flag) = 1;
            // disabling wait_key for subsequent calls
            wait_key = 0;
            fprintf(stdout, "[%d] sleeping 20us to let previous batches to run\n", my_rank);
            usleep(20);
            fprintf(stdout, "[%d] resuming...\n", my_rank);
            fflush(stdout);
        }

        /*synchronize on oldest batch*/
        if (req_inflight == req_max_inflight) {
	    if (use_async) { 
	        for (i=0; i<steps_per_batch; i++) {
	            mp_dbg_msg("waiting on recv request: %d\n", complete_rreq_idx);
                    MP_CHECK(mp_wait(&rreq[complete_rreq_idx]));
	            mp_dbg_msg("completed recv request: %d\n", complete_rreq_idx);
                    complete_rreq_idx = (complete_rreq_idx + 1)%rreq_max_inflight;
                    rreq_inflight--;
                } 
                mp_dbg_msg("after waiting on recv, rreq_inflight: %d \n", rreq_inflight);
	    }

	    for (i=0; i<steps_per_batch; i++) {
		mp_dbg_msg("waiting on send request: %d \n", complete_req_idx);
                MP_CHECK(mp_wait(&sreq[complete_req_idx]));
		mp_dbg_msg("completed send request: %d \n", complete_req_idx);
                complete_req_idx = (complete_req_idx + 1)%req_max_inflight;
                req_inflight--;
            }
	    mp_dbg_msg("after waiting on send, req_inflight: %d \n", req_inflight);

        }

        if (j == (iter_count - 1)) {
	    /*ideally, there should be validation here*/
	    if (use_async) {
                while (rreq_inflight > 0) {
                    mp_wait(&rreq[complete_rreq_idx]);
                    mp_dbg_msg("completed recv request: %d \n", complete_rreq_idx);
                    complete_rreq_idx = (complete_rreq_idx + 1)%rreq_max_inflight;
                    rreq_inflight--;
                }
	    }

            while (req_inflight > 0) {
                mp_wait(&sreq[complete_req_idx]);
                mp_dbg_msg("completed send request: %d \n", complete_req_idx);
                complete_req_idx = (complete_req_idx + 1)%req_max_inflight;
                req_inflight--;
            }
        }

        if (!my_rank && prof)  {
            PROF(prof, prof_idx++);
            prof_update(prof);
            prof_idx = 0;
        }
    }

    // TODO: move validate after timing
    if (validate) {
        CUDA_CHECK(cudaMemcpy((void *)((uintptr_t)buf), (void *)((uintptr_t)rbuf_d), 
        	buf_size, cudaMemcpyDefault));
	//CUDA_CHECK(cudaDeviceSynchronize());

        char *value = (char *)((uintptr_t)buf);
        for (i=0; i<buf_size; i++) {
             if (value[i] != (size + 1)%CHAR_MAX) {
                 mp_dbg_msg("validation check failed index: %d expected: %d actual: %d \n", 
                            i, (size + 1)%CHAR_MAX, value[i]);
                 exit(-1);
             }
        }
    }

    MPI_Barrier(comm);

    time_stop = MPI_Wtime();
    latency = (((time_stop - time_start)*1e6)/(iter_count*2));

    CUDA_CHECK(cudaDeviceSynchronize());

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
        c = getopt(argc, argv, "d:W:s:");
        if (c == -1)
            break;

        switch(c) {
        case 'd':
            gpu_id = strtol(optarg, NULL, 0);
            break;
        case 's':
            size = strtol(optarg, NULL, 0);
            printf("size=%d\n", size);
            break;
        case 'W':
            wait_key = strtol(optarg, NULL, 0);
            printf("wait_key=%d\n", wait_key);
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

    if (gpu_id >= 0) {
        local_rank = gpu_id;
    } else if (getenv("USE_GPU")) {
        local_rank = atoi(getenv("USE_GPU"));
    } else if (getenv("MV2_COMM_WORLD_LOCAL_RANK") != NULL) {
        local_rank = atoi(getenv("MV2_COMM_WORLD_LOCAL_RANK"));
    } else if (getenv("OMPI_COMM_WORLD_LOCAL_RANK") != NULL) {
        local_rank = atoi(getenv("OMPI_COMM_WORLD_LOCAL_RANK"));
    }

    dev_id = local_rank%dev_count;
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
        fprintf(stdout, "steps_per_batch: %d batches_inflight: %d \n",
                steps_per_batch, batches_inflight);
        fprintf(stdout, "WARNING: dumping half round-trip latency!!!\n");
    }

    prepost_depth = (steps_per_batch < iter_count) ? steps_per_batch : iter_count;
    req_max_inflight = steps_per_batch*batches_inflight;
    rreq_max_inflight = (steps_per_batch*batches_inflight + prepost_depth);

    /*allocating requests*/
    sreq = (mp_request_t *) malloc(req_max_inflight*sizeof(mp_request_t));
    rreq = (mp_request_t *) malloc(rreq_max_inflight*sizeof(mp_request_t));

    CUDA_CHECK(cudaStreamCreateWithFlags(&stream, cudaStreamNonBlocking));

    if (!my_rank) fprintf(stdout, "%10s\t  %10s\t    %10s\t    %10s %10s\t    %10s\n", "Size", "KernelTime", "No-async", "No-async+Kernel", "Async", "Async+Kernel");
    if (size != 1) size = max_size = size;
    for (; size<=max_size; size*=2)
    {
        double latency;
        const char *tags = "kernel|send|recv|prepost|wait|";

        if (size > 1024) {
            iter_count = ITER_COUNT_LARGE;
        }

        buf_size = size*iter_count;
        buf = malloc (buf_size);
        memset(buf, 0, buf_size);

        CUDA_CHECK(cudaMalloc((void **)&sbuf_d, buf_size));
        CUDA_CHECK(cudaMemset(sbuf_d, 0, buf_size));

        CUDA_CHECK(cudaMalloc((void **)&rbuf_d, buf_size));
        CUDA_CHECK(cudaMemset(rbuf_d, 0, buf_size));

        MP_CHECK(mp_register(sbuf_d, buf_size, &sreg, 0));
        MP_CHECK(mp_register(rbuf_d, buf_size, &rreg, 0));

        if (!my_rank) {
            if (prof_init(&prof_normal, 1000,  1000, "1us", 100, 1, tags)) {
                fprintf(stderr, "error in prof_init init.\n");
                exit(-1);
            }
            if (prof_init(&prof_async, 1000,  1000, "1us", 100, 1, tags)) {
                fprintf(stderr, "error in prof_init init.\n");
                exit(-1);
            }

            prof_start = 1;
        }

        if (!my_rank) fprintf(stdout, "%10d", size);

        /*warmup*/
        latency = sr_exchange(MPI_COMM_WORLD, size, iter_count, validate, 0/*kernel_time*/, 1/*use_async*/, NULL/*prof*/);
        latency = sr_exchange(MPI_COMM_WORLD, size, iter_count, validate, 1/*kernel_time*/, 1/*use_async*/, NULL/*prof*/);
        latency = sr_exchange(MPI_COMM_WORLD, size, iter_count, validate, 0/*kernel_time*/, 0/*use_async*/, NULL/*prof*/);
        latency = sr_exchange(MPI_COMM_WORLD, size, iter_count, validate, 1/*kernel_time*/, 0/*use_async*/, NULL/*prof*/);

        /*Normal*/
        latency = sr_exchange(MPI_COMM_WORLD, size, iter_count, validate, 0/*kernel_time*/, 0/*use_async*/, NULL/*prof*/);

        kernel_time = (comm_comp_ratio > 0) ? comm_comp_ratio*latency : kernel_time;
        if (!my_rank) fprintf(stdout, "\t   %10d", kernel_time);
        if (!my_rank) fprintf(stdout, "\t   %8.2lf", latency);

        cudaProfilerStart();

        /*Normal + Kernel*/
        latency = sr_exchange(MPI_COMM_WORLD, size, iter_count, validate, kernel_time, 0/*use_async*/, &prof_normal/*prof*/);
        if (!my_rank) fprintf(stdout, "\t   %8.2lf", latency);

        cudaProfilerStop();

        /*Async*/
        latency = sr_exchange(MPI_COMM_WORLD, size, iter_count, validate, 0/*kernel_time*/, 1/*use_async*/, NULL/*prof*/);
        if (!my_rank) fprintf(stdout, "\t   %8.2lf", latency);

        cudaProfilerStart();

        /*Async + Kernel*/
        latency = sr_exchange(MPI_COMM_WORLD, size, iter_count, validate, kernel_time, 1/*use_async*/, &prof_async/*prof*/);
        if (!my_rank) fprintf(stdout, "\t   %8.2lf \n", latency);


        cudaProfilerStop();

        if (!my_rank && validate) fprintf(stdout, "SendRecv test passed validation with message size: %d \n", size);

        if (!my_rank) {
            prof_dump(&prof_normal);
            prof_dump(&prof_async);
        }

        mp_deregister(&sreg);
        mp_deregister(&rreg);

        CUDA_CHECK(cudaFree(sbuf_d));
        CUDA_CHECK(cudaFree(rbuf_d));
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
