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
AM_LDFLAGS += -lgdsync
endif

if HAVE_VERBS
AM_LDFLAGS += -libverbs -lgdrapi
endif

if HAVE_MPI
AM_CPPFLAGS += -I$(MPI_PATH)/include
AM_LDFLAGS += -L$(MPI_PATH)/lib -lmpi
endif

# ==== BIN ====
bin_PROGRAMS = mp_pingpong_kernel_stream_latency_mpi

if HAVE_GDSYNC
#bin_PROGRAMS += benchmarks_kernels
endif

noinst_PROGRAMS = 

CCLD=$(MPICXX)
CXXLD=$(MPICXX)

# ==== SRC ====
mp_pingpong_kernel_stream_latency_mpi_SOURCES = mp_pingpong_kernel_stream_latency_mpi.cc

if HAVE_CUDA
mp_pingpong_kernel_stream_latency_mpi_SOURCES += benchmarks_kernels.cu
endif

SUFFIXES= .cu

.cu.o:
	$(NVCC) $(NVCCFLAGS) $(AM_CPPFLAGS) -c -o $@ $<

.cu.lo:
	$(LIBTOOL) --tag=CC --mode=compile $(NVCC) $(NVCCFLAGS) $(AM_CPPFLAGS) -o $@ -c $< $(NVCCLDFLAGS) $(AM_LDFLAGS)
