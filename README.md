# LibMP 2.0 -- Work in progress, use master branch

## Introduction

LibMP is a lightweight messaging library built on top of [LibGDSync APIs](https://github.com/gpudirect/libgdsync), developed as a technology demonstrator to easily deploy the GPUDirect Async technology in applications.

## Requirements

Basic LibMP requirements are:
- OpenMPI v1.10 or newer
- Mellanox OFED (MOFED) 4.0 or newer 
- Mellanox Connect-IB or ConnectX-4 HCAs

To enable GPUDirect RDMA:
- OpenMPI with CUDA support
- A recent CUDA Toolkit is required, minimally 8.0
- A recent display driver, i.e. r361, r367 or later, is required
- The Mellanox OFED GPUDirect RDMA kernel module, https://github.com/Mellanox/nv_peer_memory, is required to allow the HCA to access the GPU memory.

To enable GPUDirect Async:
- Basic requirements
- GPUDirect RDMA requirements
- [LibGDSync requirements](https://github.com/gpudirect/libgdsync#requirements)

## Build

You can configure LibMP build using the *libmp.mk* file.

### Basic LibMP configuration

In file *libmp.mk*:
```
PREFIX = /path/to/libmp/directory
GDSYNC_BUILD = 0
GDSYNC_ENABLE = no
GDRCOPY_BUILD = 0
CUDA_ENABLE = no
```

### Enable GPUDirect RDMA

In file *libmp.mk*:
```
PREFIX = /path/to/libmp/directory
GDSYNC_BUILD = 0
GDSYNC_ENABLE = no
GDRCOPY_BUILD = 0
CUDA_ENABLE = /path/to/cuda/directory
```

### Enable GPUDirect Async

```
git submodule init
git submodule update
```

In file *libmp.mk*:
```
PREFIX = /path/to/libmp/directory
GDSYNC_BUILD = 1
GDSYNC_ENABLE = /path/to/libmp/directory
GDRCOPY_BUILD = 1
CUDA_ENABLE = /path/to/cuda/directory
```

## Build the project


You just need to run the Makefile with 
```
make
```

It creates several directories:
- libmp/lib
- libmp/include
- libmp/bin

You must specify the *path/to/libmp/lib* directory manually in the environment variable LD_LIBRARY_PATH.

## Run examples

*libmp/scripts/test_examples.sh* can be used to run examples in *libmp/bin*:
```
./test_examples.sh <MPI proc num> <GPU Buffers=0:1> <libmp prefix> <test name>
```

For example, to run mp_putget_async with 2 processes without using send/recv buffers stored in GPU memory:

```
cd libmp/scripts
vim hostfile #Create your own hostfile
./test_examples.sh 2 0 /path/to/libmp mp_sendrecv_async
```

# Acknowledging LibMP and GPUDirect Async

If you find this software useful in your work, please cite:

["GPUDirect Async: exploring GPU synchronous communication techniques for InfiniBand clusters"](https://www.sciencedirect.com/science/article/pii/S0743731517303386), E. Agostini, D. Rossetti, S. Potluri. Journal of Parallel and Distributed Computing, Vol. 114, Pages 28-45, April 2018

["Offloading communication control logic in GPU accelerated applications"](http://ieeexplore.ieee.org/document/7973709), E. Agostini, D. Rossetti, S. Potluri. Proceedings of the 17th IEEE/ACM International Symposium on Cluster, Cloud and Grid Computing (CCGrid’ 17), IEEE Conference Publications, Pages 248-257, Nov 2016
