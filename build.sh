#!/bin/bash

[ ! -d config ] && mkdir -p config

[ ! -e configure ] && ./autogen.sh

[ ! -d build ] && mkdir build

cd build

#export PREFIX_LIBMP=$HOME/libmp

if [ ! -e Makefile ]; then
    echo "configuring..."
    $PREFIX_LIBMP/configure \
        --prefix=$PREFIX_LIBMP \
        --with-libibverbs=/usr \
        --with-cuda=$CUDA \
        --with-libgdsync=$PREFIX_LIBGDSYNC \
        --with-mpi=$MPI_HOME \
        --enable-tests
fi
#--with-libgdsync=$PREFIX_LIBMP \

make clean all install
