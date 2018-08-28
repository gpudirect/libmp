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

#include <malloc.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <errno.h>
#include <mpi.h>
#include <cuda_runtime.h>
#include <mp.h>
#include "comm.h"

static int mpi_comm_rank=0;

int dbg_enabled()
{
    static int dbg_is_enabled = -1;
    if (-1 == dbg_is_enabled) {        
        const char *env = getenv("COMM_ENABLE_DEBUG");
        if (env) {
            int en = atoi(env);
            dbg_is_enabled = !!en;
        } else
            dbg_is_enabled = 0;
    }
    return dbg_is_enabled;
}

static int         comm_initialized = 0;
static int         rank_to_peer[MAX_RANKS] = {0,};
static int         peers[MAX_RANKS] = {0,};
static int         n_peers = -1;
static const int   bad_peer = -1;
static int         comm_size;
static int         comm_rank;

// tables are indexed by rank, not peer
static uint32_t   *ready_table;
static mp_reg_t    ready_table_reg;
static mp_window_t ready_table_win;

static uint32_t   *ready_values;
static uint32_t   *remote_ready_values;
static mp_reg_t    remote_ready_values_reg;

#define MAX_REQS 16384 //32768
static mp_request_t reqs[MAX_REQS];
static int n_reqs = 0;

#define PAGE_BITS 12
#define PAGE_SIZE (1ULL<<PAGE_BITS)
#define PAGE_OFF  (PAGE_SIZE-1)
#define PAGE_MASK (~(PAGE_OFF))

/* ==== KI MODEL ==== */
static struct comm_dev_descs *pdreqs = NULL;
static const size_t n_dreqs = 128;
static int dreq_idx = 0;

/* ==== ARCH ==== */
#if defined(__x86_64__) || defined (__i386__)

    #define mb()    __asm__ volatile("mfence":::"memory")
    #define rmb()   __asm__ volatile("lfence":::"memory")
    #define wmb()   __asm__ volatile("sfence" ::: "memory")
    #define iomb() mb()

    static inline void arch_pause(void)
    {
            __asm__ volatile("pause\n": : :"memory");
    }


    static inline void arch_cpu_relax(void)
    {
            //rmb();
            //arch_rep_nop();
            arch_pause();
            //arch_pause(); arch_pause();
            //BUG: poll_lat hangs after a few iterations
            //arch_wait();
    }

#elif defined(__powerpc__)

    static void mb(void) __attribute__((unused)) ;
    static void mb(void)
    {
        asm volatile("sync") ;
    }

    static void wmb(void) __attribute__((unused)) ;
    static void wmb(void)
    {
        asm volatile("sync") ;
    }
    static void rmb(void) __attribute__((unused)) ;
    static void rmb(void)
    {
        asm volatile("sync") ;
    }

    #define iomb() mb()

    static void arch_cpu_relax(void) __attribute__((unused)) ;
    static void arch_cpu_relax(void)
    {
    }

#else
#error "platform not supported"
#endif
#define ACCESS_ONCE(V)                          \
    (*(volatile __typeof__ (V) *)&(V))


int comm_use_comm()
{
    static int use_comm = -1;
    if (-1 == use_comm) {
        const char *env = getenv("COMM_USE_COMM");
        if (env) {
            use_comm = !!atoi(env);
            printf("WARNING: %s Comm-based communications\n", (use_comm)?"enabling":"disabling");
        } else
            use_comm = 0; // default
    }
    return use_comm;
}

int comm_use_model_ki()
{
    static int use_gpu_comm = -1;
    if (-1 == use_gpu_comm) {
        const char *env = getenv("COMM_USE_ASYNC_KI");
        if (env) {
            use_gpu_comm = !!atoi(env);
            printf("WARNING: %s GPU-initiated communications\n", (use_gpu_comm)?"enabling":"disabling");
        } else
            use_gpu_comm = 0; // default
    }
    return use_gpu_comm;
}

int comm_use_model_sa()
{
    static int use_async = -1;
    if (-1 == use_async) {
        const char *env = getenv("COMM_USE_ASYNC_SA");
        if (env) {
            use_async = !!atoi(env);
            printf("WARNING: %s GPUDirect Async for communications\n", (use_async)?"enabling":"disabling");
        } else
            use_async = 0; // default
    }
    return use_async;
}

