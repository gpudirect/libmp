#!/usr/bin/env bash

# Copyright (c) 2011-2018, NVIDIA CORPORATION. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#  * Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
#  * Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#  * Neither the name of NVIDIA CORPORATION nor the names of its
#    contributors may be used to endorse or promote products derived
#    from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
# EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
# PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
# CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
# EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
# PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
# PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
# OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

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
        -x USE_CALC_SIZE=0 \
        -x KERNEL_TIME=0 \
        -x MAX_SIZE=131072 \
        -x GPU_ENABLE_DEBUG=0 \
        \
        -x MLX5_DEBUG_MASK=0 \
        -x LD_LIBRARY_PATH -x PATH \
        --map-by node -np $NP -hostfile hostfile ./wrapper.sh $TEST $PARAMS

# Example ./test.sh 2 ../bin/mp_pingpong

