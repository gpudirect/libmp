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
	{
		mp_err_msg(oob_rank, "request: %d, region: %d\n", (mp_req) ? 1 : 0, (mp_reg) ? 1 : 0);
		return MP_FAILURE;
	}

	if(peer > oob_size)
	{
		mp_err_msg(oob_rank, "Communication peer: %d, Tot num of peers: %d\n", peer, oob_size);
		return MP_FAILURE;
	}

	return tl_comm->pt2pt_send_prepare(buf, size, peer, mp_reg, mp_req);
}

int mp_sendv_prepare(struct iovec *v, int nvecs, int peer, mp_region_t * mp_reg, mp_request_t * mp_req) {
	MP_CHECK_COMM_OBJ();

	if(!mp_req || !mp_reg || !v || nvecs <= 0)
	{
		mp_err_msg(oob_rank, "request: %d, region: %d v: %d, nvecs: %d\n", (mp_req) ? 1 : 0, (mp_reg) ? 1 : 0, (v) ? 1 : 0, nvecs);
		return MP_FAILURE;
	}

	if(peer > oob_size)
	{
		mp_err_msg(oob_rank, "Communication peer: %d, Tot num of peers: %d\n", peer, oob_size);
		return MP_FAILURE;
	}

	return tl_comm->pt2pt_sendv_prepare(v, nvecs, peer, mp_reg, mp_req);
}


//---------------- Non-Blocking ---------------- 
int mp_isend_post_async(mp_request_t * mp_req, cudaStream_t stream) {
	MP_CHECK_COMM_OBJ();

	if(!mp_req)
	{
		mp_err_msg(oob_rank, "request: %d\n", (mp_req) ? 1 : 0);
		return MP_FAILURE;
	}

	return tl_comm->pt2pt_nb_send_post_async(mp_req, stream);
}

int mp_isend_post_all_async(int number, mp_request_t * mp_req, cudaStream_t stream) {
	MP_CHECK_COMM_OBJ();

	if(!mp_req || number <= 0)
	{
		mp_err_msg(oob_rank, "request: %d, number: %d\n", (mp_req) ? 1 : 0, number);
		return MP_FAILURE;
	}

	return tl_comm->pt2pt_nb_send_post_all_async(number, mp_req, stream);
}

int mp_isend_async(void * buf, size_t size, int peer, mp_region_t * mp_reg, mp_request_t * mp_req, cudaStream_t stream) {
	MP_CHECK_COMM_OBJ();

	if(!mp_req || !mp_reg)
	{
		mp_err_msg(oob_rank, "request: %d, region: %d\n", (mp_req) ? 1 : 0, (mp_reg) ? 1 : 0);
		return MP_FAILURE;
	}

	if(peer > oob_size)
	{
		mp_err_msg(oob_rank, "Communication peer: %d, Tot num of peers: %d\n", peer, oob_size);
		return MP_FAILURE;
	}

	return tl_comm->pt2pt_nb_send_async(buf, size, peer, mp_reg, mp_req, stream);
}

int mp_isendv_async(struct iovec *v, int nvecs, int peer, mp_region_t * mp_reg, mp_request_t * mp_req, cudaStream_t stream) {
	MP_CHECK_COMM_OBJ();
	
	if(!mp_req || !mp_reg || !v || nvecs <= 0)
	{
		mp_err_msg(oob_rank, "request: %d, region: %d v: %d, nvecs: %d\n", (mp_req) ? 1 : 0, (mp_reg) ? 1 : 0, (v) ? 1 : 0, nvecs);
		return MP_FAILURE;
	}

	if(peer > oob_size)
	{
		mp_err_msg(oob_rank, "Communication peer: %d, Tot num of peers: %d\n", peer, oob_size);
		return MP_FAILURE;
	}

	return tl_comm->pt2pt_nb_sendv_async(v, nvecs, peer, mp_reg, mp_req, stream);
}


//---------------- Blocking ---------------- 
int mp_send_post_async(mp_request_t * mp_req, cudaStream_t stream) {
	MP_CHECK_COMM_OBJ();

	if(!mp_req)
	{
		mp_err_msg(oob_rank, "request: %d\n", (mp_req) ? 1 : 0);
		return MP_FAILURE;
	}

	return tl_comm->pt2pt_b_send_post_async(mp_req, stream);
}