int comm_init(MPI_Comm comm, int gpuId)
{
    int i, j;

    MPI_Comm_size (comm, &comm_size);
    MPI_Comm_rank (comm, &comm_rank);

    mpi_comm_rank = comm_rank;

    assert(comm_size < MAX_RANKS);

    // init peers    
    for (i=0, j=0; i<comm_size; ++i) {
        if (i!=comm_rank) {
            peers[j] = i;
            rank_to_peer[i] = j;
            DBG("peers[%d]=rank %d\n", j, i);
            ++j;
        } else {
            // self reference is forbidden
            rank_to_peer[i] = bad_peer;
        }
    }
    n_peers = j;
    assert(comm_size-1 == n_peers);
    

    //CUDA context initialization
    cudaFree(0);
    DBG("mp_init(m_peers=%d, gpuId=%d), comm_size=%d\n", n_peers, gpuId, comm_size);
    MP_CHECK(mp_init(comm, peers, n_peers, MP_INIT_DEFAULT, gpuId));
    
    // init ready stuff
    size_t table_size = MAX(sizeof(*ready_table) * comm_size, PAGE_SIZE);

    ready_values = (uint32_t *)malloc(table_size);
    assert(ready_values);
    DBG("ready_values (%p, sizeof=%zd) ok\n", ready_values, sizeof(*ready_values));

    //Why does it return NULL pointer without ret != 0???
    //assert(0 == posix_memalign((void **)&remote_ready_values, PAGE_SIZE, table_size));
    remote_ready_values = (uint32_t *)malloc(table_size);
    assert(remote_ready_values);
    DBG("remote_ready_values (%p, sizeof=%zd) ok\n", remote_ready_values, sizeof(*remote_ready_values));

    //Why does it return NULL pointer without ret != 0???
    //assert(0 == posix_memalign((void **)&ready_table, PAGE_SIZE, table_size));
    ready_table = (uint32_t *)malloc(table_size);
    assert(ready_table);
    DBG("ready_table (%p, sizeof=%zd) ok\n", ready_table, sizeof(*ready_table));
    
    for (i=0; i<comm_size; ++i) {
        //DBG("Setting values for ready_table %d (%p)\n", i, &ready_table[i]);
        ready_table[i] = 0;  // remotely written table
        //DBG("Setting values for ready_values %d (%p)\n", i, &ready_values[i]);
        ready_values[i] = 1; // locally expected value
        //DBG("Setting values for remote_ready_values %d (%p)\n", i, &remote_ready_values[i]);
        remote_ready_values[i] = 1; // value to be sent remotely
    }
    iomb();

    DBG("registering ready_table size=%zd\n", table_size);
    MP_CHECK(mp_register(ready_table, table_size, &ready_table_reg));
    DBG("creating ready_table window\n");
    MP_CHECK(mp_window_create(ready_table, table_size, &ready_table_win));
    DBG("registering remote_ready_table\n");
    MP_CHECK(mp_register(remote_ready_values, table_size, &remote_ready_values_reg));

    comm_initialized = 1;

    return 0;
}

void comm_finalize() {
    mp_finalize();
}

static size_t comm_size_of_mpi_type(MPI_Datatype mpi_type)
{
    size_t ret = 0;

    if (mpi_type == MPI_DOUBLE) {
        ret = sizeof(double);
    }
    else if (mpi_type == MPI_FLOAT) {
        ret = sizeof(float);
    }
    else if (mpi_type == MPI_CHAR) {
        ret = sizeof(char);
    }
    else if (mpi_type == MPI_INT) {
        ret = sizeof(int);
    }
    else {
        comm_err("invalid type %x\n", mpi_type);
        exit(1);
    }
    return ret;
}

static int comm_mpi_rank_to_peer(int rank)
{
    assert(comm_initialized);
    assert(rank < comm_size);
    assert(rank >= 0);
    int peer = rank;
    //int peer = rank_to_peer[rank];
    //assert(peer != bad_peer);
    //assert(peer < n_peers);
    return peer;
}

static mp_reg_t *comm_reg(void *ptr, size_t size)
{
    assert(comm_initialized);
    return NULL;
}

