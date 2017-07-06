/****
 * Copyright (c) 2011-2014, NVIDIA Corporation.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *    * Redistributions of source code must retain the above copyright notice,
 *      this list of conditions and the following disclaimer.
 *    * Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
 *    * Neither the name of the NVIDIA Corporation nor the names of its
 *      contributors may be used to endorse or promote products derived from
 *      this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 ****/

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <cuda.h>
#include <cuda_runtime.h>
#include <cuda_profiler_api.h>

#define CUDA_CHECK(stmt)                                \
do {                                                    \
    cudaError_t result = (stmt);                        \
    if (cudaSuccess != result) {                        \
        fprintf(stderr, "[%s] [%d] cuda failed with %s \n",   \
         __FILE__, __LINE__, cudaGetErrorString(result));\
        exit(EXIT_FAILURE);                             \
    }                                                   \
    assert(cudaSuccess == result);                      \
} while (0)


#define CU_CHECK(stmt)                                  \
do {                                                    \
    CUresult result = (stmt);                           \
    if (CUDA_SUCCESS != result) {                       \
        fprintf(stderr, "[%s] [%d] cu failed with %d \n",    \
         __FILE__, __LINE__, result);    \
        exit(EXIT_FAILURE);                             \
    }                                                   \
    assert(CUDA_SUCCESS == result);                     \
} while (0)

#ifdef PROFILE_NVTX_RANGES
#include <nvToolsExt.h>

#define COMM_COL 1
#define SM_COL   2
#define SML_COL  3
#define OP_COL   4
#define COMP_COL 5
#define SOLVE_COL 6
#define WARMUP_COL 7
#define EXEC_COL 8

#define SEND_COL 9
#define WAIT_COL 10
#define KERNEL_COL 11


#define PUSH_RANGE(name,cid)                                            \
  do {                                                                  \
    const uint32_t colors[] = {                                         \
            0x0000ff00, 0x000000ff, 0x00ffff00, 0x00ff00ff, 0x0000ffff, 0x00ff0000, 0x00ffffff, 0xff000000, 0xff0000ff, 0x55ff3300, 0xff660000, 0x66330000  \
    };                                                                  \
    const int num_colors = sizeof(colors)/sizeof(colors[0]);            \
    int color_id = cid%num_colors;                                  \
    nvtxEventAttributes_t eventAttrib = {0};                        \
    eventAttrib.version = NVTX_VERSION;                             \
    eventAttrib.size = NVTX_EVENT_ATTRIB_STRUCT_SIZE;               \
    eventAttrib.colorType = NVTX_COLOR_ARGB;                        \
    eventAttrib.color = colors[color_id];                           \
    eventAttrib.messageType = NVTX_MESSAGE_TYPE_ASCII;              \
    eventAttrib.message.ascii = name;                               \
    nvtxRangePushEx(&eventAttrib);                                  \
  } while(0)

#define PUSH_RANGE_STR(cid, FMT, ARGS...)       \
  do {                                          \
    char str[128];                              \
    snprintf(str, sizeof(str), FMT, ## ARGS);   \
    PUSH_RANGE(str, cid);                       \
  } while(0)


#define POP_RANGE do { nvtxRangePop(); } while(0)

#else
    #define PUSH_RANGE(name,cid)
    #define POP_RANGE
#endif

int gpu_launch_calc_kernel(size_t size, int gpu_num_sm, cudaStream_t stream);
int gpu_launch_dummy_kernel(double time, double clockrate, cudaStream_t stream);
