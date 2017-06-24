CUDA_PATH = @CUDA_PATH@
MPI_PATH = @MPI_PATH@

AM_CPPFLAGS  = -I$(srcdir)
AM_CPPFLAGS += -I$(srcdir)/include
AM_CPPFLAGS += -I$(srcdir)/src
AM_CPPFLAGS += -I$(CUDA_PATH)/include
AM_CPPFLAGS += -D__STDC_FORMAT_MACROS

AM_LDFLAGS   = -L$(CUDA_PATH)/lib64

lib_LTLIBRARIES = src/libmp.la

ACLOCAL_AMFLAGS = -Iconfig
AM_CFLAGS = -g -Wall

EXTRA_DIST = autogen.sh

include_HEADERS = src/mp.hpp
#libmpincludedir = include/$(includedir)/mp
#libmpinclude_HEADERS = include/mp/device.cuh

src_libmp_la_CFLAGS = $(AM_CFLAGS)

src_libmp_la_SOURCES = src/mp.cc src/mp_comm.cc src/mp_comm_async.cc src/oob.cc src/oob_mpi.cc src/tl.cc src/tl_verbs.cc src/tl_verbs_async.cc

src_libmp_la_LDFLAGS = -version-info 3:0:0

noinst_HEADERS = src/common.hpp src/oob.hpp src/tl.hpp src/tl_verbs.hpp src/tl_verbs_common.hpp src/tl_verbs_types_async.hpp


SUBDIRS = . examples benchmarks comm_benchmarks
