/****
 * Copyright (c) 2011-2016, NVIDIA Corporation.	 All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *	  * Redistributions of source code must retain the above copyright notice,
 *		this list of conditions and the following disclaimer.
 *	  * Redistributions in binary form must reproduce the above copyright
 *		notice, this list of conditions and the following disclaimer in the
 *		documentation and/or other materials provided with the distribution.
 *	  * Neither the name of the NVIDIA Corporation nor the names of its
 *		contributors may be used to endorse or promote products derived from
 *		this software without specific prior written permission.
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

#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <mpi.h>
#include <mp.h>

#include "test_utils.h"

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

int comm_size, my_rank, peer;
int use_gpu_buffers=0;

int put_exchange (MPI_Comm comm, int size, int iter_count, int window_size, int validate)
{
	int i, j;
	size_t buf_size; 

	/*application and pack buffers*/
	void *buf = NULL, *buf_d = NULL;

	/*mp specific objects*/
	mp_request_t *req = NULL;
	mp_reg_t reg; 
	mp_window_t win; 

	buf_size = size*window_size;

	/*allocating requests*/
	req = (mp_request_t *) malloc(window_size*sizeof(mp_request_t));

	buf = malloc (buf_size);
	memset(buf, 0, buf_size); 

	if(use_gpu_buffers == 1)
	{
		CUDA_CHECK(cudaMalloc((void **)&buf_d, buf_size));
		CUDA_CHECK(cudaMemset(buf_d, 0, buf_size));
	}
	else
	{
		CUDA_CHECK(cudaMallocHost((void **)&buf_d, buf_size));
		memset(buf_d, 0, buf_size);
	}

	MP_CHECK(mp_register(buf_d, buf_size, &reg));

	MP_CHECK(mp_window_create(buf_d, buf_size, &win));

	for (i = 0; i < iter_count; i++) {

		if (!my_rank) { 
			if (validate) {
				if(use_gpu_buffers == 1)
					CUDA_CHECK(cudaMemset(buf_d, (i+1)%CHAR_MAX, buf_size));
				else
					memset(buf_d, (i+1)%CHAR_MAX, buf_size);
			}
 
			for(j=0; j < window_size; j++)	
			{  
				MP_CHECK(mp_iput ((void *)((uintptr_t)buf_d + j*size), size, &reg, peer, j*size, &win, &req[j], 0)); 
			}

			for(j=0; j < window_size; j++)	
			{
				MP_CHECK(mp_wait(&req[j])); 
			}			  

			MPI_Barrier(comm);
			MPI_Barrier(comm);
		} else {  
			MPI_Barrier(comm);

			if (validate) { 
				if(use_gpu_buffers == 1)
					CUDA_CHECK(cudaMemcpy(buf, buf_d, buf_size, cudaMemcpyDefault));
				else
					memcpy(buf, buf_d, buf_size);

				char *value = buf; 
				char expected = (char) (i+1)%CHAR_MAX;
				for (j=0; j<(window_size*size); j++) { 
					if (value[j] != ((i+1)%CHAR_MAX)) { 
						fprintf(stderr, "validation check failed iter: %d index: %d expected: %d actual: %d \n", 
								i, j, expected, value[j]);
						exit(-1);
					}
				} 
			}

			MPI_Barrier(comm);
		}
	} 

	CUDA_CHECK(cudaDeviceSynchronize());

	dbg_msg("after device sync, before barrier\n");
	MPI_CHECK(MPI_Barrier(MPI_COMM_WORLD));
	
	MP_CHECK(mp_window_destroy(&win));
	mp_deregister(&reg);
	if(use_gpu_buffers == 1)
		CUDA_CHECK(cudaFree(buf_d));
	else
		CUDA_CHECK(cudaFreeHost(buf_d));

	free(buf);
	free(req);

	return 0;
}

