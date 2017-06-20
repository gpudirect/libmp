#include "common.h"
#include "oob.h"
#include "tl.h"


#define MP_CHECK(stmt)                                  \
do {                                                    \
    int result = (stmt);                                \
    if (MP_SUCCESS != result) {                         \
        fprintf(stderr, "[%s:%d] mp call failed \n",    \
         __FILE__, __LINE__);                           \
        /*exit(-1);*/                                   \
        oob_comm->abort(-1);			                \
    }                                                   \
    assert(0 == result);                                \
} while (0)

int mp_init();
int mp_finalize();

//===== MEMORY OPs
mp_request_t * mp_create_request(int number);
mp_key_t * mp_create_keys(int number);
int mp_register_key_buffer(void * addr, size_t length, mp_key_t * mp_key);

//===== PT2PT
int mp_nb_recv(void * buf, size_t size, int peer, mp_request_t * mp_req, mp_key_t * mp_key);
int mp_nb_send(void * buf, size_t size, int peer, mp_request_t * mp_req, mp_key_t * mp_key);

//===== ONE-SIDED
int mp_nb_put(void *buf, int size, mp_key_t * mp_key, int peer, size_t displ, mp_window_t * mp_win, mp_request_t * mp_req, int flags);
int mp_nb_get(void *buf, int size, mp_key_t * mp_key, int peer, size_t displ, mp_window_t * mp_win, mp_request_t * mp_req, int flags);

//===== WAIT
int mp_wait_word(uint32_t *ptr, uint32_t value, int flags);
int mp_wait(mp_request_t * mp_req);
int mp_wait_all(int number, mp_request_t * mp_reqs);

//===== OTHERS
int mp_barrier();