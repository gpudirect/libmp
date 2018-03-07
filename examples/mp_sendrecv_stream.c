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

#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <mpi.h>
#include <cuda.h>
#include <cuda_runtime.h>
#include <mp.h>
#include "test_utils.h"

#define MIN_SIZE 1
#define MAX_SIZE 64*1024
#define ITER_COUNT_SMALL 20
#define ITER_COUNT_LARGE 1
#define WINDOW_SIZE 64 

int comm_size, my_rank, peer;

int sr_exchange (MPI_Comm comm, int size, int iter_count, int validate)
{
    int j;
    size_t buf_size; 
    cudaStream_t stream;

    /*application and pack buffers*/
    void *buf = NULL, *sbuf_d = NULL, *rbuf_d = NULL;

    /*mp specific objects*/
    mp_request_t *sreq = NULL;
    mp_request_t *rreq = NULL;
    mp_reg_t sreg, rreg; 

    buf_size = size*iter_count;

    /*allocating requests*/
    sreq = (mp_request_t *) malloc(iter_count*sizeof(mp_request_t));
    rreq = (mp_request_t *) malloc(iter_count*sizeof(mp_request_t));

    cudaMallocHost(&buf, buf_size);
    memset(buf, 0, buf_size); 

    CUDA_CHECK(cudaMalloc((void **)&sbuf_d, buf_size));
    CUDA_CHECK(cudaMemset(sbuf_d, 0, buf_size)); 

    CUDA_CHECK(cudaMalloc((void **)&rbuf_d, buf_size));
    CUDA_CHECK(cudaMemset(rbuf_d, 0, buf_size)); 
 
    CUDA_CHECK(cudaStreamCreate(&stream));	

    MP_CHECK(mp_register(sbuf_d, buf_size, &sreg));
    MP_CHECK(mp_register(rbuf_d, buf_size, &rreg));

    if (validate) {
        CUDA_CHECK(cudaMemset(sbuf_d, (my_rank + 1), buf_size));
        CUDA_CHECK(cudaMemset(rbuf_d, 0, buf_size));
    }

    for (j = 0; j < iter_count; j++) {
        if (!my_rank) { 
            MP_CHECK(mp_isend_on_stream ((void *)((uintptr_t)sbuf_d + size*j), size, peer, &sreg, &sreq[j], stream));
            MP_CHECK(mp_wait_on_stream(&sreq[j], stream));

            MP_CHECK(mp_irecv ((void *)((uintptr_t)rbuf_d + size*j), size, peer, &rreg, &rreq[j]));
            MP_CHECK(mp_wait_on_stream(&rreq[j], stream));
        } else {
            MP_CHECK(mp_irecv ((void *)((uintptr_t)rbuf_d + size*j), size, peer, &rreg, &rreq[j]));
            MP_CHECK(mp_wait_on_stream(&rreq[j], stream));

            MP_CHECK(mp_isend_on_stream ((void *)((uintptr_t)sbuf_d + size*j), size, peer, &sreg, &sreq[j], stream));
            MP_CHECK(mp_wait_on_stream(&sreq[j], stream));
        }
    } 
    MP_CHECK(mp_wait_all(iter_count, rreq));
    MP_CHECK(mp_wait_all(iter_count, sreq));
    // all ops in the stream should have been completed 
    usleep(1000);
    CUDA_CHECK(cudaStreamQuery(stream));
    MPI_CHECK(MPI_Barrier(comm));

    if (validate && my_rank) {
        CUDA_CHECK(cudaMemcpy(buf, rbuf_d, buf_size, cudaMemcpyDefault));
        char *value = buf;
        char expected = (char) (peer + 1);
        for (j=0; j<(iter_count*size); j++) {
             if (value[j] != (peer + 1)) {
                fprintf(stderr, "validation check failed index: %d expected: %d actual: %d \n", j, expected, value[j]);
                 exit(-1);
             }
        }
    }
    MPI_CHECK(MPI_Barrier(comm));
    CUDA_CHECK(cudaDeviceSynchronize());
    mp_deregister(&sreg);
    mp_deregister(&rreg);
    CUDA_CHECK(cudaStreamDestroy(stream));
    CUDA_CHECK(cudaFree(sbuf_d));
    CUDA_CHECK(cudaFree(rbuf_d));
    cudaFreeHost(buf);
    free(sreq);
    free(rreq);

    return 0;
}

int main (int c, char *v[])
{
    int iter_count, size;
    int validate = 1;

    MPI_CHECK(MPI_Init(&c, &v));
    MPI_CHECK(MPI_Comm_size(MPI_COMM_WORLD, &comm_size));
    MPI_CHECK(MPI_Comm_rank(MPI_COMM_WORLD, &my_rank));

    if (comm_size != 2) { 
	fprintf(stderr, "this test requires exactly two processes \n");
        exit(-1);
    }

    if (gpu_init(-1)) {
        fprintf(stderr, "got error while initializing GPU\n");
        MPI_Abort(MPI_COMM_WORLD, -1);
    }

    peer = !my_rank;
    //Need to set CUDA_VISIBLE_DEVICES
    MP_CHECK(mp_init(MPI_COMM_WORLD, &peer, 1, MP_INIT_DEFAULT, 0));

    iter_count = ITER_COUNT_SMALL;

    for (size=MIN_SIZE; size<=MAX_SIZE; size*=2) 
    {
        if (size > 1024) {
            iter_count = ITER_COUNT_LARGE;
        }

        sr_exchange(MPI_COMM_WORLD, size, iter_count, validate);

        if (!my_rank) fprintf(stdout, "# SendRecv test passed validation with message size: %d \n", size);
    }

    mp_finalize();
    MPI_CHECK(MPI_Barrier(MPI_COMM_WORLD));
    MPI_CHECK(MPI_Finalize());
    return 0;
}