int put_exchange_on_stream (MPI_Comm comm, int size, int iter_count, int window_size, int validate)
{
	int i, j;
	size_t buf_size; 

	/*application and pack buffers*/
	void *buf = NULL, *buf_d = NULL, *signal = NULL;

	/*mp specific objects*/
	mp_request_t *req = NULL;
	mp_request_t signal_sreq, signal_rreq;
	mp_reg_t reg, signal_reg; 
	mp_window_t win; 

	buf_size = size*window_size;

	/*allocating requests*/
	req = (mp_request_t *) malloc(window_size*sizeof(mp_request_t));
 
	/*create cuda stream*/
	cudaStream_t stream;
	CUDA_CHECK(cudaStreamCreateWithFlags(&stream, cudaStreamNonBlocking));

	signal = (void *)malloc(4096);
	memset(signal, 0, 4096);

	buf = malloc (buf_size);
	memset(buf, 0, buf_size); 

	if(use_gpu_buffers == 1)
	{
		CUDA_CHECK(cudaMalloc((void **)&buf_d, buf_size));
		CUDA_CHECK(cudaMemset(buf_d, 0, buf_size));
	}
	else
	{
		CUDA_CHECK(cudaMallocHost((void **)&buf_d, buf_size));
		memset(buf_d, 0, buf_size);
	}

	MP_CHECK(mp_register(buf_d, buf_size, &reg));
	MP_CHECK(mp_register(signal, 4096, &signal_reg));

	MP_CHECK(mp_window_create(buf_d, buf_size, &win));

	for (i = 0; i < iter_count; i++) {
		if (!my_rank) { 
			if (validate) {
				if(use_gpu_buffers == 1)
					CUDA_CHECK(cudaMemsetAsync(buf_d, (i+1)%CHAR_MAX, buf_size, stream));
				else
					memset(buf_d, (i+1)%CHAR_MAX, buf_size);
			}

			for(j=0; j < window_size; j++)	
			{  
				MP_CHECK(mp_put_prepare ((void *)((uintptr_t)buf_d + j*size), size, &reg, peer, j*size, &win, &req[j], 0)); 
			}

			/* 
			 * let's wait for 'data was checked' ack coming from the receiver side,
			 * before posting any further traffic
			 */
			if (i > 0) 
				MP_CHECK(mp_wait(&signal_rreq)); 

			MP_CHECK(mp_iput_post_all_on_stream (window_size, req, stream));
			/*just waiting on the last request as all puts are to the same target and completions are ordered*/	
			MP_CHECK(mp_wait_on_stream(&req[window_size - 1], stream));
			MP_CHECK(mp_isend_on_stream(signal, sizeof(int), peer, &signal_reg, &signal_sreq, stream));
			MP_CHECK(mp_wait_on_stream(&signal_sreq, stream));

			for(j=0; j < window_size; j++)	
			{
				dbg_msg("[%d] wait req[%d]\n", my_rank, j);
				MP_CHECK(mp_wait(&req[j])); 
			}
			dbg_msg("[%d] wait signal_sreq\n", my_rank);
			MP_CHECK(mp_wait(&signal_sreq));

			if (i < (iter_count-1)) {
				MP_CHECK(mp_irecv(signal, sizeof(int), peer, &signal_reg, &signal_rreq));
				MP_CHECK(mp_wait_on_stream(&signal_rreq, stream));
			}
		} else {  
			MP_CHECK(mp_irecv(signal, sizeof(int), peer, &signal_reg, &signal_rreq));
			MP_CHECK(mp_wait_on_stream(&signal_rreq, stream));
			dbg_msg("[%d] wait signal_rreq\n", my_rank);
			MP_CHECK(mp_wait(&signal_rreq));

			if (validate) { 

				if(use_gpu_buffers == 1)
					CUDA_CHECK(cudaMemcpyAsync(buf, buf_d, buf_size, cudaMemcpyDefault, stream));
				else
					memcpy(buf, buf_d, buf_size);

				CUDA_CHECK(cudaStreamSynchronize(stream));	
				char *value = buf; 
				char expected = (char) (i+1)%CHAR_MAX;
				for (j=0; j<(window_size*size); j++) { 
					if (value[j] != ((i+1)%CHAR_MAX)) { 
						fprintf(stderr, "validation check failed iter: %d index: %d expected: %d actual: %d \n", 
								i, j, expected, value[j]);
						exit(-1);
					}
				} 
			}
			
			if (i > 0)
				MP_CHECK(mp_wait(&signal_sreq));
			if (i < (iter_count-1))
				MP_CHECK(mp_isend_on_stream(signal, sizeof(int), peer, &signal_reg, &signal_sreq, stream));
		}
	} 

	CUDA_CHECK(cudaDeviceSynchronize());

	dbg_msg("after device sync, before barrier\n");
	MPI_CHECK(MPI_Barrier(MPI_COMM_WORLD));

	MP_CHECK(mp_window_destroy(&win));
	mp_deregister(&reg);

	CUDA_CHECK(cudaStreamDestroy(stream));

	if(use_gpu_buffers == 1)
		CUDA_CHECK(cudaFree(buf_d));
	else
		CUDA_CHECK(cudaFreeHost(buf_d));

	free(buf);
	free(req);

	return 0;
}

