use_cuda:=1
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


#===== CONFIGURE FLAGS =====
CONFIGURE_FLAGS=
#GPUDirect RDMA
ifdef use_cuda
CONFIGURE_FLAGS+=-DHAVE_CUDA
CUDA_PATH=/usr/local/cuda-8.0
CUDAINCLUDEDIR=-I$(CUDA_PATH)/include
CUDALDFLAGS=-L$(CUDA_PATH)/lib64 -L$(CUDA_PATH)/lib -lcuda -lcudart
else
CUDA_PATH=
CUDAINCLUDEDIR=
CUDALDFLAGS=
endif
#===========================

OBJ=oob.o oob_mpi.o oob_socket.o tl.o tl_verbs.o mp.o

.PHONY: all clean

all: raw_pt2pt raw_onesided mp_pt2pt mp_onesided

#mp: $(OBJ) mp.o
#	$(LD) -o mp $(OBJ) mp.o ${CFLAGS} ${CPPFLAGS} ${CONFIGURE_FLAGS} ${LDFLAGS} ${CUDALDFLAGS}

mp_pt2pt: $(OBJ) mp_pt2pt.o
	$(LD) -o mp_pt2pt $(OBJ) mp_pt2pt.o ${CFLAGS} ${CPPFLAGS} ${CONFIGURE_FLAGS} ${LDFLAGS} ${CUDALDFLAGS}

mp_onesided: $(OBJ) mp_onesided.o
	$(LD) -o mp_onesided $(OBJ) mp_onesided.o ${CFLAGS} ${CPPFLAGS} ${CONFIGURE_FLAGS} ${LDFLAGS} ${CUDALDFLAGS}

raw_pt2pt: $(OBJ) raw_pt2pt.o
	$(LD) -o raw_pt2pt $(OBJ) raw_pt2pt.o ${CFLAGS} ${CPPFLAGS} ${CONFIGURE_FLAGS} ${LDFLAGS} ${CUDALDFLAGS}

raw_onesided: $(OBJ) raw_onesided.o
	$(LD) -o raw_onesided $(OBJ) raw_onesided.o ${CFLAGS} ${CPPFLAGS} ${CONFIGURE_FLAGS} ${LDFLAGS} ${CUDALDFLAGS}

.cc.o:
	$(CC) -c $(CFLAGS) $(CPPFLAGS) $(CONFIGURE_FLAGS) $(CUDAINCLUDEDIR) $< -o $@


clean:
	rm -rf *.o raw_pt2pt raw_onesided mp_pt2pt mp_onesided 
