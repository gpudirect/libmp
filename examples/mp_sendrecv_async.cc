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

#define MIN_SIZE 1
#define MAX_SIZE 64*1024
#define ITER_COUNT_SMALL 20
#define ITER_COUNT_LARGE 1
#define WINDOW_SIZE 64 

int peers_num, my_rank, peer;
int use_gpu_buffers=0;

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
        if (!my_rank) { 
            MP_CHECK(mp_isend_async ((void *)((uintptr_t)sbuf_d + size*j), size, peer, &sreg[0], &sreq[j], stream));
            MP_CHECK(mp_wait_async(&sreq[j], stream));

            MP_CHECK(mp_irecv ((void *)((uintptr_t)rbuf_d + size*j), size, peer, &rreg[0], &rreq[j]));
            MP_CHECK(mp_wait_async(&rreq[j], stream));
        } else {
            MP_CHECK(mp_irecv ((void *)((uintptr_t)rbuf_d + size*j), size, peer, &rreg[0], &rreq[j]));
            MP_CHECK(mp_wait_async(&rreq[j], stream));

            MP_CHECK(mp_isend_async ((void *)((uintptr_t)sbuf_d + size*j), size, peer, &sreg[0], &sreq[j], stream));
            MP_CHECK(mp_wait_async(&sreq[j], stream));
        }
    } 
    MP_CHECK(mp_wait_all(iter_count, rreq));
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
    int iter_count, size, ret;
    int validate = 1;
    int device_id=MP_DEFAULT;

    //GPUDirect Async
    char * envVar = getenv("MP_USE_GPU");
    if (envVar != NULL) {
        device_id = atoi(envVar);
    }
    else
        device_id = 0;

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
    peer = !my_rank;

    // CUDA init
    CUDA_CHECK(cudaSetDevice(device_id));
    CUDA_CHECK(cudaFree(0));
    
    iter_count = ITER_COUNT_SMALL;

    for (size=MIN_SIZE; size<=MAX_SIZE; size*=2) 
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
