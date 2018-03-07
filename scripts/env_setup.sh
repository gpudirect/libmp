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

# ======== PATH SETUP ========
LIBGDSYNC_PATH="$HOME/libgdsync"
MPI_PATH="/usr/mpi/gcc/openmpi-1.10.5a1"
LIBMP_PATH="$HOME/libmp"
CUDA_PATH="/usr/local/cuda-9.0"

# ======== MPI ========
if [ -d $MPI_PATH ]; then
    export MPI_HOME=$MPI_PATH
else
    echo "ERROR: cannot find OpenMPI in $MPI_PATH "
fi

echo "MPI_HOME=$MPI_HOME"
export MPI_NAME=openmpi
export MPI_BIN=$MPI_HOME/bin
export MPI_INCLUDE=$MPI_HOME/include
export MPI_LIB=$MPI_HOME/lib:$MPI_HOME/lib64
export PATH=$MPI_BIN:$PATH
export LD_LIBRARY_PATH=$MPI_LIB:${LD_LIBRARY_PATH}

# ======== LibMP ========
export PREFIX=$LIBMP_PATH
export LD_LIBRARY_PATH=$PREFIX/lib:${LD_LIBRARY_PATH}

# ======== CUDA ========
if [ ! -z "$CUDA" ]; then
	echo "WARNING: CUDA is already defined ($CUDA), overwriting it..."
fi

if [ -e $CUDA_PATH ]; then
	echo "loading $CUDA_PATH environment..."
	export CUDA=$CUDA_PATH
	export CUDA_PATH=$CUDA_PATH
	export CUDA_HOME=$CUDA
	export CUDA_ROOT=$CUDA
	export CUDA_BIN=$CUDA/bin
	export CUDA_LIB=$CUDA/lib64
	export CUDA_LIB32=$CUDA/lib
	export CUDA_INC_PATH=$CUDA/include
	export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:$CUDA_LIB:$CUDA_LIB32:$CUDA/jre/lib:$CUDA/extras/CUPTI/lib64:$CUDA/extras/CUPTI/lib:/usr/lib64
	export PATH=$CUDA_BIN:$PATH
	export INCLUDEPATH=$CUDA/include/CL:$CUDA/include
fi

CUDADRV=$CUDA
CUDADRVLIB=$CUDADRV/lib64 #/usr/lib64
CUDADRVINC=$CUDADRV/include

# TAGs are used in scripts
export CUDADRV_TAG=${CUDADRV##*/}
echo "CUDADRV_TAG=$CUDADRV_TAG"
export CUDATK_TAG=${CUDA##*/}
echo "CUDATK_TAG=$CUDATK_TAG"

CU_LDFLAGS=
CU_CPPFLAGS=
# compiler paths
if [ ! -z "$CUDADRV" ]; then
[ ! -d $CUDADRV ] && echo "CUDADRV does not exist"
[ -d $CUDADRVLIB ] && CU_LDFLAGS="-L$CUDADRVLIB $CU_LDFLAGS"
[ -d $CUDADRVINC ] && CU_CPPFLAGS="-I$CUDADRVINC $CU_CPPFLAGS"
fi

if [ ! -z $"CUDA_INC_PATH" ]; then
CU_CPPFLAGS="$CU_CPPFLAGS -I$CUDA_INC_PATH"
fi
if [ ! -z "$CUDA_LIB" ]; then
CU_LDFLAGS="$CU_LDFLAGS -L$CUDA_LIB"
fi
CU_LDFLAGS="$CU_LDFLAGS -L/usr/lib64"

# ======== LibGDSync ========
if [ ! -d $LIBGDSYNC_PATH/lib ]; then
echo "ERROR LibGDSync: $LIBGDSYNC_PATH does not exist"
break
fi
GDSYNC_LDFLAGS="-L$LIBGDSYNC_PATH/lib"
GDSYNC_CPPFLAGS="-I$LIBGDSYNC_PATH/include"
GDSYNCLIB=$LIBGDSYNC_PATH/lib
echo "INFO: installing/picking peersync stuff from $LIBGDSYNC_PATH"

export CUDADRV
export CU_CPPFLAGS CU_LDFLAGS
export GDSYNC GDSYNC_CPPFLAGS GDSYNC_LDFLAGS

if [ ! -z "${CUDADRVLIB}" ]; then
LD_LIBRARY_PATH=${CUDADRVLIB}:${LD_LIBRARY_PATH}
fi

export LD_LIBRARY_PATH


echo "CU_CPPFLAGS=$CU_CPPFLAGS"
echo "CU_LDFLAGS=$CU_LDFLAGS"
echo "LD_LIBRARY_PATH=$LD_LIBRARY_PATH"
