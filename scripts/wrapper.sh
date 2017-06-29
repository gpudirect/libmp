#!/bin/bash

#set -x

use_perf=0
use_nvprof=0
use_gdb=0

exe=$1
shift
params=$*

extra_params=
lrank=$MV2_COMM_WORLD_LOCAL_RANK
mpi_type=1
if [[ -z "$lrank" ]]; then
    lrank=$MV2_COMM_WORLD_LOCAL_RANK
    mpi_type=2
fi
PATH=$PATH:$PWD

case ${HOSTNAME} in
    *sdgx*)
    # let's pick:
    # GPU #0,2,4,6
    # HCA #0,1,2,3
    if (( $lrank > 4 )); then echo "too many ranks"; exit; fi
    #if (( $lrank == 0 )); then hlrank=1; dlrank=4; HCA=mlx5_2; fi
    #if (( $lrank == 1 )); then hlrank=1; dlrank=6; HCA=mlx5_3; fi 
    hlrank=$(($lrank / 2)) # 0,1
    dlrank=$(($lrank * 2)) # 0,2,4,6
    HCA=mlx5_${lrank}
    #CUDA_VISIBLE_DEVICES=$dlrank
    USE_CPU=${hlrank}
    MP_USE_GPU=${dlrank}
    MV2_IBA_HCA=${HCA}
    VERBS_IB_HCA=${HCA}
    OMPI_MCA_btl_openib_if_include=${HCA}
    ;;

    *ivy*)
        hlrank=0
        dlrank=0
        MP_USE_GPU=0
        USE_CPU=0
        HCA=mlx5_0
        MP_USE_IB_HCA=${HCA}
        OMPI_MCA_btl_openib_if_include=${HCA}
    ;;
esac

#ulimit -c 100

#CUDA ENV VARS
export CUDA_VISIBLE_DEVICES CUDA_ERROR_LEVEL CUDA_ERROR_FILE CUDA_FILE_LEVEL CUDA_PASCAL_FORCE_40_BIT

#GDS ENV VARS
export \
    GDS_DISABLE_WRITE64 GDS_DISABLE_INLINECOPY GDS_DISABLE_MEMBAR \
    GDS_DISABLE_WEAK_CONSISTENCY GDS_SIMULATE_WRITE64 \
    GDS_ENABLE_DUMP_MEMOPS GDS_ENABLE_DEBUG

#IB Verbs ENV VARS
export  VERBS_IB_HCA \
        VERBS_ASYNC_EVENT_ASYNC VERBS_ASYNC_RX_CQ_ON_GPU VERBS_ASYNC_TX_CQ_ON_GPU VERBS_ASYNC_DBREC_ON_GPU

#MP ENV VARS
export \
    MP_USE_IB_HCA MP_USE_GPU \
    MP_ENABLE_DEBUG MP_ENABLE_WARN GDS_ENABLE_DEBUG \
    MP_DBREC_ON_GPU MP_RX_CQ_ON_GPU MP_TX_CQ_ON_GPU \
    MP_ENABLE_IPC MP_EVENT_ASYNC 

#MP Benchmarks & Examples
export \
    MP_BENCH_ENABLE_VALIDATION MP_BENCH_ENABLE_DEBUG MP_BENCH_KERNEL_TIME \
    MP_BENCH_COMM_COMP_RATIO MP_BENCH_CALC_SIZE MP_BENCH_USE_CALC_SIZE \
    MP_BENCH_STEPS_PER_BATCH MP_BENCH_BATCHES_INFLIGHT MP_BENCH_SIZE MP_ENABLE_UD \
    MP_BENCH_GPU_BUFFERS


if [[ $mpi_type == 1 ]]; then
    #MPI - MVAPICH
    export \
        MV2_IBA_HCA MV2_USE_CUDA MV2_SMP_USE_CMA MV2_CUDA_IPC MV2_USE_SHARED_MEM \
        MV2_USE_GPUDIRECT MV2_USE_GPUDIRECT_GDRCOPY MV2_GPUDIRECT_GDRCOPY_LIB MV2_GPUDIRECT_LIMIT MV2_USE_GPUDIRECT_RECEIVE_LIMIT \
        MV2_CUDA_BLOCK_SIZE                     \
        MV2_DEBUG_SHOW_BACKTRACE
        
else
    #MPI - OpenMPI
    export OMPI_MCA_btl_openib_if_include
fi

#Applications, Examples and Benchmarks
export \
    COMM_USE_GDRDMA COMM_USE_COMM COMM_USE_ASYNC COMM_USE_GPU_COMM OMP_NUM_THREADS \
    ENABLE_DEBUG_MSG DEBUGGER MLX5_DEBUG_MASK \
    PATH LD_LIBRARY_PATH
#    MLX5_DEBUG_MASK=65535 \


echo "# ${HOSTNAME}: MPI Type: $mpi_type, Local Rank: $lrank GPU:$CUDA_VISIBLE_DEVICES/$MP_USE_GPU CPU:$USE_CPU HCA:$MV2_IBA_HCA/$OMPI_MCA_btl_openib_if_include" >&2

if [ ! -z $USE_CPU ]; then
    set -x
    numactl --cpunodebind=${USE_CPU} -l $exe $params $extra_params
else
   set -x
    $exe $params  $extra_params
fi