int put_desc_exchange_on_stream (MPI_Comm comm, int size, int iter_count, int window_size, int validate)
{
	int i, j;
	size_t buf_size; 

	/*application and pack buffers*/
	void *buf = NULL, *buf_d = NULL, *signal = NULL;

	/*mp specific objects*/
	mp_request_t *req = NULL;
	mp_request_t signal_sreq, signal_rreq;
	mp_reg_t reg, signal_reg; 
	mp_window_t win; 

	buf_size = size*window_size;

	/*allocating requests*/
	req = (mp_request_t *) malloc(window_size*sizeof(mp_request_t));
 
	/*create cuda stream*/
	cudaStream_t stream;
	CUDA_CHECK(cudaStreamCreateWithFlags(&stream, cudaStreamNonBlocking));

	signal = (void *)malloc(4096);
	memset(signal, 0, 4096);

	buf = malloc (buf_size);
	memset(buf, 0, buf_size); 

	if(use_gpu_buffers == 1)
	{
		CUDA_CHECK(cudaMalloc((void **)&buf_d, buf_size));
		CUDA_CHECK(cudaMemset(buf_d, 0, buf_size));
	}
	else
	{
		CUDA_CHECK(cudaMallocHost((void **)&buf_d, buf_size));
		memset(buf_d, 0, buf_size);
	}

	mp_desc_queue_t dq = NULL;
	MP_CHECK(mp_desc_queue_alloc(&dq));

	MP_CHECK(mp_register(buf_d, buf_size, &reg));
	MP_CHECK(mp_register(signal, 4096, &signal_reg));

	MP_CHECK(mp_window_create(buf_d, buf_size, &win));

	for (i = 0; i < iter_count; i++) {

		dbg_msg("iter i=%d\n", i);
		MPI_CHECK(MPI_Barrier(MPI_COMM_WORLD));

		if (!my_rank) { 
			if (validate) {
				dbg_msg("validate! launching cudaMemsetAsync\n");
				if(use_gpu_buffers == 1)
					CUDA_CHECK(cudaMemsetAsync(buf_d, (i+1)%CHAR_MAX, buf_size, stream));
				else
					memset(buf_d, (i+1)%CHAR_MAX, buf_size);
			}

			for(j=0; j < window_size; j++)	
			{
				dbg_msg("mp_put_prepare j=%d\n", j);
				MP_CHECK(mp_put_prepare ((void *)((uintptr_t)buf_d + j*size), size, &reg, peer, j*size, &win, &req[j], 0));
				MP_CHECK(mp_desc_queue_add_send(&dq, &req[j]));
			}

			if (i > 0) 
				MP_CHECK(mp_wait(&signal_rreq)); 

			MP_CHECK(mp_desc_queue_add_wait_send(&dq, &req[window_size - 1]));
			MP_CHECK(mp_send_prepare(signal, sizeof(int), peer, &signal_reg, &signal_sreq));
			MP_CHECK(mp_desc_queue_add_send(&dq, &signal_sreq));
			MP_CHECK(mp_desc_queue_add_wait_send(&dq, &signal_sreq));
			MP_CHECK(mp_desc_queue_post_on_stream(stream, &dq, 0));

			for(j=0; j < window_size; j++)	
			{
				dbg_msg("wait req[%d]\n", j);
				MP_CHECK(mp_wait(&req[j])); 
			}
			dbg_msg("wait signal_sreq\n");
			MP_CHECK(mp_wait(&signal_sreq));

			if (i < (iter_count-1)) {
				MP_CHECK(mp_irecv(signal, sizeof(int), peer, &signal_reg, &signal_rreq));
				MP_CHECK(mp_wait_on_stream(&signal_rreq, stream));
			}
		} else {  
			MP_CHECK(mp_irecv(signal, sizeof(int), peer, &signal_reg, &signal_rreq));
			MP_CHECK(mp_wait_on_stream(&signal_rreq, stream));
			dbg_msg("wait signal_rreq\n");
			MP_CHECK(mp_wait(&signal_rreq));

			if (validate) { 
				dbg_msg("validating RX data, issuing cudaMemcpyAsync\n");
				if(use_gpu_buffers == 1)
					CUDA_CHECK(cudaMemcpyAsync(buf, buf_d, buf_size, cudaMemcpyDefault, stream));
				else
					memcpy(buf, buf_d, buf_size);

				CUDA_CHECK(cudaStreamSynchronize(stream));	
				char *value = buf; 
				char expected = (char) (i+1)%CHAR_MAX;
				for (j=0; j<(window_size*size); j++) { 
					if (value[j] != ((i+1)%CHAR_MAX)) { 
						fprintf(stderr, "validation check failed iter: %d index: %d expected: %d actual: %d \n", 
								i, j, expected, value[j]);
						exit(-1);
					}
				} 
			}
			
			if (i > 0) {
				dbg_msg("waiting for signal_sreq\n");
				MP_CHECK(mp_wait(&signal_sreq));
			}
			if (i < (iter_count-1)) {
				dbg_msg("mp_isend_on_stream for signal_sreq\n");
				MP_CHECK(mp_isend_on_stream(signal, sizeof(int), peer, &signal_reg, &signal_sreq, stream));
			}
		}
	} 

	CUDA_CHECK(cudaDeviceSynchronize());

	dbg_msg("after device sync, before barrier\n");
	MPI_CHECK(MPI_Barrier(MPI_COMM_WORLD));

	MP_CHECK(mp_window_destroy(&win));
	mp_deregister(&reg);

	MP_CHECK(mp_desc_queue_free(&dq));

	CUDA_CHECK(cudaStreamDestroy(stream));

	if(use_gpu_buffers == 1)
		CUDA_CHECK(cudaFree(buf_d));
	else
		CUDA_CHECK(cudaFreeHost(buf_d));

	free(buf);
	free(req);

	return 0;
}