int mp_send_post_all_async(int number, mp_request_t * mp_req, cudaStream_t stream) {
	MP_CHECK_COMM_OBJ();

	if(!mp_req || number <= 0)
	{
		mp_err_msg(oob_rank, "request: %d, number: %d\n", (mp_req) ? 1 : 0, number);
		return MP_FAILURE;
	}
	
	return tl_comm->pt2pt_b_send_post_all_async(number, mp_req, stream);
}

int mp_send_async(void * buf, size_t size, int peer, mp_region_t * mp_reg, mp_request_t * mp_req, cudaStream_t stream) {
	MP_CHECK_COMM_OBJ();

	if(!mp_req || !mp_reg)
	{
		mp_err_msg(oob_rank, "request: %d, region: %d\n", (mp_req) ? 1 : 0, (mp_reg) ? 1 : 0);
		return MP_FAILURE;
	}

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
	{
		mp_err_msg(oob_rank, "request: %d, region: %d\n", (mp_req) ? 1 : 0, (mp_reg) ? 1 : 0);
		return MP_FAILURE;
	}

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
	{
		mp_err_msg(oob_rank, "request: %d\n", (mp_req) ? 1 : 0);
		return MP_FAILURE;
	}

	return tl_comm->onesided_nb_put_post_async(mp_req, stream); 
}

int mp_iput_post_all_async(int number, mp_request_t * mp_req, cudaStream_t stream) {
	MP_CHECK_COMM_OBJ();

	if(!mp_req || number <= 0)
	{
		mp_err_msg(oob_rank, "request: %d, number: %d\n", (mp_req) ? 1 : 0, number);
		return MP_FAILURE;
	}

	return tl_comm->onesided_nb_put_post_all_async(number, mp_req, stream); 
}

int mp_iput_async(void *buf, int size, mp_region_t * mp_reg, int peer, size_t displ, mp_window_t * mp_win, mp_request_t * mp_req, int flags, cudaStream_t stream) {
	MP_CHECK_COMM_OBJ();

	if(!mp_req || !mp_reg)
	{
		mp_err_msg(oob_rank, "request: %d, region: %d\n", (mp_req) ? 1 : 0, (mp_reg) ? 1 : 0);
		return MP_FAILURE;
	}

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
	{
		mp_err_msg(oob_rank, "request: %d, region: %d\n", (mp_req) ? 1 : 0, (mp_reg) ? 1 : 0);
		return MP_FAILURE;
	}

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
		mp_err_msg(oob_rank, "ptr: %d\n", (ptr) ? 1 : 0);
		return MP_FAILURE;
	}


	return tl_comm->wait_word_async(ptr, value, flags, stream);
}

int mp_wait_async(mp_request_t * mp_req, cudaStream_t stream) {
	if(!mp_req)
	{
		mp_err_msg(oob_rank, "mp_req: %d\n", (mp_req) ? 1 : 0);
		return MP_FAILURE;
	}

	return tl_comm->wait_async(mp_req, stream);
}

int mp_wait_all_async(int number, mp_request_t * mp_reqs, cudaStream_t stream) {
	MP_CHECK_COMM_OBJ();

	if(!mp_reqs)
	{
		mp_err_msg(oob_rank, "mp_reqs: %d\n", (mp_reqs) ? 1 : 0);
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
		mp_err_msg(oob_rank, "mp_reqs: %d\n", (mp_reqs) ? 1 : 0);
		return MP_FAILURE;
	}

	if(number <= 0)
	{
		mp_err_msg(oob_rank, "Erroneous number of requests (%d)\n", number);
		return MP_FAILURE;
	}	
	
	return tl_comm->progress_requests(number, mp_reqs);	
}

// ============================= DESC QUEUE CREATION - GDS ONLY!
int mp_comm_descriptors_queue_alloc(mp_comm_descriptors_queue_t *pdq)
{
	MP_CHECK_TL_OBJ();

	if(!pdq)
	{
		mp_err_msg(oob_rank, "descr queue: %d\n", (pdq) ? 1 : 0);
		return MP_FAILURE;
	}

	return tl_comm->descriptors_queue_alloc(pdq);
}

int mp_comm_descriptors_queue_free(mp_comm_descriptors_queue_t *pdq)
{
	MP_CHECK_TL_OBJ();

	if(!pdq)
	{
		mp_err_msg(oob_rank, "descr queue: %d\n", (pdq) ? 1 : 0);
		return MP_FAILURE;
	}

	return tl_comm->descriptors_queue_free(pdq);
}

