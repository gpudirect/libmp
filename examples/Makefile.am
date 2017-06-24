CUDA_PATH = @CUDA_PATH@
MPI_PATH = @MPI_PATH@

AM_CPPFLAGS  = -I$(CUDA_PATH)/include -I$(MPI_PATH)/include
AM_CPPFLAGS += -D__STDC_FORMAT_MACROS
AM_CPPFLAGS += -I$(top_srcdir)/include

AM_LDFLAGS   = -L$(CUDA_PATH)/lib64 -L$(CUDA_PATH)/lib $(top_builddir)/src/libmp.la -lgdsync -lgdrapi -lmpi -lcuda -lcudart

bin_PROGRAMS = mp_pt2pt
#mp_putget mp_sendrecv mp_sendrecv_kernel mp_sendrecv_stream

noinst_PROGRAMS = 

CCLD=$(MPICXX)
CXXLD=$(MPICXX)

mp_pt2pt_SOURCES = mp_pt2pt.cc

mp_putget_SOURCES = mp_putget.c

mp_sendrecv_SOURCES = mp_sendrecv.c

mp_sendrecv_stream_SOURCES = mp_sendrecv_stream.c

mp_sendrecv_kernel_SOURCES = mp_sendrecv_kernel.cu


SUFFIXES= .cu

.cu.o:
	$(NVCC) $(NVCCFLAGS) $(AM_CPPFLAGS) -c -o $@ $<

.cu.lo:
	$(LIBTOOL) --tag=CC --mode=compile $(NVCC) $(NVCCFLAGS) $(AM_CPPFLAGS) -o $@ -c $< $(NVCCLDFLAGS)
