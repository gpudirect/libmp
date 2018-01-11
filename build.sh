#!/bin/bash

[ ! -d config ] && mkdir -p config

[ ! -e configure ] && ./autogen.sh

[ ! -d build ] && mkdir build

cd build

CUDA_PATH=$1
GDSYNC_PATH=$2
MPI_PATH=$3

if [[ -z $CUDA_PATH ]]; then
	CUDA_PATH="no"
fi

if [[ -z $GDSYNC_PATH ]]; then
	GDSYNC_PATH="no"
fi

if [[ -z $MPI_PATH ]]; then
	MPI_PATH="no"
fi

if [ ! -e Makefile ]; then
    echo "configuring..."

    $PREFIX/configure \
        --prefix=$PREFIX \
        --with-libibverbs=/usr \
        --with-cuda=$CUDA_PATH \
        --with-libgdsync=$GDSYNC_PATH \
        --with-mpi=$MPI_PATH \
        --enable-tests
fi

make clean all install
