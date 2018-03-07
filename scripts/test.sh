#!/usr/bin/env bash

if [[ $# -ne 2 ]]; then
    echo "Usage: ./test.sh <MPI ranks> <PATH/TO/EXECUTABLE and options>"
        exit 1
fi

NP=$1
TEST=$2
shift 2
PARAMS=$@

[[ -z $MPI_HOME ]] 	&& { echo "ERROR: MPI_HOME env var empy";	exit 1; }
[[ -z $NP ]] 		&& { echo "ERROR: NP input not specified";	exit 1; }
[[ -z $TEST ]] 		&& { echo "ERROR: TEST input not specified";	exit 1; }
[[ ! -e $TEST ]] 	&& { echo "ERROR: $TEST not found"; 		exit 1; }
[[ ! -e hostfile ]] 	&& { echo "ERROR: hostfile missing"; 		exit 1; }

export PATH=$PATH
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH

#Assuming OpenMPI here
OMPI_params="$OMPI_params --mca btl openib,self"
OMPI_params="$OMPI_params --mca btl_openib_want_cuda_gdr 1"
OMPI_params="$OMPI_params --mca btl_openib_warn_default_gid_prefix 0"

#set -x
#MLX5_DEBUG_MASK=65535
$MPI_HOME/bin/mpirun -verbose  $OMPI_params \
	-x COMM_ENABLE_DEBUG=0  \
        -x COMM_USE_COMM=1     	\
        -x COMM_USE_ASYNC_SA=1  \
        -x COMM_USE_ASYNC_KI=0 	\
	\
        -x MP_ENABLE_DEBUG=0 \
        -x MP_ENABLE_WARN=0 \
        -x MP_EVENT_ASYNC=0 \
        -x MP_DBREC_ON_GPU=0 \
        -x MP_RX_CQ_ON_GPU=0 \
        -x MP_TX_CQ_ON_GPU=0 \
        \
        -x GDS_ENABLE_DEBUG=0 \
        -x GDS_FLUSHER_TYPE=0 \
        -x GDS_DISABLE_WRITE64=0 \
        -x GDS_SIMULATE_WRITE64=0 \
        -x GDS_DISABLE_INLINECOPY=0 \
        -x GDS_DISABLE_WEAK_CONSISTENCY=0 \
        -x GDS_DISABLE_MEMBAR=0 \
        \
        -x USE_CALC_SIZE=1 \
        -x KERNEL_TIME=0 \
        -x GPU_ENABLE_DEBUG=0 \
        \
        -x MLX5_DEBUG_MASK=0 \
        -x LD_LIBRARY_PATH -x PATH -x CUDA_PASCAL_FORCE_40_BIT=1 \
        --map-by node -np $NP -hostfile hostfile ./wrapper.sh $TEST $PARAMS

# Example ./test.sh 2 ../bin/mp_pingpong