static void atomic_inc(uint32_t *ptr)
{
    __sync_fetch_and_add(ptr, 1);
    //++ACCESS_ONCE(*ptr);
    iomb();
}

static void comm_track_request(mp_request_t *req)
{
    assert(n_reqs < MAX_REQS);    
    reqs[n_reqs++] = *req;
    DBG("n_reqs=%d\n", n_reqs);
}

/* ==================================== SEND/WAIT READY ACK ==================================== */

int comm_send_ready_on_stream(int rank, comm_request_t *creq, cudaStream_t stream)
{
    assert(comm_initialized);
    assert(rank < comm_size);
    int ret = 0;
    int peer = comm_mpi_rank_to_peer(rank);
    mp_request_t *req = (mp_request_t*)creq;
    assert(req);
    int remote_offset = /*self rank*/comm_rank * sizeof(uint32_t);
    DBG("dest_rank=%d/%d payload=%x offset=%d\n", rank, peer, remote_ready_values[rank], remote_offset);
    MP_CHECK(mp_iput_on_stream(&remote_ready_values[rank], sizeof(uint32_t), &remote_ready_values_reg, 
                               peer, remote_offset, &ready_table_win, req, MP_PUT_INLINE /* MP_PUT_NOWAIT */, stream));
    comm_track_request(req);
    //MP_CHECK(mp_wait(req));
    atomic_inc(&remote_ready_values[rank]);
    return ret;
}

int comm_send_ready(int rank, comm_request_t *creq)
{
    assert(comm_initialized);
    assert(rank < comm_size);
    int ret = 0;
    int peer = comm_mpi_rank_to_peer(rank);
    mp_request_t *req = (mp_request_t*)creq;
    assert(req);
    int remote_offset = /*my rank*/comm_rank * sizeof(uint32_t);
    DBG("dest_rank=%d payload=%x offset=%d\n", rank, remote_ready_values[rank], remote_offset);
    MP_CHECK(mp_iput(&remote_ready_values[rank], sizeof(uint32_t), &remote_ready_values_reg, 
                     peer, remote_offset, &ready_table_win, req, MP_PUT_INLINE | MP_PUT_NOWAIT));
    //MP_CHECK(mp_wait(req));
    //comm_track_request(req);
    atomic_inc(&remote_ready_values[rank]);
    return ret;
}

int comm_wait_ready_on_stream(int rank, cudaStream_t stream)
{
    assert(comm_initialized);
    assert(rank < comm_size);
    int ret = 0;
    int peer = comm_mpi_rank_to_peer(rank);
    DBG("rank=%d payload=%x\n", rank, ready_values[rank]); 
    MP_CHECK(mp_wait_dword_geq_on_stream(&ready_table[rank], ready_values[rank], stream));
    ready_values[rank]++;
    return ret;
}

int comm_wait_ready(int rank)
{
    assert(comm_initialized);
    assert(rank < comm_size);
    int ret = 0;
    int peer = comm_mpi_rank_to_peer(rank);
    int cnt = 0;
    DBG("rank=%d payload=%x\n", rank, ready_values[rank]);
    while (ACCESS_ONCE(ready_table[rank]) < ready_values[rank]) {
        rmb();
        arch_cpu_relax();
        ++cnt;
        if (cnt > 10000) {
            comm_progress();
            cnt = 0;
        }
    }
    ready_values[rank]++;
    return ret;
}

int comm_test_ready(int rank, int *p_rdy)
{
    assert(comm_initialized);
    assert(rank < comm_size);
    int ret = 0;
    int peer = comm_mpi_rank_to_peer(rank);
    static int cnt = 0;
    DBG("rank=%d payload=%x\n", rank, ready_values[rank]);
    do {
        rmb();
        *p_rdy = !(ACCESS_ONCE(ready_table[rank]) < ready_values[rank]);
        if (*p_rdy) {
            ++ready_values[rank];
            break;
        }
        ++cnt;
        if (cnt > 10000) {
            arch_cpu_relax();
            cnt = 0;
        }
    } while(0);
    return ret;
}
/* ============================================================================================= */


/* ==================================== WAIT REQS ==================================== */

