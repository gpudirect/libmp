#!/usr/bin/env bash

if [ "$#" -ne 3 ]; then
    echo "Illegal parameters number: <MPI proc num> <GPU Buffers=0:1> </path/test/name>"
    exit 1
fi

[ ! -e "hostfile" ] 	&& { echo "ERROR: missing hostfile";  exit 1; }

NP=$1
GPUBUF=$2
TEST_NAME=$3

export PATH=$PATH
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH

OMPI_params="$OMPI_params --mca btl openib,self"
OMPI_params="$OMPI_params --mca btl_openib_want_cuda_gdr 1"
OMPI_params="$OMPI_params --mca btl_openib_warn_default_gid_prefix 0"
OMPI_params="$OMPI_params --mca btl_openib_cuda_rdma_limit 16777216"
OMPI_params="$OMPI_params --map-by node"
OMPI_params="$OMPI_params -x LD_LIBRARY_PATH -x PATH -x CUDA_PASCAL_FORCE_40_BIT=1 -x NCHUNK_X_BETHE=16 -x NGPU_X_BETHE=1 -x NCCL_SHM_DISABLE=1 -x NCCL_DEBUG=INFO"
OMPI_params="$OMPI_params -x MP_ENABLE_WARN=1 -x MP_ENABLE_DEBUG=0 "
OMPI_params="$OMPI_params -x MP_GPU_BUFFERS=$GPUBUF "
OMPI_params=""

MVAPICH_params="$MVAPICH_params -genv MV2_USE_CUDA 1 -genv MV2_USE_GPUDIRECT 1 -genv MV2_CUDA_IPC 0 "
MVAPICH_params="$MVAPICH_params -genv MV2_USE_SHARED_MEM 0 -genv MV2_SMP_USE_CMA 0 "
MVAPICH_params="$MVAPICH_params -genv LD_LIBRARY_PATH $LD_LIBRARY_PATH -genv PATH $PATH -genv CUDA_PASCAL_FORCE_40_BIT 1 -genv NCHUNK_X_BETHE 16 -genv NGPU_X_BETHE 1 -genv NCCL_SHM_DISABLE 1 -genv NCCL_DEBUG INFO"
MVAPICH_params="$MVAPICH_params -genv MV2_CUDA_BLOCK_SIZE 16777216 -genv MV2_GPUDIRECT_LIMIT 16777216 -genv MV2_USE_GPUDIRECT_RECEIVE_LIMIT 16777216"
MVAPICH_params="$MVAPICH_params -genv MP_ENABLE_WARN 1 -genv MP_ENABLE_DEBUG 0"
MVAPICH_params="$MVAPICH_params -genv MP_GPU_BUFFERS $GPUBUF "
#MVAPICH_params=""

set -x
$MPI_HOME/bin/mpirun $OMPI_params $MVAPICH_params -np $NP -hostfile hostfile $HOME/libmp/scripts/wrapper.sh $HOME/libmp/bin/$TEST_NAME

#-verbose
#nvprof -o nvprof-singlestream.%q{MV2_COMM_WORLD_LOCAL_RANK}.nvprof
