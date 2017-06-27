#include "oob.hpp"
#include "tl.hpp"
#include "mp_external.hpp"
#include "mp_common.hpp"
#include "mp.hpp"

//=============== PT2PT ===============
//Non-Blocking
int mp_irecv(void * buf, size_t size, int peer, mp_request_t * mp_req, mp_key_t * mp_key) {
	MP_CHECK_COMM_OBJ();

	if(!mp_req || !mp_key)
		return MP_FAILURE;

	if(peer > oob_size)
	{
		mp_err_msg(oob_rank, "Communication peer: %d, Tot num of peers: %d\n", peer, oob_size);
		return MP_FAILURE;
	}

	return tl_comm->pt2pt_nb_recv(buf, size, peer, mp_req, mp_key);
}

int mp_irecvv(struct iovec *v, int nvecs, int peer, mp_request_t * mp_req, mp_key_t * mp_key) {
	MP_CHECK_COMM_OBJ();

	if(!mp_req || !mp_key || !v || nvecs <= 0)
		return MP_FAILURE;

	if(peer > oob_size)
	{
		mp_err_msg(oob_rank, "Communication peer: %d, Tot num of peers: %d\n", peer, oob_size);
		return MP_FAILURE;
	}

	return tl_comm->pt2pt_nb_recvv(v, nvecs, peer, mp_req, mp_key);
}


int mp_isend(void * buf, size_t size, int peer, mp_request_t * mp_req, mp_key_t * mp_key) {
	MP_CHECK_COMM_OBJ();

	if(!mp_req || !mp_key || !buf || size <= 0)
		return MP_FAILURE;

	if(peer > oob_size)
	{
		mp_err_msg(oob_rank, "Communication peer: %d, Tot num of peers: %d\n", peer, oob_size);
		return MP_FAILURE;
	}

	return tl_comm->pt2pt_nb_send(buf, size, peer, mp_req, mp_key);
}

int mp_isendv(struct iovec *v, int nvecs, int peer, mp_request_t * mp_req, mp_key_t * mp_key) {
	MP_CHECK_COMM_OBJ();

	if(!mp_req || !mp_key || !v || nvecs <= 0)
		return MP_FAILURE;

	if(peer > oob_size)
	{
		mp_err_msg(oob_rank, "Communication peer: %d, Tot num of peers: %d\n", peer, oob_size);
		return MP_FAILURE;
	}

	return tl_comm->pt2pt_nb_sendv(v, nvecs, peer, mp_req, mp_key);
}

//Blocking


//=============== ONE-SIDED ===============
//Non-Blocking
int mp_iput(void *buf, int size, mp_key_t * mp_key, int peer, size_t displ, mp_window_t * mp_win, mp_request_t * mp_req, int flags) {
	MP_CHECK_COMM_OBJ();

	if(!mp_req || !mp_key)
		return MP_FAILURE;

	if(peer > oob_size)
	{
		mp_err_msg(oob_rank, "Communication peer: %d, Tot num of peers: %d\n", peer, oob_size);
		return MP_FAILURE;
	}

	if(flags != 0 && !(flags & MP_PUT_NOWAIT) && !(flags & MP_PUT_INLINE))
	{
		mp_err_msg(oob_rank, "Wrong input flags %x\n", flags);
		return MP_FAILURE;
	}

	return tl_comm->onesided_nb_put(buf, size, mp_key, peer, displ, mp_win, mp_req, flags); 
}

int mp_iget(void *buf, int size, mp_key_t * mp_key, int peer, size_t displ, mp_window_t * mp_win, mp_request_t * mp_req) {
	MP_CHECK_COMM_OBJ();

	if(!mp_req || !mp_key)
		return MP_FAILURE;

	if(peer > oob_size)
	{
		mp_err_msg(oob_rank, "Communication peer: %d, Tot num of peers: %d\n", peer, oob_size);
		return MP_FAILURE;
	}

	return tl_comm->onesided_nb_get(buf, size, mp_key, peer, displ, mp_win, mp_req); 
}
//Blocking


//=============== WAIT ===============
int mp_wait_word(uint32_t *ptr, uint32_t value, int flags) {
	if(!ptr)
	{
		mp_err_msg(oob_rank, "Input ptr NULL\n");
		return MP_FAILURE;
	}

	return tl_comm->wait_word(ptr, value, flags);
}

int mp_wait(mp_request_t * mp_req) {
	if(!mp_req)
	{
		mp_err_msg(oob_rank, "Input request NULL\n");
		return MP_FAILURE;
	}

	return tl_comm->wait(mp_req);
}

int mp_wait_all(int number, mp_request_t * mp_reqs) {
	MP_CHECK_COMM_OBJ();

	if(!mp_reqs)
	{
		mp_err_msg(oob_rank, "Input request NULL\n");
		return MP_FAILURE;
	}

	if(number <= 0)
	{
		mp_err_msg(oob_rank, "Erroneous number of requests (%d)\n", number);
		return MP_FAILURE;
	}	
	
	return tl_comm->wait_all(number, mp_reqs);
}
