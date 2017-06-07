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

#define VERBS_SUCCESS 0
#define VERBS_FAILURE 1

#define MAX_PEERS 50

#endif