#!/bin/bash

[ ! -d config ] && mkdir -p config

[ ! -e configure ] && ./autogen.sh

[ ! -d build ] && mkdir build

mkdir $PREFIX/bin
mkdir $PREFIX/include
mkdir $PREFIX/lib

cd build


if [ ! -e Makefile ]; then
    echo "configuring..."
    ../configure \
        --prefix=$PREFIX \
        --with-libibverbs=$PREFIX \
        --with-libgdsync=$PREFIX \
        --with-cuda=$CUDA \
        --with-mpi=$MPI_HOME \
        --enable-gpu-arch=sm_35 \
        --enable-tests
fi

make clean all install
