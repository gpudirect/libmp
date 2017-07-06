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

#define MAX_SIZE 64*1024
#define ITER_COUNT_SMALL 50
#define ITER_COUNT_LARGE 10
#define WINDOW_SIZE 64

#if ITER_COUNT_SMALL < 3
#error "ITER_COUNT_SMALL too small"
#endif
#if ITER_COUNT_LARGE >= ITER_COUNT_SMALL
#error "ITER_COUNT_LARGE too big"
#endif

int peers_num, my_rank, peer;
int use_gpu_buffers=0;

int put_exchange (int size, int iter_count, int window_size, int validate)
{
	int i, j;
	size_t buf_size; 

	/*application and pack buffers*/
	void *buf = NULL, *commBuf = NULL;

	/*mp specific objects*/
	mp_region_t * regs;
	mp_request_t * reqs;
	mp_window_t win;

	buf_size = size*window_size;

	/*allocating requests*/
	regs = mp_create_regions(1);
	reqs = mp_create_request(window_size);

	buf = (char *) calloc(buf_size, sizeof(char));

	if(use_gpu_buffers == 1)
	{
		CUDA_CHECK(cudaMalloc((void **)&commBuf, buf_size));
		CUDA_CHECK(cudaMemset(commBuf, 0, buf_size));
	}
	else
	{
		CUDA_CHECK(cudaMallocHost((void **)&commBuf, buf_size));
		memset(commBuf, 0, buf_size);
	}

	MP_CHECK(mp_register_region_buffer(commBuf, buf_size, &regs[0]));
	MP_CHECK(mp_window_create(commBuf, buf_size, &win));

	for (i = 0; i < iter_count; i++) {

		if (!my_rank) { 
			if (validate) {
				if(use_gpu_buffers == 1)
					CUDA_CHECK(cudaMemset(commBuf, (i+1)%CHAR_MAX, buf_size));
				else
					memset(commBuf, (i+1)%CHAR_MAX, buf_size);
			}
 
			for(j=0; j < window_size; j++)	
			{  
				MP_CHECK(mp_iput ((void *)((uintptr_t)commBuf + j*size), size, &regs[0], peer, j*size, &win, &reqs[j], 0)); 
			}

			for(j=0; j < window_size; j++)	
			{
				MP_CHECK(mp_wait(&reqs[j])); 
			}			  

			mp_barrier();
			mp_barrier();
		} else {  
			mp_barrier();

			if (validate) { 
				if(use_gpu_buffers == 1)
					CUDA_CHECK(cudaMemcpy(buf, commBuf, buf_size, cudaMemcpyDefault));
				else
					memcpy(buf, commBuf, buf_size);

				char *value = (char *)buf; 
				char expected = (char) (i+1)%CHAR_MAX;
				for (j=0; j<(window_size*size); j++) { 
					if (value[j] != ((i+1)%CHAR_MAX)) { 
						fprintf(stderr, "validation check failed iter: %d index: %d expected: %d actual: %d \n", i, j, expected, value[j]);
						mp_abort();
					}
				} 
			}

			mp_barrier();
		}
	} 

	CUDA_CHECK(cudaDeviceSynchronize());

	dbg_msg("after device sync, before barrier\n");
	mp_barrier();
	
	MP_CHECK(mp_window_destroy(&win));
	MP_CHECK(mp_unregister_regions(1, regs));

	if(use_gpu_buffers == 1)
		CUDA_CHECK(cudaFree(commBuf));
	else
		CUDA_CHECK(cudaFreeHost(commBuf));

	free(buf);
	free(reqs);

	return 0;
}

int get_exchange (int size, int iter_count, int window_size, int validate)
{
	int i, j;
	size_t buf_size; 

	/*application and pack buffers*/
	void *buf = NULL, *commBuf = NULL;

	/*mp specific objects*/
	mp_region_t * regs;
	mp_request_t * reqs;
	mp_window_t win;

	buf_size = size*window_size;

	/*allocating requests*/
	regs = mp_create_regions(1);
	reqs = mp_create_request(window_size);

	buf = (char *) calloc(buf_size, sizeof(char));

	if(use_gpu_buffers == 1)
	{
		CUDA_CHECK(cudaMalloc((void **)&commBuf, buf_size));
		CUDA_CHECK(cudaMemset(commBuf, 0, buf_size));
	}
	else
	{
		CUDA_CHECK(cudaMallocHost((void **)&commBuf, buf_size));
		memset(commBuf, 0, buf_size);
	}

	MP_CHECK(mp_register_region_buffer(commBuf, buf_size, &regs[0]));
	MP_CHECK(mp_window_create(commBuf, buf_size, &win));

	for (i = 0; i < iter_count; i++) {

		if (!my_rank) {	 
			mp_barrier();

			for(j=0; j < window_size; j++)
			{
				MP_CHECK(mp_iget ((void *)((uintptr_t)commBuf + j*size), size, &regs[0], peer, j*size, &win, &reqs[j])); 
			}
			
			for(j=0; j < window_size; j++)	
				MP_CHECK(mp_wait(&reqs[j])); 
			
			if (validate) {
				if(use_gpu_buffers == 1)
					CUDA_CHECK(cudaMemcpy(buf, commBuf, buf_size, cudaMemcpyDefault));
				else
					memcpy(buf, commBuf, buf_size);

				char *value = (char *)buf;
				char expected = (char) (i+1)%CHAR_MAX;
				for (j=0; j<(window_size*size); j++) {
					if (value[j] != ((i+1)%CHAR_MAX)) {
						fprintf(stderr, "validation check failed iter: %d index: %d expected: %d actual: %d \n", i, j, expected, value[j]);
						mp_abort();
					}
				}
			} 

			mp_barrier();
		} else {
			if (validate) {
				if(use_gpu_buffers == 1) CUDA_CHECK(cudaMemset(commBuf, (i+1)%CHAR_MAX, buf_size));
				else memset(commBuf, (i+1)%CHAR_MAX, buf_size);
			}
	
			mp_barrier();
			mp_barrier();
		}
	} 

	CUDA_CHECK(cudaDeviceSynchronize());

	dbg_msg("after device sync, before barrier\n");
	mp_barrier();
	
	MP_CHECK(mp_window_destroy(&win));
	MP_CHECK(mp_unregister_regions(1, regs));

	if(use_gpu_buffers == 1)
		CUDA_CHECK(cudaFree(commBuf));
	else
		CUDA_CHECK(cudaFreeHost(commBuf));

	free(buf);
	free(reqs);

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
    peer = !my_rank;

	// CUDA init
	if(device_id > MP_DEFAULT)
	{
		CUDA_CHECK(cudaSetDevice(device_id));
		CUDA_CHECK(cudaFree(0));	
	}

	iter_count = ITER_COUNT_SMALL;
	window_size = WINDOW_SIZE; 

	for (size=1; size<=MAX_SIZE; size*=2) 
	{
		if (size > 1024) {
			iter_count = ITER_COUNT_LARGE;
		}

		mp_barrier();

		put_exchange(size, iter_count, window_size, validate);

		mp_barrier();
		if (!my_rank) fprintf(stdout, "test:%-20s message size:%-10d validation passed\n", "Put", size);
		//sleep(1);
		mp_barrier();

		get_exchange(size, iter_count, window_size, validate);

		mp_barrier();
		if (!my_rank) fprintf(stdout, "test:%-20s message size:%-10d validation passed\n", "Get", size);
		//sleep(1);
		mp_barrier();
	}


    mp_barrier();
    mp_finalize();

    return EXIT_SUCCESS;
}
