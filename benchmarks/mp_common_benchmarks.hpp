#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <stdarg.h>
#include <mp.hpp>

#define dbg_msg(FMT, ARGS...)  __dbg_msg("[%d] [%d] DBG  %s() " FMT, getpid(),  my_rank, __FUNCTION__ , ## ARGS)

static int __dbg_msg(const char *fmt, ...)
{
    static int enable_debug_prints = -1;
    int ret = 0;
    if (-1 == enable_debug_prints) {
        const char *value = getenv("MP_BENCH_ENABLE_DEBUG");
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

#define MPI_CHECK(stmt)                                             \
do {                                                                \
    int result = (stmt);                                            \
    if (MPI_SUCCESS != result) {                                    \
        char string[MPI_MAX_ERROR_STRING];                          \
        int resultlen = 0;                                          \
        MPI_Error_string(result, string, &resultlen);               \
        fprintf(stderr, " (%s:%d) MPI check failed with %d (%*s)\n",     \
                   __FILE__, __LINE__, result, resultlen, string);  \
        exit(-1);                                                   \
    }                                                               \
} while(0)
