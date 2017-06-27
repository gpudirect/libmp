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

#ifdef __cplusplus
extern "C" {
#endif

    int comm_use_comm();
    int comm_use_gdrdma();
    int comm_use_async();
    int comm_use_gpu_comm();

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
        int comm_select_device(int mpiRank);

#ifdef __cplusplus
}
#endif
