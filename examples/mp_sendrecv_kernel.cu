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

#include "mp_common_examples.hpp"
#include <mp_comm_kernel.cuh>

#define MAX_SIZE 64*1024
// NOTE: cannot iterate too much before exhausting resources, like CQs and WQs.
#define ITER_COUNT_SMALL 20
#define ITER_COUNT_LARGE 1
#define WINDOW_SIZE 64 

int peers_num, my_rank, peer;
int use_gpu_buffers=0;

struct kernel_comm_descs {
    enum { max_n_descs = ITER_COUNT_SMALL };
    mp_kernel_desc_send tx[max_n_descs];
    mp_kernel_desc_wait tx_wait[max_n_descs];
    mp_kernel_desc_wait rx_wait[max_n_descs];
};

__global__ void exchange_kernel(int my_rank, kernel_comm_descs descs, int iter_count)
{
    int i;
    assert(gridDim.x == 1);

    //if (threadIdx.x == 0) printf("iter_count=%d\n", iter_count);

    for (i=0; i<iter_count; ++i) {
        if (!my_rank) {
            if (0 == threadIdx.x) {
                //printf("i=%d send+recv\n", i);
                // make sure NIC can fetch coherent data
                __threadfence();
                mp_isend_kernel(descs.tx[i]);
                mp_wait_kernel(descs.tx_wait[i]);
                mp_signal_kernel(descs.tx_wait[i]);
                mp_wait_kernel(descs.rx_wait[i]);
                mp_signal_kernel(descs.rx_wait[i]);
            }
            __syncthreads();
        } else {
            if (0 == threadIdx.x) {
                //printf("i=%d recv+send\n", i);
                // make sure NIC can fetch coherent data
                __threadfence();
                mp_wait_kernel(descs.rx_wait[i]);
                mp_signal_kernel(descs.rx_wait[i]);
                mp_isend_kernel(descs.tx[i]);
                mp_wait_kernel(descs.tx_wait[i]);
                mp_signal_kernel(descs.tx_wait[i]);
            }
            __syncthreads();
        }
    }
}

int launch_exchange_kernel(int my_rank, kernel_comm_descs &descs, int iter_count, cudaStream_t stream)
{
    exchange_kernel<<<1,16,0,stream>>>(my_rank, descs, iter_count);
    CUDA_CHECK(cudaGetLastError());
    return 0;
}

