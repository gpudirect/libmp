#!/bin/bash

self=$0
path=${self%/*}

if [ -z $path ]; then path='.'; fi

hostfile=$path/hostfile
nprocs=0
vexps=
ofile=1
efile=2

while [ "$1" != "${1##[-+]}" ]; do
    case $1 in
	-n)
	    nprocs=$2
	    gotbase="yes"
	    shift 2
	    ;;
	-x)
	    var=$2
            echo "$var"
            eval "export $var"
            vexps="$vexps -x $var"
	    shift 2
	    ;;
        -o)
            ofile=$2.stdout
            efile=$2.stderr
            shift 2
            ;;
#	--user)
#	    user=$2
#	    shift 2
#	    ;;
#	--user=?*)
#	user=${1#--user=}
#	shift
#	;;
	*)     echo $"$0: Usage: run.sh -n nprocs {program} {params}"
	    return 1;;
    esac
done

[ 0 == $nprocs ] && { echo "ERROR: set number of MPI procs" >&2; exit 1; }

[ ! -e $hostfile ] && { echo "ERROR: missing $hostfile" >&2; exit 1; }

exe=$1
shift 1
parms=$*
#echo "##INFO: nprocs=$nprocs $exe $parms"
#echo $vexps
#exit

extra_params=
#    -x MLX5_DEBUG_MASK=65535 \
#    -verbose -verbose \
#extra_params="$extra_params --mca btl ^openib"
#extra_params="$extra_params --mca ess env"
#extra_params="$extra_params --mca btl openib,sm,self"
#extra_params="$extra_params --mca btl openib,smcuda,self"
extra_params="$extra_params --mca btl openib,self"
extra_params="$extra_params --mca btl_openib_want_cuda_gdr 1"
extra_params="$extra_params --mca btl_openib_warn_default_gid_prefix 0"
#extra_params="$extra_params --tag-output"

echo "# running NP=$nprocs, $exe $parms, stdout:$ofile"

# save stdout/err to 3/4
if [ $ofile != "1" ]; then
    exec 3>&1
    exec 4>&2
    exec 1>$ofile 2>$efile
fi
#set -x
$MPI_BIN/mpirun \
    $extra_params \
    -x QUDA_ENABLE_P2P \
    -x QUDA_RESOURCE_PATH \
    -x QUDA_USE_COMM_ASYNC_STREAM \
    -x QUDA_USE_COMM_ASYNC_PREPARED \
    -x QUDA_ASYNC_ENABLE_DEBUG \
    -x GDS_ENABLE_DEBUG \
    -x GDS_ENABLE_DUMP_MEMOPS \
    -x GDS_DISABLE_WRITE64 \
    -x GDS_SIMULATE_WRITE64 \
    -x GDS_DISABLE_INLINECOPY \
    -x GDS_DISABLE_MEMBAR \
    -x GDS_DISABLE_WEAK_CONSISTENCY \
    -x MP_ENABLE_DEBUG \
    -x MP_ENABLE_WARN \
    -x MP_EVENT_ASYNC \
    -x MP_DBREC_ON_GPU \
    -x MP_TX_CQ_ON_GPU \
    -x MP_RX_CQ_ON_GPU \
    -x MP_ENABLE_IPC \
    -x MP_USE_IB_HCA \
    -x CUDA_VISIBLE_DEVICES \
    -x LD_LIBRARY_PATH \
    -x PATH \
    -x SIZE \
    -x MAX_SIZE \
    -x KERNEL_TIME \
    -x CALC_SIZE \
    -x COMM_COMP_RATIO \
    -x USE_SINGLE_STREAM \
    -x USE_GPU_ASYNC \
    -x OMP_NUM_THREADS \
    -x COMM_USE_ASYNC \
    -x COMM_USE_COMM \
    -x COMM_USE_GPU_COMM \
    -x COMM_USE_GDRDMA \
    -x HPGMG_ENABLE_DEBUG \
    -x MLX5_DEBUG_MASK \
    -x PX -x PY -x USE_GPU_COMM_BUFFERS -x USE_MPI \
    --map-by node \
    -hostfile $hostfile \
    -np $nprocs \
    $exe $parms

rc=$?

if [ $ofile != "1" ]; then
    #restore back to stdout/err
    exec 1>&3
    exec 2>&4
fi

if [ "$rc" -ne 0 ]; then
    echo "# got error $rc, captured output to:" >&2
    echo "# stdout $ofile" >&2
    echo "# stderr $efile" >&2
elif [ $ofile != "1" ]; then
    if fgrep -i 'error' $ofile >/dev/null; then echo "WARNING: error string deteted on $ofile"; fi
    if fgrep -i 'error' $efile >/dev/null; then echo "WARNING: error string deteted on $efile"; fi
fi