int comm_wait_all_on_stream(int count, comm_request_t *creqs, cudaStream_t stream)
{
    int ret = 0;
    DBG("count=%d\n", count);
    assert(comm_initialized);
    mp_request_t *reqs = (mp_request_t*)creqs;
    if (1 == count) {
        assert(*reqs);
        MP_CHECK(mp_wait_on_stream(reqs, stream));
    } else {
        MP_CHECK(mp_wait_all_on_stream(count, reqs, stream));
    }
    memset(creqs, 0, sizeof(comm_request_t)*count);
    return ret;
}

int comm_wait_all(int count, comm_request_t *creqs)
{
    int ret = 0;
    DBG("count=%d\n", count);
    assert(comm_initialized);
    mp_request_t *reqs = (mp_request_t*)creqs;
    MP_CHECK(mp_wait_all(count, reqs));
    memset(creqs, 0, sizeof(comm_request_t)*count);
    return ret;
}

int comm_wait(comm_request_t *creq)
{
    int ret = 0;
    assert(comm_initialized);
    mp_request_t *req = (mp_request_t*)creq;
    MP_CHECK(mp_wait(req));
    memset(creq, 0, sizeof(comm_request_t));
    return ret;
}
/* ====================================================================================== */

// tags are not supported!!!
/* ============================== SEND/RECV ===================================== */
int comm_irecv(void *recv_buf, size_t size, MPI_Datatype type, comm_reg_t *creg,
               int src_rank, comm_request_t *creq)
{
    assert(comm_initialized);
    int ret = 0, retcode = 0;
    mp_reg_t *reg;
    mp_request_t *req = (mp_request_t*)creq;
    size_t nbytes = size*comm_size_of_mpi_type(type);
    int peer = comm_mpi_rank_to_peer(src_rank);

    DBG("src_rank=%d peer=%d nbytes=%zd buf=%p\n", src_rank, peer, nbytes, recv_buf);

    COMM_CHECK(comm_register(recv_buf, nbytes, creg));
    reg = (mp_reg_t*)creg;
    assert(reg);

    retcode = mp_irecv(recv_buf, nbytes, peer, reg, req);
    if (retcode) {
        comm_err("error in mp_irecv ret=%d\n", retcode);
        ret = -1;
        comm_deregister(creg);
        goto out;
    }

    comm_track_request(req);
out:
    return ret;
}

//Trigger the send
int comm_post_isend_stream_exp(int dest_rank, comm_request_t *creq, cudaStream_t stream)
{
    int ret = 0, retcode = 0;
    assert(comm_initialized);
    assert(creq);
    mp_request_t *req = (mp_request_t*)creq;
    int peer = comm_mpi_rank_to_peer(dest_rank);

    retcode = mp_post_send_on_stream_exp(peer, req, stream);
    if (retcode) {
        comm_err("error in mp_post_send_on_stream_exp ret=%d\n", retcode);
        ret = -1;
        goto out;
    }

    out:
        return ret;
}

int comm_isend_on_stream(void *send_buf, size_t size, MPI_Datatype type, comm_reg_t *creg,
                         int dest_rank, comm_request_t *creq, cudaStream_t stream)
{
    assert(comm_initialized);
    int ret = 0, retcode = 0;
    mp_reg_t *reg;
    mp_request_t *req = (mp_request_t*)creq;
    size_t nbytes = size*comm_size_of_mpi_type(type);
    int peer = comm_mpi_rank_to_peer(dest_rank);

    DBG("dest_rank=%d peer=%d nbytes=%zd\n", dest_rank, peer, nbytes);

    COMM_CHECK(comm_register(send_buf, nbytes, creg));
    reg = (mp_reg_t*)creg;
    assert(reg);
    
    retcode = mp_isend_on_stream(send_buf, nbytes, peer, reg, req, stream);
    if (retcode) {
        comm_err("error in mp_isend_on_stream ret=%d\n", retcode);
        ret = -1;
        comm_deregister(creg);
        goto out;
    }

    comm_track_request(req);
out:
    return ret;
}

