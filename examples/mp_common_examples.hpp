#include <mp.hpp>
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

