#include "common.h"
#include "oob.h"
#include "tl.h"

int main(int argc, char *argv[])
{
	int ret=0;
	OOB::Communicator * oob_comm = getBestOOB();

	ret = oob_comm->init(argc, argv);

	printf("Comm Size: %d\n", oob_comm->getSize());
	printf("My Rank: %d\n", oob_comm->getMyId());


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
	
	tl_comm->cleanupInit();
	printf("cleanupInit set\n");
	
	tl_comm->finalize();
	printf("finalize set\n");
	
	oob_comm->finalize();

	return 0;
}