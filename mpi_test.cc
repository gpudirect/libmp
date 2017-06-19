#include "common.h"
#include "oob.h"
#include "tl.h"

#define TEST_RECV_REQ 0
#define TEST_SEND_REQ 1

int main(int argc, char *argv[])
{
	int ret=0;
	int myId=0;
	OOB::Communicator * oob_comm = getBestOOB();

	ret = oob_comm->init(argc, argv);

	printf("Comm Size: %d\n", oob_comm->getSize());
	printf("My Rank: %d\n", oob_comm->getMyId());
	myId = oob_comm->getMyId();

#ifdef HAVE_VERBS
	TL::Communicator * tl_comm = getTLObj(TL_INDEX_VERBS);
#else
	TL::Communicator * tl_comm = getTLObj(TL_INDEX_PSM);
#endif

	tl_comm->setupOOB(oob_comm);
	printf("OOB set\n");

	tl_comm->setupNetworkDevices();
	printf("setupNetworkDevices set\n");
	
	tl_comm->createEndpoints();
	printf("createEndpoints set\n");
	
	tl_comm->exchangeEndpoints();
	printf("exchangeEndpoints set\n");

	tl_comm->updateEndpoints();
	printf("updateEndpoints set\n");
	
	size_t bufSize=10;
	char * rBuf = (char *) calloc (bufSize, sizeof(char));
	memset(rBuf, 0, bufSize);
	
	char * sBuf = (char *) calloc (bufSize, sizeof(char));
	if(!myId)	memset(sBuf, 'a', bufSize);
	else	memset(sBuf, 'b', bufSize);

	mp_key_t * mp_key;

	mp_key = tl_comm->create_keys(2);
	printf("create_requests set\n");

	tl_comm->register_buffer(rBuf, bufSize, &mp_key[TEST_RECV_REQ]);
	printf("register_buffer recv\n");

	tl_comm->register_buffer(sBuf, bufSize, &mp_key[TEST_SEND_REQ]);
	printf("register_buffer send\n");

	mp_request_t * reqs;
	reqs = tl_comm->create_requests(2);
	printf("create_requests set\n");

	tl_comm->receive(rBuf, bufSize, !myId, &reqs[TEST_RECV_REQ], &mp_key[TEST_RECV_REQ]);
	printf("receive set\n");

	tl_comm->send(sBuf, bufSize, !myId, &reqs[TEST_SEND_REQ], &mp_key[TEST_SEND_REQ]);
	printf("receive set\n");

	//wait recv
	tl_comm->wait(&reqs[TEST_RECV_REQ]);
	//wait send
	tl_comm->wait(&reqs[TEST_SEND_REQ]);

	printf("myRank: %d, received buffer: %s\n", myId, rBuf);

	tl_comm->cleanupInit();
	printf("cleanupInit set\n");
	
	tl_comm->finalize();
	printf("finalize set\n");
	
	oob_comm->finalize();

	return 0;
}