int comm_isend(void *send_buf, size_t size, MPI_Datatype type, comm_reg_t *creg,
               int dest_rank, comm_request_t *creq)
{
    assert(comm_initialized);
    int ret = 0, retcode = 0;
    mp_reg_t *reg;
    mp_request_t *req = (mp_request_t*)creq;
    size_t nbytes = size*comm_size_of_mpi_type(type);
    int peer = comm_mpi_rank_to_peer(dest_rank);

    DBG("dest_rank=%d peer=%d nbytes=%zd\n", dest_rank, peer, nbytes);

    COMM_CHECK(comm_register(send_buf, nbytes, creg));
    reg = (mp_reg_t*)creg;
    assert(reg);

    retcode = mp_isend(send_buf, nbytes, peer, reg, req);
    if (retcode) {
        comm_err("error in mp_isend ret=%d\n", retcode);
        ret = -1;
        comm_deregister(creg);
        goto out;
    }

    comm_track_request(req);

out:
    return ret;
}
/* ====================================================================================== */


/* ============================== FLUSH/CLEANUP CQ ===================================== */
//N.B. it can be very expensive with a lot of pending creqs (i.e. HPGMG, 32 ranks)
int comm_progress()
{
    int ret=0;

    DBG("n_reqs=%d\n", n_reqs);
    assert(n_reqs < MAX_REQS);
    ret = mp_progress_all(n_reqs, reqs);

    return ret;
}
 
int comm_flush()
{
    int ret = 0;
    DBG("n_reqs=%d\n", n_reqs);
    if(n_reqs == 0)
        return ret;

    assert(n_reqs < MAX_REQS);
    ret = mp_wait_all(n_reqs, reqs);
    n_reqs=0;

    return ret;
}
/* ====================================================================================== */


/* ============================== KI MODEL CALLS ===================================== */
static struct comm_dev_descs *dreqs()
{
    if (!pdreqs) {
        cudaError_t err;
        err = cudaHostAlloc(&pdreqs, n_dreqs*sizeof(*pdreqs), 
                            cudaHostAllocPortable | cudaHostAllocMapped 
                            /*|cudaHostAllocWriteCombined*/ );
        if (err != cudaSuccess) {
            comm_err("error while allocating comm_dev_descs, exiting...\n");
            exit(-1);
        }
        assert(pdreqs);
        memset(pdreqs, 0, n_dreqs*sizeof(*pdreqs));
        dreq_idx = 0;
    }
    return pdreqs + dreq_idx;
}
    
int comm_prepare_wait_ready(int rank)
{
    assert(comm_initialized);
    assert(rank < comm_size);
    int ret = 0;
    int peer = comm_mpi_rank_to_peer(rank);
    DBG("rank=%d payload=%x n_ready=%d\n", rank, ready_values[rank], dreqs()->n_ready);
    MP_CHECK(mp::mlx5::get_descriptors(&dreqs()->ready[dreqs()->n_ready++], &ready_table[rank], ready_values[rank]));
    //dreqs.ready.ptr = &ready_table[rank];
    //dreqs.ready.value = ready_values[rank];
    ready_values[rank]++;
    return ret;
}

int comm_prepare_isend(void *send_buf, size_t size, MPI_Datatype type, comm_reg_t *creg,
                       int dest_rank, comm_request_t *creq)
{
    assert(comm_initialized);
    int ret = 0, retcode = 0;
    mp_reg_t *reg;
    mp_request_t *req = (mp_request_t*)creq;
    size_t nbytes = size*comm_size_of_mpi_type(type);
    int peer = comm_mpi_rank_to_peer(dest_rank);

    DBG("dest_rank=%d peer=%d nbytes=%zd\n", dest_rank, peer, nbytes);

    COMM_CHECK(comm_register(send_buf, nbytes, creg));
    reg = (mp_reg_t*)creg;
    assert(reg);

    retcode = mp_send_prepare(send_buf, nbytes, peer, reg, req);
    if (retcode) {
        comm_err("error in mp_send_prepare ret=%d\n", retcode);
        ret = -1;
        comm_deregister(creg);
        goto out;
    }

    retcode = mp::mlx5::get_descriptors(&dreqs()->tx[dreqs()->n_tx++], req);
    if (retcode) {
        comm_err("error in comm_prepare_isend/get_descriptors ret=%d\n", retcode);
        ret = -1;
        comm_deregister(creg);
        goto out;
    }

    DBG("prepares send=%p (%d), rank: %d, %d\n", &dreqs()->tx[(dreqs()->n_tx-1)], dreqs()->n_tx, peer, dest_rank);

    comm_track_request(req);
out:
    return ret;

unreg:
    // BUG: call mp_unregister

err:
    return ret;
}

