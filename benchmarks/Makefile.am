# Copyright (c) 2011-2018, NVIDIA CORPORATION. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#  * Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
#  * Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#  * Neither the name of NVIDIA CORPORATION nor the names of its
#    contributors may be used to endorse or promote products derived
#    from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
# EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
# PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
# CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
# EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
# PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
# PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
# OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

CUDA_PATH = @CUDA_PATH@
MPI_PATH = @MPI_PATH@

AM_CPPFLAGS  = -I$(CUDA_PATH)/include -I$(MPI_PATH)/include
AM_CPPFLAGS += -D__STDC_FORMAT_MACROS
AM_CPPFLAGS += -I$(top_srcdir)/include

AM_LDFLAGS   = $(LDFLAGS) -L$(CUDA_PATH)/lib64 -L$(CUDA_PATH)/lib $(top_builddir)/src/libmp.la -lgdsync -lgdrapi -lcuda -lcudart -lnvToolsExt

bin_PROGRAMS = mp_pingpong_all \
	mp_sendrecv_kernel_stream \
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

mp_pingpong_all_SOURCES = mp_pingpong_all.cu
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
