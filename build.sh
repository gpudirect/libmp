#!/bin/bash

[ ! -d config ] && mkdir -p config

[ ! -e configure ] && ./autogen.sh

[ ! -d build ] && mkdir build

cd build

CUDA_PATH=$1
GDSYNC_PATH=$2
MPI_ENABLE=$3

if [[ -z $CUDA_PATH ]]; then
	CUDA_PATH="no"
fi

if [[ -z $GDSYNC_PATH ]]; then
	GDSYNC_PATH="no"
fi

if ( [ -z $MPI_ENABLE ] || [ ! -d $MPI_ENABLE ] ); then
	echo "ERROR: MPI not found ($MPI_ENABLE)! You can't build LibMP without MPI";
	exit 1;
fi

if [ ! -e Makefile ]; then
    echo "configuring..."

    $PREFIX/configure \
        --prefix=$PREFIX \
        --with-libibverbs=/usr \
        --with-cuda=$CUDA_PATH \
        --with-libgdsync=$GDSYNC_PATH \
        --with-mpi=$MPI_ENABLE \
        --enable-tests
fi

make clean all install
