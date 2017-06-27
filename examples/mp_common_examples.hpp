#include <mp.hpp>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <stdarg.h>

#define dbg_msg(FMT, ARGS...)  __dbg_msg("[%d] [%d] DBG  %s() " FMT, getpid(),  my_rank, __FUNCTION__ , ## ARGS)

static int __dbg_msg(const char *fmt, ...)
{
    static int enable_debug_prints = -1;
    int ret = 0;
    if (-1 == enable_debug_prints) {
        const char *value = getenv("MP_ENABLE_APP_DEBUG");
        if (value != NULL)
            enable_debug_prints = atoi(value);
        else
            enable_debug_prints = 0;
    }

    if (enable_debug_prints) {
        va_list ap;
        va_start(ap, fmt);
        ret = vfprintf(stderr, fmt, ap);
        va_end(ap);
        fflush(stderr);
    }

    return ret;
}

#if define(HAVE_CUDA) || define(HAVE_GDSYNC)

#include <cuda.h>
#include <cuda_runtime.h>

#define CUDA_CHECK(stmt)                                \
do {                                                    \
    cudaError_t result = (stmt);                        \
    if (cudaSuccess != result) {                        \
        fprintf(stderr, "[%s] [%d] cuda failed with %s \n",   \
         __FILE__, __LINE__, cudaGetErrorString(result));\
        exit(-1);                                       \
    }                                                   \
    assert(cudaSuccess == result);                      \
} while (0)


#define CU_CHECK(stmt)                                  \
do {                                                    \
    CUresult result = (stmt);                           \
    if (CUDA_SUCCESS != result) {                       \
        fprintf(stderr, "[%s] [%d] cu failed with %d \n",    \
         __FILE__, __LINE__, result);    \
        exit(-1);                                       \
    }                                                   \
    assert(CUDA_SUCCESS == result);                     \
} while (0)

#endif