CUDA_PATH = @CUDA_PATH@
MPI_PATH = @MPI_PATH@

AM_CPPFLAGS  = -I$(CUDA_PATH)/include -I$(MPI_PATH)/include
AM_CPPFLAGS += -D__STDC_FORMAT_MACROS
AM_CPPFLAGS += -I$(top_srcdir)/include

AM_LDFLAGS   = -L$(CUDA_PATH)/lib64 -L$(CUDA_PATH)/lib $(top_builddir)/src/libmp.la -lgdsync -lgdrapi -lmpi -lcuda -lcudart

bin_PROGRAMS = comm_pingpong

noinst_PROGRAMS = 

CCLD=$(MPICXX)
CXXLD=$(MPICXX)

comm_pingpong_SOURCES = comm_pingpong.cc comm.cc

SUFFIXES= .cu

.cu.o:
	$(NVCC) $(NVCCFLAGS) $(AM_CPPFLAGS) -c -o $@ $<

.cu.lo:
	$(LIBTOOL) --tag=CC --mode=compile $(NVCC) $(NVCCFLAGS) $(AM_CPPFLAGS) -o $@ -c $< $(NVCCLDFLAGS)