int put_desc_nowait_exchange_on_stream (MPI_Comm comm, int size, int iter_count, int window_size, int validate)
{
	int i, j;
	size_t buf_size; 

	/*application and pack buffers*/
	void *buf = NULL, *buf_d = NULL, *signal = NULL;

	/*mp specific objects*/
	mp_request_t *req = NULL;
	mp_request_t signal_sreq, signal_rreq;
	mp_reg_t reg, signal_reg; 
	mp_window_t win; 

	buf_size = size*window_size;

	/*allocating requests*/
	req = (mp_request_t *) malloc(window_size*sizeof(mp_request_t));
 
	/*create cuda stream*/
	cudaStream_t stream;
	CUDA_CHECK(cudaStreamCreateWithFlags(&stream, cudaStreamNonBlocking));

	signal = (void *)malloc(4096);
	memset(signal, 0, 4096);

	buf = malloc (buf_size);
	memset(buf, 0, buf_size); 

	if(use_gpu_buffers == 1)
	{
		CUDA_CHECK(cudaMalloc((void **)&buf_d, buf_size));
		CUDA_CHECK(cudaMemset(buf_d, 0, buf_size));
	}
	else
	{
		CUDA_CHECK(cudaMallocHost((void **)&buf_d, buf_size));
		memset(buf_d, 0, buf_size);
	}

	mp_desc_queue_t dq = NULL;
	MP_CHECK(mp_desc_queue_alloc(&dq));

	MP_CHECK(mp_register(buf_d, buf_size, &reg));
	MP_CHECK(mp_register(signal, 4096, &signal_reg));

	MP_CHECK(mp_window_create(buf_d, buf_size, &win));

	for (i = 0; i < iter_count; i++) {

		dbg_msg("iter i=%d\n", i);
		MPI_CHECK(MPI_Barrier(MPI_COMM_WORLD));

		if (!my_rank) { 
			if (validate) {
				dbg_msg("validate! launching cudaMemsetAsync\n");
				if(use_gpu_buffers == 1)
					CUDA_CHECK(cudaMemsetAsync(buf_d, (i+1)%CHAR_MAX, buf_size, stream));
				else
					memset(buf_d, (i+1)%CHAR_MAX, buf_size);
			}

			for(j=0; j < window_size; j++)	
			{
				dbg_msg("mp_put_prepare j=%d\n", j);
				MP_CHECK(mp_put_prepare ((void *)((uintptr_t)buf_d + j*size), size, &reg, peer, j*size, &win, &req[j], MP_PUT_NOWAIT));
				MP_CHECK(mp_desc_queue_add_send(&dq, &req[j]));
			}

			if (i > 0) 
				MP_CHECK(mp_wait(&signal_rreq)); 

			//MP_CHECK(mp_desc_queue_add_wait_send(&dq, &req[window_size - 1]));
			MP_CHECK(mp_send_prepare(signal, sizeof(int), peer, &signal_reg, &signal_sreq));
			MP_CHECK(mp_desc_queue_add_send(&dq, &signal_sreq));
			MP_CHECK(mp_desc_queue_add_wait_send(&dq, &signal_sreq));
			MP_CHECK(mp_desc_queue_post_on_stream(stream, &dq, 0));

			for(j=0; j < window_size; j++)	
			{
				//dbg_msg("wait req[%d]\n", j);
				//MP_CHECK(mp_wait(&req[j])); 
			}
			dbg_msg("wait signal_sreq\n");
			MP_CHECK(mp_wait(&signal_sreq));

			if (i < (iter_count-1)) {
				MP_CHECK(mp_irecv(signal, sizeof(int), peer, &signal_reg, &signal_rreq));
				MP_CHECK(mp_wait_on_stream(&signal_rreq, stream));
			}
		} else {  
			MP_CHECK(mp_irecv(signal, sizeof(int), peer, &signal_reg, &signal_rreq));
			MP_CHECK(mp_wait_on_stream(&signal_rreq, stream));
			dbg_msg("wait signal_rreq\n");
			MP_CHECK(mp_wait(&signal_rreq));

			if (validate) { 
				dbg_msg("validating RX data, issuing cudaMemcpyAsync\n");
				if(use_gpu_buffers == 1)
					CUDA_CHECK(cudaMemcpyAsync(buf, buf_d, buf_size, cudaMemcpyDefault, stream));
				else
					memcpy(buf, buf_d, buf_size);

				CUDA_CHECK(cudaStreamSynchronize(stream));	
				char *value = buf; 
				char expected = (char) (i+1)%CHAR_MAX;
				for (j=0; j<(window_size*size); j++) { 
					if (value[j] != ((i+1)%CHAR_MAX)) { 
						fprintf(stderr, "validation check failed iter: %d index: %d expected: %d actual: %d \n", 
								i, j, expected, value[j]);
						exit(-1);
					}
				} 
			}
			
			if (i > 0) {
				dbg_msg("waiting for signal_sreq\n");
				MP_CHECK(mp_wait(&signal_sreq));
			}
			if (i < (iter_count-1)) {
				dbg_msg("mp_isend_on_stream for signal_sreq\n");
				MP_CHECK(mp_isend_on_stream(signal, sizeof(int), peer, &signal_reg, &signal_sreq, stream));
			}
		}
	} 

	CUDA_CHECK(cudaDeviceSynchronize());

	dbg_msg("after device sync, before barrier\n");
	MPI_CHECK(MPI_Barrier(MPI_COMM_WORLD));

	MP_CHECK(mp_window_destroy(&win));
	mp_deregister(&reg);

	MP_CHECK(mp_desc_queue_free(&dq));

	CUDA_CHECK(cudaStreamDestroy(stream));

	if(use_gpu_buffers == 1)
		CUDA_CHECK(cudaFree(buf_d));
	else
		CUDA_CHECK(cudaFreeHost(buf_d));

	free(buf);
	free(req);

	return 0;
}

