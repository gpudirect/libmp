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
