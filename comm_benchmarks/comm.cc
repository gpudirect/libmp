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

static int startGlobalReqsIndex=0;
static int startGlobalFlushReqsIndex=0;

#define MAX_REQS 16384 //32768 //1048576
static mp_request_t reqs[MAX_REQS];
static int n_reqs = 0;

#define PAGE_BITS 12
#define PAGE_SIZE (1ULL<<PAGE_BITS)
#define PAGE_OFF  (PAGE_SIZE-1)
#define PAGE_MASK (~(PAGE_OFF))

#define mb()    __asm__ volatile("mfence":::"memory")
#define rmb()   __asm__ volatile("lfence":::"memory")
#define wmb()   __asm__ volatile("sfence" ::: "memory")
#define iomb() mb()

#define ACCESS_ONCE(V)                          \
    (*(volatile __typeof__ (V) *)&(V))

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

int comm_use_gdrdma()
{
    static int use_gdrdma = -1;
    if (-1 == use_gdrdma) {
        const char *env = getenv("COMM_USE_GDRDMA");
        if (env) {
            use_gdrdma = !!atoi(env);
            printf("WARNING: %s CUDA-aware/RDMA communications\n", (use_gdrdma)?"enabling":"disabling");
        } else
            use_gdrdma = 0; // default
    }
    return use_gdrdma;
}

int comm_use_gpu_comm()
{
    static int use_gpu_comm = -1;
    if (-1 == use_gpu_comm) {
        const char *env = getenv("COMM_USE_GPU_COMM");
        if (env) {
            use_gpu_comm = !!atoi(env);
            printf("WARNING: %s GPU-initiated communications\n", (use_gpu_comm)?"enabling":"disabling");
        } else
            use_gpu_comm = 0; // default
    }
    return use_gpu_comm;
}

