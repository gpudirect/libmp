# LibMP

## Introduction

LibMP is a lightweight messaging library built on top of [LibGDSync APIs](https://github.com/gpudirect/libgdsync/tree/devel), developed as a technology demonstrator to easily deploy the GPUDirect Async technology in applications.

## Requirements

Basic LibMP requirements are:
- OpenMPI v1.10 or newer
- Mellanox OFED (MOFED) 4.0 or newer 
- Mellanox Connect-IB, ConnectX-4 HCAs or newer
- [LibGDSync](https://github.com/gpudirect/libgdsync#requirements)

To use GPUDirect Async in combination with GPUDirect RDMA:
- OpenMPI with CUDA support
- A recent CUDA Toolkit is required, minimally 8.0
- A recent display driver, i.e. r361, r367 or later, is required
- The Mellanox OFED GPUDirect RDMA kernel module, https://github.com/Mellanox/nv_peer_memory, is required to allow the HCA to access the GPU memory.

## Build

Use the *scripts/env_setup.sh* file to specify MPI_PATH, CUDA_PATH, LIBGDSYNC_PATH and LIBMP_PATH env vars useful for both LibMP and LibGDSync.

Use the *build.sh* script to build LibMP.

## Run

In *scripts* folder:
- wrapper.sh: sample script with some topology example
- test.sh: sample script to test all libmp examples and benchmarks

You need to create your own hostfile inside *scripts* directory

## COMM library

COMM is an additional library built on top of LibMP.
With COMM you can easily deploy LibMP in you applications; the pingpong is an example of COMM usage.

## GPUDirect Async suite

We created a new GPUDirect Asyc repository [here](https://github.com/gpudirect/gdasync) in order, under the same project, all the required libraries and applications.<br>
In this repo you can find several scripts useful to configure, build and run all GPUDirect Async libraries, tests, benchmarks and examples.

## Acknowledging LibMP and GPUDirect Async

If you find this software useful in your work, please cite:

["GPUDirect Async: exploring GPU synchronous communication techniques for InfiniBand clusters"](https://www.sciencedirect.com/science/article/pii/S0743731517303386), E. Agostini, D. Rossetti, S. Potluri. Journal of Parallel and Distributed Computing, Vol. 114, Pages 28-45, April 2018

["Offloading communication control logic in GPU accelerated applications"](http://ieeexplore.ieee.org/document/7973709), E. Agostini, D. Rossetti, S. Potluri. Proceedings of the 17th IEEE/ACM International Symposium on Cluster, Cloud and Grid Computing (CCGrid’ 17), IEEE Conference Publications, Pages 248-257, Nov 2016
