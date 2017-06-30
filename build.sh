#!/bin/bash

[ ! -d config ] && mkdir -p config

[ ! -e configure ] && ./autogen.sh

[ ! -d build ] && mkdir build

cd build

#export PREFIX=$HOME/libmp

if [ ! -e Makefile ]; then
    echo "configuring..."
    $PREFIX/configure \
        --prefix=$PREFIX \
        --with-libibverbs=/usr \
        --with-cuda=$CUDA \
        --with-libgdsync=$PREFIX \
        --with-mpi=$MPI_HOME \
        --enable-tests
fi
#--with-libgdsync=$PREFIX \

make clean all install