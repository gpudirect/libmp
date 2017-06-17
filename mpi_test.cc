#include "common.h"
#include "oob.h"
#include "tl.h"

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
	
	size_t rsize=10;
	char * rBuf = (char *) calloc (rsize, sizeof(char));
	mp_key_t * mp_key;

	mp_key = tl_comm->create_keys(1);
	printf("create_requests set\n");

	tl_comm->register_buffer(rBuf, rsize, &mp_key[0]);
	printf("register_buffer set\n");

	mp_request_t * reqs;
	reqs = tl_comm->create_requests(1);
	printf("create_requests set\n");

	tl_comm->receive(rBuf, rsize, !myId, reqs, &mp_key[0]);
	printf("receive set\n");

	tl_comm->cleanupInit();
	printf("cleanupInit set\n");
	
	tl_comm->finalize();
	printf("finalize set\n");
	
	oob_comm->finalize();

	return 0;
}