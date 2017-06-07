#use_nccl:=1
#use_gdr:=1
#use_singlestream:=1

CC=mpic++
LD=mpic++ 
#nvcc
NVCC=nvcc
CPPFLAGS=-I. -I${MPI_HOME}/include -DHAVE_VERBS
# -DMACOSX
CFLAGS=-O2 -g
LDFLAGS=-L${MPI_HOME}/lib64 -lmpi -libverbs
#-lpthread
NVCCFLAGS=
#-O2 -arch=sm_60 -Xptxas -dlcm=ca -Xptxas=-v -DPROFILE_NVTX_RANGES

CUDADIR= /usr/local/cuda-8.0
CUDAINCLUDEDIR= $(CUDADIR)/include
CUDALDFLAGS=-L$(CUDADIR)/lib -lcudart

OBJ=oob.o oob_mpi.o oob_socket.o tl.o tl_verbs.o mpi_test.o

ifdef use_gdr
	CPPFLAGS+=-DGPURDMA
endif

.PHONY: all clean

all: mpi_test

mpi_test: $(OBJ)
	$(LD) -o mpi_test $(OBJ) ${CFLAGS} ${CPPFLAGS} ${LDFLAGS}

.cc.o:
	$(CC) -c $(CFLAGS) $(CPPFLAGS) $< -o $@


clean:
	rm -rf *.o mpi_test
