/****
 * Copyright (c) 2011-2016, NVIDIA Corporation.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *    * Redistributions of source code must retain the above copyright notice,
 *      this list of conditions and the following disclaimer.
 *    * Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
 *    * Neither the name of the NVIDIA Corporation nor the names of its
 *      contributors may be used to endorse or promote products derived from
 *      this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 ****/

#pragma once

#include <assert.h>
#include <infiniband/verbs.h>
#include <gdsync.h>

#ifdef __cplusplus
extern "C" 
{ 
#endif

#define UD_ADDITION 40 

//progress flow fix
#define MP_MAX_PROGRESS_FLOW_TRY 100
#define MP_PROGRESS_ERROR_CHECK_TMOUT_US ((us_t)60*1000*1000)

typedef enum mp_state {
    MP_UNDEF,
    MP_PREPARED,       // req just prepared
    MP_PENDING_NOWAIT, // req posted, but not wait-for-end pending yet
    MP_PENDING,        // req posted and wait is pending
    // MP_WAIT_POSTED,
    MP_COMPLETE,
    MP_N_STATES
} mp_state_t ;

typedef enum mp_req_type {
    MP_NULL = 0,
    MP_SEND,
    MP_RECV,
    MP_RDMA,
    MP_N_TYPES
} mp_req_type_t;

/*exchange info*/
typedef struct {
    uint16_t lid;
    uint32_t psn;
    uint32_t qpn;
} qpinfo_t;

typedef enum mp_flow {
    TX_FLOW, // requests associated with tx_cq
    RX_FLOW, // same for rx_cq
    N_FLOWS
} mp_flow_t;

typedef enum mp_req_list {
    PREPARED_LIST,
    PENDING_LIST,
} mp_req_list_t;

typedef struct {
   uint32_t busy;
   uint32_t free;
   struct mp_request *sreq;
   struct mp_request *rreq;
   CUipcMemHandle handle;
   void *base_addr; 
   size_t base_size; 
   void *addr; 
   size_t size;
   int offset; 
} smp_buffer_t;

typedef struct {
   smp_buffer_t *local_buffer;
   smp_buffer_t *remote_buffer;
   int local_tail_process;
   int local_tail_complete;
   int remote_head;	
} smp_channel_t;

typedef struct ipc_handle_cache_entry {
    void *base;
    size_t size;
    void *remote_base;
    CUipcMemHandle handle;
    struct ipc_handle_cache_entry *next;
    struct ipc_handle_cache_entry *prev;
} ipc_handle_cache_entry_t;

/*client resources*/
typedef struct {
   //void *region;
   //int region_size;
   int mpi_rank;
   uint32_t last_req_id;
   uint32_t last_done_id;
   uint32_t last_posted_trigger_id[N_FLOWS];
   uint32_t last_posted_tracked_id[N_FLOWS];
   uint32_t last_trigger_id[N_FLOWS]; //has to be moved to device
   uint32_t last_tracked_id[N_FLOWS];
   struct mp_request *last_posted_stream_req[N_FLOWS];
   struct mp_request *posted_stream_req[N_FLOWS];
   struct mp_request *last_waited_stream_req[N_FLOWS]; //head
   struct mp_request *waited_stream_req[N_FLOWS]; //tail
   /*ib related*/
   struct gds_qp *qp;
   struct gds_cq *send_cq;
   struct gds_cq *recv_cq;
   struct ibv_mr *region_mr;
   //UD info
   struct ibv_ah *ah;
   uint32_t qpn;
   //ICP
   int is_local;
   int local_rank;
   int can_use_ipc;
   smp_channel_t smp;
   ipc_handle_cache_entry_t *ipc_handle_cache;
   struct mp_request *last_posted_ipc_rreq;
   struct mp_request *posted_ipc_rreq;
   struct mp_request *last_processed_ipc_rreq;
   struct mp_request *processed_ipc_rreq;
} client_t;

/*IB resources*/
typedef struct {
    struct ibv_context *context;
    struct ibv_pd      *pd;
} ib_context_t;

struct mp_reg {
    uint32_t key;
    struct ibv_mr *mr;
};

struct CUstream_st;

struct mp_request {
   mp_req_type_t type;
   int peer;
   int status;
   int trigger;
   uint32_t id;
   int flags;
   struct CUstream_st *stream;
   union
   {
       struct ibv_recv_wr rr;
       gds_send_wr sr;
   } in;
   union
   {
       gds_send_wr* bad_sr;
       struct ibv_recv_wr* bad_rr;
   } out;
   struct ibv_sge sg_entry;
   struct ibv_sge ud_sg_entry[2];
   struct ibv_sge *sgv;
   gds_send_request_t gds_send_info;
   gds_wait_request_t gds_wait_info;
   struct mp_request *next;
   struct mp_request *prev;
}; 

struct mp_window {
   void **base_ptr;
   int size;
   struct mp_reg *reg;
   uint32_t lkey;
   uint32_t *rkey;
   uint64_t *rsize;
};

typedef struct mem_region {
  void *region;
  struct mem_region *next;
} mem_region_t;

extern client_t *clients;
extern int *client_index;
extern int ib_max_sge;
extern int mp_enable_ipc;

extern int mpi_comm_rank;
int mp_dbg_enabled();
void mp_enable_dbg(int);
#define mp_dbg_msg(FMT, ARGS...)  do {                                  \
    if (mp_dbg_enabled())  {                                            \
        fprintf(stderr, "[%d] [%d] MP DBG  %s() "                       \
                FMT, getpid(),  mpi_comm_rank, __FUNCTION__ , ## ARGS); \
        fflush(stderr);                                                 \
    }                                                                   \
} while(0)

#define mp_dbg_msg_c(CNT, FMT, ARGS...) do {    \
    static int ___dbg_msg_cnt = (CNT);          \
    if (___dbg_msg_cnt--)                       \
        mp_dbg_msg(FMT, ##ARGS);                \
} while(0)

int mp_warn_enabled();
#define mp_warn_msg(FMT, ARGS...) do {                                  \
        if (mp_warn_enabled()) {                                        \
            fprintf(stderr, "[%d] [%d] MP WARN %s() "                   \
                    FMT, getpid(), mpi_comm_rank, __FUNCTION__ , ## ARGS); \
            fflush(stderr);                                             \
        }                                                               \
    } while(0)

#define mp_info_msg(FMT, ARGS...) do {                                  \
        fprintf(stderr, "[%d] [%d] MP INFO %s() "                       \
                FMT, getpid(), mpi_comm_rank, __FUNCTION__ , ## ARGS);  \
        fflush(stderr);                                                 \
    } while(0)

#define mp_err_msg(FMT, ARGS...)  do {                                  \
        fprintf(stderr, "[%d] [%d] MP ERR  %s() "                       \
                FMT, getpid(), mpi_comm_rank, __FUNCTION__ , ## ARGS);  \
        fflush(stderr);                                                 \
    } while(0)

#ifndef MIN
#define MIN(A,B) ((A)<(B)?(A):(B))
#endif

extern int use_event_sync;
extern int mp_enable_ud;

int mp_progress_single_flow(mp_flow_t flow);
void allocate_requests ();
struct mp_request *new_stream_request(client_t *client, mp_req_type_t type, mp_state_t state, struct CUstream_st *stream);
static inline struct mp_request *new_request(client_t *client, mp_req_type_t type, mp_state_t state)
{
    return new_stream_request(client, type, state, 0);
}
void release_mp_request(struct mp_request *req);

//void init_req (struct mp_request *req);

static inline int req_valid(struct mp_request *req)
{
    return (req->type   >= MP_NULL  && req->type   < MP_N_TYPES ) && 
           (req->status >= MP_UNDEF && req->status < MP_N_STATES);
}

static inline int req_type_tx(mp_req_type_t type)
{
    assert(type > MP_NULL && type < MP_N_TYPES);
    return (type == MP_SEND) || (type == MP_RDMA);
}

static inline int req_type_rx(mp_req_type_t type)
{
    assert(type > MP_NULL && type < MP_N_TYPES);
    return (type == MP_RECV);
}

static inline mp_flow_t mp_type_to_flow(mp_req_type_t type)
{
    return req_type_tx(type) ? TX_FLOW : RX_FLOW;
}

static inline const char *mp_flow_to_str(mp_flow_t flow) {
    return flow==TX_FLOW?"TX":"RX";
}

static inline mp_flow_t mp_req_to_flow(struct mp_request *req)\
{
    return mp_type_to_flow((mp_req_type_t)req->type);
}

static inline int req_can_be_waited(struct mp_request *req)
{
    assert(req_valid(req));
    return req_type_rx(req->type) || (
        req_type_tx(req->type) && !(req->flags & MP_PUT_NOWAIT));
}

static int mp_query_print_qp(struct gds_qp *qp, struct mp_request *req, int async)
{
    assert(qp);
    struct ibv_qp_attr qp_attr;
    struct ibv_qp_init_attr qp_init_attr;

    memset(&qp_init_attr, 0, sizeof(struct ibv_qp_init_attr));
    memset(&qp_attr, 0, sizeof(struct ibv_qp_attr));


    if (ibv_query_qp(qp->qp, &qp_attr, IBV_QP_STATE | IBV_QP_PATH_MTU | IBV_QP_CAP, &qp_init_attr))
    {
        mp_err_msg("client query qp attr fail\n");
        return MP_FAILURE;
    }
   
    mp_warn_msg("Init QP attr: max_send_wr=%d, max_recv_wr=%d, max_inline_data=%d, qp_type=%d\nCurrent QP attr: QP State=%d QP Cur State=%d Access Flags=%d max_send_wr=%d, max_recv_wr=%d, max_inline_data=%d\n",
                    qp_init_attr.cap.max_send_wr,
                    qp_init_attr.cap.max_recv_wr,
                    qp_init_attr.cap.max_inline_data,
                    qp_init_attr.qp_type,
                    qp_attr.qp_state,
                    qp_attr.cur_qp_state,
                    qp_attr.qp_access_flags,
                    qp_attr.cap.max_send_wr,
                    qp_attr.cap.max_recv_wr,
                    qp_attr.cap.max_inline_data
    );

#if 0
    if(req != NULL)
    {
        if(req->in.sr.exp_opcode == IBV_EXP_WR_SEND)
            mp_warn_msg("This is an IBV_EXP_WR_SEND\n");
            
        if(req->in.sr.exp_opcode == IBV_EXP_WR_RDMA_WRITE)
            mp_warn_msg("This is an IBV_EXP_WR_RDMA_WRITE\n");
    }
#endif
    return MP_SUCCESS;
}


#ifndef ACCESS_ONCE
#define ACCESS_ONCE(V)                          \
    (*(volatile typeof (V) *)&(V))
#endif

uint32_t *client_last_tracked_id_ptr(client_t *client, struct mp_request *req);
void client_track_waited_stream_req(client_t *client, struct mp_request *req, mp_flow_t flow);

typedef uint64_t us_t;
static inline us_t mp_get_cycles()
{
    struct timespec ts;
    int ret = clock_gettime(CLOCK_MONOTONIC, &ts);
    if (ret) {
        mp_err_msg("error in gettime %d/%s\n", errno, strerror(errno));
        exit(EXIT_FAILURE);
    }
    return (us_t)ts.tv_sec * 1000 * 1000 + (us_t)ts.tv_nsec / 1000;
}

int mp_check_gpu_error();

#ifdef __cplusplus
}
#endif
