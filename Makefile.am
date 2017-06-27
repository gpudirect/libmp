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

include_HEADERS = include/mp.h
libmpincludedir = $(includedir)/mp
libmpinclude_HEADERS = include/mp/device.cuh

src_libmp_la_CFLAGS = $(AM_CFLAGS)
src_libmp_la_SOURCES = src/mp.c src/mp_peersync.c src/mp_device.cpp src/desc_queue.cpp
src_libmp_la_LDFLAGS = -version-info 2:0:0

noinst_HEADERS = src/mp_internal.h src/archutils.h


SUBDIRS = . examples benchmarks comm_benchmarks
