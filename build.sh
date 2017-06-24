#!/bin/bash

[ ! -d config ] && mkdir -p config

[ ! -e configure ] && ./autogen.sh

[ ! -d build ] && mkdir build

cd build

export PREFIX=$HOME/libmp

if [ ! -e Makefile ]; then
    echo "configuring..."
    ../configure \
        --prefix=$PREFIX \
        --with-libgdsync=$PREFIX \
        --with-cuda=$CUDA \
        --with-mpi=$MPI_HOME \
        --enable-tests
fi
#no more needed: now use default libibverbs
#--with-libibverbs=$PREFIX \

make clean all install

:<<COMMENT
#Should not be necessary
AC_ARG_WITH([libibverbs],
    AC_HELP_STRING([--with-libibverbs], [ Set path to libibverbs installation ]))
if test x$with_libibverbs = x || test x$with_libibverbs = xno; then
    want_libibverbs=no
else
    want_libibverbs=yes
    if test -d $with_libibverbs; then
        CPPFLAGS="$CPPFLAGS -I$with_libibverbs/include"
        LDFLAGS="$LDFLAGS -L$with_libibverbs/lib -L$with_libibverbs/lib64"
    fi
fi
COMMENT