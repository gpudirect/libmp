# install path
PREFIX ?= $(HOME)/libmp

# **********************************************************
# Enable GDSync library setting the path
# or set the value to "no"
# **********************************************************
GDSYNC_BUILD = 1
GDSYNC_ENABLE = $(PREFIX)

# **********************************************************
# Enable GPUDirect RDMA Copy library setting the path
# or set the value to "no"
# **********************************************************
GDRCOPY_BUILD = 1

# **********************************************************
# Enable CUDA mode setting the path
# or set the value to "no"
# **********************************************************
CUDA_ENABLE = /usr/local/cuda-8.0

# MPI is used in some tests
MPI_PATH = /opt/openmpi/v1.10.2/cuda7.5

#------------------------------------------------------------------------------
# build tunables
#------------------------------------------------------------------------------

CXX:=g++
CC:=gcc
NVCC:=nvcc
LD:=g++
COMMON_CFLAGS:=-O2
CPPFLAGS+=-DGDS_USE_EXP_INIT_ATTR -DGDS_OFED_HAS_PEER_DIRECT_ASYNC
NVCC_ARCHS:=-gencode arch=compute_35,code=sm_35
NVCC_ARCHS+=-gencode arch=compute_50,code=compute_50
NVCC_ARCHS+=-gencode arch=compute_60,code=compute_60
#-arch=compute_35 -code=sm_35 

# static or dynamic
LIBS_TYPE=static