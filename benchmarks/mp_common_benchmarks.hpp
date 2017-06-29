#include <mp.hpp>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <stdarg.h>
#include "prof.h"

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