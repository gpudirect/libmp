#!/bin/bash

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
    *dgx*)
	# let's pick:
	# GPU #0,2,4,6
	# HCA #0,1,2,3
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

    *ivy2*) CUDA_VISIBLE_DEVICES=0; USE_CPU=0; MP_USE_IB_HCA=mlx5_0;;
    *ivy3*) CUDA_VISIBLE_DEVICES=0; USE_CPU=0; MP_USE_IB_HCA=mlx5_0;;
    *hsw0*) CUDA_VISIBLE_DEVICES=0; USE_CPU=0; MP_USE_IB_HCA=mlx5_0;;
    *hsw1*)                         USE_GPU=0; USE_CPU=0; MP_USE_IB_HCA=mlx5_0; 
            ;;
esac

echo "# ${HOSTNAME}: picking GPU:$CUDA_VISIBLE_DEVICES/$USE_GPU CPU:$USE_CPU HCA:$MP_USE_IB_HCA" >&2
PATH=$PATH:$PWD

export \

	COMM_ENABLE_DEBUG= \
        COMM_USE_COMM=1   	\
        COMM_USE_ASYNC_SA= \
        COMM_USE_ASYNC_KI=	\
	\
        MP_ENABLE_DEBUG \
        MP_ENABLE_WARN \
        MP_EVENT_ASYNC \
        MP_DBREC_ON_GPU \
        MP_RX_CQ_ON_GPU \
        MP_TX_CQ_ON_GPU \
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
        GPU_ENABLE_DEBUG \
        \
        MLX5_DEBUG_MASK \
        LD_LIBRARY_PATH PATH CUDA_PASCAL_FORCE_40_BIT \
        PATH LD_LIBRARY_PATH
        
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
