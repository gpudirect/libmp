#include "tl.h"
#include <infiniband/verbs.h>
#include <infiniband/verbs_exp.h>
#include <infiniband/peer_ops.h>
#include <memory>
#include <string>

//GDS

struct gds_cq {
        struct ibv_cq *cq;
        uint32_t curr_offset;
};

struct gds_qp {
        struct ibv_qp *qp;
        struct gds_cq send_cq;
        struct gds_cq recv_cq;
};

typedef struct ibv_qp_init_attr_ex gds_qp_init_attr_t;
typedef struct ibv_exp_send_wr gds_send_wr;



// batched submission APIs

typedef enum gds_wait_cond_flag {
        GDS_WAIT_COND_GEQ = 0, // must match verbs_exp enum
        GDS_WAIT_COND_EQ,
        GDS_WAIT_COND_AND,
        GDS_WAIT_COND_NOR
} gds_wait_cond_flag_t;

typedef enum gds_memory_type {
        GDS_MEMORY_GPU  = 1,
        GDS_MEMORY_HOST = 2,
        GDS_MEMORY_IO   = 4,
	GDS_MEMORY_MASK = 0x7
} gds_memory_type_t;

typedef enum gds_wait_flags {
	GDS_WAIT_POST_FLUSH = 1<<3,
} gds_wait_flags_t;

typedef enum gds_write_flags {
	GDS_WRITE_PRE_BARRIER = 1<<4,
} gds_write_flags_t;

typedef enum gds_immcopy_flags {
	GDS_IMMCOPY_POST_TAIL_FLUSH = 1<<4,
} gds_immcopy_flags_t;

typedef enum gds_membar_flags {
	GDS_MEMBAR_FLUSH_REMOTE = 1<<4,
	GDS_MEMBAR_DEFAULT      = 1<<5,
	GDS_MEMBAR_SYS          = 1<<6,
} gds_membar_flags_t;

enum {
        GDS_SEND_INFO_MAX_OPS = 32,
        GDS_WAIT_INFO_MAX_OPS = 32
};

typedef struct gds_send_request {
        struct ibv_exp_peer_commit commit;
        struct peer_op_wr wr[GDS_SEND_INFO_MAX_OPS];
} gds_send_request_t;

typedef struct gds_wait_request {
        struct ibv_exp_peer_peek peek;
        struct peer_op_wr wr[GDS_WAIT_INFO_MAX_OPS];
} gds_wait_request_t;

typedef struct gds_wait_value32 { 
        uint32_t  *ptr;
        uint32_t   value;
        gds_wait_cond_flag_t cond_flags;
        int        flags; // takes gds_memory_type_t | gds_wait_flags_t
} gds_wait_value32_t;

typedef struct gds_write_value32 { 
        uint32_t  *ptr;
        uint32_t   value;
        int        flags; // takes gds_memory_type_t | gds_write_flags_t
} gds_write_value32_t;

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
//   smp_channel_t smp;
//   ipc_handle_cache_entry_t *ipc_handle_cache;
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


