CUDA_PATH = @CUDA_PATH@
MPI_PATH = @MPI_PATH@

AM_CPPFLAGS  = -I$(CUDA_PATH)/include -I$(MPI_PATH)/include
AM_CPPFLAGS += -D__STDC_FORMAT_MACROS
AM_CPPFLAGS += -I$(top_srcdir)/include

AM_LDFLAGS   = $(LDFLAGS) -L$(CUDA_PATH)/lib64 -L$(CUDA_PATH)/lib $(top_builddir)/src/libmp.la -lgdsync -lgdrapi -lmpi -lcuda -lcudart

bin_PROGRAMS = mp_sendrecv_kernel_stream \
	mp_pingpong_kernel_stream \
	mp_pingpong_kernel_stream_mpi \
	mp_producer_consumer_kernel_stream \
	mp_pingpong_kernel_stream_latency \
	mp_pingpong_kernel_stream_latency_mpi \
	mp_pingpong_kernel

noinst_PROGRAMS = 

CCLD=$(MPICXX)
CXXLD=$(MPICXX)

LINK = $(LIBTOOL) --tag=CXX --mode=link $(CXXLD) -o $@ $(AM_LDFLAGS) $(AM_LIBS)

mp_sendrecv_kernel_stream_SOURCES = mp_sendrecv_kernel_stream.cu
mp_pingpong_kernel_stream_SOURCES = mp_pingpong_kernel_stream.cu
mp_pingpong_kernel_stream_mpi_SOURCES = mp_pingpong_kernel_stream_mpi.cu
mp_producer_consumer_kernel_stream_SOURCES = mp_producer_consumer_kernel_stream.cu
mp_pingpong_kernel_stream_latency_SOURCES = mp_pingpong_kernel_stream_latency.cu
mp_pingpong_kernel_stream_latency_mpi_SOURCES = mp_pingpong_kernel_stream_latency_mpi.cu
mp_pingpong_kernel_SOURCES = mp_pingpong_kernel.cu


SUFFIXES= .cu

.cu.o:
	$(NVCC) $(NVCCFLAGS) $(AM_CPPFLAGS) -c -o $@ $<

.cu.lo:
	$(LIBTOOL) --tag=CC --mode=compile $(NVCC) $(NVCCFLAGS) $(AM_CPPFLAGS) -o $@ -c $< $(NVCCLDFLAGS)
