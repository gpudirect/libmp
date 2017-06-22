#ifndef VERBS_TYPES_H
#define VERBS_TYPES_H

#ifdef HAVE_GDSYNC
	#include <infiniband/peer_ops.h>
	#include <gdsync.h>
	#include <gdsync/tools.h>
	#include <gdsync/core.h>
#endif

/*client resources*/
typedef struct {
    //void *region;
    //int region_size;
    int oob_rank;
    uint32_t last_req_id;
    uint32_t last_done_id;
    uint32_t last_posted_trigger_id[N_FLOWS];
    uint32_t last_posted_tracked_id[N_FLOWS];
    uint32_t last_trigger_id[N_FLOWS]; //has to be moved to device
    uint32_t last_tracked_id[N_FLOWS];
    struct verbs_request *last_posted_stream_req[N_FLOWS];
    struct verbs_request *posted_stream_req[N_FLOWS];
    struct verbs_request *last_waited_stream_req[N_FLOWS]; //head
    struct verbs_request *waited_stream_req[N_FLOWS]; //tail

    /*ib related*/
#ifdef HAVE_GDSYNC
	struct gds_qp *qp;
	struct gds_cq *send_cq;
	struct gds_cq *recv_cq;
#else    
    struct verbs_qp *qp;
    struct verbs_cq *send_cq;
    struct verbs_cq *recv_cq;
#endif

    struct ibv_mr *region_mr;
    //UD info
    struct ibv_ah *ah;
    uint32_t qpn;

  #ifdef HAVE_IPC
    //ICP
    int is_local;
    int local_rank;
    int can_use_ipc;
    //   smp_channel_t smp;
    //   ipc_handle_cache_entry_t *ipc_handle_cache;
    struct verbs_request *last_posted_ipc_rreq;
    struct verbs_request *posted_ipc_rreq;
    struct verbs_request *last_processed_ipc_rreq;
    struct verbs_request *processed_ipc_rreq;
  #endif
} client_t;

struct verbs_request {
  int peer;
  mp_req_type_t type;
  int status;
  int trigger;
  int id;
  //uint32_t id;
  int flags;

#ifdef HAVE_GDSYNC

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

  struct CUstream_st *stream;
  gds_send_request_t gds_send_info;
  gds_wait_request_t gds_wait_info;

#else

  union
  {
    struct ibv_recv_wr rr;
    verbs_send_wr sr;
  } in;
  union
  {
    verbs_send_wr* bad_sr;
    struct ibv_recv_wr* bad_rr;
  } out;

#endif

  struct ibv_sge sg_entry;
  struct ibv_sge ud_sg_entry[2];
  struct ibv_sge *sgv;

  struct verbs_request *next;
  struct verbs_request *prev;
};
typedef struct verbs_request * verbs_request_t;

#endif