# LibMP

## Introduction

LibMP is a lightweight messaging library built on top of [LibGDSync APIs](https://github.com/gpudirect/libgdsync/tree/devel), developed as a technology demonstrator to easily deploy the GPUDirect Async technology in applications.

## Requirements

Basic LibMP requirements are:
- OpenMPI v1.10 or newer
- Mellanox OFED (MOFED) 4.0 or newer 
- Mellanox Connect-IB or ConnectX-4 HCAs
- [LibGDSync requirements](https://github.com/gpudirect/libgdsync#requirements)

To use GPUDirect Async in combination with GPUDirect RDMA:
- OpenMPI with CUDA support
- A recent CUDA Toolkit is required, minimally 8.0
- A recent display driver, i.e. r361, r367 or later, is required
- The Mellanox OFED GPUDirect RDMA kernel module, https://github.com/Mellanox/nv_peer_memory, is required to allow the HCA to access the GPU memory.

# Acknowledging LibMP and GPUDirect Async

If you find this software useful in your work, please cite:

["GPUDirect Async: exploring GPU synchronous communication techniques for InfiniBand clusters"](https://www.sciencedirect.com/science/article/pii/S0743731517303386), E. Agostini, D. Rossetti, S. Potluri. Journal of Parallel and Distributed Computing, Vol. 114, Pages 28-45, April 2018

["Offloading communication control logic in GPU accelerated applications"](http://ieeexplore.ieee.org/document/7973709), E. Agostini, D. Rossetti, S. Potluri. Proceedings of the 17th IEEE/ACM International Symposium on Cluster, Cloud and Grid Computing (CCGrid’ 17), IEEE Conference Publications, Pages 248-257, Nov 2016
