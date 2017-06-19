#include "common.h"
#include "oob.h"
#include "tl.h"

#define TEST_RECV_REQ 0
#define TEST_SEND_REQ 1
#define BUF_SIZE	20

int main(int argc, char *argv[])
{
	int ret=0, i=0, myId=0, oobSize=0;
	int use_gpu_buffers=0;
	char *envVar = NULL;
	char ** sBuf, ** rBuf;
	char ** hostBuf;
	
	mp_key_t * mp_keys_recv, * mp_keys_send;
	mp_request_t * mp_reqs_recv, * mp_reqs_send;
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
	rBuf 			= (char ** ) calloc(oobSize, sizeof(char *));
	sBuf 			= (char ** ) calloc(oobSize, sizeof(char *));
	hostBuf 		= (char ** ) calloc(oobSize, sizeof(char *));
	mp_keys_recv 	= tl_comm->create_keys(oobSize);
	mp_keys_send 	= tl_comm->create_keys(oobSize);
	mp_reqs_recv 	= tl_comm->create_requests(oobSize);
	mp_reqs_send 	= tl_comm->create_requests(oobSize);

	for(i=0; i<oobSize; i++) {
		if(i != myId)
		{
			if(use_gpu_buffers == 1)
			{
				CUDA_CHECK(cudaMalloc((void **)&rBuf[i], BUF_SIZE));
				CUDA_CHECK(cudaMemset(rBuf[i], 0, BUF_SIZE)); 

				CUDA_CHECK(cudaMalloc((void **)&sBuf[i], BUF_SIZE));
				CUDA_CHECK(cudaMemset(sBuf[i], ('a'+myId), BUF_SIZE)); 

				hostBuf[i] = (char *) calloc(BUF_SIZE, sizeof(char));
			}
			else
			{
				rBuf[i] = (char *) calloc(BUF_SIZE, sizeof(char));
				memset(rBuf[i], 0, BUF_SIZE);
				sBuf[i] = (char *) calloc(BUF_SIZE, sizeof(char));
				memset(sBuf[i], ('a'+myId), BUF_SIZE);
			}

			tl_comm->register_key_buffer(rBuf[i], BUF_SIZE, &mp_keys_recv[i]);
			tl_comm->pt2pt_nb_receive(rBuf[i], BUF_SIZE, i, &mp_reqs_recv[i], &mp_keys_recv[i]);
			if(!myId) printf("[%d] Recv Client %d, request=%p\n", myId, i, &mp_reqs_recv[i]);

			tl_comm->register_key_buffer(sBuf[i], BUF_SIZE, &mp_keys_send[i]);
		}	
	}
	
	//Ensure all recvs have been posted
	oob_comm->sync();

	if(!myId)
		printf("\n==== Send Msg ====\n");

	for(i=0; i<oobSize; i++) {
		if(i != myId)
		{
			tl_comm->pt2pt_nb_send(sBuf[i], BUF_SIZE, i, &mp_reqs_send[i], &mp_keys_send[i]);
			if(!myId) printf("[%d] Send Client %d, request=%p\n", myId, i, &mp_reqs_send[i]);
		}
	}
	
	if(!myId) printf("\n==== Wait ====\n");

	//wait recv
	for(i=0; i<oobSize; i++) {
		if(i != myId)
		{
			tl_comm->wait(&mp_reqs_recv[i]);
			if(!myId) printf("[%d] Wait for Recv %d, req=%p\n", myId, i, &mp_reqs_recv[i]);
		}
	}

	for(i=0; i<oobSize; i++) {
		if(i != myId)
		{
			if(use_gpu_buffers == 1)
			{
				CUDA_CHECK(cudaMemcpy(hostBuf[i], rBuf[i], BUF_SIZE, cudaMemcpyDeviceToHost));
				printf("[%d] Received from [%d]: %s\n", myId, i, hostBuf[i]);
			}
			else
				printf("[%d] Received from [%d]: %s\n", myId, i, rBuf[i]);
		}
	}

	//wait send
	for(i=0; i<oobSize; i++) {
		if(i != myId)
		{
			tl_comm->wait(&mp_reqs_send[i]);
			if(!myId) printf("[%d] Wait for Send %d, req=%p\n", myId, i, &mp_reqs_send[i]);
		}
	}

	// ===== Cleanup
	for(i=0; i<oobSize; i++)
	{
		if(i != myId)
		{
			tl_comm->unregister_key(&mp_keys_recv[i]);
			tl_comm->unregister_key(&mp_keys_send[i]);			

			if(use_gpu_buffers == 1)
			{
				cudaFree(rBuf[i]);
				cudaFree(sBuf[i]);
			}
			else
			{				
				free(rBuf[i]);
				free(sBuf[i]);
			}

		}
	}

	free(rBuf);
	free(sBuf);
	free(mp_keys_recv);
	free(mp_keys_send);
	free(mp_reqs_recv);
	free(mp_reqs_send);
	
	// ===== Finalize
	tl_comm->finalize();
	oob_comm->finalize();

	return 0;
}