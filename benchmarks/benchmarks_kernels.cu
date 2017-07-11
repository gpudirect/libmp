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

#include "benchmarks_kernels.hpp"
static const int over_sub_factor = 2;

// ============ Working only with CUDA or LibGDSync  ============ 
__global__ void calc_kernel(int n, float c, float *in, float *out)
{
        const uint tid = threadIdx.x;
        const uint bid = blockIdx.x;
        const uint block_size = blockDim.x;
        const uint grid_size = gridDim.x;
        const uint gid = tid + bid*block_size;
        const uint n_threads = block_size*grid_size;
        for (int i=gid; i<n; i += n_threads)
                out[i] = in[i] * c;
}

int gpu_launch_calc_kernel(size_t size, int gpu_num_sm, cudaStream_t stream)
{
        const int nblocks = over_sub_factor * gpu_num_sm;
        const int nthreads = 32*2;
        int n = size / sizeof(float);
        static float *in = NULL;
        static float *out = NULL;
        if (!in) {
            CUDA_CHECK(cudaMalloc((void **)&in, size));
            CUDA_CHECK(cudaMalloc((void **)&out, size));

            CUDA_CHECK(cudaMemset((void *)in, 1, size));
            CUDA_CHECK(cudaMemset((void *)out, 1, size));
        }
        calc_kernel<<<nblocks, nthreads, 0, stream>>>(n, 1.0f, in, out);
        CUDA_CHECK(cudaGetLastError());
        return 0;
}

__global__ void dummy_kernel(double time, double clockrate)
{
    long long int start, stop;
    double usec;
    volatile int counter;

    start = clock64();
    do {
        stop = clock64();
    usec = ((double)(stop-start)*1000)/((double)clockrate); 
    counter = usec;
    } while(usec < time);
}

int gpu_launch_dummy_kernel(double time, double clockrate, cudaStream_t stream)
{
    dummy_kernel <<<1, 1, 0, stream>>>(time, clockrate);
    CUDA_CHECK(cudaGetLastError());
    return 0;
}
