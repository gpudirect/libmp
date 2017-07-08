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

int put_exchange_async (int size, int iter_count, int window_size, int validate)
{
	int i, j;
	size_t buf_size; 

	/*application and pack buffers*/
	void *buf = NULL, *commBuf = NULL, *signal = NULL;

	/*mp specific objects*/
	mp_request_t *req;
	mp_request_t * signal_sreq, * signal_rreq;
	mp_region_t * reg, * signal_reg; 
	mp_window_t win; 

	buf_size = size*window_size;

 	/*allocating requests*/
	req = mp_create_request(window_size);
	signal_sreq = mp_create_request(1);
	signal_rreq = mp_create_request(1);

	reg = mp_create_regions(1);
	signal_reg = mp_create_regions(1);

	/*create cuda stream*/
	cudaStream_t stream;
	CUDA_CHECK(cudaStreamCreateWithFlags(&stream, cudaStreamNonBlocking));

	signal = (void *)malloc(4096);
	memset(signal, 0, 4096);

	buf = malloc (buf_size);
	memset(buf, 0, buf_size); 

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

	MP_CHECK(mp_register_region_buffer(commBuf, buf_size, &reg[0]));
	MP_CHECK(mp_register_region_buffer(signal, 4096, &signal_reg[0]));

	MP_CHECK(mp_window_create(commBuf, buf_size, &win));

	for (i = 0; i < iter_count; i++) {
		if (!my_rank) { 
			if (validate) {
				if(use_gpu_buffers == 1)
					CUDA_CHECK(cudaMemsetAsync(commBuf, (i+1)%CHAR_MAX, buf_size, stream));
				else
					memset(commBuf, (i+1)%CHAR_MAX, buf_size);
			}

			for(j=0; j < window_size; j++)	
			{  
				MP_CHECK(mp_put_prepare ((void *)((uintptr_t)commBuf + j*size), size, &reg[0], peer, j*size, &win, &req[j], 0)); 
			}

			/* 
			 * let's wait for 'data was checked' ack coming from the receiver side,
			 * before posting any further traffic
			 */
			if (i > 0) 
				MP_CHECK(mp_wait(&signal_rreq[0])); 

			MP_CHECK(mp_iput_post_all_async (window_size, req, stream));
			/*just waiting on the last request as all puts are to the same target and completions are ordered*/	
			MP_CHECK(mp_wait_async(&req[window_size - 1], stream));
			MP_CHECK(mp_isend_async(signal, sizeof(int), peer, &signal_reg[0], &signal_sreq[0], stream));
			MP_CHECK(mp_wait_async(&signal_sreq[0], stream));

			for(j=0; j < window_size; j++)	
			{
				dbg_msg("[%d] wait iput async req[%d]\n", my_rank, j);
				MP_CHECK(mp_wait(&req[j])); 
				dbg_msg("[%d] wait iput async req[%d] done\n", my_rank, j);
			}
			dbg_msg("[%d] wait signal_sreq\n", my_rank);
			MP_CHECK(mp_wait(&signal_sreq[0]));
			dbg_msg("[%d] wait signal_sreq done\n", my_rank);

			if (i < (iter_count-1)) {
				dbg_msg("[%d] mp_irecv signal_rreq\n", my_rank);
				MP_CHECK(mp_irecv(signal, sizeof(int), peer, &signal_reg[0], &signal_rreq[0]));
				dbg_msg("[%d] mp_wait_async signal_rreq\n", my_rank);
				MP_CHECK(mp_wait_async(&signal_rreq[0], stream));
			}
		} else {  
			MP_CHECK(mp_irecv(signal, sizeof(int), peer, &signal_reg[0], &signal_rreq[0]));
			MP_CHECK(mp_wait_async(&signal_rreq[0], stream));
			dbg_msg("[%d] wait signal_rreq\n", my_rank);
			MP_CHECK(mp_wait(&signal_rreq[0]));

			if (validate) { 

				if(use_gpu_buffers == 1)
					CUDA_CHECK(cudaMemcpyAsync(buf, commBuf, buf_size, cudaMemcpyDefault, stream));
				else
					memcpy(buf, commBuf, buf_size);

				CUDA_CHECK(cudaStreamSynchronize(stream));	
				char *value = (char*)buf; 
				char expected = (char) (i+1)%CHAR_MAX;
				for (j=0; j<(window_size*size); j++) { 
					if (value[j] != ((i+1)%CHAR_MAX)) { 
						fprintf(stderr, "validation check failed iter: %d index: %d expected: %d actual: %d \n", i, j, expected, value[j]);
						exit(EXIT_FAILURE);
					}
				} 
			}
			
			dbg_msg("[%d] received signal_rreq\n", my_rank);
			if (i > 0)
				MP_CHECK(mp_wait(&signal_sreq[0]));
			if (i < (iter_count-1))
				MP_CHECK(mp_isend_async(signal, sizeof(int), peer, &signal_reg[0], &signal_sreq[0], stream));
		}
	} 

	CUDA_CHECK(cudaDeviceSynchronize());

	dbg_msg("after device sync, before barrier\n");
	mp_barrier();

	MP_CHECK(mp_window_destroy(&win));
	MP_CHECK(mp_unregister_regions(1, reg));
	MP_CHECK(mp_unregister_regions(1, signal_reg));

	CUDA_CHECK(cudaStreamDestroy(stream));

	if(use_gpu_buffers == 1)
		CUDA_CHECK(cudaFree(commBuf));
	else
		CUDA_CHECK(cudaFreeHost(commBuf));

	free(buf);
	free(req);

	return 0;
}

