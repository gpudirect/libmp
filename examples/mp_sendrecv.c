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
#include <unistd.h>
#include <assert.h>
#include <mpi.h>
#include <mp.h>

#include "test_utils.h"

#define MAX_SIZE 64*1024
#define ITER_COUNT_SMALL 1 //50
#define ITER_COUNT_LARGE 1 //10
#define WINDOW_SIZE 1 //64 

int comm_size, my_rank, peer;

int sr_exchange (MPI_Comm comm, int size, int iter_count, int window_size, int validate)
{
    int i, j, k;
    size_t buf_size; 

    /*application and pack buffers*/
    void *buf = NULL, *buf_d = NULL;

    /*mp specific objects*/
    mp_request_t *req = NULL;
    mp_reg_t reg; 

    buf_size = size*window_size;

    /*allocating requests*/
    req = (mp_request_t *) malloc(window_size*sizeof(mp_request_t));

    buf = malloc (buf_size);
    memset(buf, 0, buf_size); 

    CUDA_CHECK(cudaMalloc((void **)&buf_d, buf_size));
    CUDA_CHECK(cudaMemset(buf_d, 0, buf_size)); 

    MP_CHECK(mp_register(buf_d, buf_size, &reg));

    dbg_msg("registered ptr: %p size: %zu\n", buf_d, buf_size);

    for (i = 0; i < iter_count; i++) {
        dbg_msg("i=%d\n", i);
        if (!my_rank) { 
            if (validate) {
                CUDA_CHECK(cudaMemset(buf_d, (i+1)%CHAR_MAX, buf_size));
		CUDA_CHECK(cudaDeviceSynchronize());
            }
            dbg_msg("calling Barrier\n");
	    MPI_Barrier(MPI_COMM_WORLD);

            if (0) {
                static int done = 0;
                if (!done) {
                    printf("sleeping 20s\n");
                    sleep(20);
                    done = 1;
                }
            }

            for(j=0; j < window_size; j++)  
            {  
               MP_CHECK(mp_isend ((void *)((uintptr_t)buf_d + size*j), size, peer, &reg, &req[j])); 
	    }
        } else {
            for(j=0; j < window_size; j++)
            {
               MP_CHECK(mp_irecv ((void *)((uintptr_t)buf_d + size*j), size, peer, &reg, &req[j]));
            }
            dbg_msg("calling Barrier\n");
	    MPI_Barrier(MPI_COMM_WORLD);
        }
        dbg_msg("calling mp_wait\n");
        for(j=0; j < window_size; j++)
        {
           MP_CHECK(mp_wait(&req[j]));
        }
        dbg_msg("calling #2 Barrier\n");
        MPI_Barrier(comm);

        if (validate && my_rank) { 
            CUDA_CHECK(cudaMemcpy(buf, buf_d, buf_size, cudaMemcpyDefault));
	    CUDA_CHECK(cudaDeviceSynchronize());
            char *value; 
            char expected = (char) (i+1)%CHAR_MAX;
            for (j=0; j<window_size; j++) { 
		 value = (char *)buf + size*j;
	         for (k=0; k<size; k++) {
                     if (value[k] != ((i+1)%CHAR_MAX)) { 
 	                fprintf(stderr, "validation check failed iter: %d index: %d expected: %d actual: %d \n", i, k, expected, value[k]);
                         exit(-1);
		     }
		 }
 	    } 
        }

        MPI_Barrier(comm);
    } 

    CUDA_CHECK(cudaDeviceSynchronize());

    mp_deregister(&reg);

    CUDA_CHECK(cudaFree(buf_d));
    free(buf);
    free(req);

    return 0;
}

int main (int c, char *v[])
{
    int iter_count, window_size, size;
    int validate = 1;

    MPI_Init(&c, &v);
    MPI_Comm_size(MPI_COMM_WORLD, &comm_size);
    MPI_Comm_rank(MPI_COMM_WORLD, &my_rank);

    if (comm_size != 2) { 
	fprintf(stderr, "this test requires exactly two processes \n");
        exit(-1);
    }

#if 0
    char hostname[256] = {0};
    assert(0 == gethostname(hostname, sizeof(hostname)));
    const char *deb = getenv("DEBUGGER");
    if (deb && atoi(deb) > 0) {
        printf("%s: press a key\n", hostname); fflush(stdout);
        char c;
        scanf("%c", &c);
        printf("going on...\n");
    } else {
        printf("%s: sleeping 2s\n", hostname);
        sleep(2);
    }
#endif

    if (gpu_init(-1)) {
        fprintf(stderr, "got error while initializing GPU\n");
        MPI_Abort(MPI_COMM_WORLD, -1);
    }

    peer = !my_rank;
    //Need to set CUDA_VISIBLE_DEVICES
    MP_CHECK(mp_init(MPI_COMM_WORLD, &peer, 1, MP_INIT_DEFAULT, 0));

    iter_count = ITER_COUNT_SMALL;
    window_size = WINDOW_SIZE; 

    for (size=1; size<=MAX_SIZE; size*=2) 
    {
        if (size > 1024) {
            iter_count = ITER_COUNT_LARGE;
        }

        sr_exchange(MPI_COMM_WORLD, size, iter_count, window_size, validate);

        if (!my_rank) fprintf(stdout, "SendRecv test passed validation with message size: %d \n", size);
    }

    mp_finalize ();

    MPI_Barrier(MPI_COMM_WORLD);

    MPI_Finalize();

    return 0;
}
