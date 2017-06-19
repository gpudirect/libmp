#ifndef COMMON_H
#define COMMON_H

#if HAVE_CONFIG_H
#  include <config.h>
#endif /* HAVE_CONFIG_H */

#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <iostream>

#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <limits.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <time.h>
#include <assert.h>
#include <errno.h>

//#include <cuda.h>
//#include <cudaProfiler.h>
//#include <mpi.h>

#define MAX_OOB 20
#define MAX_TL 20

#define TL_INDEX_VERBS 0
#define TL_INDEX_PSM 1

#define OOB_PRIORITY_MPI 0
#define OOB_PRIORITY_SOCKET 1

#define OOB_SUCCESS 0
#define OOB_FAILURE	1

#define MP_SUCCESS 0
#define MP_FAILURE 1

#define MAX_PEERS 50

//progress flow fix
#define MP_MAX_PROGRESS_FLOW_TRY 100
#define MP_PROGRESS_ERROR_CHECK_TMOUT_US ((us_t)60*1000*1000)

typedef enum {
    MP_CHAR=0,
    MP_BYTE,
    MP_INT,
    MP_LONG,
    MP_FLOAT,
    MP_DOUBLE
} mp_data_type;

typedef enum mp_state {
    MP_UNDEF,
    MP_PREPARED,       // req just prepared
    MP_PENDING_NOWAIT, // req posted, but not wait-for-end pending yet
    MP_PENDING,        // req posted and wait is pending
    // MP_WAIT_POSTED,
    MP_COMPLETE,
    MP_N_STATES
} mp_state_t ;

typedef enum mp_req_type {
    MP_NULL = 0,
    MP_SEND,
    MP_RECV,
    MP_RDMA,
    MP_N_TYPES
} mp_req_type_t;

typedef enum mp_flow {
    TX_FLOW, // requests associated with tx_cq
    RX_FLOW, // same for rx_cq
    N_FLOWS
} mp_flow_t;

enum mp_put_flags {
    MP_PUT_INLINE  = 1<<0,
    MP_PUT_NOWAIT  = 1<<1, // don't generate a CQE, req cannot be waited for
};


//to be continued.....
//cast within tl functions to verbs_request_t
typedef void * mp_request_t;
typedef void * mp_key_t;

#ifndef ACCESS_ONCE
#define ACCESS_ONCE(V)                          \
    (*(volatile typeof (V) *)&(V))
#endif

#ifndef MIN
#define MIN(A,B) ((A)<(B)?(A):(B))
#endif

extern int oob_rank;

#define mp_dbg_msg(FMT, ARGS...)  do {                                  \
    if (mp_dbg_is_enabled)  {                                            \
        fprintf(stderr, "[%d] [%d] MP DBG  %s() "                       \
                FMT, getpid(),  oob_rank, __FUNCTION__ , ## ARGS); \
        fflush(stderr);                                                 \
    }                                                                   \
} while(0)

#define mp_warn_msg(FMT, ARGS...) do {                                  \
        if (mp_warn_is_enabled) {                                        \
            fprintf(stderr, "[%d] [%d] MP WARN %s() "                   \
                    FMT, getpid(), oob_rank, __FUNCTION__ , ## ARGS); \
            fflush(stderr);                                             \
        }                                                               \
    } while(0)



#define mp_info_msg(FMT, ARGS...) do {                                  \
        fprintf(stderr, "[%d] [%d] MP INFO %s() "                       \
                FMT, getpid(), oob_rank, __FUNCTION__ , ## ARGS);  \
        fflush(stderr);                                                 \
    } while(0)

#define mp_err_msg(FMT, ARGS...)  do {                                  \
        fprintf(stderr, "[%d] [%d] MP ERR  %s() "                       \
                FMT, getpid(), oob_rank, __FUNCTION__ , ## ARGS);  \
        fflush(stderr);                                                 \
    } while(0)


typedef uint64_t us_t;
static inline us_t mp_get_cycles()
{
    struct timespec ts;
    int ret = clock_gettime(CLOCK_MONOTONIC, &ts);
    if (ret) {
        mp_err_msg("error in gettime %d/%s\n", errno, strerror(errno));
        exit(EXIT_FAILURE);
    }
    return (us_t)ts.tv_sec * 1000 * 1000 + (us_t)ts.tv_nsec / 1000;
}


#endif