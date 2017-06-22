use_cuda:=1
use_gds:=1

PREFIX=$(HOME)/libmp
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


#===== CONFIGURE FLAGS =====
CONFIGURE_FLAGS=
#GPUDirect RDMA
ifdef use_cuda
CONFIGURE_FLAGS+=-DHAVE_CUDA
CUDA_PATH=/usr/local/cuda-8.0
CUDA_INCLUDE=-I$(CUDA_PATH)/include
CUDA_LD=-L$(CUDA_PATH)/lib64 -L$(CUDA_PATH)/lib -lcuda -lcudart
else
CUDA_PATH=
CUDA_INCLUDE=
CUDA_LD=
endif

#LibGDSync
ifdef use_gds
CONFIGURE_FLAGS+=-DHAVE_GDSYNC
GDS_INCLUDE=-I$(PREFIX)/include
GDS_LD=-L$(PREFIX)/lib -lgdsync -lgdrapi
OBJ=oob.o oob_mpi.o oob_socket.o tl.o tl_verbs.o tl_verbs_async.o mp.o mp_comm.o mp_comm_async.o 
else
GDS_PATH=
GDS_INCLUDE=
GDS_LD=
OBJ=oob.o oob_mpi.o oob_socket.o tl.o tl_verbs.o mp.o mp_comm.o
endif
#===========================

.PHONY: all clean

all: raw_pt2pt raw_onesided mp_pt2pt mp_onesided

#mp: $(OBJ) mp.o
#	$(LD) -o mp $(OBJ) mp.o ${CFLAGS} ${CPPFLAGS} ${CONFIGURE_FLAGS} ${LDFLAGS} ${CUDA_LD}

mp_pt2pt: $(OBJ) mp_pt2pt.o
	$(LD) -o mp_pt2pt $(OBJ) mp_pt2pt.o ${CFLAGS} ${CPPFLAGS} ${CONFIGURE_FLAGS} ${LDFLAGS} ${CUDA_LD} ${GDS_LD}

mp_onesided: $(OBJ) mp_onesided.o
	$(LD) -o mp_onesided $(OBJ) mp_onesided.o ${CFLAGS} ${CPPFLAGS} ${CONFIGURE_FLAGS} ${LDFLAGS} ${CUDA_LD} ${GDS_LD}

raw_pt2pt: $(OBJ) raw_pt2pt.o
	$(LD) -o raw_pt2pt $(OBJ) raw_pt2pt.o ${CFLAGS} ${CPPFLAGS} ${CONFIGURE_FLAGS} ${LDFLAGS} ${CUDA_LD} ${GDS_LD}

raw_onesided: $(OBJ) raw_onesided.o
	$(LD) -o raw_onesided $(OBJ) raw_onesided.o ${CFLAGS} ${CPPFLAGS} ${CONFIGURE_FLAGS} ${LDFLAGS} ${CUDA_LD} ${GDS_LD}

.cc.o:
	$(CC) -c $(CFLAGS) $(CPPFLAGS) $(CONFIGURE_FLAGS) $(CUDA_INCLUDE) $(GDS_INCLUDE) $< -o $@


clean:
	rm -rf *.o raw_pt2pt raw_onesided mp_pt2pt mp_onesided 
