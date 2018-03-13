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

#set -x

use_perf=0
use_nvprof=0
use_gdb=0

exe=$1
shift
params=$*

extra_params=

#Assuming OpenMPI
lrank=$OMPI_COMM_WORLD_LOCAL_RANK
echo "hostname=${HOSTNAME}"
echo "lrank=$lrank"

case ${HOSTNAME} in
	#DGX-1 Topology GPU <--> HCA
	*dgx*)
	if (( $lrank > 4 )); then echo "too many ranks"; exit; fi
	hlrank=$(($lrank / 2)) # 0,1
	dlrank=$(($lrank * 2)) # 0,2,4,6
	#CUDA_VISIBLE_DEVICES=$dlrank
	USE_GPU=${dlrank}
	USE_CPU=${hlrank}
	HCA=mlx5_${lrank}
	MP_USE_IB_HCA=${HCA}
	OMPI_MCA_btl_openib_if_include=${HCA}
	;;

	*brdw0*) CUDA_VISIBLE_DEVICES=3; USE_CPU=0; MP_USE_IB_HCA=mlx5_0;;
	*brdw1*) CUDA_VISIBLE_DEVICES=0; USE_CPU=0; MP_USE_IB_HCA=mlx5_0;;
	*ivy2*) CUDA_VISIBLE_DEVICES=0; USE_CPU=0; MP_USE_IB_HCA=mlx5_0;;
	*ivy3*) CUDA_VISIBLE_DEVICES=0; USE_CPU=0; MP_USE_IB_HCA=mlx5_0;;
	*hsw0*) CUDA_VISIBLE_DEVICES=0; USE_CPU=0; MP_USE_IB_HCA=mlx5_0;;
	*hsw1*)                         USE_GPU=0; USE_CPU=0; MP_USE_IB_HCA=mlx5_0; 
	    ;;
esac

echo "# ${HOSTNAME}: picking GPU:$CUDA_VISIBLE_DEVICES/$USE_GPU CPU:$USE_CPU HCA:$MP_USE_IB_HCA" >&2
PATH=$PATH:$PWD

export \
	CUDA_VISIBLE_DEVICES USE_GPU USE_CPU MP_USE_IB_HCA OMPI_MCA_btl_openib_if_include \
	\
	COMM_ENABLE_DEBUG \
        COMM_USE_COMM 	  \
        COMM_USE_ASYNC_SA \
        COMM_USE_ASYNC_KI \
	\
        MP_ENABLE_DEBUG   \
        MP_ENABLE_WARN 	  \
        MP_EVENT_ASYNC    \
        MP_DBREC_ON_GPU   \
        MP_RX_CQ_ON_GPU   \
        MP_TX_CQ_ON_GPU   \
        \
        GDS_ENABLE_DEBUG \
        GDS_FLUSHER_TYPE \
        GDS_DISABLE_WRITE64 \
        GDS_SIMULATE_WRITE64 \
        GDS_DISABLE_INLINECOPY \
        GDS_DISABLE_WEAK_CONSISTENCY \
        GDS_DISABLE_MEMBAR \
        \
        USE_CALC_SIZE \
        KERNEL_TIME \
        MAX_SIZE \
        GPU_ENABLE_DEBUG \
        \
        MLX5_DEBUG_MASK \
        LD_LIBRARY_PATH PATH

set -x

if [ "$use_nvprof" != "0" ]; then
    now=$(date +%F-%T)
    inst=${lrank:-bho}
    #--profile-from-start off
    nvprof -o /tmp/\%h-${inst}-${now}.nvprof  --replay-mode disabled $exe $params
    mv -v /tmp/${HOSTNAME}-${inst}-${now}.nvprof .
elif [ "$use_perf" != "0" ]; then
    exec perf record -F 99 -o /tmp/$HOSTNAME.prof $exe $params
elif [ "$use_gdb" != "0" ]; then
    echo "command options are: $params"
    sleep 1
    exec gdb $exe
elif [ ! -z $USE_CPU ]; then
    echo "$HOSTNAME: binding to CPU $USE_CPU"
    echo "MP_USE_IB_HCA: $MP_USE_IB_HCA"
    numactl --cpunodebind=${USE_CPU} -l $exe $params $extra_params
#    numactl --physcpubind=${USE_CPU} -l $exe $params $extra_params
else
    $exe $params  $extra_params
# ) 2>&1 |tee bu-${HOSTNAME}.log
fi
