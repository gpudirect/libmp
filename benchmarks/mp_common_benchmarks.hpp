#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
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


#if defined(HAVE_CUDA) || defined(HAVE_GDSYNC)

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
//        printf("nblocks: %d, over_sub_factor: %d, gpu_num_sm: %d, nthreads: %d\n", 
//              nblocks, over_sub_factor, gpu_num_sm, nthreads);
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

#else
//NVTX Profiler Utility
#define PUSH_RANGE(name,cid)
#define POP_RANGE
#endif