int put_desc_exchange_async (int size, int iter_count, int window_size, int validate)
{
	int i, j;
	size_t buf_size; 

	/*application and pack buffers*/
	void *buf = NULL, *commBuf = NULL, *signal = NULL;

	/*mp specific objects*/
	mp_request_t *req;
	mp_request_t * signal_sreq, * signal_rreq;
	mp_region_t * reg, * signal_reg; 
	mp_window_t win; 

	buf_size = size*window_size;

	/*allocating requests*/
	req = mp_create_request(window_size);
	signal_sreq = mp_create_request(1);
	signal_rreq = mp_create_request(1);

	reg = mp_create_regions(1);
	signal_reg = mp_create_regions(1);
 
	/*create cuda stream*/
	cudaStream_t stream;
	CUDA_CHECK(cudaStreamCreateWithFlags(&stream, cudaStreamNonBlocking));

	signal = (void *)malloc(4096);
	memset(signal, 0, 4096);

	buf = malloc (buf_size);
	memset(buf, 0, buf_size); 

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

	mp_comm_descriptors_queue_t dq = NULL;
	MP_CHECK(mp_comm_descriptors_queue_alloc(&dq));

	MP_CHECK(mp_register_region_buffer(commBuf, buf_size, &reg[0]));
	MP_CHECK(mp_register_region_buffer(signal, 4096, &signal_reg[0]));

	MP_CHECK(mp_window_create(commBuf, buf_size, &win));

	for (i = 0; i < iter_count; i++) {

		dbg_msg("iter i=%d\n", i);
		mp_barrier();

		if (!my_rank) { 
			if (validate) {
				dbg_msg("validate! launching cudaMemsetAsync\n");
				if(use_gpu_buffers == 1)
					CUDA_CHECK(cudaMemsetAsync(commBuf, (i+1)%CHAR_MAX, buf_size, stream));
				else
					memset(commBuf, (i+1)%CHAR_MAX, buf_size);
			}

			for(j=0; j < window_size; j++)	
			{
				dbg_msg("mp_put_prepare j=%d\n", j);
				MP_CHECK(mp_put_prepare ((void *)((uintptr_t)commBuf + j*size), size, &reg[0], peer, j*size, &win, &req[j], 0));
				MP_CHECK(mp_comm_descriptors_queue_add_send(&dq, &req[j]));
			}

			if (i > 0) 
				MP_CHECK(mp_wait(&signal_rreq[0])); 

			MP_CHECK(mp_comm_descriptors_queue_add_wait_send(&dq, &req[window_size - 1]));
			MP_CHECK(mp_send_prepare(signal, sizeof(int), peer, &signal_reg[0], &signal_sreq[0]));
			dbg_msg("mp_send_prepare\n");

			MP_CHECK(mp_comm_descriptors_queue_add_send(&dq, &signal_sreq[0]));
			dbg_msg("mp_comm_descriptors_queue_add_send\n");

			MP_CHECK(mp_comm_descriptors_queue_add_wait_send(&dq, &signal_sreq[0]));
			dbg_msg("mp_comm_descriptors_queue_add_wait_send\n");

			MP_CHECK(mp_comm_descriptors_queue_post_async(stream, &dq, 0));
			dbg_msg("mp_comm_descriptors_queue_post_async\n");

			for(j=0; j < window_size; j++)	
			{
				dbg_msg("wait req[%d]\n", j);
				MP_CHECK(mp_wait(&req[j])); 
				dbg_msg("wait req[%d] done\n", j);
			}

			dbg_msg("wait signal_sreq[0]\n");
			MP_CHECK(mp_wait(&signal_sreq[0]));
			dbg_msg("wait signal_sreq[0] done\n");

			if (i < (iter_count-1)) {
				MP_CHECK(mp_irecv(signal, sizeof(int), peer, &signal_reg[0], &signal_rreq[0]));
				MP_CHECK(mp_wait_async(&signal_rreq[0], stream));
			}
		} else {  
			MP_CHECK(mp_irecv(signal, sizeof(int), peer, &signal_reg[0], &signal_rreq[0]));
			MP_CHECK(mp_wait_async(&signal_rreq[0], stream));
			dbg_msg("wait signal_rreq\n");
			MP_CHECK(mp_wait(&signal_rreq[0]));
			dbg_msg("wait signal_rreq done\n");
			if (validate) { 
				dbg_msg("validating RX data, issuing cudaMemcpyAsync\n");
				if(use_gpu_buffers == 1)
					CUDA_CHECK(cudaMemcpyAsync(buf, commBuf, buf_size, cudaMemcpyDefault, stream));
				else
					memcpy(buf, commBuf, buf_size);

				CUDA_CHECK(cudaStreamSynchronize(stream));	
				char *value = (char*)buf; 
				char expected = (char) (i+1)%CHAR_MAX;
				for (j=0; j<(window_size*size); j++) { 
					if (value[j] != ((i+1)%CHAR_MAX)) { 
						fprintf(stderr, "validation check failed iter: %d index: %d expected: %d actual: %d \n", 
								i, j, expected, value[j]);
						exit(EXIT_FAILURE);
					}
				} 
			}
			
			if (i > 0) {
				dbg_msg("mp_wait for signal_sreq[0] %d \n", i);
				MP_CHECK(mp_wait(&signal_sreq[0]));
				dbg_msg("mp_wait for signal_sreq[0] %d done \n", i);
			}
			if (i < (iter_count-1)) {
				dbg_msg("mp_isend_async for signal_sreq[0] %d \n", i);
				MP_CHECK(mp_isend_async(signal, sizeof(int), peer, &signal_reg[0], &signal_sreq[0], stream));
				dbg_msg("mp_isend_async for signal_sreq[0] %d done \n", i);
			}
		}
	} 

	CUDA_CHECK(cudaDeviceSynchronize());

	dbg_msg("after device sync, before barrier\n");
	mp_barrier();

	MP_CHECK(mp_window_destroy(&win));
	MP_CHECK(mp_unregister_regions(1, reg));
	MP_CHECK(mp_unregister_regions(1, signal_reg));
	MP_CHECK(mp_comm_descriptors_queue_free(&dq));

	CUDA_CHECK(cudaStreamDestroy(stream));

	if(use_gpu_buffers == 1)
		CUDA_CHECK(cudaFree(commBuf));
	else
		CUDA_CHECK(cudaFreeHost(commBuf));

	free(buf);
	free(req);

	return 0;
}

