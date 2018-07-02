/****
 * Copyright (c) 2011-2018, NVIDIA CORPORATION. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *  * Neither the name of NVIDIA CORPORATION nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 ****/

#pragma once

#ifdef __cplusplus
#include <mp/device.cuh>
struct comm_dev_descs {
    enum { max_n_descs = 32 };
    int n_ready;
    mp::mlx5::isem32_t ready[max_n_descs];

    int n_tx;
    mp::mlx5::send_desc_t tx[max_n_descs];

    int n_wait;
    mp::mlx5::wait_desc_t wait[max_n_descs];
};
#endif
typedef struct comm_dev_descs *comm_dev_descs_t;


#define __COMM_CHECK(stmt, cond_str)                    \
    do {                                \
            int result = (stmt);                                        \
            if (result) {                                               \
                fprintf(stderr, "%s failed at %s:%d error=%d\n",        \
                        cond_str, __FILE__, __LINE__, result);          \
                exit(EXIT_FAILURE);                                     \
            }                                                           \
        } while (0)

#define COMM_CHECK(stmt) __COMM_CHECK(stmt, #stmt)

#define DBG(FMT, ARGS...)                                           \
do {                                                                \
    if (dbg_enabled()) {                                            \
        fprintf(stderr, "[%d] [%d] COMM DBG %s(): " FMT,               \
                getpid(), mpi_comm_rank, __FUNCTION__ , ## ARGS);   \
        fflush(stderr);                                             \
    }                                                               \
} while(0)


#define MP_CHECK(stmt)                                  \
do {                                                    \
    int result = (stmt);                                \
    if (0 != result) {                                  \
        fprintf(stderr, "[%s:%d] mp call failed \n",    \
         __FILE__, __LINE__);                           \
        exit(-1);                                       \
    }                                                   \
    assert(0 == result);                                \
} while (0)


#define comm_err(FMT, ARGS...)  do { fprintf(stderr, "ERR [%d] %s() " FMT, comm_rank, __FUNCTION__ , ## ARGS); fflush(stderr); } while(0)

#ifndef MAX
#define MAX(A,B) ((A)>(B)?(A):(B))
#endif

#define MAX_RANKS 128

#ifdef __cplusplus
extern "C" {
#endif

    int comm_use_comm();
    int comm_use_gdrdma();
    int comm_use_model_sa();
    int comm_use_model_ki();

    typedef struct comm_request  *comm_request_t;
    typedef struct comm_reg      *comm_reg_t;
    typedef struct CUstream_st   *comm_stream_t;
    int comm_init(MPI_Comm comm, int gpuId);
    void comm_finalize();
    int comm_send_ready_on_stream(int rank, comm_request_t *creq, comm_stream_t stream);
    int comm_send_ready(int rank, comm_request_t *creq);
    int comm_wait_ready_on_stream(int rank, comm_stream_t stream);
    int comm_wait_ready(int rank);
    int comm_test_ready(int rank, int *p_rdy);
    int comm_irecv(void *recv_buf, size_t size, MPI_Datatype type, comm_reg_t *reg, int src_rank, 
                   comm_request_t *req);
    int comm_isend_on_stream(void *send_buf, size_t size, MPI_Datatype type, comm_reg_t *reg,
                             int dest_rank, comm_request_t *req, comm_stream_t stream);
    int comm_isend(void *send_buf, size_t size, MPI_Datatype type, comm_reg_t *reg,
                   int dest_rank, comm_request_t *req);
    int comm_wait_all(int count, comm_request_t *creqs);
    int comm_wait_all_on_stream(int count, comm_request_t *creqs, comm_stream_t stream);
    int comm_wait(comm_request_t *creq);
    int comm_flush();
    int comm_flush_new();
    int comm_progress();

    int comm_prepare_wait_ready(int rank);
    int comm_prepare_isend(void *send_buf, size_t size, MPI_Datatype type, comm_reg_t *creg,
                           int dest_rank, comm_request_t *creq);
    int comm_prepare_wait_all(int count, comm_request_t *creqs);
    comm_dev_descs_t comm_prepared_requests();
    int comm_register(void *buf, size_t size, comm_reg_t *creg);
    int comm_register_odp(comm_reg_t *creg);
    int comm_deregister(comm_reg_t *creg);
    int comm_select_device(int mpiRank);


#ifdef __cplusplus
}
#endif