int sr_exchange (int size, int iter_count, int validate)
{
    int j;
    size_t buf_size; 
    cudaStream_t stream;

    /*application and pack buffers*/
    void *buf = NULL, *sbuf_d = NULL, *rbuf_d = NULL;

    /*mp specific objects*/
    mp_request_t * sreq, * rreq = NULL;
    mp_region_t * sreg, * rreg = NULL; 
    kernel_comm_descs descs;

    buf_size = size*iter_count;

    /*allocating requests*/
    sreq = mp_create_request(iter_count);
    rreq = mp_create_request(iter_count);

    CUDA_CHECK(cudaMallocHost(&buf, buf_size));
    memset(buf, 0, buf_size); 

    if(use_gpu_buffers == 1)
    {
        CUDA_CHECK(cudaMalloc((void **)&sbuf_d, buf_size));
        CUDA_CHECK(cudaMemset(sbuf_d, 0, buf_size)); 

        CUDA_CHECK(cudaMalloc((void **)&rbuf_d, buf_size));
        CUDA_CHECK(cudaMemset(rbuf_d, 0, buf_size));        
    }
    else
    {
        sbuf_d = (char *) calloc(buf_size, sizeof(char));
        rbuf_d = (char *) calloc(buf_size, sizeof(char));
    }
 
    CUDA_CHECK(cudaStreamCreate(&stream));  

    sreg = mp_create_regions(1);
    rreg = mp_create_regions(1);
    MP_CHECK(mp_register_region_buffer(sbuf_d, buf_size, &sreg[0]));
    MP_CHECK(mp_register_region_buffer(rbuf_d, buf_size, &rreg[0]));

    if (validate) {
        if(use_gpu_buffers == 1)
        {
            CUDA_CHECK(cudaMemset(sbuf_d, (my_rank + 1), buf_size));
            CUDA_CHECK(cudaMemset(rbuf_d, 0, buf_size));
        }
        else
        {
            memset(sbuf_d, (my_rank + 1), buf_size);
            memset(rbuf_d, 0, buf_size);   
        }
    }


    for (j = 0; j < iter_count; j++) {
        assert(j < kernel_comm_descs::max_n_descs);
        // note: the ordering is not important here, no risk of deadlocks
        MP_CHECK(mp_prepare_kernel_recv( (void *)((uintptr_t)rbuf_d + size*j), size, peer, &rreg[0], &rreq[j], &descs.rx_wait[j]));
        MP_CHECK(mp_prepare_kernel_send( (void *)((uintptr_t)sbuf_d + size*j), size, peer, &sreg[0], &sreq[j], &descs.tx[j], &descs.tx_wait[j]));
#if 0
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
#endif
    }
    //printf("launching kernel iter_count=%d\n", iter_count);
    launch_exchange_kernel(my_rank, descs, iter_count, stream);
    //CUDA_CHECK(cudaStreamSynchronize(stream));
    //printf("waiting for recv reqs\n");
    MP_CHECK(mp_wait_all(iter_count, rreq));
    //printf("waiting for send reqs\n");
    MP_CHECK(mp_wait_all(iter_count, sreq));
    // all ops in the stream should have been completed 
    usleep(1000);
    CUDA_CHECK(cudaStreamQuery(stream));
    mp_barrier();

    if (validate && my_rank) {
        CUDA_CHECK(cudaMemcpy(buf, rbuf_d, buf_size, cudaMemcpyDefault));
        char *value = (char *) buf;
        char expected = (char) (peer + 1);
        for (j=0; j<(iter_count*size); j++) {
             if (value[j] != (peer + 1)) {
                fprintf(stderr, "validation check failed index: %d expected: %d actual: %d \n", j, expected, value[j]);
                 exit(-1);
             }
        }
    }
    mp_barrier();
    CUDA_CHECK(cudaDeviceSynchronize());

    MP_CHECK(mp_unregister_regions(1, sreg));
    MP_CHECK(mp_unregister_regions(1, rreg));

    CUDA_CHECK(cudaStreamDestroy(stream));
    if(use_gpu_buffers == 1)
    {
        CUDA_CHECK(cudaFree(sbuf_d));
        CUDA_CHECK(cudaFree(rbuf_d));        
    }
    else
    {
        free(sbuf_d);
        free(rbuf_d);
    }

    cudaFreeHost(buf);
    free(sreq);
    free(rreq);

    return 0;
}

int main (int argc, char *argv[])
{
    int iter_count, window_size, size, ret;
    int validate = 1;
    int device_id=MP_DEFAULT;

    //GPUDirect Async
    char * envVar = getenv("MP_USE_GPU");
    if (envVar != NULL) {
        device_id = atoi(envVar);
    }

    //GPUDirect RDMA
    envVar = getenv("MP_BENCH_GPU_BUFFERS"); 
    if (envVar != NULL) {
        use_gpu_buffers = atoi(envVar);
        if(use_gpu_buffers == 1)
            dbg_msg("Using GPU buffers, GPUDirect RDMA\n");
    }

    ret = mp_init(argc, argv, device_id);
    if(ret) exit(EXIT_FAILURE);
    
    mp_query_param(MP_MY_RANK, &my_rank);
    mp_query_param(MP_NUM_RANKS, &peers_num);
    
    if(peers_num != 2)
    {
        fprintf(stderr, "This test requires exactly two processes\n");
        mp_abort();
    }

    peer = !my_rank;
    iter_count = ITER_COUNT_SMALL;

    for (size=1; size<=MAX_SIZE; size*=2) 
    {
        if (size > 1024) {
            iter_count = ITER_COUNT_LARGE;
        }

        sr_exchange(size, iter_count, validate);

        if (!my_rank) fprintf(stdout, "# SendRecv test passed validation with message size: %d \n", size);
    }

    mp_barrier();
    mp_finalize();
    
    return EXIT_SUCCESS;
}
