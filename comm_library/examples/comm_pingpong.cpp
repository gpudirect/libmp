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

#include <malloc.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <errno.h>
#include <mpi.h>
#include <cuda_runtime.h>
#include <mp.h>
#include "../comm.h"

#define MAX_PEERS 10
#define BUF_SIZE 1024
#define MAX_ITERS 128

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


comm_reg_t * sreg, * rreg, * odpreg;
int comm_size, my_rank, device_id;
unsigned char * send_buf[MAX_PEERS];
unsigned char * recv_buf[MAX_PEERS];
int tot_iters=MAX_ITERS;
int buf_size=BUF_SIZE;
int use_gpu_buffers=0;
int validate=0;
int use_odp=0;

static void usage()
{
    printf("Options:\n");
    printf("  -g            allocate GPU intead of CPU memory buffers\n");
    printf("  -o            use implici ODP\n");
    printf("  -n=<iter>     number of exchanges (default %d)\n", MAX_ITERS);
    printf("  -s=<bytes>    S/R buffer size (default %d)\n", BUF_SIZE);
}

int async_exchange(int iter) {
    int peer, n_sreqs=0, n_rreqs=0;
    comm_request_t ready_requests[MAX_PEERS];
    comm_request_t recv_requests[MAX_PEERS];
    comm_request_t send_requests[MAX_PEERS];

    for(peer=0; peer<comm_size; peer++)
    {
        if(peer != my_rank)
        {
            COMM_CHECK(comm_irecv(recv_buf[peer], buf_size, MPI_CHAR, 
                                    (use_odp ? &odpreg[0] : &rreg[peer]),
                                    peer, &recv_requests[n_rreqs]));
            COMM_CHECK(comm_send_ready_on_stream(peer, &ready_requests[n_rreqs], NULL));
            n_rreqs++;
        }
    }

    for(peer=0; peer<comm_size; peer++)
    {
        if(peer != my_rank)
        {
            COMM_CHECK(comm_wait_ready_on_stream(peer,NULL));
            COMM_CHECK(comm_isend_on_stream(send_buf[peer], buf_size, MPI_CHAR,
                            (use_odp ? &odpreg[0] : &sreg[peer]),
                            peer, &send_requests[n_sreqs], NULL));

            n_sreqs++;
        }
    }

    COMM_CHECK(comm_wait_all_on_stream(n_rreqs, recv_requests, NULL));
    COMM_CHECK(comm_wait_all_on_stream(n_rreqs, ready_requests, NULL));
    COMM_CHECK(comm_wait_all_on_stream(n_sreqs, send_requests, NULL));

    //printf("Before progress, %d iter, %d n_rreqs, %d n_sreqs, %d comm_size\n", iter, n_rreqs, n_sreqs, comm_size);
    comm_progress();
}

int sync_exchange(int iter) {
    int peer, n_sreqs=0, n_rreqs=0;
    comm_request_t ready_requests[MAX_PEERS];
    comm_request_t recv_requests[MAX_PEERS];
    comm_request_t send_requests[MAX_PEERS];

    for(peer=0; peer<comm_size; peer++)
    {
        if(peer != my_rank)
        {
            COMM_CHECK(comm_irecv(recv_buf[peer], buf_size, MPI_CHAR, 
                                    (use_odp ? &odpreg[0] : &rreg[peer]),
                                    peer, &recv_requests[n_rreqs]));
            COMM_CHECK(comm_send_ready(peer, &ready_requests[n_rreqs]));
            n_rreqs++;
        }
    }

    for(peer=0; peer<comm_size; peer++)
    {
        if(peer != my_rank)
        {
            COMM_CHECK(comm_wait_ready(peer));
            COMM_CHECK(comm_isend(send_buf[peer], buf_size, MPI_CHAR, 
                            (use_odp ? &odpreg[0] : &sreg[peer]),
                            peer, &send_requests[n_sreqs]));

            n_sreqs++;
        }
    }

    comm_flush();
}

