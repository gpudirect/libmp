#include "oob.hpp"
#include "tl.hpp"
#include "mp_external.hpp"
#include "mp.hpp"
#include "mp_common.hpp"


// SA Model: GPU Synchronous, CPU Asynchronous

//============================== PT2PT ==============================
//Prepare and Post
int mp_send_prepare(void * buf, size_t size, int peer, mp_region_t * mp_reg, mp_request_t * mp_req) {
	MP_CHECK_COMM_OBJ();

	if(!mp_req || !mp_reg)
		return MP_FAILURE;

	if(peer > oob_size)
	{
		mp_err_msg(oob_rank, "Communication peer: %d, Tot num of peers: %d\n", peer, oob_size);
		return MP_FAILURE;
	}

	return tl_comm->pt2pt_send_prepare(buf, size, peer, mp_reg, mp_req);
}

//---------------- Non-Blocking ---------------- 
int mp_isend_post_async(mp_request_t * mp_req, cudaStream_t stream) {
	MP_CHECK_COMM_OBJ();

	if(!mp_req)
		return MP_FAILURE;

	return tl_comm->pt2pt_nb_send_post_async(mp_req, stream);
}

int mp_isend_post_all_async(int number, mp_request_t * mp_req, cudaStream_t stream) {
	MP_CHECK_COMM_OBJ();

	if(!mp_req || number <= 0)
		return MP_FAILURE;

	return tl_comm->pt2pt_nb_send_post_all_async(number, mp_req, stream);
}

int mp_isend_async(void * buf, size_t size, int peer, mp_region_t * mp_reg, mp_request_t * mp_req, cudaStream_t stream) {
	MP_CHECK_COMM_OBJ();

	if(!mp_req || !mp_reg)
		return MP_FAILURE;

	if(peer > oob_size)
	{
		mp_err_msg(oob_rank, "Communication peer: %d, Tot num of peers: %d\n", peer, oob_size);
		return MP_FAILURE;
	}

	return tl_comm->pt2pt_nb_send_async(buf, size, peer, mp_reg, mp_req, stream);
}

//---------------- Blocking ---------------- 
int mp_send_post_async(mp_request_t * mp_req, cudaStream_t stream) {
	MP_CHECK_COMM_OBJ();

	if(!mp_req)
		return MP_FAILURE;

	return tl_comm->pt2pt_b_send_post_async(mp_req, stream);
}

int mp_send_post_all_async(int number, mp_request_t * mp_req, cudaStream_t stream) {
	MP_CHECK_COMM_OBJ();

	if(!mp_req || number <= 0)
		return MP_FAILURE;

	return tl_comm->pt2pt_b_send_post_all_async(number, mp_req, stream);
}

int mp_send_async(void * buf, size_t size, int peer, mp_region_t * mp_reg, mp_request_t * mp_req, cudaStream_t stream) {
	MP_CHECK_COMM_OBJ();

	if(!mp_req || !mp_reg)
		return MP_FAILURE;

	if(peer > oob_size)
	{
		mp_err_msg(oob_rank, "Communication peer: %d, Tot num of peers: %d\n", peer, oob_size);
		return MP_FAILURE;
	}

	return tl_comm->pt2pt_b_send_async(buf, size, peer, mp_reg, mp_req, stream);
}

//============================== ONE-SIDED ==============================
//---------------- Non-Blocking ---------------- 
int mp_put_prepare(void *buf, int size, mp_region_t * mp_reg, int peer, size_t displ, mp_window_t * mp_win, mp_request_t * mp_req, int flags) {
	MP_CHECK_COMM_OBJ();

	if(!mp_req || !mp_reg)
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

	return tl_comm->onesided_put_prepare(buf, size, mp_reg, peer, displ, mp_win, mp_req, flags); 
}

int mp_iput_post_async(mp_request_t * mp_req, cudaStream_t stream) {
	MP_CHECK_COMM_OBJ();

	if(!mp_req)
		return MP_FAILURE;

	return tl_comm->onesided_nb_put_post_async(mp_req, stream); 
}

int mp_iput_post_all_async(int number, mp_request_t * mp_req, cudaStream_t stream) {
	MP_CHECK_COMM_OBJ();

	if(!mp_req || number <= 0)
		return MP_FAILURE;

	return tl_comm->onesided_nb_put_post_all_async(number, mp_req, stream); 
}

int mp_iput_async(void *buf, int size, mp_region_t * mp_reg, int peer, size_t displ, mp_window_t * mp_win, mp_request_t * mp_req, int flags, cudaStream_t stream) {
	MP_CHECK_COMM_OBJ();

	if(!mp_req || !mp_reg)
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

	return tl_comm->onesided_nb_put_async(buf, size, mp_reg, peer, displ, mp_win, mp_req, flags, stream); 
}

int mp_iget_async(void *buf, int size, mp_region_t * mp_reg, int peer, size_t displ, mp_window_t * mp_win, mp_request_t * mp_req, cudaStream_t stream) {
	MP_CHECK_COMM_OBJ();

	if(!mp_req || !mp_reg)
		return MP_FAILURE;

	if(peer > oob_size)
	{
		mp_err_msg(oob_rank, "Communication peer: %d, Tot num of peers: %d\n", peer, oob_size);
		return MP_FAILURE;
	}

	return tl_comm->onesided_nb_get_async(buf, size, mp_reg, peer, displ, mp_win, mp_req, stream); 
}

//============================== WAIT ==============================
int mp_wait_word_async(uint32_t *ptr, uint32_t value, int flags, cudaStream_t stream) {
	if(!ptr)
	{
		mp_err_msg(oob_rank, "Input ptr NULL\n");
		return MP_FAILURE;
	}

	return tl_comm->wait_word_async(ptr, value, flags, stream);
}

int mp_wait_async(mp_request_t * mp_req, cudaStream_t stream) {
	if(!mp_req)
	{
		mp_err_msg(oob_rank, "Input request NULL\n");
		return MP_FAILURE;
	}

	return tl_comm->wait_async(mp_req, stream);
}

int mp_wait_all_async(int number, mp_request_t * mp_reqs, cudaStream_t stream) {
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
	
	return tl_comm->wait_all_async(number, mp_reqs, stream);
}

int mp_progress_requests(int number, mp_request_t * mp_reqs) {
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
	
	return tl_comm->progress_requests(number, mp_reqs);	
}