int get_exchange (MPI_Comm comm, int size, int iter_count, int window_size, int validate)
{
	int i, j;
	size_t buf_size; 

	/*application and pack buffers*/
	void *buf = NULL, *buf_d = NULL;

	/*mp specific objects*/
	mp_request_t *req = NULL;
	mp_reg_t reg; 
	mp_window_t win; 

	buf_size = size*window_size;

	/*allocating requests*/
	req = (mp_request_t *) malloc(window_size*sizeof(mp_request_t));

	buf = malloc (buf_size);
	memset(buf, 0, buf_size); 

	if(use_gpu_buffers == 1)
	{
		CUDA_CHECK(cudaMalloc((void **)&buf_d, buf_size));
		CUDA_CHECK(cudaMemset(buf_d, 0, buf_size));
	}
	else
	{
		CUDA_CHECK(cudaMallocHost((void **)&buf_d, buf_size));
		memset(buf_d, 0, buf_size);
	}

	MP_CHECK(mp_register(buf_d, buf_size, &reg));

	MP_CHECK(mp_window_create(buf_d, buf_size, &win));

	for (i = 0; i < iter_count; i++) {

		if (!my_rank) {	 
			MPI_Barrier(comm);

			for(j=0; j < window_size; j++)	
			{  
				MP_CHECK(mp_iget ((void *)((uintptr_t)buf_d + j*size), size, &reg, peer, j*size, &win, &req[j])); 
			}

			for(j=0; j < window_size; j++)	
			{
				MP_CHECK(mp_wait(&req[j])); 
			}			 

			if (validate) {
				if(use_gpu_buffers == 1)
					CUDA_CHECK(cudaMemcpy(buf, buf_d, buf_size, cudaMemcpyDefault));
				else
					memcpy(buf, buf_d, buf_size);

				char *value = buf;
				char expected = (char) (i+1)%CHAR_MAX;
				for (j=0; j<(window_size*size); j++) {
					if (value[j] != ((i+1)%CHAR_MAX)) {
						fprintf(stderr, "validation check failed iter: %d index: %d expected: %d actual: %d \n", i, j, expected, value[j]);
						exit(-1);
					}
				}
			} 

			MPI_Barrier(comm);
		} else {
			if (validate) {
				CUDA_CHECK(cudaMemset(buf_d, (i+1)%CHAR_MAX, buf_size));
			}
	
			MPI_Barrier(comm);
			MPI_Barrier(comm);
		}
	} 

	CUDA_CHECK(cudaDeviceSynchronize());

	dbg_msg("after device sync, before barrier\n");
	MPI_CHECK(MPI_Barrier(MPI_COMM_WORLD));

	MP_CHECK(mp_window_destroy(&win));
	mp_deregister(&reg);

	if(use_gpu_buffers == 1)
		CUDA_CHECK(cudaFree(buf_d));
	else
		CUDA_CHECK(cudaFreeHost(buf_d));

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

	if (gpu_init(-1)) {
		fprintf(stderr, "got error while initializing GPU\n");
		MPI_Abort(MPI_COMM_WORLD, -1);
	}

	char * value = getenv("USE_GPU_COMM_BUFFERS");
    if (value != NULL) {
        use_gpu_buffers = atoi(value);
    }

    printf("use_gpu_buffers=%d\n", use_gpu_buffers);


	peer = !my_rank;
	//Need to set CUDA_VISIBLE_DEVICES
	MP_CHECK(mp_init (MPI_COMM_WORLD, &peer, 1, MP_INIT_DEFAULT, 0));

	iter_count = ITER_COUNT_SMALL;
	window_size = WINDOW_SIZE; 

	for (size=1; size<=MAX_SIZE; size*=2) 
	{
		if (size > 1024) {
			iter_count = ITER_COUNT_LARGE;
		}

		MPI_CHECK(MPI_Barrier(MPI_COMM_WORLD));
		//sleep(1);
		//MPI_CHECK(MPI_Barrier(MPI_COMM_WORLD));

		put_exchange(MPI_COMM_WORLD, size, iter_count, window_size, validate);

		MPI_CHECK(MPI_Barrier(MPI_COMM_WORLD));
		if (!my_rank) fprintf(stdout, "test:%-20s message size:%-10d validation passed\n", "Put", size);
		//sleep(1);
		MPI_CHECK(MPI_Barrier(MPI_COMM_WORLD));

		get_exchange(MPI_COMM_WORLD, size, iter_count, window_size, validate);

		MPI_CHECK(MPI_Barrier(MPI_COMM_WORLD));
		if (!my_rank) fprintf(stdout, "test:%-20s message size:%-10d validation passed\n", "Get", size);
		//sleep(1);
		MPI_CHECK(MPI_Barrier(MPI_COMM_WORLD));

		put_exchange_on_stream(MPI_COMM_WORLD, size, iter_count, window_size, validate);

		MPI_CHECK(MPI_Barrier(MPI_COMM_WORLD));
		if (!my_rank) fprintf(stdout, "test:%-20s message size:%-10d validation passed\n", "Put-on-stream", size);
		//sleep(1);
		MPI_CHECK(MPI_Barrier(MPI_COMM_WORLD));
		
		put_desc_exchange_on_stream(MPI_COMM_WORLD, size, iter_count, window_size, validate);

		MPI_CHECK(MPI_Barrier(MPI_COMM_WORLD));
		if (!my_rank) fprintf(stdout, "test:%-20s message size:%-10d validation passed\n", "Put-desc-on-stream", size);
		//sleep(1);
		MPI_CHECK(MPI_Barrier(MPI_COMM_WORLD));

		put_desc_nowait_exchange_on_stream(MPI_COMM_WORLD, size, iter_count, window_size, validate);

		MPI_CHECK(MPI_Barrier(MPI_COMM_WORLD));
		if (!my_rank) fprintf(stdout, "test:%-20s message size:%-10d validation passed\n", "Put-desc-nowait-on-stream", size);
		//sleep(1);
		MPI_CHECK(MPI_Barrier(MPI_COMM_WORLD));
	}

	mp_finalize ();
	MPI_Barrier(MPI_COMM_WORLD);
	MPI_Finalize();

	return 0;
}