int main(int argc, char **argv) {
    int i,j,k,iter;
    char *value;
    double tot_time, start_time, stop_time;
    int c;

        while (1) {

        c = getopt(argc, argv, "gon:s:");
        if (c == -1)
            break;

        switch (c) {
        case 'g':
            use_gpu_buffers=1;
            printf("Using GPU memory for communication buffers\n");
            break;

        case 'n':
            tot_iters = strtol(optarg, NULL, 0);
            if(tot_iters > MAX_ITERS)
                tot_iters = MAX_ITERS;
            printf("Tot iters=%d\n", tot_iters);
            break;

        case 'o':
            use_odp=1;
            printf("Using implicit ODP\n");
            break;


        case 's':
            buf_size=strtol(optarg, NULL, 0);
            printf("Using buf_size=%d\n", buf_size);
            
            break;

        default:
            usage();
            return 1;
        }
    }
   
    value = getenv("ENABLE_VALIDATION");
    if (value != NULL) {
        validate = atoi(value);
    }

    if(!comm_use_comm())
        fprintf(stderr, "ERROR: pingpong + one sided for comm library only\n");

    MPI_Init(&argc, &argv);
    MPI_Comm_size(MPI_COMM_WORLD, &comm_size);
    MPI_Comm_rank(MPI_COMM_WORLD, &my_rank);

    if (comm_size < 2 || comm_size > MAX_PEERS) { 
    fprintf(stderr, "this test requires 2<ranks<%d\n", MAX_PEERS);
        exit(-1);
    }

    device_id = comm_select_device(my_rank);
    CUDA_CHECK(cudaSetDevice(device_id));
    if (my_rank < 10) {
        struct cudaDeviceProp devProp;
        CUDA_CHECK(cudaGetDeviceProperties(&devProp, device_id));
        printf("rank %d:  Selecting device %d (%s)\n",my_rank,device_id,devProp.name);
    }
    
    comm_init(MPI_COMM_WORLD, device_id);
    
    for(i=0; i<comm_size; i++)
    {
        if(!use_gpu_buffers)
        {
            CUDA_CHECK(cudaMallocHost((void **)&send_buf[i], buf_size*sizeof(char)));
            assert(send_buf[i]);
            memset(send_buf[i], 9, buf_size*sizeof(char));

            CUDA_CHECK(cudaMallocHost((void **)&recv_buf[i], buf_size*sizeof(char)));
            assert(recv_buf[i]);
            memset(recv_buf[i], 0, buf_size*sizeof(char));            
        }
        else
        {
            CUDA_CHECK(cudaMalloc((void **)&send_buf[i], buf_size*sizeof(char)));
            CUDA_CHECK(cudaMemset(send_buf[i], 9, buf_size*sizeof(char)));

            CUDA_CHECK(cudaMalloc((void **)&recv_buf[i], buf_size*sizeof(char)));
            CUDA_CHECK(cudaMemset(recv_buf[i], 0, buf_size*sizeof(char)));
        }
    }

    if(use_odp)
    {
        odpreg = (comm_reg_t*)calloc(1, sizeof(comm_reg_t));
        assert(odpreg);
        COMM_CHECK(comm_register_odp(NULL, 0, &odpreg[0]));
    }
    else
    {
        sreg = (comm_reg_t*)calloc(comm_size, sizeof(comm_reg_t));
        assert(sreg);
        rreg = (comm_reg_t*)calloc(comm_size, sizeof(comm_reg_t));
        assert(sreg);        
    }

    if(!my_rank) 
        printf("# SA Model=%d\n# use_gpu_buffers=%d\n# buf_size=%d\n# tot_iters=%d\n# num peers=%d\n# validate=%d\n# use_odp=%d\n",
                    comm_use_model_sa()?1:0, use_gpu_buffers, buf_size, tot_iters, comm_size, validate, use_odp);

    start_time = MPI_Wtime();
    for(iter=0; iter<tot_iters; iter++)
    {
        if(comm_use_model_sa())
            async_exchange(iter);        
        else
            sync_exchange(iter);
    }

    if(comm_use_model_sa())
    {
        CUDA_CHECK(cudaDeviceSynchronize());
        COMM_CHECK(comm_flush());
    }   
    stop_time = MPI_Wtime();

    tot_time=( ((stop_time - start_time)*1e6) / tot_iters);
    if(!my_rank)
        printf("Stats:\n\tIterations: %d\n\tProcs: %d\n\tTotal RTT: %8.2lfusec\n", 
                    tot_iters, comm_size, tot_time);

    if(!use_gpu_buffers)
    {
        for(i=0; i<comm_size; i++)
        {
            CUDA_CHECK(cudaFreeHost(send_buf[i]));
            CUDA_CHECK(cudaFreeHost(recv_buf[i]));
        }        
    }
    else
    {
        for(i=0; i<comm_size; i++)
        {
            CUDA_CHECK(cudaFree(send_buf[i]));
            CUDA_CHECK(cudaFree(recv_buf[i]));
        } 
    }

    if(use_odp)
        free(odpreg);
    else
    {
        free(sreg);
        free(rreg);
    }

    comm_finalize();
    MPI_Finalize();

    return 0;
}
