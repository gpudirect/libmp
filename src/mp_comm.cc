#include "oob.hpp"
#include "tl.hpp"
#include "mp_external.hpp"
#include "mp.hpp"
#include "mp_common.hpp"

//=============== PT2PT ===============
//Non-Blocking
int mp_irecv(void * buf, size_t size, int peer, mp_region_t * mp_reg, mp_request_t * mp_req) {
	MP_CHECK_COMM_OBJ();

	if(!mp_req || !mp_reg)
		return MP_FAILURE;

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
		return MP_FAILURE;

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
		return MP_FAILURE;

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
		return MP_FAILURE;

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
	MP_CHECK_COMM_OBJ();

	if(!mp_req || !mp_reg)
		return MP_FAILURE;

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

	return tl_comm->onesided_nb_put(buf, size, mp_reg, peer, displ, mp_win, mp_req, flags); 
}

int mp_iget(void *buf, int size, mp_region_t * mp_reg, int peer, size_t displ, mp_window_t * mp_win, mp_request_t * mp_req) {
	MP_CHECK_COMM_OBJ();

	if(!mp_req || !mp_reg)
		return MP_FAILURE;

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

// ============================= SYNC ACK FACILITY =============================
// tables are indexed by rank, not peer
static uint32_t   	* local_ack_table;
static mp_region_t  * local_ack_table_reg;
static mp_window_t 	local_ack_table_win;

static uint32_t   	* local_ack_values;
static uint32_t   	* remote_ack_values;
static mp_region_t  * remote_ack_values_reg;

int mp_prepare_acks()
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

	for (i=0; i<num_ranks; ++i) {
	    local_ack_table[i] = 0;  // remotely written table
	    local_ack_values[i] = 1; // locally expected value
	    remote_ack_values[i] = 1; // value to be sent remotely
	}
	iomb();

	local_ack_table_reg = mp_create_regions(1);
	assert(local_ack_table_reg);
	mp_dbg_msg(oob_rank, "registering local_ack_table size=%d\n", ack_table_size);
	MP_CHECK(mp_register(local_ack_table, ack_table_size, local_ack_table_reg));

	remote_ack_values_reg = mp_create_regions(1);
	assert(remote_ack_values_reg);
	mp_dbg_msg(oob_rank, "registering remote_ack_table\n");
	MP_CHECK(mp_register(remote_ack_values, ack_table_size, &remote_ack_values_reg));

	mp_dbg_msg(oob_rank, "creating local_ack_table window\n");
	MP_CHECK(mp_window_create(local_ack_table, ack_table_size, &local_ack_table_win));

	return MP_SUCCESS;
}

int mp_send_ack(int dst_rank)
{
	int ret = 0;
	mp_request_t * ack_request;

	MP_CHECK_COMM_OBJ();
	assert(dst_rank < oob_size);

	ack_request = mp_create_request(1);
	assert(ack_request);

	int remote_offset = oob_rank * sizeof(uint32_t);
	mp_dbg_msg(oob_rank, "dest_rank=%d payload=%x offset=%d\n", dst_rank, remote_ack_values[dst_rank], remote_offset);

	MP_CHECK(mp_iput(&remote_ack_values[dst_rank], sizeof(uint32_t), &remote_ack_values_reg, dst_rank, remote_offset, 
						&local_ack_table_win, &ack_request[0], MP_PUT_INLINE | MP_PUT_NOWAIT));

	//atomic_inc
	__sync_fetch_and_add(&remote_ack_values[dst_rank], 1);
	//++ACCESS_ONCE(*ptr);
	iomb();
}

int mp_wait_ack(int src_rank)
{
	MP_CHECK_COMM_OBJ();
	assert(src_rank < oob_size);

	mp_dbg_msg(oob_rank, "src_rank=%d payload=%x\n", src_rank, local_ack_values[src_rank]);
	MP_CHECK(mp_wait_word(&(local_ack_table[rank]), local_ack_values[rank], MP_WAIT_GEQ));
	local_ack_values[rank]++;
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
