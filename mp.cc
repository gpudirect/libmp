#include "common.h"
#include "oob.h"
#include "tl.h"

int oob_size, oob_rank;

int mp_init() {

	// ====== OOB INIT
	OOB::Communicator * oob_comm;
	oob_comm = getBestOOB();
	ret = oob_comm->init(argc, argv);
	if(ret)
	{
		fprintf(stderr, "OOB Init error %d\n", ret);
		return MP_ERROR;
	}

	oob_rank = oob_comm->getMyId();
	oob_size = oob_comm->getSize();
	if(!oob_rank) printf("OOB Comm Size: %d My OOB Id: %d\n", oob_size, oob_rank);

	// ====== TL INIT
#ifdef HAVE_VERBS
	TL::Communicator * tl_comm = getTLObj(TL_INDEX_VERBS);
#else
	TL::Communicator * tl_comm = getTLObj(TL_INDEX_PSM);
#endif

	if(!oob_rank) printf("\nLibMP Init Transport Layer Obj\n");
	tl_comm->setupOOB(oob_comm);
	tl_comm->setupNetworkDevices();
	tl_comm->createEndpoints();
	tl_comm->exchangeEndpoints();
	tl_comm->updateEndpoints();
	tl_comm->cleanupInit();

#ifndef HAVE_CUDA
	fprintf(stderr, "WARNING: GPUDirect RDMA requires HAVE_CUDA configure flag\n", );
#endif

	return MP_SUCCESS;
}

//===== MEMORY OPs
mp_request_t * mp_create_request(int number) {
	return tl_comm->create_requests(number);
}

mp_key_t * mp_create_keys(int number) {
	return tl_comm->create_keys(number);
}

int mp_register_key_buffer(void * addr, size_t length, mp_key_t * mp_key) {
	return tl_comm->register_key_buffer(addr, length, mp_key)
}

//===== PT2PT
int mp_nb_recv(void * buf, size_t size, int peer, mp_request_t * mp_req, mp_key_t * mp_key) {
	if(!mp_req || !mp_key)
		return MP_ERROR;
	if(peer > oob_size)
	{
		mp_err_msg("Communication peer: %d, Tot num of peers: %d\n", peer, oob_size);
		return MP_ERROR;
	}

	return tl_comm->pt2pt_nb_receive(buf, size, peer, mp_req, mp_key);
}

int mp_nb_send(void * buf, size_t size, int peer, mp_request_t * mp_req, mp_key_t * mp_key) {
	if(!mp_req || !mp_key)
		return MP_ERROR;

	if(peer > oob_size)
	{
		mp_err_msg("Communication peer: %d, Tot num of peers: %d\n", peer, oob_size);
		return MP_ERROR;
	}

	return tl_comm->pt2pt_nb_send(buf, size, peer, mp_req, mp_key);
}


//===== ONE-SIDED
int mp_nb_put(void *buf, int size, mp_key_t * mp_key, int peer, size_t displ, mp_window_t * mp_win, mp_request_t * mp_req, int flags) {
	if(!mp_req || !mp_key)
		return MP_ERROR;

	if(peer > oob_size)
	{
		mp_err_msg("Communication peer: %d, Tot num of peers: %d\n", peer, oob_size);
		return MP_ERROR;
	}

	if(flags != 0 && !(flags & MP_PUT_NOWAIT) && !(flags & MP_SEND_INLINE))
	{
		mp_err_msg("Wrong input flags %x\n", flags);
		return MP_ERROR;
	}

	return tl_comm->onesided_nb_put(buf, size, mp_key, peer, displ, mp_win, mp_req, flags); 
}


int mp_nb_get(void *buf, int size, mp_key_t * mp_key, int peer, size_t displ, mp_window_t * mp_win, mp_request_t * mp_req, int flags) {
	if(!mp_req || !mp_key)
		return MP_ERROR;

	if(peer > oob_size)
	{
		mp_err_msg("Communication peer: %d, Tot num of peers: %d\n", peer, oob_size);
		return MP_ERROR;
	}

	if(flags != 0)
	{
		mp_warn_msg("Wrong input flags %x. Using flags = 0\n", flags);
		flags=0;
	}

	return tl_comm->onesided_nb_get(buf, size, mp_key, peer, displ, mp_win, mp_req, flags); 
}


//===== WAIT
int mp_wait_word(uint32_t *ptr, uint32_t value, int flags) {
	if(!ptr)
	{
		mp_err_msg("Input ptr NULL\n");
		return MP_ERROR;
	}

	return tl_comm->onesided_wait_word(ptr, value, flags);
}

int mp_wait(mp_request_t * mp_req) {
	if(!mp_req)
	{
		mp_err_msg("Input request NULL\n");
		return MP_ERROR;
	}

	return mp_wait_all(1, mp_req);
}

int mp_wait_all(int number, mp_request_t * mp_reqs) {
	if(!mp_reqs)
	{
		mp_err_msg("Input request NULL\n");
		return MP_ERROR;
	}

	if(number <= 0)
	{
		mp_err_msg("Erroneous number of requests (%d)\n", number);
		return MP_ERROR;
	}	
	
	return tl_comm->wait_all(number, mp_req);
}


//===== OTHERS
int mp_barrier(){
	oob_comm->barrier();
}
