#!/bin/bash

function err {
    echo $* >&2
    exit
}

self=$0
path=${self%/*}

if [ -z $path ]; then path='.'; fi

if [ -z $CUDADRV_TAG ]; then echo "error: CUDADRV_TAG undefined"; exit 1; fi
if [ -z $CUDATK_TAG ]; then echo "error: CUDATK_TAG undefined"; exit 1; fi

srcdir=$path/..
scriptdir=$path
results=$srcdir/../outputs
now=$(date +%F-%T)
tag=allopt
odir=$results/${CUDATK_TAG}/${CUDADRV_TAG}/${tag}/multinode/${now}
run=$scriptdir/run.sh
wrapper=$scriptdir/wrapper.sh

[ -e $odir ] && { echo "renaming $odir -> $odir.old"; mv -f $odir ${odir}.old; }
mkdir -p $odir

[ -e $run ] || { echo "script $run is missing"; exit 1; }
[ -e $wrapper ] || { echo "script $wrapper is missing"; exit 1; }

mpdir=$PREFIX/bin

if [ -d $mpdir ]; then

    NP=2
    echo
    echo libmp functionality tests
    echo using NP=$NP
    echo

    $run -o $odir/mp_putget          -n $NP $wrapper $mpdir/mp_putget             || err "got error $?"
    $run -o $odir/mp_sendrecv        -n $NP $wrapper $mpdir/mp_sendrecv           || err "got error $?"
    $run -o $odir/mp_sendrecv_stream -n $NP $wrapper $mpdir/mp_sendrecv_stream    || err "got error $?"
    $run -o $odir/mp_sendrecv_kernel -n $NP $wrapper $mpdir/mp_sendrecv_kernel    || err "got error $?"

    NP=2
    echo
    echo libmp benchmarks
    echo using NP=$NP
    echo
    
    $run -o $odir/mp_pingpong_kernel_stream_latency     -n $NP $wrapper $mpdir/mp_pingpong_kernel_stream_latency        || err "got error $?"
    $run -o $odir/mp_pingpong_kernel_stream             -n $NP $wrapper $mpdir/mp_pingpong_kernel_stream                || err "got error $?"
    $run -o $odir/mp_pingpong_kernel                    -n $NP $wrapper $mpdir/mp_pingpong_kernel                       || err "got error $?"
    $run -o $odir/mp_producer_consumer_kernel_stream    -n $NP $wrapper $mpdir/mp_producer_consumer_kernel_stream       || err "got error $?"
    $run -o $odir/mp_sendrecv_kernel_stream             -n $NP $wrapper $mpdir/mp_sendrecv_kernel_stream                || err "got error $?"
    $run -o $odir/mp_pingpong_kernel_stream_mpi         -n $NP $wrapper $mpdir/mp_pingpong_kernel_stream_mpi            || err "got error $?"
    $run -o $odir/mp_pingpong_kernel_stream_latency_mpi -n $NP $wrapper $mpdir/mp_pingpong_kernel_stream_latency_mpi    || err "got error $?"

else
    echo "missing dir $mpdir"
    exit
fi
