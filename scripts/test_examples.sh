#!/usr/bin/env bash

if [ "$#" -ne 4 ]; then
    echo "Illegal parameters number: <MPI proc num> <GPU Buffers=0:1> <libmp prefix> <test name>"
    exit 1
fi

[ ! -e "hostfile" ] && { echo "ERROR: missing hostfile";  exit 1; }

NP=$1
GPUBUF=$2
PREFIX_LIBMP=$3
TEST_NAME=$4

[ ! -e $PREFIX_LIBMP/bin/$TEST_NAME ] && { echo "ERROR: test $PREFIX_LIBMP/bin/$TEST_NAME not found";  exit 1; }


export PATH=$PATH
# N.B. PREFIX_LIBMP/lib must be included in LD_LIBRARY_PATH
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH

OMPI_params="$OMPI_params --mca btl openib,self"
OMPI_params="$OMPI_params --mca btl_openib_want_cuda_gdr 1"
OMPI_params="$OMPI_params --mca btl_openib_warn_default_gid_prefix 0"
OMPI_params="$OMPI_params --mca btl_openib_cuda_rdma_limit 16777216"
OMPI_params="$OMPI_params --map-by node"
OMPI_params="$OMPI_params -x LD_LIBRARY_PATH -x PATH -x CUDA_PASCAL_FORCE_40_BIT=1"
OMPI_params="$OMPI_params -x MP_ENABLE_WARN=1 -x MP_ENABLE_DEBUG=0"
#Benchmarks & Examples
OMPI_params="$OMPI_params -x MP_BENCH_GPU_BUFFERS=$GPUBUF "
OMPI_params="$OMPI_params -x MP_BENCH_ENABLE_DEBUG -x MP_BENCH_ENABLE_VALIDATION -x MP_BENCH_KERNEL_TIME"
OMPI_params="$OMPI_params -x MP_BENCH_COMM_COMP_RATIO -x MP_BENCH_CALC_SIZE -x MP_BENCH_USE_CALC_SIZE"
OMPI_params="$OMPI_params -x MP_BENCH_STEPS_PER_BATCH -x MP_BENCH_BATCHES_INFLIGHT -x MP_ENABLE_UD"
OMPI_params="$OMPI_params -x MP_BENCH_SIZE -x MP_BENCH_USE_CALC_SIZE=0 -x MP_PINGPONG_TYPE=0"
#OMPI_params=""

GDR_COPY_LIB=$PREFIX/lib/libgdrapi.so
MVAPICH_params="$MVAPICH_params -genv MV2_USE_CUDA 1 -genv MV2_USE_GPUDIRECT 1 -genv MV2_GPUDIRECT_GDRCOPY_LIB $GDR_COPY_LIB -genv MV2_CUDA_IPC 0 "
MVAPICH_params="$MVAPICH_params -genv MV2_USE_SHARED_MEM 0 -genv MV2_SMP_USE_CMA 0 "
MVAPICH_params="$MVAPICH_params -genv MV2_CUDA_BLOCK_SIZE 16777216 -genv MV2_GPUDIRECT_LIMIT 16777216 -genv MV2_USE_GPUDIRECT_RECEIVE_LIMIT 16777216"
MVAPICH_params="$MVAPICH_params -genv LD_LIBRARY_PATH $LD_LIBRARY_PATH -genv PATH $PATH -genv CUDA_PASCAL_FORCE_40_BIT 1"
MVAPICH_params="$MVAPICH_params -genv MP_ENABLE_WARN 1 -genv MP_ENABLE_DEBUG 0"
#Benchmarks & Examples
MVAPICH_params="$MVAPICH_params -genv MP_BENCH_GPU_BUFFERS $GPUBUF "
MVAPICH_params="$MVAPICH_params -genv MP_BENCH_ENABLE_DEBUG 0 -genv MP_BENCH_ENABLE_VALIDATION 1"

#MVAPICH_params="$MVAPICH_params -genv MP_BENCH_KERNEL_TIME -genv MP_BENCH_COMM_COMP_RATIO -genv MP_BENCH_CALC_SIZE -genv MP_BENCH_USE_CALC_SIZE"
#MVAPICH_params="$MVAPICH_params -genv MP_BENCH_STEPS_PER_BATCH -genv MP_BENCH_BATCHES_INFLIGHT -genv MP_ENABLE_UD"
#MVAPICH_params="$MVAPICH_params -genv MP_BENCH_SIZE"

MVAPICH_params=""

set -x
$MPI_HOME/bin/mpirun $OMPI_params $MVAPICH_params -np $NP -hostfile hostfile $PREFIX_LIBMP/scripts/wrapper.sh $PREFIX_LIBMP/bin/$TEST_NAME

#-verbose
#nvprof -o nvprof-singlestream.%q{MV2_COMM_WORLD_LOCAL_RANK}.nvprof
