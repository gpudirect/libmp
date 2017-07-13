CUDA_PATH = @CUDA_PATH@
MPI_PATH = @MPI_PATH@


AM_CPPFLAGS  = -I$(srcdir)
AM_CPPFLAGS += -I$(srcdir)/include
AM_CPPFLAGS += -I$(srcdir)/src
AM_CPPFLAGS += -D__STDC_FORMAT_MACROS
AM_LDFLAGS = -L$(top_srcdir)/lib

# ==== Define Configure ====
if HAVE_CUDA
AM_CPPFLAGS += -I$(CUDA_PATH)/include
AM_LDFLAGS += -L$(CUDA_PATH)/lib64 -L$(CUDA_PATH)/lib -lcuda -lcudart
endif

if HAVE_VERBS
AM_LDFLAGS += -libverbs
endif

if HAVE_MPI
AM_CPPFLAGS += -I$(MPI_PATH)/include
AM_LDFLAGS += -L$(MPI_PATH)/lib -lmpi
endif

if HAVE_GDSYNC
AM_LDFLAGS += -lgdsync -lgdrapi
endif

ACLOCAL_AMFLAGS = -Iconfig
AM_CFLAGS = -g -Wall

SRC_LIST = src/mp.cc src/mp_comm.cc 

if HAVE_GDSYNC
SRC_LIST += src/mp_comm_async.cc src/mp_comm_kernel.cc
endif

SRC_LIST += src/oob.cc src/oob_mpi.cc src/tl.cc

if HAVE_GDSYNC

SRC_LIST += src/tl_verbs_async.cc

else

if HAVE_VERBS

SRC_LIST += src/tl_verbs.cc

else

SRC_LIST += src/tl_psm2.cc

endif

endif

lib_LTLIBRARIES = src/libmp.la

EXTRA_DIST = autogen.sh

include_HEADERS = src/mp.hpp src/mp_comm_kernel.cuh
noinst_HEADERS = src/mp_common.hpp src/mp_external.hpp src/oob.hpp src/tl.hpp src/tl_verbs.hpp src/tl_verbs_common.hpp src/tl_verbs_types_async.hpp src/tl_psm2.hpp

#libmpincludedir = include/$(includedir)/mp
#libmpinclude_HEADERS = include/mp/device.cuh

src_libmp_la_CFLAGS = $(AM_CFLAGS)
src_libmp_la_SOURCES = $(SRC_LIST)
src_libmp_la_LDFLAGS = -version-info 3:0:0


SUBDIRS = . examples benchmarks