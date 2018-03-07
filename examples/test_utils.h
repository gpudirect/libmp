/****
 * Copyright (c) 2011-2018, NVIDIA CORPORATION. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *  * Neither the name of NVIDIA CORPORATION nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 ****/

#pragma once

#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <stdarg.h>

#define MPI_CHECK(stmt)                                             \
do {                                                                \
    int result = (stmt);                                            \
    if (MPI_SUCCESS != result) {                                    \
        char string[MPI_MAX_ERROR_STRING];                          \
        int resultlen = 0;                                          \
        MPI_Error_string(result, string, &resultlen);               \
        fprintf(stderr, " (%s:%d) MPI check failed with %d (%*s)\n",    \
                   __FILE__, __LINE__, result, resultlen, string);  \
        exit(-1);                                                   \
    }                                                               \
} while(0)

#define CUDA_CHECK(stmt)                                \
do {                                                    \
    cudaError_t result = (stmt);                        \
    if (cudaSuccess != result) {                        \
        fprintf(stderr, "[%s:%d] cuda failed with %s \n",   \
         __FILE__, __LINE__,cudaGetErrorString(result));\
        exit(-1);                                       \
    }                                                   \
    assert(cudaSuccess == result);                      \
} while (0)

#define MP_CHECK(stmt)                                  \
do {                                                    \
    int result = (stmt);                                \
    if (0 != result) {                                  \
        fprintf(stderr, "[%s:%d] mp call failed \n",    \
         __FILE__, __LINE__);                           \
        /*exit(-1);*/                                   \
        MPI_Abort(MPI_COMM_WORLD, -1);                  \
    }                                                   \
    assert(0 == result);                                \
} while (0)


#define dbg_msg(FMT, ARGS...)  __dbg_msg("[%d] [%d] DBG  %s() " FMT, getpid(),  my_rank, __FUNCTION__ , ## ARGS)

static int __dbg_msg(const char *fmt, ...)
{
    static int enable_debug_prints = -1;
    int ret = 0;
    if (-1 == enable_debug_prints) {
        const char *value = getenv("ENABLE_DEBUG_MSG");
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

static int gpu_init(int gpu_id)
{
    int local_rank = 0;
    int dev_count;
    int dev_id;

    CUDA_CHECK(cudaGetDeviceCount(&dev_count));
    if (dev_count <= 0) {
        fprintf(stderr, "no CUDA devices found \n");
        return EINVAL;
    }

    if (gpu_id >= 0) {
        local_rank = gpu_id;
    } else if (getenv("USE_GPU")) {
        local_rank = atoi(getenv("USE_GPU"));
    } else if (getenv("MV2_COMM_WORLD_LOCAL_RANK") != NULL) {
        local_rank = atoi(getenv("MV2_COMM_WORLD_LOCAL_RANK"));
    } else if (getenv("OMPI_COMM_WORLD_LOCAL_RANK") != NULL) {
        local_rank = atoi(getenv("OMPI_COMM_WORLD_LOCAL_RANK"));
    } else {
        local_rank = 0;
    }

    dev_id = local_rank%dev_count;

    struct cudaDeviceProp prop;
    CUDA_CHECK(cudaGetDeviceProperties(&prop, dev_id));

    CUDA_CHECK(cudaSetDevice(dev_id));

    // CUDA init
    CUDA_CHECK(cudaFree(0));

    fprintf(stdout, "using GPU device: %d (%s #SMs=%d)\n", 
            dev_id, prop.name, prop.multiProcessorCount); 

    return 0;
}
