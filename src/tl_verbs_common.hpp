#ifndef VERBS_COMMON_H
#define VERBS_COMMON_H

#include <infiniband/verbs.h>
#include <infiniband/verbs_exp.h>

enum verbs_wait_flags {
        VERBS_WAIT_GEQ = 0,
        VERBS_WAIT_EQ,
        VERBS_WAIT_AND,
};

struct verbs_cq {
        struct ibv_cq *cq;
        uint32_t curr_offset;
};

struct verbs_qp {
        struct ibv_qp *qp;
        struct verbs_cq send_cq;
        struct verbs_cq recv_cq;
};

typedef struct ibv_qp_init_attr_ex verbs_qp_init_attr_t;
typedef struct ibv_exp_send_wr verbs_send_wr;

#define UD_ADDITION 40 

/*exchange info*/
typedef struct {
        uint16_t lid;
        uint32_t psn;
        uint32_t qpn;
} qpinfo_t;

/*IB resources*/
typedef struct {
        struct ibv_context *context;
        struct ibv_pd      *pd;
} ib_context_t;


struct verbs_region : mp_region {
        uint32_t key;
        struct ibv_mr *mr;
};
typedef struct verbs_region * verbs_region_t;

struct verbs_window : mp_window {
        void **base_ptr;
        int size;
        struct verbs_region *reg;
        uint32_t lkey;
        uint32_t *rkey;
        uint64_t *rsize;
};
typedef struct verbs_window * verbs_window_t;

typedef struct mem_region {
        void *region;
        struct mem_region *next;
} mem_region_t;

typedef uint64_t us_t;

typedef struct {
        void *base_addr;
        uint32_t rkey;
        int size;
} exchange_win_info;

#endif