int put_desc_nowait_exchange_async (int size, int iter_count, int window_size, int validate)
{
	int i, j;
	size_t buf_size; 

	/*application and pack buffers*/
	void *buf = NULL, *commBuf = NULL, *signal = NULL;

	/*mp specific objects*/
	mp_request_t *req;
	mp_request_t * signal_sreq, * signal_rreq;
	mp_region_t * reg, * signal_reg; 
	mp_window_t win; 

	buf_size = size*window_size;

	/*allocating requests*/
	req = mp_create_request(window_size);
	signal_sreq = mp_create_request(1);
	signal_rreq = mp_create_request(1);

	reg = mp_create_regions(1);
	signal_reg = mp_create_regions(1);
 
	/*create cuda stream*/
	cudaStream_t stream;
	CUDA_CHECK(cudaStreamCreateWithFlags(&stream, cudaStreamNonBlocking));

	signal = (void *)malloc(4096);
	memset(signal, 0, 4096);

	buf = malloc (buf_size);
	memset(buf, 0, buf_size); 

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

	mp_comm_descriptors_queue_t dq = NULL;
	MP_CHECK(mp_comm_descriptors_queue_alloc(&dq));

	MP_CHECK(mp_register_region_buffer(commBuf, buf_size, &reg[0]));
	MP_CHECK(mp_register_region_buffer(signal, 4096, &signal_reg[0]));

	MP_CHECK(mp_window_create(commBuf, buf_size, &win));

	for (i = 0; i < iter_count; i++) {

		dbg_msg("iter i=%d\n", i);
		mp_barrier();

		if (!my_rank) { 
			if (validate) {
				dbg_msg("validate! launching cudaMemsetAsync\n");
				if(use_gpu_buffers == 1)
					CUDA_CHECK(cudaMemsetAsync(commBuf, (i+1)%CHAR_MAX, buf_size, stream));
				else
					memset(commBuf, (i+1)%CHAR_MAX, buf_size);
			}

			for(j=0; j < window_size; j++)	
			{
				dbg_msg("mp_put_prepare j=%d\n", j);
				MP_CHECK(mp_put_prepare ((void *)((uintptr_t)commBuf + j*size), size, &reg[0], peer, j*size, &win, &req[j], MP_PUT_NOWAIT));
				MP_CHECK(mp_comm_descriptors_queue_add_send(&dq, &req[j]));
			}

			if (i > 0) 
				MP_CHECK(mp_wait(&signal_rreq[0])); 

			//MP_CHECK(mp_comm_descriptors_queue_add_wait_send(&dq, &req[window_size - 1]));
			MP_CHECK(mp_send_prepare(signal, sizeof(int), peer, &signal_reg[0], &signal_sreq[0]));
			MP_CHECK(mp_comm_descriptors_queue_add_send(&dq, &signal_sreq[0]));
			MP_CHECK(mp_comm_descriptors_queue_add_wait_send(&dq, &signal_sreq[0]));
			MP_CHECK(mp_comm_descriptors_queue_post_async(stream, &dq, 0));

			for(j=0; j < window_size; j++)	
			{
				//dbg_msg("wait req[%d]\n", j);
				//MP_CHECK(mp_wait(&req[j])); 
			}
			dbg_msg("wait signal_sreq[0]\n");
			MP_CHECK(mp_wait(&signal_sreq[0]));

			if (i < (iter_count-1)) {
				MP_CHECK(mp_irecv(signal, sizeof(int), peer, &signal_reg[0], &signal_rreq[0]));
				MP_CHECK(mp_wait_async(&signal_rreq[0], stream));
			}
		} else {  
			MP_CHECK(mp_irecv(signal, sizeof(int), peer, &signal_reg[0], &signal_rreq[0]));
			MP_CHECK(mp_wait_async(&signal_rreq[0], stream));
			dbg_msg("wait signal_rreq[0]\n");
			MP_CHECK(mp_wait(&signal_rreq[0]));

			if (validate) { 
				dbg_msg("validating RX data, issuing cudaMemcpyAsync\n");
				if(use_gpu_buffers == 1)
					CUDA_CHECK(cudaMemcpyAsync(buf, commBuf, buf_size, cudaMemcpyDefault, stream));
				else
					memcpy(buf, commBuf, buf_size);

				CUDA_CHECK(cudaStreamSynchronize(stream));	
				char *value = (char*)buf; 
				char expected = (char) (i+1)%CHAR_MAX;
				for (j=0; j<(window_size*size); j++) { 
					if (value[j] != ((i+1)%CHAR_MAX)) { 
						fprintf(stderr, "validation check failed iter: %d index: %d expected: %d actual: %d \n",  i, j, expected, value[j]);
						exit(EXIT_FAILURE);
					}
				} 
			}
			
			if (i > 0) {
				dbg_msg("waiting for signal_sreq[0]\n");
				MP_CHECK(mp_wait(&signal_sreq[0]));
			}
			if (i < (iter_count-1)) {
				dbg_msg("mp_isend_on_stream for signal_sreq[0]\n");
				MP_CHECK(mp_isend_async(signal, sizeof(int), peer, &signal_reg[0], &signal_sreq[0], stream));
			}
		}
	} 

	CUDA_CHECK(cudaDeviceSynchronize());

	dbg_msg("after device sync, before barrier\n");
	mp_barrier();

	MP_CHECK(mp_window_destroy(&win));
	MP_CHECK(mp_unregister_regions(1, reg));
	MP_CHECK(mp_unregister_regions(1, signal_reg));
	MP_CHECK(mp_comm_descriptors_queue_free(&dq));

	CUDA_CHECK(cudaStreamDestroy(stream));

	if(use_gpu_buffers == 1)
		CUDA_CHECK(cudaFree(commBuf));
	else
		CUDA_CHECK(cudaFreeHost(commBuf));

	free(buf);
	free(req);

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
    CUDA_CHECK(cudaSetDevice(device_id));
    CUDA_CHECK(cudaFree(0));


	iter_count = ITER_COUNT_SMALL;
	window_size = WINDOW_SIZE; 

	for (size=1; size<=MAX_SIZE; size*=2) 
	{
		if (size > 1024) {
			iter_count = ITER_COUNT_LARGE;
		}

		//sleep(1);
		mp_barrier();

		put_exchange_async(size, iter_count, window_size, validate);

		mp_barrier();
		if (!my_rank) fprintf(stdout, "test:%-20s message size:%-10d validation passed\n", "Put-on-stream", size);
		//sleep(1);
		mp_barrier();
		
		put_desc_exchange_async(size, iter_count, window_size, validate);

		mp_barrier();
		if (!my_rank) fprintf(stdout, "test:%-20s message size:%-10d validation passed\n", "Put-desc-on-stream", size);
		//sleep(1);
		mp_barrier();

		put_desc_nowait_exchange_async(size, iter_count, window_size, validate);

		mp_barrier();
		if (!my_rank) fprintf(stdout, "test:%-20s message size:%-10d validation passed\n", "Put-desc-nowait-on-stream", size);
		//sleep(1);
		mp_barrier();
	}


    mp_barrier();
    mp_finalize();

    return EXIT_SUCCESS;
}
