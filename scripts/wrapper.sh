#!/bin/bash

#set -x

use_perf=0
use_nvprof=0
use_gdb=0

exe=$1
shift
params=$*

extra_params=
lrank=$OMPI_COMM_WORLD_LOCAL_RANK
echo "hostname=${HOSTNAME}"
echo "lrank=$lrank"

case ${HOSTNAME} in
    *sdgx*)
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

    *ivy0*) CUDA_VISIBLE_DEVICES=1; USE_CPU=0; MP_USE_IB_HCA=mlx5_0;;
    *ivy1*) CUDA_VISIBLE_DEVICES=0; USE_CPU=0; MP_USE_IB_HCA=mlx5_0;;
    *ivy2*) CUDA_VISIBLE_DEVICES=0; USE_CPU=0; MP_USE_IB_HCA=mlx5_0;;
    *ivy3*) CUDA_VISIBLE_DEVICES=0; USE_CPU=0; MP_USE_IB_HCA=mlx5_0;;
    *hsw0*) CUDA_VISIBLE_DEVICES=0; USE_CPU=0; MP_USE_IB_HCA=mlx5_0;;
    *hsw1*)                         USE_GPU=0; USE_CPU=0; MP_USE_IB_HCA=mlx5_0; 
            # used by mp_pinpong_kernel_stream
            #extra_params="-W 1"; 
            # not implemented on OFED 3.2... not true!
            #MP_DBREC_ON_GPU=1
            #MP_RX_CQ_ON_GPU=1
            #MP_TX_CQ_ON_GPU=1
            #GDS_ENABLE_DEBUG=1
            #MP_ENABLE_DEBUG=1
            #ENABLE_DEBUG_MSG=1
            #CUDA_ERROR_LEVEL=100
            #CUDA_FILE_LEVEL=100
            #CUDA_ERROR_FILE=cuda.log
            #CUDA_PASCAL_FORCE_40_BIT=1
            #CUDA_VISIBLE_DEVICES="0,1"
            ;;
esac

echo "# ${HOSTNAME}: picking GPU:$CUDA_VISIBLE_DEVICES/$USE_GPU CPU:$USE_CPU HCA:$MP_USE_IB_HCA" >&2
#ulimit -c 100

PATH=$PATH:$PWD
#echo "PATH=$PATH"
#echo "LD_LIBRARY_PATH=$LD_LIBRARY_PATH"

export \
    QUDA_ENABLE_P2P \
    QUDA_RESOURCE_PATH \
    QUDA_USE_COMM_ASYNC_STREAM \
    QUDA_USE_COMM_ASYNC_PREPARED \
    QUDA_ASYNC_ENABLE_DEBUG \
    ENABLE_DEBUG_MSG \
    CUDA_VISIBLE_DEVICES CUDA_ERROR_LEVEL CUDA_ERROR_FILE CUDA_FILE_LEVEL CUDA_PASCAL_FORCE_40_BIT \
    MP_USE_IB_HCA USE_IB_HCA USE_CPU USE_GPU \
    USE_SINGLE_STREAM USE_GPU_ASYNC \
    MP_ENABLE_DEBUG MP_ENABLE_WARN GDS_ENABLE_DEBUG \
    MP_DBREC_ON_GPU MP_RX_CQ_ON_GPU MP_TX_CQ_ON_GPU \
    MP_ENABLE_IPC MP_EVENT_ASYNC \
    GDS_DISABLE_WRITE64 GDS_DISABLE_INLINECOPY GDS_DISABLE_MEMBAR \
    GDS_DISABLE_WEAK_CONSISTENCY GDS_SIMULATE_WRITE64 \
    COMM_USE_GDRDMA COMM_USE_COMM COMM_USE_ASYNC COMM_USE_GPU_COMM OMP_NUM_THREADS \
    DEBUGGER \
    MLX5_DEBUG_MASK \
    OMPI_MCA_btl_openib_if_include \
    GDS_ENABLE_DUMP_MEMOPS \
    PX PY USE_GPU_COMM_BUFFERS USE_MPI 
#    MLX5_DEBUG_MASK=65535 \

    
#echo $LD_LIBRARY_PATH
#ldd $exe
#unset USE_CPU
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