int mp_comm_descriptors_queue_add_send(mp_comm_descriptors_queue_t *pdq, mp_request_t * mp_req)
{
	MP_CHECK_TL_OBJ();

	if(!pdq || !mp_req)
	{
		mp_err_msg(oob_rank, "pdq: %d, request: %d\n", (pdq) ? 1 : 0, (mp_req) ? 1 : 0);
		return MP_FAILURE;
	}

	return tl_comm->descriptors_queue_add_send(pdq, mp_req);
}

int mp_comm_descriptors_queue_add_wait_send(mp_comm_descriptors_queue_t *pdq, mp_request_t *mp_req)
{
	MP_CHECK_TL_OBJ();

	if(!pdq || !mp_req)
	{
		mp_err_msg(oob_rank, "pdq: %d, request: %d\n", (pdq) ? 1 : 0, (mp_req) ? 1 : 0);
		return MP_FAILURE;
	}

	return tl_comm->descriptors_queue_add_wait_send(pdq, mp_req);
}

int mp_comm_descriptors_queue_add_wait_recv(mp_comm_descriptors_queue_t *pdq, mp_request_t *mp_req)
{
	MP_CHECK_TL_OBJ();

	if(!pdq || !mp_req)
	{
		mp_err_msg(oob_rank, "pdq: %d, request: %d\n", (pdq) ? 1 : 0, (mp_req) ? 1 : 0);
		return MP_FAILURE;
	}

	return tl_comm->descriptors_queue_add_wait_recv(pdq, mp_req);
}

int mp_comm_descriptors_queue_add_wait_value32(mp_comm_descriptors_queue_t *pdq, uint32_t *ptr, uint32_t value, int flags)
{
	MP_CHECK_TL_OBJ();

	if(!pdq || !ptr)
	{
		mp_err_msg(oob_rank, "pdq: %d, ptr: %d\n", (pdq) ? 1 : 0, (ptr) ? 1 : 0);
		return MP_FAILURE;
	}

	return tl_comm->descriptors_queue_add_wait_value32(pdq, ptr, value, flags);
}

int mp_comm_descriptors_queue_add_write_value32(mp_comm_descriptors_queue_t *pdq, uint32_t *ptr, uint32_t value)
{
	MP_CHECK_TL_OBJ();

	if(!pdq || !ptr)
	{
		mp_err_msg(oob_rank, "pdq: %d, ptr: %d\n", (pdq) ? 1 : 0, (ptr) ? 1 : 0);
		return MP_FAILURE;
	}

	return tl_comm->descriptors_queue_add_write_value32(pdq, ptr, value);
}

int mp_comm_descriptors_queue_post_async(cudaStream_t stream, mp_comm_descriptors_queue_t *pdq, int flags)
{
    MP_CHECK_TL_OBJ();

	if(!pdq)
	{
		mp_err_msg(oob_rank, "descr queue: %d\n", (pdq) ? 1 : 0);
		return MP_FAILURE;
	}

	return tl_comm->descriptors_queue_post_async(stream, pdq, flags);

}

// ========================================================== SYNC ACK FACILITY ==========================================================
static int ack_rdma=0;
static int ack_pt2pt=0;

// ---------------------------------------------------------- RDMA ----------------------------------------------------------
// tables are indexed by rank, not peer
static uint32_t   	* local_ack_table;
static mp_region_t  * local_ack_table_reg;
static mp_window_t 	local_ack_table_win;

static uint32_t   	* local_ack_values;
static uint32_t   	* remote_ack_values;
static mp_region_t  * remote_ack_values_reg;

static mp_request_t * ack_request;
static int num_ack_reqs=0;
static int current_ack_req=0;

