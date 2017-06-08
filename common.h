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

typedef enum {
	MP_CHAR=0,
	MP_BYTE,
	MP_INT,
	MP_LONG,
	MP_FLOAT,
	MP_DOUBLE
} mp_data_type;

#define mp_info_msg(FMT, ARGS...) do {                                  \
        fprintf(stderr, "[%d] [%d] MP INFO %s() "                       \
                FMT, getpid(), 0 /*mpi_comm_rank*/, __FUNCTION__ , ## ARGS);  \
        fflush(stderr);                                                 \
    } while(0)

#define mp_err_msg(FMT, ARGS...)  do {                                  \
        fprintf(stderr, "[%d] [%d] MP ERR  %s() "                       \
                FMT, getpid(), 0 /*mpi_comm_rank*/, __FUNCTION__ , ## ARGS);  \
        fflush(stderr);                                                 \
    } while(0)

#endif