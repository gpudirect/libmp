#include "oob.hpp"
#include "tl.hpp"
#include "mp_external.hpp"
#include "mp.hpp"
#include "mp_common.hpp"

//=============== PT2PT ===============
//Non-Blocking
int mp_irecv(void * buf, size_t size, int peer, mp_region_t * mp_reg, mp_request_t * mp_req) {
	MP_CHECK_COMM_OBJ();

	if(!mp_req || !mp_reg || !buf || size <= 0)
	{
		mp_err_msg(oob_rank, "request: %d, region: %d buf: %d, size: %d\n", (mp_req) ? 1 : 0, (mp_reg) ? 1 : 0, (buf) ? 1 : 0, size);
		return MP_FAILURE;
	}

	if(peer > oob_size)
	{
		mp_err_msg(oob_rank, "Communication peer: %d, Tot num of peers: %d\n", peer, oob_size);
		return MP_FAILURE;
	}

	return tl_comm->pt2pt_nb_recv(buf, size, peer, mp_reg, mp_req);
}

int mp_irecvv(struct iovec *v, int nvecs, int peer, mp_region_t * mp_reg, mp_request_t * mp_req) {
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

	return tl_comm->pt2pt_nb_recvv(v, nvecs, peer,  mp_reg, mp_req);
}


int mp_isend(void * buf, size_t size, int peer, mp_region_t * mp_reg, mp_request_t * mp_req) {
	MP_CHECK_COMM_OBJ();

	if(!mp_req || !mp_reg || !buf || size <= 0)
	{
		mp_err_msg(oob_rank, "request: %d, region: %d buf: %d, size: %d\n", (mp_req) ? 1 : 0, (mp_reg) ? 1 : 0, (buf) ? 1 : 0, size);
		return MP_FAILURE;
	}

	if(peer > oob_size)
	{
		mp_err_msg(oob_rank, "Communication peer: %d, Tot num of peers: %d\n", peer, oob_size);
		return MP_FAILURE;
	}

	return tl_comm->pt2pt_nb_send(buf, size, peer,  mp_reg, mp_req);
}

int mp_isendv(struct iovec *v, int nvecs, int peer, mp_region_t * mp_reg, mp_request_t * mp_req) {
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

	return tl_comm->pt2pt_nb_sendv(v, nvecs, peer,  mp_reg, mp_req);
}

//Blocking


//=============== ONE-SIDED ===============
//Non-Blocking
int mp_iput(void *buf, int size, mp_region_t * mp_reg, int peer, size_t displ, mp_window_t * mp_win, mp_request_t * mp_req, int flags) {
	int ret=MP_SUCCESS;

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

	if(flags != 0 && !(flags & MP_PUT_NOWAIT) && !(flags & MP_PUT_INLINE) && !(flags & (MP_PUT_INLINE | MP_PUT_NOWAIT)))
	{
		mp_err_msg(oob_rank, "Wrong input flags %x\n", flags);
		return MP_FAILURE;
	}

	ret = tl_comm->onesided_nb_put(buf, size, mp_reg, peer, displ, mp_win, mp_req, flags);
	if(ret)
	{
		mp_err_msg(oob_rank, "onesided_nb_put error %s\n", strerror(errno));
		return MP_FAILURE;
	}

	return ret;
}

int mp_iget(void *buf, int size, mp_region_t * mp_reg, int peer, size_t displ, mp_window_t * mp_win, mp_request_t * mp_req) {
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

	return tl_comm->onesided_nb_get(buf, size, mp_reg, peer, displ, mp_win, mp_req); 
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

int mp_prepare_acks_rdma()
{
	MP_CHECK_COMM_OBJ();
	assert(oob_size);

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

	ack_rdma=1;

	return MP_SUCCESS;
}

int mp_send_ack_rdma(int dst_rank)
{
	mp_request_t * ack_request;
	assert(ack_rdma);

	MP_CHECK_COMM_OBJ();
	assert(dst_rank < oob_size);
	assert(dst_rank != oob_rank);
	assert(local_ack_table_win);
	ack_request = mp_create_request(1);
	assert(ack_request);

	int remote_offset = oob_rank * sizeof(uint32_t);
	mp_dbg_msg(oob_rank, "dest_rank=%d payload=%x remote_offset=%d\n", dst_rank, remote_ack_values[dst_rank], remote_offset);

	MP_CHECK(mp_iput(&remote_ack_values[dst_rank], sizeof(uint32_t), &remote_ack_values_reg[0], dst_rank, remote_offset, 
						&local_ack_table_win, &ack_request[0], MP_PUT_INLINE)); 
	
	//MP_PUT_NOWAIT will full the Send Queue! Why?
	MP_CHECK(mp_wait(&ack_request[0]));
	//atomic_inc
	__sync_fetch_and_add(&remote_ack_values[dst_rank], 1);
	//++ACCESS_ONCE(*ptr);
	iomb();
	free(ack_request);

	return MP_SUCCESS;
}

int mp_wait_ack_rdma(int src_rank)
{
	assert(ack_rdma);
	MP_CHECK_COMM_OBJ();
	assert(src_rank < oob_size);
	assert(src_rank != oob_rank);
	mp_dbg_msg(oob_rank, "src_rank=%d payload=%x\n", src_rank, local_ack_values[src_rank]);
	MP_CHECK(mp_wait_word(&(local_ack_table[src_rank]), local_ack_values[src_rank], MP_WAIT_GEQ));
	local_ack_values[src_rank]++;
	#if 0
	    while (ACCESS_ONCE(local_ack_table[rank]) < local_ack_values[rank]) {
	        rmb();
	        arch_cpu_relax();
	        ++cnt;
	        if (cnt > 10000) {
	            comm_progress();
	            cnt = 0;
	        }
	    }
	    local_ack_values[rank]++;
	#endif
    return MP_SUCCESS;
}

int mp_cleanup_acks_rdma()
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

// ---------------------------------------------------------- PT2PT ----------------------------------------------------------
mp_region_t * s_regs, * r_regs;
mp_request_t * s_reqs, * r_reqs;

int mp_prepare_acks_pt2pt()
{
	return MP_SUCCESS;
}

int mp_send_ack_pt2pt(int dst_rank)
{

	return MP_SUCCESS;
}

int mp_wait_ack_pt2pt(int src_rank)
{
    return MP_SUCCESS;
}

int mp_cleanup_acks_pt2pt()
{
	return MP_SUCCESS;
}