int comm_use_async()
{
    static int use_async = -1;
    if (-1 == use_async) {
        const char *env = getenv("COMM_USE_ASYNC");
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
    DBG("n_peers=%d\n", n_peers);

    //CUDA context initialization
    cudaFree(0);
    MP_CHECK(mp_init(comm, peers, n_peers, MP_INIT_DEFAULT, gpuId));

    // init ready stuff
    size_t table_size = MAX(sizeof(*ready_table) * comm_size, PAGE_SIZE);
    assert(0 == posix_memalign((void **)&ready_table, PAGE_SIZE, table_size));
    assert(ready_table);

    ready_values = (uint32_t *)malloc(table_size);
    assert(ready_values);

    assert(0 == posix_memalign((void **)&remote_ready_values, PAGE_SIZE, table_size));
    assert(remote_ready_values);
    
    for (i=0; i<comm_size; ++i) {
        ready_table[i] = 0;  // remotely written table
        ready_values[i] = 1; // locally expected value
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
    if (mpi_type == MPI_FLOAT) {
        ret = sizeof(float);
    }
    else if (mpi_type == MPI_CHAR) {
        ret = sizeof(char);
    }
    else if (mpi_type == MPI_INT) {
        ret = sizeof(int);
    }
     else {
        comm_err("invalid type\n");
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
    /*
    if(comm_rank == 0)
        printf("TRACK n_reqs: %d\n", n_reqs);
    */
    DBG("n_reqs=%d\n", n_reqs);
}

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
                               peer, remote_offset, &ready_table_win, req, MP_PUT_INLINE | MP_PUT_NOWAIT, stream));
    //MP_CHECK(mp_wait(req));
    //comm_track_request(req);
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

// tags are not supported!!!
int comm_irecv(void *recv_buf, size_t size, MPI_Datatype type, comm_reg_t *creg,
               int src_rank, comm_request_t *creq)
{
    assert(comm_initialized);
    int ret = 0;
    int retcode;
    size_t nbytes = size*comm_size_of_mpi_type(type);
    mp_reg_t *reg = (mp_reg_t*)creg;
    assert(reg);
    mp_request_t *req = (mp_request_t*)creq;
    assert(req);
    int peer = comm_mpi_rank_to_peer(src_rank);

    DBG("src_rank=%d peer=%d nbytes=%zd buf=%p *reg=%p\n", src_rank, peer, nbytes, recv_buf, *reg);

    if (!size) {
        ret = -EINVAL;
        comm_err("SIZE==0\n");
        goto out;
    }

    if (!*reg) {
        DBG("registering buffer %p\n", recv_buf);
        MP_CHECK(mp_register(recv_buf, nbytes, reg));
    }

    retcode = mp_irecv(recv_buf,
                       nbytes,
                       peer,
                       reg,
                       req);
    if (retcode) {
        comm_err("error in mp_irecv ret=%d\n", retcode);
        ret = -1;
        goto out;
    }
    comm_track_request(req);
out:
    return ret;
}

int comm_isend_on_stream(void *send_buf, size_t size, MPI_Datatype type, comm_reg_t *creg,
                         int dest_rank, comm_request_t *creq, cudaStream_t stream)
{
    assert(comm_initialized);
    int ret = 0;
    int retcode;
    size_t nbytes = size*comm_size_of_mpi_type(type);
    mp_reg_t *reg = (mp_reg_t*)creg;
    assert(reg);
    mp_request_t *req = (mp_request_t*)creq;
    int peer = comm_mpi_rank_to_peer(dest_rank);

    DBG("dest_rank=%d peer=%d nbytes=%zd\n", dest_rank, peer, nbytes);

    if (!size) {
        ret = -EINVAL;
        comm_err("SIZE==0\n");
        goto out;
    }

    if (!*reg) {
        DBG("registering buffer %p\n", send_buf);
        MP_CHECK(mp_register(send_buf, nbytes, reg));
    }
    retcode = mp_isend_on_stream(send_buf,
                                 nbytes,
                                 peer,
                                 reg,
                                 req,
                                 stream);
    if (retcode) {
        comm_err("error in mp_isend_on_stream ret=%d\n", retcode);
        ret = -1;
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
    int ret = 0;
    int retcode;
    size_t nbytes = size*comm_size_of_mpi_type(type);
    mp_reg_t *reg = (mp_reg_t*)creg;
    assert(reg);
    mp_request_t *req = (mp_request_t*)creq;
    int peer = comm_mpi_rank_to_peer(dest_rank);

    DBG("dest_rank=%d peer=%d nbytes=%zd\n", dest_rank, peer, nbytes);

    if (!size) {
        ret = -EINVAL;
        comm_err("SIZE==0\n");
        goto out;
    }

    if (!*reg) {
        DBG("registering buffer %p\n", send_buf);
        MP_CHECK(mp_register(send_buf, nbytes, reg));
    }

    retcode = mp_isend(send_buf,
                       nbytes,
                       peer,
                       reg,
                       req);
    if (retcode) {
        comm_err("error in mp_isend ret=%d\n", retcode);
        ret = -1;
        goto out;
    }
    comm_track_request(req);
out:
    return ret;
}

int comm_register(void *buf, size_t size, MPI_Datatype type, comm_reg_t *creg)
{
    assert(comm_initialized);
    int ret = 0;
    int retcode;
    size_t nbytes = size * comm_size_of_mpi_type(type);
    mp_reg_t *reg = (mp_reg_t*)creg;
    assert(reg);

    if (!size) {
        ret = -EINVAL;
        comm_err("SIZE==0\n");
        goto out;
    }

    if (!*reg) {
        DBG("registering buffer %p\n", buf);
        MP_CHECK(mp_register(buf, nbytes, reg));
    }

out:
    return ret;
}


static struct comm_dev_descs *pdreqs = NULL;
static const size_t n_dreqs = 128; //36 prima
static int dreq_idx = 0;
#define CONST_FLUSH 1024
#define CONST_PROGRESS 50

int comm_progress()
{
    int ret=0;

    DBG("n_reqs=%d\n", n_reqs);
    assert(n_reqs < MAX_REQS);
    ret = mp_progress_all(n_reqs, reqs);
#if 0
   // if( (startGlobalReqsIndex+CONST_PROGRESS) < n_reqs)
   // {
//        if(comm_rank == 0)
 //           printf("progress CONST_PROGRESS da req: startGlobalReqsIndex %d, n_reqs: %d\n", startGlobalReqsIndex, n_reqs);
        
        ret = p_progress_all(CONST_PROGRESS, reqs+startGlobalReqsIndex); //*CONST_PROGRESS));
        if (ret < 0)
            comm_err("ret=%d\n", ret);

    //    startGlobalReqsIndex = (startGlobalReqsIndex+CONST_PROGRESS)%MAX_REQS; //(startGlobalReqsIndex+1)%MAX_REQS;
   // }
#endif
    return ret;
}

 
int comm_flush()
{
    int ret = 0;
    DBG("n_reqs=%d\n", n_reqs);
    if(n_reqs == 0)
        return ret;

    assert(n_reqs < MAX_REQS);
    //int diff = n_reqs - startGlobalFlushReqsIndex;
    ret = mp_wait_all(n_reqs, reqs); //+startGlobalFlushReqsIndex);
    n_reqs=0;
    startGlobalReqsIndex=0;
    startGlobalFlushReqsIndex=0;

    return ret;
}

#if 0

int comm_flush_new()
{
    int ret = 0;
    DBG("n_reqs=%d\n", n_reqs);
    assert(n_reqs < MAX_REQS);
#if 0
    do {
        rmb();
        uint32_t w0 = ACCESS_ONCE(ready_table[0]);
        uint32_t w1 = ACCESS_ONCE(ready_table[1]);
        DBG("ready_table: %08x %08x\n", w0, w1);
        ret = mp_progress_all(n_reqs, reqs);
        arch_cpu_relax();
        cudaStreamQuery(NULL);
    } while(ret < n_reqs);
#endif
/*
    ret = mp_wait_all(n_reqs, reqs);
    n_reqs=0;
    startGlobalReqsIndex=0;
*/
#if 1
     //int tmp = n_reqs;
    if( (startGlobalFlushReqsIndex+CONST_FLUSH) < n_reqs)
    {
        //if(comm_rank == 0)
        //    printf("WAIT FLUSH da req: startGlobalFlushReqsIndex %d, n_reqs: %d\n", startGlobalFlushReqsIndex, n_reqs);

        ret = mp_wait_all(CONST_FLUSH, reqs+startGlobalFlushReqsIndex);
        if (ret) {
            comm_err("got error in mp_wait_all ret=%d\n", ret);
            exit(EXIT_FAILURE);
        }

        if((startGlobalFlushReqsIndex+CONST_FLUSH) == MAX_REQS)
        {
            n_reqs = 0;
            startGlobalReqsIndex=0;
            startGlobalFlushReqsIndex = 0;

       //    if(comm_rank == 0)
        //        printf("WAIT FLUSH 0 da startGlobalFlushReqsIndex: %d, startGlobalReqsIndex: %d\n", startGlobalFlushReqsIndex, startGlobalReqsIndex);
        }
        else
        {
            startGlobalFlushReqsIndex = (startGlobalFlushReqsIndex+CONST_FLUSH)%MAX_REQS;
            if(startGlobalFlushReqsIndex > startGlobalReqsIndex)
                startGlobalReqsIndex = startGlobalFlushReqsIndex;

        //    if(comm_rank == 0)
        //        printf("WAIT FLUSH ++ da startGlobalFlushReqsIndex: %d, startGlobalReqsIndex: %d\n", startGlobalFlushReqsIndex, startGlobalReqsIndex);
        }
       

        //n_reqs -= CONST_FLUSH;
//        startGlobalReqsIndex--;
    }
    #endif
    return ret;
}

int comm_flush_force()
{
    int ret = 0;
    DBG("n_reqs=%d\n", n_reqs);
    assert(n_reqs < MAX_REQS);

    if((n_reqs-startGlobalFlushReqsIndex) > 0)
    {

    //    if(comm_rank == 0)
    //        printf("WAIT FORCE flush startGlobalFlushReqsIndex: %d, (n_reqs-startGlobalFlushReqsIndex): %d\n", startGlobalFlushReqsIndex, (n_reqs-startGlobalFlushReqsIndex));

        ret = mp_wait_all(n_reqs-startGlobalFlushReqsIndex, reqs+startGlobalFlushReqsIndex);
        if (ret) {
            comm_err("got error in mp_wait_all ret=%d\n", ret);
            exit(EXIT_FAILURE);
        }

        n_reqs=0;
        startGlobalFlushReqsIndex=0;
        startGlobalReqsIndex=0;
    }

    return ret;
}
#endif

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
    int ret = 0;
    int retcode;
    size_t nbytes = size*comm_size_of_mpi_type(type);
    mp_reg_t *reg = (mp_reg_t*)creg;
    assert(reg);
    mp_request_t *req = (mp_request_t*)creq;
    int peer = comm_mpi_rank_to_peer(dest_rank);

    DBG("dest_rank=%d peer=%d nbytes=%zd\n", dest_rank, peer, nbytes);

    if (!size) {
        ret = -EINVAL;
        comm_err("SIZE==0\n");
        goto err;
    }

    if (!*reg) {
        DBG("registering buffer %p\n", send_buf);
        MP_CHECK(mp_register(send_buf, nbytes, reg));
    }

    retcode = mp_send_prepare(send_buf, nbytes, peer, reg, req);
    if (retcode) {
        // BUG: call mp_unregister
        comm_err("error in mp_isend_on_stream ret=%d\n", retcode);
        ret = -1;
        goto unreg;
    }

    retcode = mp::mlx5::get_descriptors(&dreqs()->tx[dreqs()->n_tx++], req);
    if (retcode) {
        comm_err("error in mp_isend_on_stream ret=%d\n", retcode);
        ret = -1;
        goto unreg;
    }

    comm_track_request(req);

    return ret;

unreg:
    // BUG: call mp_unregister

err:
    return ret;
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

//DGX helper
int comm_select_device(int mpiRank)
{
    int numDevices=0;
    int gpuId=0;

    char * value = getenv("USE_GPU"); 
    if (value != NULL) {
        gpuId = atoi(value);
        //DBG("USE_GPU: %d\n", gpuId);
    }
    else
    {
        // query number of GPU devices in the system
        cudaGetDeviceCount(&numDevices);
        gpuId = mpiRank % numDevices;
    }

    return gpuId;
}
