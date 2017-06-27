#include "mp_common_examples.hpp"

#define SIZE 20
#define WINDOW_SIZE 64
#define ITER_COUNT 20

int main(int argc, char *argv[])
{
	int ret=0, i=0, j=0, myId=0, peersNum=0, libmp_version, oob_type, tl_type;
	int use_gpu_buffers=0;
	char *envVar = NULL;
	char * commBuf;
	char * hostBuf;
	int totSize=SIZE*WINDOW_SIZE;
	int device_id=MP_DEFAULT;

	mp_region_t * mp_regs_put;
	mp_request_t * mp_reqs_put;
	mp_window_t mp_win;

	//GPUDirect Async
	envVar = getenv("MP_USE_GPU");
	if (envVar != NULL) {
		device_id = atoi(envVar);
	}

	ret = mp_init(argc, argv, device_id);
	if(ret)
		exit(EXIT_FAILURE);

	mp_query_param(MP_PARAM_VERSION, &libmp_version);
	mp_query_param(MP_OOB_TYPE, &oob_type);
	mp_query_param(MP_TL_TYPE, &tl_type);
	mp_query_param(MP_MY_RANK, &myId);
	mp_query_param(MP_NUM_RANKS, &peersNum);
	if(!myId) printf("*************\nNum Peers: %d My Id: %d\nLibMP version: %x, OOB Type: %d, TL Type: %d\n*************\n", 
					peersNum, myId, libmp_version, oob_type, tl_type);
	if(peersNum != 2)
	{
		fprintf(stderr, "Only 2 peers allowed\n");
		mp_abort();
	}

	//GPUDirect RDMA
	envVar = getenv("MP_GPU_BUFFERS"); 
	if (envVar != NULL) {
		use_gpu_buffers = atoi(envVar);
	}

	printf("Rank %d, Using GPU buffers: %d\n", myId, use_gpu_buffers);

	// ===== Create mem objs
	mp_regs_put 	= mp_create_regions(1);
	mp_reqs_put 	= mp_create_request(WINDOW_SIZE);

	hostBuf = (char *) calloc(totSize, sizeof(char));

	if(use_gpu_buffers == 1)
	{
		CUDA_CHECK(cudaMalloc((void **)&commBuf, totSize));
		CUDA_CHECK(cudaMemset(commBuf, 0, totSize)); 
	}
	else
	{
		commBuf = (char *) calloc(totSize, sizeof(char));
		memset(commBuf, 0, totSize);
	}

	MP_CHECK(mp_register_region_buffer(commBuf, totSize, &mp_regs_put[0]));
	MP_CHECK(mp_window_create(commBuf, totSize, &mp_win));

	for (i = 0; i < ITER_COUNT; i++)
	{
		if (!myId) { 
 			printf("Iter %d\n", i);
			//Validate
			if(use_gpu_buffers == 1)
				CUDA_CHECK(cudaMemset(commBuf, (i+1)%CHAR_MAX, totSize));
			else
				memset(commBuf, (i+1)%CHAR_MAX, totSize);
 			
			for(j=0; j < WINDOW_SIZE; j++)	
				MP_CHECK(mp_iput((void *)((uintptr_t)commBuf + j*SIZE), SIZE, mp_regs_put, !myId, j*SIZE, &mp_win, &mp_reqs_put[j], 0)); 

			for(j=0; j < WINDOW_SIZE; j++)	
				MP_CHECK(mp_wait(&mp_reqs_put[j]));

			mp_barrier();
			mp_barrier();
		} else {  
			mp_barrier();
			//validate
			if(use_gpu_buffers == 1)
				CUDA_CHECK(cudaMemcpy(hostBuf, commBuf, totSize, cudaMemcpyDeviceToHost));
			else
				memcpy(hostBuf, commBuf, totSize);

			char expected = (char) (i+1)%CHAR_MAX;
			for (j=0; j<totSize; j++) { 
				if (hostBuf[j] != ((i+1)%CHAR_MAX)) { 
					fprintf(stderr, "validation check failed iter: %d index: %d expected: %d actual: %d \n", i, j, expected, hostBuf[j]);
					exit(-1);
				}
			} 
			
			mp_barrier();
		}
	}

	if(!myId) printf("Test passed!\n");
	// ===== Cleanup
	MP_CHECK(mp_window_destroy(&mp_win));
	MP_CHECK(mp_unregister_regions(1, mp_regs_put));

	if(use_gpu_buffers == 1)
	{
		cudaFree(commBuf);
		free(hostBuf);
	}
	else
		free(commBuf);

	free(mp_reqs_put);
	
	// ===== Finalize
	mp_finalize();

	return 0;
}