int comm_prepare_isend_exp(void *send_buf, size_t size, MPI_Datatype type, 
                            int dest_rank,
                            comm_reg_t *creg, comm_request_t *creq,
                            mp_send_info_t * mp_sinfo)
{
    int ret = 0, retcode = 0;
    assert(mp_sinfo);
    assert(comm_initialized);
    mp_reg_t *reg;
    mp_request_t *req = (mp_request_t*)creq;
    size_t nbytes = size*comm_size_of_mpi_type(type);
    int peer = comm_mpi_rank_to_peer(dest_rank);

    DBG("dest_rank=%d peer=%d nbytes=%zd\n", dest_rank, peer, nbytes);

    MP_CHECK(mp_alloc_send_info(mp_sinfo, MP_GPUMEM));

    COMM_CHECK(comm_register(send_buf, nbytes, creg));
    reg = (mp_reg_t*)creg;
    assert(reg);

    retcode = mp_prepare_send_exp(send_buf, nbytes, peer, reg, req, mp_sinfo);
    if (retcode) {
        comm_err("error in mp_send_prepare ret=%d\n", retcode);
        ret = -1;
        comm_deregister(creg);
        goto out;
    }

    comm_track_request(req);

    out:
        return ret;

    //Second method: prepare descriptors
    //MP_CHECK(mp_prepare_send_exp((void *)((uintptr_t)sbuf_d + size*j), size, peer, &sreg, &sreq[j], &mp_sinfo));
}

int comm_prepare_wait_all(int count, comm_request_t *creqs)
{
    int retcode;
    int ret = 0;
    DBG("count=%d\n", count);
    assert(comm_initialized);
    mp_request_t *req = (mp_request_t*)creqs;
    for (int i=0; i < count; ++i) {
        retcode = mp::mlx5::get_descriptors(&dreqs()->wait[dreqs()->n_wait++], req+i);
        if (retcode) {
            comm_err("error in get_descriptors(wait) (%d)\n", retcode);
            ret = -1;
            goto out;
        }
    }
    //memset(creqs, 0, sizeof(comm_request_t)*count);
out:
    return ret;
}

// Note: this is ugly and non thread-safe
comm_dev_descs_t comm_prepared_requests()
{
    comm_dev_descs_t ret;
    //static struct comm_dev_descs local;
    // copy dreqs to static storage
    //memcpy(&local, dreqs(), sizeof(local));
    // return static storage
    ret = dreqs();
    // super ugly
    dreq_idx = (dreq_idx + 1) % n_dreqs;
    // reset dreqs for next usage
    memset(dreqs(), 0, sizeof(struct comm_dev_descs));
    return ret;
}
/* ====================================================================================== */

/*  ==================================== OTHERS ==================================== */
int comm_register(void *buf, size_t size, comm_reg_t *creg)
{
    assert(comm_initialized);
    int ret = 0;
    int retcode;
    mp_reg_t *reg = (mp_reg_t*)creg;
    assert(reg);

    if (!size) {
        ret = -EINVAL;
        comm_err("SIZE==0\n");
        goto out;
    }

    if (!*reg) {
        DBG("registering buffer %p\n", buf);
        MP_CHECK(mp_register(buf, size, reg));
    }

out:
    return ret;
}

int comm_deregister(comm_reg_t *creg)
{
    assert(comm_initialized);
    int ret = 0;
    mp_reg_t *reg = (mp_reg_t*)creg;

    if (*reg) {
	MP_CHECK(mp_deregister(reg));
	*creg=NULL;
    }

out:
    return ret;
}

int comm_select_device(int mpiRank)
{
    int numDevices=0;
    int gpuId=0;

    char * value = getenv("COMM_USE_GPU_ID"); 
    if (value != NULL) {
        gpuId = atoi(value);
    }
    else
    {
        // Default policy
        cudaGetDeviceCount(&numDevices);
        gpuId = mpiRank % numDevices;
    }

    DBG("Using GPU ID: %d\n", gpuId);

    return gpuId;
}
/*  ================================================================================ */

