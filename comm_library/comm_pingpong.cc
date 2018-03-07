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
#include "comm.h"

#define MAX_PEERS 10
#define BUF_SIZE 1024
#define NUM_ITERS 1024

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


comm_reg_t * sreg, * rreg;
int comm_size, my_rank, device_id;
double time_start, time_stop;
unsigned char * send_buf[MAX_PEERS];
unsigned char * recv_buf[MAX_PEERS];
int use_gpu_buffers=0;
int tot_iters=NUM_ITERS;
int max_size=BUF_SIZE;
int validate=0;

int async_exchange(int iter) {
    int peer, n_sreqs=0, n_rreqs=0;
    comm_request_t ready_requests[MAX_PEERS];
    comm_request_t recv_requests[MAX_PEERS];
    comm_request_t send_requests[MAX_PEERS];

    for(peer=0; peer<comm_size; peer++)
    {
        if(peer != my_rank)
        {
            comm_irecv(recv_buf[peer], max_size, MPI_CHAR, &rreg[peer], peer, &recv_requests[n_rreqs]);
            comm_send_ready_on_stream(peer, &ready_requests[n_rreqs], NULL);
            n_rreqs++;
        }
    }

    for(peer=0; peer<comm_size; peer++)
    {
        if(peer != my_rank)
        {
            comm_wait_ready_on_stream(peer,NULL);
            comm_isend_on_stream(send_buf[peer], max_size, MPI_CHAR, 
                            &sreg[peer], peer, &send_requests[n_sreqs], NULL);

            n_sreqs++;
        }
    }

    comm_wait_all_on_stream(n_rreqs, recv_requests, NULL);
    comm_wait_all_on_stream(n_sreqs, send_requests, NULL);
    //comm_wait_all_on_stream(n_rreqs, ready_requests, NULL);

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
            comm_irecv(recv_buf[peer], max_size, MPI_CHAR, &rreg[peer], peer, &recv_requests[n_rreqs]);
            comm_send_ready(peer, &ready_requests[n_rreqs]);
            n_rreqs++;
        }
    }

    for(peer=0; peer<comm_size; peer++)
    {
        if(peer != my_rank)
        {
            comm_wait_ready(peer);
            comm_isend(send_buf[peer], max_size, MPI_CHAR, 
                            &sreg[peer], peer, &send_requests[n_sreqs]);

            n_sreqs++;
        }
    }
    comm_flush();
}

int main(int argc, char **argv) {
    int i,j,k,iter;
    char *value;
    double tot_time, start_time, stop_time;

    value = getenv("USE_GPU_BUFFERS");
    if (value != NULL) {
        use_gpu_buffers = atoi(value);
    }

    value = getenv("MAX_SIZE");
    if (value != NULL) {
        max_size = atoi(value);
    }

    value = getenv("TOT_ITERS");
    if (value != NULL) {
        tot_iters = atoi(value);
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

    assert(comm_size <= MAX_PEERS);

    device_id = comm_select_device(my_rank);
    cudaSetDevice(device_id);
    if (my_rank < 10) {
        struct cudaDeviceProp devProp;
        cudaGetDeviceProperties(&devProp, device_id);
        printf("rank %d:  Selecting device %d (%s)\n",my_rank,device_id,devProp.name);
    }
    
    comm_init(MPI_COMM_WORLD, device_id);

    if(!my_rank) 
        printf("----> async sa=%d, use_gpu_buffers=%d, max_size=%d, tot_iters=%d num peers=%d validate=%d\n", 
                        comm_use_async()?0:1, use_gpu_buffers, max_size, tot_iters, comm_size, validate);

    for(i=0; i<comm_size; i++)
    {
        if(!use_gpu_buffers)
        {
            CUDA_CHECK(cudaMallocHost((void **)&send_buf[i], max_size*sizeof(char)));
            assert(send_buf[i]);
            memset(send_buf[i], 9, max_size*sizeof(char));

            CUDA_CHECK(cudaMallocHost((void **)&recv_buf[i], max_size*sizeof(char)));
            assert(recv_buf[i]);
            memset(recv_buf[i], 0, max_size*sizeof(char));            
        }
        else
        {
            CUDA_CHECK(cudaMalloc((void **)&send_buf[i], max_size*sizeof(char)));
            CUDA_CHECK(cudaMemset(send_buf[i], 9, max_size*sizeof(char)));

            CUDA_CHECK(cudaMalloc((void **)&recv_buf[i], max_size*sizeof(char)));
            CUDA_CHECK(cudaMemset(recv_buf[i], 0, max_size*sizeof(char)));
        }
    }

    //1 region for each buffer
    sreg = (comm_reg_t*)calloc(comm_size, sizeof(comm_reg_t));
    rreg = (comm_reg_t*)calloc(comm_size, sizeof(comm_reg_t));
    
    start_time = MPI_Wtime();
    for(iter=0; iter<tot_iters; iter++)
    {
        if(comm_use_async())
            async_exchange(iter);        
        else
            sync_exchange(iter);
    }

    if(comm_use_async())
    {
        cudaDeviceSynchronize();
        comm_flush();
    }   
    stop_time = MPI_Wtime();

    tot_time=((time_stop - time_start)*1e6);
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

    free(sreg);
    free(rreg);

    comm_finalize();
    MPI_Finalize();

    return 0;
}