dnl Process this file with autoconf to produce a configure script.

AC_PREREQ(2.57)
AC_INIT(libmp, 2.0.0, gpudirect@github.com)
AC_CONFIG_SRCDIR([src/mp.c])
AC_CONFIG_AUX_DIR(config)
AC_CONFIG_HEADER(config.h)
AM_INIT_AUTOMAKE([1.10 foreign tar-ustar silent-rules subdir-objects])
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

AC_PROG_CC
AC_PROG_CXX
AC_GNU_SOURCE
AC_PROG_LN_S
AC_PROG_LIBTOOL

LT_INIT

AC_ARG_ENABLE(
    [tests],
    [AC_HELP_STRING([--enable-tests],
                   [Build test programs (default=no)])],
    [enable_tests=$enableval],
    [enable_tests=no])
AM_CONDITIONAL(TESTS_ENABLED, test x$enable_tests = xyes)

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

AC_ARG_WITH([libgdsync],
    AC_HELP_STRING([--with-libgdsync], [ Set path to libgdsync installation ]))
if test x$with_libgdsync = x || test x$with_libgdsync = xno; then
    want_libgdsync=no
else
    want_libgdsync=yes
    if test -d $with_libgdsync; then
        CPPFLAGS="$CPPFLAGS -I$with_libgdsync/include"
        LDFLAGS="$LDFLAGS -L$with_libgdsync/lib -L$with_libgdsync/lib64"
    fi
fi

AC_ARG_WITH([mpi],
    AC_HELP_STRING([--with-mpi], [ Set path to mpi installation ]))
if test x$with_mpi = x || test x$with_mpi = xno; then
    # assuming system location
    mpi_home=/usr
    MPICC=$with_home/bin/mpicc
    MPICXX=$with_home/bin/mpic++
else
    if test -d $with_mpi; then
        mpi_home=$with_mpi
        MPICC=${mpi_home}/bin/mpicc
        MPICXX=${mpi_home}/bin/mpic++
        CPPFLAGS="$CPPFLAGS -I${mpi_home}/include"
        LDFLAGS="$LDFLAGS -L${mpi_home}/lib -L${mpi_home}/lib64"
    else
        echo "MPI dir does not exist"
    fi
fi

dnl Specify CUDA Location
AC_ARG_WITH(cuda,
  AC_HELP_STRING([--with-cuda=CUDADIR], [ Specify CUDA installation directory (default: /usr/local/cuda)]),
  [ cuda_home=${withval} ],
  [ cuda_home=/usr/local/cuda ]
)

dnl Specify GPU Arch
AC_ARG_ENABLE(gpu-arch,
 AC_HELP_STRING([--enable-gpu-arch=arch], [ Set GPU arch: sm_20, sm_21, sm_30, sm_35, sm_50, sm_52 (default: sm_35)]),
 [ gpu_arch=${enableval} ],
 [ gpu_arch="sm_35" ]
)


dnl Checks for programs
AC_PROG_CC

dnl Checks for header files.
AC_HEADER_STDC

dnl Checks for Verbs support
AC_CHECK_LIB(ibverbs, ibv_get_device_list, [],
    AC_MSG_ERROR([ibv_get_device_list() not found.  libmp requires libibverbs.]))

AC_CHECK_LIB(gdsync, gds_query_param,
    AC_MSG_ERROR([gds_query_param not found. libmp requires libgdsync.]))

dnl Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_CHECK_SIZEOF(long)

dnl Output Substitutions
AC_MSG_NOTICE([Setting CUDA_PATH = ${cuda_home} ])
AC_SUBST( CUDA_PATH, [${cuda_home} ])

AC_MSG_NOTICE([Setting GPU_ARCH = ${gpu_arch} ])
AC_SUBST( GPU_ARCH, [${gpu_arch}] )

AC_MSG_NOTICE([Setting MPI_PATH = ${mpi_home} ])
AC_SUBST( MPI_PATH, [${mpi_home} ])
AC_SUBST( MPICC, [${MPICC} ])
AC_SUBST( MPICXX, [${MPICXX} ])

NVCCFLAGS="$NVCCFLAGS"
MPI_CFLAGS="$MPI_CFLAGS -I$MPI_HOME/include"
MPI_LDFLAGS="$MPI_LDFLAGS -L$MPI_HOME/lib"
CUDA_CFLAGS="$CUDA_CFLAGS -arch=${gpu_arch}"
CUDA_CFLAGS="$CUDA_CFLAGS -I$CUDA_PATH/include"
CUDA_LDFLAGS="-L$CUDA_INSTALL_PATH/lib64"
CUDA_LIBS="-lcuda -lcudart -lcufft"
NVCCFLAGS="$NVCCFLAGS $CPPFLAGS $MPI_CFLAGS $CUDA_CFLAGS"
NVCCLDFLAGS="$NVCCLDFLAGS $MPI_LDFLAGS $CUDA_LDFLAGS $CUDA_LIBS"
AC_SUBST(NVCC, [nvcc])
AC_SUBST(NVCCFLAGS)
AC_SUBST(NVCCLDFLAGS)

AC_CONFIG_FILES([Makefile libmp.spec examples/Makefile benchmarks/Makefile comm_benchmarks/Makefile])
AC_OUTPUT