int mp_prepare_acks_rdma_async(int numAcks)
{
	MP_CHECK_COMM_OBJ();
	assert(oob_size);
	assert(numAcks);
	num_ack_reqs=0;
	// init ready stuff
	size_t ack_table_size = MAX(sizeof(*local_ack_table) * oob_size, PAGE_SIZE);

	posix_memalign((void **)&local_ack_table, PAGE_SIZE, ack_table_size);
	assert(local_ack_table);

	local_ack_values = (uint32_t *)malloc(ack_table_size);
	assert(local_ack_values);

	posix_memalign((void **)&remote_ack_values, PAGE_SIZE, ack_table_size);
	assert(remote_ack_values);

	for (int i=0; i<oob_size; ++i) {
	    local_ack_table[i] = 0;  // remotely written table
	    local_ack_values[i] = 1; // locally expected value
	    remote_ack_values[i] = 1; // value to be sent remotely
	}
	iomb();

	local_ack_table_reg = mp_create_regions(1);
	assert(local_ack_table_reg);
	MP_CHECK(mp_register_region_buffer(local_ack_table, ack_table_size, &local_ack_table_reg[0]));
	mp_dbg_msg(oob_rank, "registering local_ack_table size=%d\n", ack_table_size);

	remote_ack_values_reg = mp_create_regions(1);
	assert(remote_ack_values_reg);
	MP_CHECK(mp_register_region_buffer(remote_ack_values, ack_table_size, &remote_ack_values_reg[0]));
	mp_dbg_msg(oob_rank, "registering remote_ack_table\n");

	MP_CHECK(mp_window_create(local_ack_table, ack_table_size, &local_ack_table_win));
	mp_dbg_msg(oob_rank, "creating local_ack_table window\n");

	num_ack_reqs=numAcks;
	ack_request = mp_create_request(num_ack_reqs);
	assert(ack_request);
	ack_rdma=1;

	return MP_SUCCESS;
}

int mp_send_ack_rdma_async(int dst_rank, cudaStream_t ackStream)
{
	assert(ack_rdma);
	MP_CHECK_COMM_OBJ();
	assert(dst_rank < oob_size);
	assert(dst_rank != oob_rank);
	assert(local_ack_table_win);
	
	if(num_ack_reqs <= 0)
	{
		mp_err_msg(oob_rank, "num_ack_reqs: %d\n", num_ack_reqs);
		return MP_FAILURE;
	}

	if(current_ack_req >= num_ack_reqs)
	{
		mp_err_msg(oob_rank, "Current ACK (%d) > Tot Acks (%d). Need to flush!\n", current_ack_req, num_ack_reqs);
		return MP_FAILURE;
	}

	assert(ack_request);
	int remote_offset = oob_rank * sizeof(uint32_t);
	mp_dbg_msg(oob_rank, "dest_rank=%d payload=%x remote_offset=%d\n", dst_rank, remote_ack_values[dst_rank], remote_offset);

	MP_CHECK(mp_iput_async(&remote_ack_values[dst_rank], sizeof(uint32_t), &remote_ack_values_reg[0], dst_rank, remote_offset, 
						&local_ack_table_win, &ack_request[current_ack_req], MP_PUT_INLINE | MP_PUT_NOWAIT, ackStream)); 
	
	current_ack_req=(current_ack_req+1)%num_ack_reqs;
	//atomic_inc
	__sync_fetch_and_add(&remote_ack_values[dst_rank], 1);
	//++ACCESS_ONCE(*ptr);
	iomb();

	return MP_SUCCESS;
}

int mp_wait_ack_rdma_async(int src_rank, cudaStream_t ackStream)
{
	assert(ack_rdma);
	MP_CHECK_COMM_OBJ();
	assert(src_rank < oob_size);
	assert(src_rank != oob_rank);
	mp_dbg_msg(oob_rank, "src_rank=%d payload=%x\n", src_rank, local_ack_values[src_rank]);
	MP_CHECK(mp_wait_word_async(&(local_ack_table[src_rank]), local_ack_values[src_rank], MP_WAIT_GEQ, ackStream));
	local_ack_values[src_rank]++;
	return MP_SUCCESS;
}

int mp_flush_ack_rdma_async()
{
	assert(ack_rdma);
	assert(ack_request);

	MP_CHECK_COMM_OBJ();

	if(current_ack_req == 0)
		return MP_SUCCESS;

	MP_CHECK(mp_wait_all(current_ack_req, ack_request));

	current_ack_req=0;
	return MP_SUCCESS;
}

int mp_cleanup_acks_rdma_async()
{
	assert(ack_rdma);

	if(local_ack_table_reg)
		MP_CHECK(mp_unregister_regions(1, local_ack_table_reg));
	
	if(remote_ack_values_reg)
		MP_CHECK(mp_unregister_regions(1, remote_ack_values_reg));
	
	if(local_ack_table_win)
		MP_CHECK(mp_window_destroy(&local_ack_table_win));
	
	if(local_ack_table)
		free(local_ack_table);

	if(remote_ack_values)
		free(remote_ack_values);

	return MP_SUCCESS;
}
