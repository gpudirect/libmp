CUDA_PATH = @CUDA_PATH@
MPI_PATH = @MPI_PATH@

#AM_CPPFLAGS = $AM_MAKEFLAGS
AM_CPPFLAGS = -D__STDC_FORMAT_MACROS
AM_CPPFLAGS += -I$(top_srcdir)/include
AM_CPPFLAGS += -I$(top_srcdir)/src

AM_LDFLAGS = -L$(top_srcdir)/lib $(top_builddir)/src/libmp.la

# ==== Define Configure ====
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

# ==== BIN ====
bin_PROGRAMS = mp_sendrecv mp_putget

if HAVE_GDSYNC
bin_PROGRAMS += mp_sendrecv_async mp_putget_async
endif

noinst_PROGRAMS = 

CCLD=$(MPICXX)
CXXLD=$(MPICXX)

# ==== SRC ====
mp_sendrecv_SOURCES = mp_sendrecv.cc
mp_putget_SOURCES = mp_putget.cc

if HAVE_GDSYNC
mp_sendrecv_async_SOURCES = mp_sendrecv_async.cc
mp_putget_async_SOURCES = mp_putget_async.cc
endif

SUFFIXES= .cu

.cu.o:
	$(NVCC) $(NVCCFLAGS) $(AM_CPPFLAGS) -c -o $@ $<

.cu.lo:
	$(LIBTOOL) --tag=CC --mode=compile $(NVCC) $(NVCCFLAGS) $(AM_CPPFLAGS) -o $@ -c $< $(NVCCLDFLAGS)
