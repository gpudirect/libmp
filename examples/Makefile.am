CUDA_PATH = @CUDA_PATH@
MPI_PATH = @MPI_PATH@

#AM_CPPFLAGS = $AM_MAKEFLAGS
AM_CPPFLAGS = -D__STDC_FORMAT_MACROS
AM_CPPFLAGS += -I$(top_srcdir)/include
AM_CPPFLAGS += -I$(top_srcdir)/src

AM_LDFLAGS = -L$(top_srcdir)/lib $(top_builddir)/src/libmp.la

if HAVE_CUDA
AM_CPPFLAGS += -I$(CUDA_PATH)/include
AM_LDFLAGS += -L$(CUDA_PATH)/lib64 -L$(CUDA_PATH)/lib -lcuda -lcudart
endif

if HAVE_GDSYNC
AM_LDFLAGS += -libverbs -lgdsync -lgdrapi
endif

if HAVE_VERBS
AM_LDFLAGS += -libverbs
endif

if HAVE_MPI
AM_CPPFLAGS += -I$(MPI_PATH)/include
AM_LDFLAGS += -L$(MPI_PATH)/lib -lmpi
endif

bin_PROGRAMS = mp_pt2pt mp_onesided mp_sendrecv
if HAVE_GDSYNC
bin_PROGRAMS += mp_pt2pt_async
endif

noinst_PROGRAMS = 

CCLD=$(MPICXX)
CXXLD=$(MPICXX)

mp_pt2pt_SOURCES = mp_pt2pt.cc
mp_onesided_SOURCES = mp_onesided.cc
mp_sendrecv_SOURCES = mp_sendrecv.cc
if HAVE_GDSYNC
mp_pt2pt_async_SOURCES = mp_pt2pt_async.cc
endif

SUFFIXES= .cu

.cu.o:
	$(NVCC) $(NVCCFLAGS) $(AM_CPPFLAGS) -c -o $@ $<

.cu.lo:
	$(LIBTOOL) --tag=CC --mode=compile $(NVCC) $(NVCCFLAGS) $(AM_CPPFLAGS) -o $@ -c $< $(NVCCLDFLAGS)
