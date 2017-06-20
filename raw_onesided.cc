#include "common.h"
#include "oob.h"
#include "tl.h"

#define SIZE 20
#define WINDOW_SIZE 64
#define ITER_COUNT 20

int main(int argc, char *argv[])
{
	int ret=0, i=0, j=0, myId=0, oobSize=0;
	int use_gpu_buffers=0;
	char *envVar = NULL;
	char * commBuf;
	char * hostBuf;
	int totSize=SIZE*WINDOW_SIZE;

	mp_key_t * mp_keys_put;
	mp_request_t * mp_reqs_put;
	mp_window_t mp_win;
	OOB::Communicator * oob_comm;

	envVar = getenv("MP_GPU_BUFFERS"); 
	if (envVar != NULL) {
		use_gpu_buffers = atoi(envVar);
	}

	printf("Using GPU buffers: %d\n", use_gpu_buffers);
#ifndef HAVE_CUDA
	if(use_gpu_buffers == 1)
	{
		fprintf(stderr, "ERROR: use_gpu_buffers set to 1 but HAVE_CUDA not configure\n");
		exit(EXIT_FAILURE);
	}
#endif

	oob_comm = getBestOOB();
	ret = oob_comm->init(argc, argv);
	if(ret)
	{
		fprintf(stderr, "OOB Init error %d\n", ret);
		exit(-1);
	}

	myId = oob_comm->getMyId();
	oobSize = oob_comm->getSize();
	if(!myId) printf("OOB Comm Size: %d My Id: %d\n", oobSize, myId);

#ifdef HAVE_VERBS
	TL::Communicator * tl_comm = getTLObj(TL_INDEX_VERBS);
#else
	TL::Communicator * tl_comm = getTLObj(TL_INDEX_PSM);
#endif

	// ===== Init Transport Layer
	if(!myId) printf("\n==== Init Transport Layer Obj ====\n");
	tl_comm->setupOOB(oob_comm);
	tl_comm->setupNetworkDevices();
	tl_comm->createEndpoints();
	tl_comm->exchangeEndpoints();
	tl_comm->updateEndpoints();
	tl_comm->cleanupInit();

	// ===== Create mem objs
	if(!myId) printf("\n==== Create mem objs ====\n");
	mp_keys_put 	= tl_comm->create_keys(1);
	mp_reqs_put 	= tl_comm->create_requests(WINDOW_SIZE);

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

	tl_comm->register_key_buffer(commBuf, totSize, &mp_keys_put[0]);
	tl_comm->onesided_window_create(commBuf, totSize, &mp_win);

	for (i = 0; i < ITER_COUNT; i++)
	{
		if (!myId) { 
			//Validate
			if(use_gpu_buffers == 1)
				CUDA_CHECK(cudaMemset(commBuf, (i+1)%CHAR_MAX, totSize));
			else
				memset(commBuf, (i+1)%CHAR_MAX, totSize);
 
			for(j=0; j < WINDOW_SIZE; j++)	
			{  
				tl_comm->onesided_nb_put((void *)((uintptr_t)commBuf + j*SIZE), SIZE, mp_keys_put, !myId, j*SIZE, &mp_win, &mp_reqs_put[j], 0); 
			}

			for(j=0; j < WINDOW_SIZE; j++)	
			{
				tl_comm->wait(&mp_reqs_put[j]); 
			}			  

			oob_comm->barrier();
			oob_comm->barrier();
		} else {  
			oob_comm->barrier();
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
			
			oob_comm->barrier();
		}
	}

	if(!myId) printf("Test passed!\n");
	// ===== Cleanup
	tl_comm->onesided_window_destroy(&mp_win);
	tl_comm->unregister_key(mp_keys_put);

	if(use_gpu_buffers == 1)
	{
		cudaFree(commBuf);
		free(hostBuf);
	}
	else
		free(commBuf);

	free(mp_reqs_put);
	
	// ===== Finalize
	tl_comm->finalize();
	oob_comm->finalize();

	return 0;
}