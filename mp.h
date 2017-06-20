#include "common.h"
#include "oob.h"
#include "tl.h"

#define MP_CHECK(stmt)                                  \
do {                                                    \
    int result = (stmt);                                \
    if (MP_SUCCESS != result) {                         \
        fprintf(stderr, "[%s:%d] mp call failed \n",    \
         __FILE__, __LINE__);                           \
        exit(EXIT_FAILURE);                             \
    }                                                   \
    assert(0 == result);                                \
} while (0)
//oob_comm->abort(-1);

#define MP_CHECK_OOB_OBJ()																\
	if(!oob_comm) {																		\
		fprintf(stderr, "[%s:%d] OOB object not initialized \n", __FILE__, __LINE__);	\
		exit(EXIT_FAILURE);																\
	}

#define MP_CHECK_TL_OBJ()																\
	if(!tl_comm) {																		\
		fprintf(stderr, "[%s:%d] TL object not initialized \n", __FILE__, __LINE__);	\
		oob_comm->abort(-1);					                						\
		exit(EXIT_FAILURE);																\
	}

#define MP_CHECK_COMM_OBJ()		\
	MP_CHECK_OOB_OBJ();			\
	MP_CHECK_TL_OBJ();

#define MP_API_MAJOR_VERSION    3
#define MP_API_MINOR_VERSION    0
#define MP_API_VERSION          ((MP_API_MAJOR_VERSION << 16) | MP_API_MINOR_VERSION)

#define MP_API_VERSION_COMPATIBLE(v) \
    ( ((((v) & 0xffff0000U) >> 16) == MP_API_MAJOR_VERSION) &&   \
      ((((v) & 0x0000ffffU) >> 0 ) >= MP_API_MINOR_VERSION) )

//===== INFO
typedef enum mp_param {
    MP_PARAM_VERSION=0,
    MP_NUM_PARAMS,
    MP_OOB_TYPE,
    MP_TL_TYPE,
    MP_NUM_RANKS,
    MP_MY_RANK
} mp_param_t;

int mp_query_param(mp_param_t param, int *value);

int mp_init(int argc, char *argv[], int par1);
void mp_finalize();
void mp_get_envars();

//===== MEMORY OPs
mp_request_t * mp_create_request(int number);
mp_key_t * mp_create_keys(int number);
int mp_unregister_keys(int number, mp_key_t * mp_keys);
int mp_register_key_buffer(void * addr, size_t length, mp_key_t * mp_key);

//===== PT2PT
int mp_nb_recv(void * buf, size_t size, int peer, mp_request_t * mp_req, mp_key_t * mp_key);
int mp_nb_send(void * buf, size_t size, int peer, mp_request_t * mp_req, mp_key_t * mp_key);

//===== ONE-SIDED
int mp_nb_put(void *buf, int size, mp_key_t * mp_key, int peer, size_t displ, mp_window_t * mp_win, mp_request_t * mp_req, int flags);
int mp_nb_get(void *buf, int size, mp_key_t * mp_key, int peer, size_t displ, mp_window_t * mp_win, mp_request_t * mp_req);
int mp_window_create(void *addr, size_t size, mp_window_t *window_t);
int mp_window_destroy(mp_window_t *window_t);

//===== WAIT
int mp_wait_word(uint32_t *ptr, uint32_t value, int flags);
int mp_wait(mp_request_t * mp_req);
int mp_wait_all(int number, mp_request_t * mp_reqs);

//===== SYNC
void mp_barrier();
void mp_abort();