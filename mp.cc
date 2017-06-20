#include "mp.h"

static int oob_size=0, oob_rank=0;
static int mp_warn_is_enabled=0, mp_dbg_is_enabled=0;
static OOB::Communicator * oob_comm=NULL;
static TL::Communicator * tl_comm=NULL;
static int oob_type=-1;
static int tl_type=-1;

//=============== INIT ===============
void mp_get_envars()
{
	char * value = getenv("MP_ENABLE_DEBUG");
	if (value) {
		int en = atoi(value);
		mp_dbg_is_enabled = !!en;
		//printf("MP_ENABLE_DEBUG=%s\n", value);
	} else
		mp_dbg_is_enabled = 0;

	value = getenv("MP_ENABLE_WARN");
	if (value) {
		int en = atoi(value);
		mp_warn_is_enabled = !!en;
		//printf("MP_ENABLE_DEBUG=%s\n", value);
	} else
		mp_warn_is_enabled = 0;
}

int mp_init(int argc, char *argv[], int par1)
{
	int ret=MP_SUCCESS;
	mp_get_envars();

	// ====== OOB INIT
	oob_comm = getBestOOB();
	assert(oob_comm);
	oob_type=OOB_PRIORITY_MPI;

	ret = oob_comm->init(argc, argv);
	if(ret)
	{
		mp_err_msg(oob_rank, "oob_comm->init() error %d\n", ret);
		exit(EXIT_FAILURE);
	}

	oob_rank = oob_comm->getMyId();
	oob_size = oob_comm->getSize();

	// ====== TL INIT
#ifdef HAVE_GDSYNC
	tl_comm = getTLObj(TL_INDEX_VERBS_GDS);
	tl_type = TL_INDEX_VERBS_GDS;
#else
	#ifdef HAVE_VERBS
		tl_comm = getTLObj(TL_INDEX_VERBS);
		tl_type = TL_INDEX_VERBS;
	#else
		tl_comm = getTLObj(TL_INDEX_PSM);
		tl_type = TL_INDEX_PSM;
	#endif
#endif

	MP_CHECK_COMM_OBJ();
	MP_CHECK(tl_comm->setupOOB(oob_comm));
	//GDSync in case of Verbs 
	MP_CHECK(tl_comm->setup_sublayer(par1));
	MP_CHECK(tl_comm->setupNetworkDevices());
	MP_CHECK(tl_comm->createEndpoints());
	MP_CHECK(tl_comm->exchangeEndpoints());
	MP_CHECK(tl_comm->updateEndpoints());
	tl_comm->cleanupInit();

	printf("Return casuale: %d\n", tl_comm->funzione_casuale());

#ifndef HAVE_CUDA
	fprintf(stderr, "WARNING: GPUDirect RDMA requires HAVE_CUDA configure flag\n");
#endif

	return MP_SUCCESS;
}

//=============== FINALIZE ===============
void mp_finalize() {
	MP_CHECK_COMM_OBJ();

	MP_CHECK(tl_comm->finalize());
	MP_CHECK(oob_comm->finalize());
}

//=============== MEMORY OPs ===============
mp_request_t * mp_create_request(int number) {
	MP_CHECK_TL_OBJ();
	return tl_comm->create_requests(number);
}

mp_key_t * mp_create_keys(int number) {
	MP_CHECK_TL_OBJ();
	return tl_comm->create_keys(number);
}

int mp_unregister_keys(int number, mp_key_t * mp_keys) {
	MP_CHECK_TL_OBJ();
	int i;
	if(!mp_keys)
	{
		mp_err_msg(oob_rank, "Keys not initialized\n");
		return MP_FAILURE;
	}

	if(number <= 0)
	{
		mp_err_msg(oob_rank, "Erroneous number of keys (%d)\n", number);
		return MP_FAILURE;
	}	

	for(i=0; i<number; i++) {
		if(mp_keys[i])
			tl_comm->unregister_key(&mp_keys[i]);
	}

	return MP_SUCCESS;
}

int mp_register_key_buffer(void * addr, size_t length, mp_key_t * mp_key) {
	MP_CHECK_TL_OBJ();
	return tl_comm->register_key_buffer(addr, length, mp_key);
}

//=============== PT2PT ===============
int mp_nb_recv(void * buf, size_t size, int peer, mp_request_t * mp_req, mp_key_t * mp_key) {
	MP_CHECK_COMM_OBJ();

	if(!mp_req || !mp_key)
		return MP_FAILURE;

	if(peer > oob_size)
	{
		mp_err_msg(oob_rank, "Communication peer: %d, Tot num of peers: %d\n", peer, oob_size);
		return MP_FAILURE;
	}

	return tl_comm->pt2pt_nb_receive(buf, size, peer, mp_req, mp_key);
}

int mp_nb_send(void * buf, size_t size, int peer, mp_request_t * mp_req, mp_key_t * mp_key) {
	MP_CHECK_COMM_OBJ();

	if(!mp_req || !mp_key)
		return MP_FAILURE;

	if(peer > oob_size)
	{
		mp_err_msg(oob_rank, "Communication peer: %d, Tot num of peers: %d\n", peer, oob_size);
		return MP_FAILURE;
	}

	return tl_comm->pt2pt_nb_send(buf, size, peer, mp_req, mp_key);
}


//=============== ONE-SIDED ===============
int mp_nb_put(void *buf, int size, mp_key_t * mp_key, int peer, size_t displ, mp_window_t * mp_win, mp_request_t * mp_req, int flags) {
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


int mp_nb_get(void *buf, int size, mp_key_t * mp_key, int peer, size_t displ, mp_window_t * mp_win, mp_request_t * mp_req) {
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

int mp_window_create(void *addr, size_t size, mp_window_t *window_t) {
	MP_CHECK_COMM_OBJ();

	if(!addr)
		return MP_FAILURE;

	if(size <= 0)
	{
		mp_err_msg(oob_rank, "Erroneous size of window (%zd)\n", size);
		return MP_FAILURE;
	}

	return tl_comm->onesided_window_create(addr, size, window_t); 
}

int mp_window_destroy(mp_window_t *window_t) {

	if(!window_t)
		return MP_FAILURE;

	return tl_comm->onesided_window_destroy(window_t);
}

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

	return mp_wait_all(1, mp_req);
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

//=============== OTHERS ===============
void mp_barrier() {
	MP_CHECK_COMM_OBJ();

	oob_comm->barrier();
}

void mp_abort() {
	MP_CHECK_OOB_OBJ();

	oob_comm->abort(-1);
}
//=============== INFO ===============
int mp_query_param(mp_param_t param, int *value)
{
	int ret = 0;
	if (!value) return EINVAL;

	switch (param) {
        case MP_PARAM_VERSION:
            *value = (MP_API_MAJOR_VERSION << 16)|MP_API_MINOR_VERSION;
            break;
        
        case MP_NUM_PARAMS:
            *value = 6;
            break;

        case MP_NUM_RANKS:
        	MP_CHECK_OOB_OBJ();
        	*value=oob_size;
        	break;
        
        case MP_MY_RANK:
        	MP_CHECK_OOB_OBJ();
        	*value=oob_rank;
        	break;

        case MP_OOB_TYPE:
        	MP_CHECK_OOB_OBJ();
        	*value=oob_type;
        	break;

        case MP_TL_TYPE:
        	MP_CHECK_TL_OBJ();
        	*value=tl_type;
        	break;

        default:
                ret = EINVAL;
                break;
	};
	return ret;
}
