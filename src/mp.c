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

#if HAVE_CONFIG_H
#  include <config.h>
#endif /* HAVE_CONFIG_H */

#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <limits.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <time.h>
#include <cuda.h>
#include <cudaProfiler.h>
#include <mpi.h>

#include "mp.h"
#include "mp_internal.h"
#include "archutils.h"

#define DADO_DEBUG

int mp_dbg_is_enabled = -1;
int use_event_sync = 0;
int mp_enable_ud = 0;
int mp_enable_ipc = 0;
int use_wq_gpu = 0;
int use_rx_cq_gpu = 0;
int use_tx_cq_gpu = 0;
int use_dbrec_gpu = 0;

int mp_warn_is_enabled = -1;

MPI_Comm mpi_comm;
int mpi_comm_size;
int mpi_comm_rank;
int ipc_num_procs = 0;
int smp_num_procs = 0;
int smp_leader;
int smp_local_rank = -1;

int cq_poll_count = 20;
client_t *clients;
const int bad_index = -1;
int *client_index;
int client_count;
gds_send_request_t *gds_send_info_region = NULL;
gds_wait_request_t *gds_wait_info_region = NULL;
mem_region_t *mem_region_list = NULL;
struct mp_request *mp_request_free_list = NULL;
int mp_request_active_count;
int mp_request_limit = 512;

struct ibv_device *ib_dev = NULL;
int ib_port = 1;
ib_context_t *ib_ctx = NULL;
int smp_depth = 256;
int ib_tx_depth = 256*2;
int ib_rx_depth = 256*2;
int num_cqes = 256; // it gets actually rounded up to 512
int ib_max_sge = 30;
int ib_inline_size = 64;
struct ibv_port_attr ib_port_attr;

struct node_info { 
    char hname[20];
    int gpu_id;
};

struct node_info *node_info_all;

#define MPI_CHECK(stmt)                                             \
do {                                                                \
    int result = (stmt);                                            \
    if (MPI_SUCCESS != result) {                                    \
        char string[MPI_MAX_ERROR_STRING];                          \
        int resultlen = 0;                                          \
        MPI_Error_string(result, string, &resultlen);               \
        mp_err_msg(" (%s:%d) MPI check failed with %d (%*s)\n",     \
                   __FILE__, __LINE__, result, resultlen, string);  \
        exit(-1);                                                   \
    }                                                               \
} while(0)

#define CU_CHECK(stmt)                                  \
do {                                                    \
    CUresult result = (stmt);                           \
    if (CUDA_SUCCESS != result) {                       \
        fprintf(stderr, "[%s:%d] [%d] cu failed with %d \n",    \
         __FILE__, __LINE__, mpi_comm_rank, result);    \
        exit(-1);                                       \
    }                                                   \
    assert(CUDA_SUCCESS == result);                     \
} while (0)


#define CUDA_CHECK(stmt)                                \
do {                                                    \
    cudaError_t result = (stmt);                        \
    if (cudaSuccess != result) {                        \
        fprintf(stderr, "[%s:%d] [%d] cuda failed with %s \n",   \
         __FILE__, __LINE__, mpi_comm_rank, cudaGetErrorString(result)); \
        exit(-1);                                       \
    }                                                   \
    assert(cudaSuccess == result);                      \
} while (0)

int mp_dbg_enabled()
{
    if (-1 == mp_dbg_is_enabled) {
        const char *env = getenv("MP_ENABLE_DEBUG");
        if (env) {
            int en = atoi(env);
            mp_dbg_is_enabled = !!en;
            //printf("MP_ENABLE_DEBUG=%s\n", env);
        } else
            mp_dbg_is_enabled = 0;
    }
    return mp_dbg_is_enabled;
}

void mp_enable_dbg(int enabled)
{
    mp_dbg_is_enabled = !!enabled;
}

int mp_warn_enabled()
{
    if (-1 == mp_warn_is_enabled) {
        const char *env = getenv("MP_ENABLE_WARN");
        if (env) {
            int en = atoi(env);
            mp_warn_is_enabled = !!en;
        } else
            mp_warn_is_enabled = 1;
    }
    return mp_warn_is_enabled;
}

/*TODO: need to check for handle id to detect reallocations*/
static inline void ipc_handle_cache_find (void *addr, int size, ipc_handle_cache_entry_t **entry, int peer)
{
    int cidx;
    ipc_handle_cache_entry_t *temp = NULL;

    *entry = NULL;
    cidx = client_index[peer];

    temp = clients[cidx].ipc_handle_cache;
    while(temp) {
        void *handle_base = (peer == mpi_comm_rank) ? temp->base : temp->remote_base; 
	int handle_size = temp->size;
        if(!(((uint64_t)handle_base + handle_size <= (uint64_t)addr)
                || ((uint64_t)handle_base >= (uint64_t)addr + size))) {
            *entry = temp;
            break;
        }
        temp = temp->next;
    }
}

static inline void ipc_handle_cache_insert(ipc_handle_cache_entry_t *new_handle, int peer)
{
    int cidx;

    cidx = client_index[peer];

    new_handle->next = new_handle->prev = NULL;
    if (clients[cidx].ipc_handle_cache == NULL) {
        clients[cidx].ipc_handle_cache = new_handle;
    } else {
        new_handle->next = clients[cidx].ipc_handle_cache;
        clients[cidx].ipc_handle_cache->prev = new_handle;
        clients[cidx].ipc_handle_cache = new_handle;
    }
}

static void track_processed_ipc_stream_rreq(int peer, struct mp_request *req)
{
    int cidx = client_index[peer];
    client_t *client = &clients[cidx];
    
    if (!client->processed_ipc_rreq) { 
        // init 1st pending req
        assert(client->last_processed_ipc_rreq == NULL);
        client->processed_ipc_rreq = client->last_processed_ipc_rreq = req;
    } else {
        // append req to stream list
        client->last_processed_ipc_rreq->next = req;
	req->prev = client->last_processed_ipc_rreq;
	assert(req->next == NULL);
        client->last_processed_ipc_rreq = req;
    }
}

static void track_ipc_stream_rreq(int peer, struct mp_request *req)
{
    int cidx = client_index[peer];
    client_t *client = &clients[cidx];
    
    if (!client->posted_ipc_rreq) { 
        // init 1st pending req
        assert(client->last_posted_ipc_rreq == NULL);
        client->posted_ipc_rreq = client->last_posted_ipc_rreq = req;
    } else {
        // append req to stream list
        client->last_posted_ipc_rreq->next = req;
	req->prev = client->last_posted_ipc_rreq;
	assert(req->next == NULL);
        client->last_posted_ipc_rreq = req;
    }
}

static int cleanup_request(struct mp_request *req)
{
    if (req->sgv) {
        free(req->sgv);
        req->sgv = NULL;
    }

    return 0;
}

int progress_ipc ()
{
    int i, cidx, ret = 0;
    struct mp_request *req = NULL;
    smp_buffer_t *next;
    ipc_handle_cache_entry_t *entry = NULL;
    client_t *client = NULL;

    for (i=0; i<client_count; i++) {
        cidx = client_index[i];
        client = &clients[cidx];
        
	next = client->smp.local_buffer + client->smp.local_tail_process;
        if (!next->busy) { 
	    continue;
        }
        assert(next->free == 0); 

	//no receive had been posted
	if (!client->posted_ipc_rreq) { 
	    continue; 
        } else { 
	    req = client->posted_ipc_rreq;
	    if (req->status != MP_PENDING) {
		assert(req->status == MP_PENDING_NOWAIT);
		continue;
	    }
	}
	assert(!req);
	client->posted_ipc_rreq = client->posted_ipc_rreq->next;
	req->next = NULL;

	entry = NULL;
        ipc_handle_cache_find (next->addr, next->size, &entry, i);

	if (entry) {
	    assert (0 == memcmp(&next->handle, &entry->handle, sizeof(CUipcMemHandle)));
	} else {
            entry = malloc(sizeof(ipc_handle_cache_entry_t));
            if (!entry) {
                fprintf(stderr, "cache entry allocation failed \n");
                ret = MP_FAILURE;
                goto out;
            }

            CU_CHECK(cuIpcOpenMemHandle ((CUdeviceptr *) &entry->base, next->handle, CU_IPC_MEM_LAZY_ENABLE_PEER_ACCESS));
	    entry->remote_base = next->base_addr;
	    entry->size = next->base_size;

            ipc_handle_cache_insert(entry, i);
	}

        assert(entry != NULL); 

	//need node to process the req, initiate the copy; 

        track_processed_ipc_stream_rreq(i, req);
    }
out:
    return ret;
}

int progress_posted_list (mp_flow_t flow)
{
    int i, ret = 0;
    struct mp_request *req = NULL;

    if (!use_event_sync) 
	return ret;

    for (i=0; i<client_count; i++) {
        client_t *client = &clients[i];

        req = client->posted_stream_req[flow];

        while (req != NULL) { 
	    if (req->id > client->last_trigger_id[flow]) break;

            assert(req->status == MP_PREPARED);
            assert(req->type == MP_SEND || req->type == MP_RDMA);

            mp_dbg_msg("posting req id %d from posted_stream_req list trigger id :%d \n", req->id, client->last_trigger_id[flow]);

            ret = gds_post_send(client->qp, &req->in.sr, &req->out.bad_sr);
            if (ret) {
              fprintf(stderr, "posting send failed: %s \n", strerror(errno));
              goto out;
            }

            req->status = MP_PENDING;

            // remove request from waited list
            mp_dbg_msg("removing req %p from posted_stream_req list\n", req);

	    //delink the request
            if (req->next != NULL) {
	        req->next->prev = req->prev;
	    }
	    if (req->prev != NULL) {
	        req->prev->next = req->next; 
	    }	

	    //adjust head and tail
            if (client->posted_stream_req[flow] == req)
 	        client->posted_stream_req[flow] = req->next;
            if (client->last_posted_stream_req[flow] == req)
                client->last_posted_stream_req[flow] = req->prev;

	    //clear request links
	    req->prev = req->next = NULL;

	    req = client->posted_stream_req[flow];
        }
    }

out:
    return ret;
}

static int progress_request(struct mp_request *req)
{
    switch(req->status) {
    case MP_PREPARED:
        mp_dbg_msg("req=%p id=%d PREPARED\n", req, req->id);
        break;
    case MP_PENDING_NOWAIT:
        mp_dbg_msg("req=%p id=%d NOWAIT\n", req, req->id);
    case MP_PENDING:
        mp_dbg_msg("req=%p id=%d PENDING->COMPLETE\n", req, req->id);
        req->status = MP_COMPLETE;
        cleanup_request(req);
        break;
    case MP_COMPLETE:
        mp_warn_msg("attempt at progressing a complete req:%p \n", req);
        break;
    default:
        mp_err_msg("invalid status %d for req:%p \n", req->status, req);
        break;
    }
    return 0;
}

static int client_can_poll(client_t *client, mp_flow_t flow)
{
    struct mp_request *pending_req;

    if (use_event_sync) {
        return cq_poll_count;
    }

    //if (!mp_guard_progress) return 1; 
    

    pending_req = client->waited_stream_req[flow];

    // no pending stream req
    // or next non-completed req is at least the 1st pending stream req
    int ret = 0;

    while (pending_req) {
        // re-reading each time as it might have been updated
        int threshold_id = ACCESS_ONCE(client->last_tracked_id[flow]);
        if (threshold_id < pending_req->id) {
            mp_dbg_msg("client[%d] stalling progress flow=%s threshold_id=%d req->id=%d\n", 
                       client->mpi_rank, mp_flow_to_str(flow), threshold_id, pending_req->id);
            break;
        } else {
            mp_dbg_msg("client[%d] flow=%s threshold_id=%d req->id=%d\n", 
                       client->mpi_rank, mp_flow_to_str(flow), threshold_id, pending_req->id);
	    ret++;
	    pending_req = pending_req->next;
        }
    }
	
    if (!pending_req) {
        ret = cq_poll_count;
    }

    mp_dbg_msg("pending_req=%p ret=%d\n", pending_req, ret);
    return ret;
}

struct ibv_wc *wc = NULL;

int mp_progress_single_flow(mp_flow_t flow)
{
    int i, ne = 0, ret = 0;
    struct gds_cq *cq = NULL; 
    int cqe_count = 0;

    if (!wc) {
        wc = malloc(sizeof(struct ibv_wc)*cq_poll_count);
    }

    const char *flow_str = mp_flow_to_str(flow);

    //mp_dbg_msg("flow=%s\n", flow_str);

    progress_posted_list(flow);

    for (i=0; i<client_count; i++) {
        client_t *client = &clients[i];
        cq = (flow == TX_FLOW) ? client->send_cq : client->recv_cq; 

        // WARNING: can't progress a CQE if it is associated to an RX req
        // which is dependent upon GPU work which has not been triggered yet
        cqe_count = client_can_poll(client, flow);
        cqe_count = MIN(cqe_count, cq_poll_count);
        if (!cqe_count) {
            mp_dbg_msg("cannot poll client[%d] flow=%s\n", client->mpi_rank, flow_str);
            continue;
        }
        ne = ibv_poll_cq(cq->cq, cqe_count, wc);
        //mp_dbg_msg("client[%d] flow=%s cqe_count=%d nw=%d\n", client->mpi_rank, flow_str, cqe_count, ne);
        if (ne == 0) {
            //if (errno) mp_dbg_msg("client[%d] flow=%s errno=%s\n", client->mpi_rank, flow_str, strerror(errno));
        }
        else if (ne < 0) {
            mp_err_msg("error %d(%d) in ibv_poll_cq\n", ne, errno);
            ret = MP_FAILURE;
            goto out;
        } else if (ne) {
            int j;
            for (j=0; j<ne; j++) {
                struct ibv_wc *wc_curr = wc + j;
                mp_dbg_msg("client:%d wc[%d]: status=%x(%s) opcode=%x byte_len=%d wr_id=%"PRIx64"\n",
                           client->mpi_rank, j,
                           wc_curr->status, ibv_wc_status_str(wc_curr->status), 
                           wc_curr->opcode, wc_curr->byte_len, wc_curr->wr_id);

                struct mp_request *req = (struct mp_request *) wc_curr->wr_id;

                if (wc_curr->status != IBV_WC_SUCCESS) {
                    mp_err_msg("ERROR!!! completion error, status:'%s' client:%d rank:%d req:%p flow:%s\n",
                               ibv_wc_status_str(wc_curr->status),
                               i, client->mpi_rank,
                               req, flow_str);
                    exit(-1);
                    //continue;
                }

                if (req) { 
                    mp_dbg_msg("polled new CQE for req:%p flow:%s id=%d peer=%d type=%d\n", req, flow_str, req->id, req->peer, req->type);

                    if (!(req->status == MP_PENDING_NOWAIT || req->status == MP_PENDING))
                        mp_err_msg("status not pending, value: %d \n", req->status);

                    if (req->status == MP_PENDING_NOWAIT) {
                    } else if (req->status != MP_PENDING) {
                        mp_err_msg("status not pending, value: %d \n", req->status);
                        exit(-1);
                    }

                    if (use_event_sync) { 
                        if (req->trigger) {
                            assert(client->last_tracked_id[flow] < req->id);
                            client->last_tracked_id[flow] = req->id;
                        }
                    }

                    ACCESS_ONCE(client->last_done_id) = req->id;
                    progress_request(req);
                } else {
                    mp_dbg_msg("received completion with null wr_id \n");
                }
            }
        }
    }

out: 
    return ret;
}

int mp_wait(mp_request_t *req)
{
  int ret = 0;

  ret = mp_wait_all(1, req);

  return ret;
}

int mp_wait_all (uint32_t count, mp_request_t *req_)
{
    int complete = 0, ret = 0;
    
    us_t start = mp_get_cycles();
    us_t tmout = MP_PROGRESS_ERROR_CHECK_TMOUT_US;
    
    /*poll until completion*/
    while (complete < count) {
        struct mp_request *req = req_[complete];
    	
        // user did not call post_wait_cq()
        // if req->status == WAIT_PENDING && it is a stream request
        //   manually ack the cqe info (NEW EXP verbs API)
        //   req->status = MP_WAIT_POSTED

        // BUG: Is this used only in IPC transfers;
        if (mp_enable_ipc) { 
            if (req->status == MP_PENDING_NOWAIT) 
                req->status = MP_PENDING;
        }
        else
        {
            if (!req_can_be_waited(req))
            {
                mp_dbg_msg("cannot wait req:%p status:%d id=%d peer=%d type=%d flags=%08x\n", req, req->status, req->id, req->peer, req->type, req->flags);
                ret = EINVAL;
                goto out;
            }
            if (req->status == MP_PENDING_NOWAIT) {
                mp_dbg_msg("PENDING_NOWAIT->PENDING req:%p status:%d id=%d peer=%d type=%d\n", req, req->status, req->id, req->peer, req->type);
                client_t *client = &clients[client_index[req->peer]];
                mp_flow_t req_flow = mp_type_to_flow(req->type);
                struct gds_cq *cq = (req_flow == TX_FLOW) ? client->send_cq : client->recv_cq;
                ret = gds_post_wait_cq(cq, &req->gds_wait_info, 0);
                if (ret) {
                  mp_err_msg("got %d while posting cq\n", ret);
                  goto out;
                }
                req->stream = NULL;
                req->status = MP_PENDING;
            }
        }
        complete++;

    }
    
    complete=0;

    while (complete < count) {
        struct mp_request *req = req_[complete];
        
        while (req->status != MP_COMPLETE) {
            ret = mp_progress_single_flow (TX_FLOW);
            if (ret) {
                goto out;
            }
            ret = mp_progress_single_flow (RX_FLOW);
            if (ret) {
                goto out;
            }

            us_t now = mp_get_cycles();
            if (((long)now-(long)start) > (long)tmout) {
                start = now;
                mp_warn_msg("checking for GPU errors\n");
                int retcode = mp_check_gpu_error();
                if (retcode) {
                    ret = MP_FAILURE;
                    goto out;
                }
                mp_warn_msg("enabling dbg tracing\n");
                mp_enable_dbg(1);

                mp_dbg_msg("complete=%d req:%p status:%d id=%d peer=%d type=%d\n", complete, req, req->status, req->id, req->peer, req->type);

                // TODO: remove this
                //mp_warn_msg("stopping CUDA profiler\n");
                //cuProfilerStop();
            }
        }
        
        complete++;
    }
    //ret = complete;

    if(!ret)
    {
        complete=0;
        while (complete < count) {
            struct mp_request *req = req_[complete];
            if (req->status == MP_COMPLETE)
                release_mp_request((struct mp_request *) req);
            else
                ret = MP_FAILURE;

            complete++;
        }
    }

out:
    return ret;
}

int mp_progress_all (uint32_t count, mp_request_t *req_)
{
  int r = 0, ret = 0;
  int completed_reqs = 0;
  /*poll until completion*/
  while (r < count) {
    struct mp_request *req = req_[r];
    
    if (req->status == MP_COMPLETE) {
        completed_reqs++;
        r++;
        continue;
    }

    if (!req_valid(req)) {
        mp_err_msg("invalid req:%p status:%d id=%d peer=%d type=%d, going on anyway\n", req, req->status, req->id, req->peer, req->type);
    }

    ret = mp_progress_single_flow(TX_FLOW);
    if (ret) {
        mp_dbg_msg("progress error %d\n", ret);
        goto out;
    }

    ret = mp_progress_single_flow(RX_FLOW);
    if (ret) {
        mp_dbg_msg("progress error %d\n", ret);
        goto out;
    }

    if (req->status == MP_COMPLETE) {
        completed_reqs++;
        //release_mp_request (req);
    }

    r++;
  }
  if (completed_reqs)
      mp_dbg_msg("%d completed reqs, not being released!\n", completed_reqs);
  ret = completed_reqs;

 out:
  return ret;
}

#ifdef DADO_DEBUG
#include <sched.h>
int t=0;
static void spin_forever()
{
	while(!*(volatile int *)&t) sched_yield();
}
static void check_cuda_ptr(void *addr, size_t length)
{
	unsigned long long id = 0, id2 = 0;
	CUresult ret = cuPointerGetAttribute(&id, CU_POINTER_ATTRIBUTE_BUFFER_ID, (CUdeviceptr)addr);
	if (ret != CUDA_SUCCESS) {
		const char *err_str = NULL;
		cuGetErrorString(ret, &err_str);		
		fprintf(stderr, "%s error ret=%d(%s)\n", __FUNCTION__, ret, err_str);
		fflush(stderr);
	} else {
		mp_dbg_msg("id=%llx\n", id);
	}

	ret = cuPointerGetAttribute(&id2, CU_POINTER_ATTRIBUTE_BUFFER_ID, (CUdeviceptr)addr+length-1);
	if (ret != CUDA_SUCCESS) {
		const char *err_str = NULL;
		cuGetErrorString(ret, &err_str);		
		fprintf(stderr, "%s error ret=%d(%s)\n", __FUNCTION__, ret, err_str);
		fflush(stderr);
	} else {
		mp_dbg_msg("id2=%llx\n", id2);
	}

	if (id != 0 && id2 != 0 && id != id2) {
		fprintf(stderr, "%s ERROR buffer %p:%zu overrun detected id=%lld id2=%lld\n", __FUNCTION__, addr, length, id, id2);
		fflush(stderr);		
	}

	CUdeviceptr base;
	size_t size;
	ret = cuMemGetAddressRange(&base, &size, (CUdeviceptr)addr);
	if (ret != CUDA_SUCCESS) {
		const char *err_str = NULL;
		cuGetErrorString(ret, &err_str);
		fprintf(stderr, "%s error ret=%d(%s)\n", __FUNCTION__, ret, err_str);
		fflush(stderr);
	} else {
		fprintf(stderr, "%s base=%lx size=%zu\n", __FUNCTION__, (unsigned long)base, size);
		if (((CUdeviceptr)addr+length-1) > (base + size -1)) {
			size_t off = ((CUdeviceptr)addr+length-1) - (base + size -1);
			fprintf(stderr, "%s ERROR range is %zu bytes past the allocation\n", __FUNCTION__, off);
		}
		fflush(stderr);
	}
}
#endif // DADO_DEBUG

int mp_register(void *addr, size_t length, mp_reg_t *reg_, int exp_flags)
{
    /*set SYNC MEMOPS if its device buffer*/
    unsigned int type, flag;
    size_t size;
    CUdeviceptr base;
    CUresult curesult; 
    int flags;

    struct mp_reg *reg = calloc(1, sizeof(struct mp_reg));
    if (!reg) {
      mp_err_msg("malloc returned NULL while allocating struct mp_reg\n");
      return MP_FAILURE;
    }

    curesult = cuPointerGetAttribute((void *)&type, CU_POINTER_ATTRIBUTE_MEMORY_TYPE, (CUdeviceptr)addr);
    if ((curesult == CUDA_SUCCESS) && (type == CU_MEMORYTYPE_DEVICE)) { 
       CU_CHECK(cuMemGetAddressRange(&base, &size, (CUdeviceptr)addr));

       flag = 1;
       CU_CHECK(cuPointerSetAttribute(&flag, CU_POINTER_ATTRIBUTE_SYNC_MEMOPS, base)); 
    }

    if (mp_enable_ud) {
        mp_dbg_msg("UD enabled, registering buffer for LOCAL_WRITE\n");
        flags = IBV_ACCESS_LOCAL_WRITE;
    } else { 
        flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE |
                 IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_ATOMIC;
    }

    if(exp_flags & IBV_EXP_ACCESS_ON_DEMAND)
    {
        struct ibv_exp_device_attr dattr;
        dattr.comp_mask = IBV_EXP_DEVICE_ATTR_ODP | IBV_EXP_DEVICE_ATTR_EXP_CAP_FLAGS;
        int ret = ibv_exp_query_device(ib_ctx->context, &dattr);
        if (!(dattr.exp_device_cap_flags & IBV_EXP_DEVICE_ODP))
        {
            mp_err_msg("ODP not supported!\n");
            return MP_FAILURE;
        }

        //In LibMP we only support implicit ODP for the moment
        if(!(dattr.odp_caps.general_odp_caps & IBV_EXP_ODP_SUPPORT_IMPLICIT))
        {
            mp_err_msg("Implicit ODP not supported!\n");
            return MP_FAILURE;
        }
        
        //Implicit On-Demand Paging is supported.
        struct ibv_exp_reg_mr_in in;
        in.pd = ib_ctx->pd;
        in.addr = 0;
        in.length = IBV_EXP_IMPLICIT_MR_SIZE;
        in.exp_access = IBV_EXP_ACCESS_ON_DEMAND|flags;
        in.comp_mask = 0;

        mp_dbg_msg("ibv_exp_reg_mr addr:0 size:IBV_EXP_IMPLICIT_MR_SIZE flags=0x%08x\n", in.exp_access);
        reg->mr = ibv_exp_reg_mr(&in);    
    }
    else
    {
        // note: register addr, not base. no advantage in registering the whole buffer as we don't
        // maintain a registration cache yet
        mp_dbg_msg("ibv_reg_mr addr:%p size:%zu flags=0x%08x\n", addr, length, flags);
        reg->mr = ibv_reg_mr(ib_ctx->pd, addr, length, flags);
    }

    if (!reg->mr) {
        mp_err_msg("ibv_reg_mr returned NULL for addr:%p size:%zu errno=%d(%s)\n", 
                    addr, length, errno, strerror(errno));
       
        #ifdef DADO_DEBUG
            check_cuda_ptr(addr, length);
            spin_forever();
            free(reg);
            MPI_Abort(MPI_COMM_WORLD, 1);
        #endif
       
        return MP_FAILURE;
    }

    reg->key = reg->mr->lkey;

    mp_dbg_msg("reg=%p key=%x\n", reg, reg->key);

    *reg_ = reg;

    return MP_SUCCESS;
}

int mp_deregister(mp_reg_t *reg_)
{
    int ret=0;
    struct mp_reg *reg = (struct mp_reg *) *reg_; 

    assert(reg);
    assert(reg->mr);
    ret = ibv_dereg_mr(reg->mr);
    if(ret)
    {
      mp_err_msg("ibv_dereg_mr returned %d\n", ret);
      return MP_FAILURE;
    }

    free(reg);
    return MP_SUCCESS;
}

char shm_filename[100];
int shm_fd;
int shm_client_bufsize;
int shm_proc_bufsize;
int shm_filesize;
void *shm_mapptr;
char ud_padding[UD_ADDITION];
mp_reg_t ud_padding_reg;

/*initialized end point and establishes alltoall connections*/
int mp_init (MPI_Comm comm, int *peers, int count, int init_flags, int gpu_id)
{
  int i, num_devices;
  struct ibv_device **dev_list = NULL;
  const char *select_dev;
  char *req_dev = NULL;

  struct ibv_qp_attr ib_qp_attr;
  struct ibv_ah_attr ib_ah_attr;
  int peer;
  int gds_flags, comm_size, comm_rank;
  qpinfo_t *qpinfo_all;
  struct ibv_device_attr dev_attr;
  int ret = MP_SUCCESS;

  if(gpu_id < 0)
  {
    mp_err_msg("Invalid input GPU ID (%d)\n", gpu_id);
    return MP_FAILURE;    
  }

  MPI_Comm_size (comm, &comm_size);
  MPI_Comm_rank (comm, &comm_rank);

  mpi_comm = comm;
  mpi_comm_size = comm_size;
  mpi_comm_rank = comm_rank;

  char *value = NULL;
  value = getenv("MP_USE_IB_HCA"); 
  if (value != NULL) {
    req_dev = value;
  } else {
    // old env var, for compatibility
    value = getenv("USE_IB_HCA"); 
    if (value != NULL) {
      mp_warn_msg("USE_IB_HCA is deprecated\n");
      req_dev = getenv(value);
    }
  }

  value = getenv("MP_ENABLE_UD"); 
  if (value != NULL) {
    mp_enable_ud = atoi(value);
  }

  value = getenv("MP_CQ_POLL_COUNT"); 
  if (value != NULL) {
    cq_poll_count = atoi(value);
  }

  value = getenv("MP_IB_CQ_DEPTH");
  if (value != NULL) {
    num_cqes = atoi(value);
    mp_dbg_msg("setting num_cqes=%d\n", num_cqes);
  }

  value = getenv ("MP_IB_MAX_SGL"); 
  if (value != NULL) { 
    ib_max_sge = atoi(value);
  }

  value = getenv ("MP_ENABLE_IPC"); 
  if (value != NULL) { 
    mp_enable_ipc = atoi(value);
  }

  value = getenv("MP_EVENT_ASYNC");
  if (value != NULL) {
    use_event_sync = atoi(value);
  }
  if (use_event_sync) mp_warn_msg("EVENT_ASYNC enabled\n");

  if (init_flags & MP_INIT_RX_CQ_ON_GPU)
      use_rx_cq_gpu = 1;
  value = getenv("MP_RX_CQ_ON_GPU");
  if (value != NULL) {
    use_rx_cq_gpu = atoi(value);
  }
  if (use_rx_cq_gpu) mp_warn_msg("RX CQ on GPU memory enabled\n");

  if (init_flags & MP_INIT_TX_CQ_ON_GPU)
      use_tx_cq_gpu = 1;
  value = getenv("MP_TX_CQ_ON_GPU");
  if (value != NULL) {
    use_tx_cq_gpu = atoi(value);
  }
  if (use_tx_cq_gpu) mp_warn_msg("TX CQ on GPU memory enabled\n");

  if (init_flags & MP_INIT_DBREC_ON_GPU)
      use_dbrec_gpu = 1;
  value = getenv("MP_DBREC_ON_GPU");
  if (value != NULL) {
      use_dbrec_gpu = atoi(value);
  }
  if (use_dbrec_gpu) mp_warn_msg("WQ DBREC on GPU memory enabled\n");

  mp_dbg_msg("libgdsync build version 0x%08x, major=%d minor=%d\n", GDS_API_VERSION, GDS_API_MAJOR_VERSION, GDS_API_MINOR_VERSION);

  int version;
  ret = gds_query_param(GDS_PARAM_VERSION, &version);
  if (ret) {
      mp_err_msg("error querying libgdsync version\n");
      return MP_FAILURE;
  }
  mp_dbg_msg("libgdsync queried version 0x%08x\n", version);
  if (!GDS_API_VERSION_COMPATIBLE(version)) {
      mp_err_msg("incompatible libgdsync version 0x%08x\n", version);
      return MP_FAILURE;
  }

  client_count = count;

  /*pick the right device*/
  dev_list = ibv_get_device_list (&num_devices);
  if (dev_list == NULL) {
    mp_err_msg("ibv_get_device_list returned NULL \n");
    return MP_FAILURE;
  }

  ib_dev = dev_list[0];
  if (req_dev != NULL) {
    for (i=0; i<num_devices; i++) {
      select_dev = ibv_get_device_name(dev_list[i]);
      if (strstr(select_dev, req_dev) != NULL) {
        ib_dev = dev_list[i];
        mp_info_msg("using IB device: %s \n", req_dev);
        break;
      }
    }
    if (i == num_devices) {
      select_dev = ibv_get_device_name(dev_list[0]);
      ib_dev = dev_list[0];
      mp_err_msg("request device: %s not found, defaulting to %s \n", req_dev, select_dev);
    }
  }
  mp_info_msg("HCA dev: %s\n", ibv_get_device_name(ib_dev));

  /*create context, pd, cq*/
  ib_ctx = malloc (sizeof (ib_context_t));
  if (ib_ctx == NULL) {
    mp_err_msg("ib_ctx allocation failed \n");
    return MP_FAILURE;
  }

  ib_ctx->context = ibv_open_device(ib_dev);
  if (ib_ctx->context == NULL) {
    mp_err_msg("ibv_open_device failed \n");
    return MP_FAILURE;
  }

  /*get device attributes and check relevant leimits*/
  if (ibv_query_device(ib_ctx->context, &dev_attr)) {
    mp_err_msg("query_device failed \n"); 	 
    return MP_FAILURE;	
  }

  if (ib_max_sge > dev_attr.max_sge) {
      mp_err_msg("warning!! requested sgl length longer than supported by the adapter, reverting to max, requested: %d max: %d \n", ib_max_sge, dev_attr.max_sge);
      ib_max_sge = dev_attr.max_sge;
  }

  ib_ctx->pd = ibv_alloc_pd (ib_ctx->context);
  if (ib_ctx->pd == NULL) {
    fprintf(stderr ,"ibv_alloc_pd failed \n");
    return MP_FAILURE;
  }

  ibv_query_port (ib_ctx->context, ib_port, &ib_port_attr);

  /*allocate requests*/
  allocate_requests();
  assert(mp_request_free_list != NULL);

  /*establish connections*/
  client_index = malloc(sizeof(int)*comm_size);
  if (client_index == NULL) {
    mp_err_msg("allocation failed \n");
    return MP_FAILURE;
  }
  memset(client_index, bad_index, sizeof(int)*comm_size);

  clients = malloc(sizeof(client_t)*client_count);
  if (clients == NULL) {
    mp_err_msg("allocation failed \n");
    return MP_FAILURE;
  }
  memset(clients, 0, sizeof(client_t)*client_count);

  qpinfo_all = malloc (sizeof(qpinfo_t)*comm_size);
  if (qpinfo_all == NULL) {
    mp_err_msg("qpinfo allocation failed \n");
    return MP_FAILURE;
  }

  /*creating qps for all peers*/
  for (i=0; i<count; i++) {
      // MPI rank of i-th peer
      peer = peers[i];

      //if (peer == mpi_comm_rank) {
      //    mp_err_msg("cannot establish self-connection\n");
      //    return MP_FAILURE;
      //}

      /*rank to peer id mapping */
      client_index[peer] = i;
      /*peer id to rank mapping */
      clients[i].mpi_rank = peer;
      clients[i].last_req_id = 0;
      clients[i].last_done_id = 0;
      assert(sizeof(clients[i].last_waited_stream_req) == N_FLOWS*sizeof(void*));


      memset(clients[i].last_posted_trigger_id, 0, sizeof(clients[0].last_posted_trigger_id));
      memset(clients[i].last_posted_tracked_id, 0, sizeof(clients[0].last_posted_tracked_id));
      memset(clients[i].last_tracked_id,        0, sizeof(clients[0].last_tracked_id));
      memset(clients[i].last_trigger_id,        0, sizeof(clients[0].last_trigger_id));
      memset(clients[i].last_waited_stream_req, 0, sizeof(clients[0].last_waited_stream_req));
      memset(clients[i].waited_stream_req,      0, sizeof(clients[0].waited_stream_req));
      memset(clients[i].last_posted_stream_req, 0, sizeof(clients[0].last_posted_stream_req));
      memset(clients[i].posted_stream_req,      0, sizeof(clients[0].posted_stream_req));

      gds_qp_init_attr_t ib_qp_init_attr;
      memset(&ib_qp_init_attr, 0, sizeof(ib_qp_init_attr));
      ib_qp_init_attr.cap.max_send_wr  = ib_tx_depth;
      ib_qp_init_attr.cap.max_recv_wr  = ib_rx_depth;
      ib_qp_init_attr.cap.max_send_sge = ib_max_sge;
      ib_qp_init_attr.cap.max_recv_sge = ib_max_sge;

      //create QP, set to INIT state and exchange QPN information
      if (mp_enable_ud) {
          ib_qp_init_attr.qp_type = IBV_QPT_UD;
          ib_qp_init_attr.cap.max_inline_data = ib_inline_size;
      } else {
          ib_qp_init_attr.qp_type = IBV_QPT_RC;
          ib_qp_init_attr.cap.max_inline_data = ib_inline_size;
      }

      gds_flags = GDS_CREATE_QP_DEFAULT;
      if (use_wq_gpu)
          gds_flags |= GDS_CREATE_QP_WQ_ON_GPU;
      if (use_rx_cq_gpu)
          gds_flags |= GDS_CREATE_QP_RX_CQ_ON_GPU;
      if (use_tx_cq_gpu)
          gds_flags |= GDS_CREATE_QP_TX_CQ_ON_GPU;
      if (use_dbrec_gpu)
          gds_flags |= GDS_CREATE_QP_WQ_DBREC_ON_GPU;

      //is the CUDA context already initialized?
      clients[i].qp = gds_create_qp(ib_ctx->pd, ib_ctx->context, &ib_qp_init_attr, gpu_id, gds_flags);
      if (clients[i].qp == NULL) {
          mp_err_msg("qp creation failed \n");
          return MP_FAILURE;
      }
      clients[i].send_cq = &clients[i].qp->send_cq;
      clients[i].recv_cq = &clients[i].qp->recv_cq;

      assert(clients[i].qp);
      assert(clients[i].send_cq);
      assert(clients[i].recv_cq);

      memset(&ib_qp_attr, 0, sizeof(struct ibv_qp_attr));
      ib_qp_attr.qp_state        = IBV_QPS_INIT;
      ib_qp_attr.pkey_index      = 0;
      ib_qp_attr.port_num        = ib_port;
      int flags = 0;
      if (mp_enable_ud) { 
          ib_qp_attr.qkey            = 0;
          flags                      = IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_QKEY;
      } else {
          ib_qp_attr.qp_access_flags = IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_LOCAL_WRITE;
          flags                      = IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS;
      }

      ret = ibv_modify_qp (clients[i].qp->qp, &ib_qp_attr, flags);
      if (ret != 0) {
          mp_err_msg("Failed to modify QP to INIT: %d, %s\n", ret, strerror(errno));
          exit(EXIT_FAILURE);
      }

//      mp_query_print_qp(clients[i].qp, NULL, 0);

      qpinfo_all[peer].lid = ib_port_attr.lid;
      qpinfo_all[peer].qpn = clients[i].qp->qp->qp_num;
      qpinfo_all[peer].psn = 0;
      mp_dbg_msg("QP lid:%04x qpn:%06x psn:%06x\n", 
                 qpinfo_all[peer].lid,
                 qpinfo_all[peer].qpn,
                 qpinfo_all[peer].psn);
  }

  /*exchange qpinfo*/
  MPI_CHECK(MPI_Alltoall(MPI_IN_PLACE, sizeof(qpinfo_t),
                         MPI_CHAR, qpinfo_all, sizeof(qpinfo_t),
                         MPI_CHAR, comm));

  for (i=0; i<count; i++) {
      int flags;
      peer = peers[i];

      memset(&ib_qp_attr, 0, sizeof(struct ibv_qp_attr));
      if (mp_enable_ud) { 
          ib_qp_attr.qp_state       = IBV_QPS_RTR;
          flags = IBV_QP_STATE;
      } else { 
          ib_qp_attr.qp_state       = IBV_QPS_RTR;
          ib_qp_attr.path_mtu       = ib_port_attr.active_mtu;
          ib_qp_attr.dest_qp_num    = qpinfo_all[peer].qpn;
          ib_qp_attr.rq_psn         = qpinfo_all[peer].psn;
          ib_qp_attr.ah_attr.dlid   = qpinfo_all[peer].lid;
          ib_qp_attr.max_dest_rd_atomic     = 1;
          ib_qp_attr.min_rnr_timer          = 12;
          ib_qp_attr.ah_attr.is_global      = 0;
          ib_qp_attr.ah_attr.sl             = 0;
          ib_qp_attr.ah_attr.src_path_bits  = 0;
          ib_qp_attr.ah_attr.port_num       = ib_port;
          flags = IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU
              | IBV_QP_DEST_QPN | IBV_QP_RQ_PSN
              | IBV_QP_MIN_RNR_TIMER | IBV_QP_MAX_DEST_RD_ATOMIC;
      }

      ret = ibv_modify_qp(clients[i].qp->qp, &ib_qp_attr, flags);
      if (ret != 0) {
          mp_err_msg("Failed to modify RC QP to RTR\n");
          return MP_FAILURE;
      }
  }

  MPI_Barrier(comm);

  for (i=0; i<count; i++) {
      int flags = 0;
      peer = peers[i];

      memset(&ib_qp_attr, 0, sizeof(struct ibv_qp_attr));
      if (mp_enable_ud) { 
          ib_qp_attr.qp_state       = IBV_QPS_RTS;
          ib_qp_attr.sq_psn         = 0;
          flags = IBV_QP_STATE | IBV_QP_SQ_PSN; 
      } else { 
          ib_qp_attr.qp_state       = IBV_QPS_RTS;
          ib_qp_attr.sq_psn         = 0;
          ib_qp_attr.timeout        = 20;
          ib_qp_attr.retry_cnt      = 7;
          ib_qp_attr.rnr_retry      = 7;
          ib_qp_attr.max_rd_atomic  = 1;
          flags = IBV_QP_STATE | IBV_QP_SQ_PSN | IBV_QP_TIMEOUT
            | IBV_QP_RETRY_CNT | IBV_QP_RNR_RETRY
            | IBV_QP_MAX_QP_RD_ATOMIC;
      }

      ret = ibv_modify_qp(clients[i].qp->qp, &ib_qp_attr, flags);
      if (ret != 0)
      {
        mp_err_msg("Failed to modify RC QP to RTS\n");
        return MP_FAILURE;
      }

      if (mp_enable_ud) {
          mp_err_msg("setting up connection with peer: %d lid: %d qpn: %d \n", peer, qpinfo_all[peer].lid,
                         qpinfo_all[peer].qpn);

          memset(&ib_ah_attr, 0, sizeof(ib_ah_attr));
          ib_ah_attr.is_global     = 0;
          ib_ah_attr.dlid          = qpinfo_all[peer].lid;
          ib_ah_attr.sl            = 0;
          ib_ah_attr.src_path_bits = 0;
          ib_ah_attr.port_num      = ib_port;

          clients[i].ah = ibv_create_ah(ib_ctx->pd, &ib_ah_attr);
          if (!clients[i].ah) {
              mp_err_msg("Failed to create AH\n");
              return MP_FAILURE;
          }

          clients[i].qpn = qpinfo_all[peer].qpn; 
      }
  }

  if (mp_enable_ud) { 
      int result = mp_register(ud_padding, UD_ADDITION, &ud_padding_reg, 0);
      assert(result == MP_SUCCESS);
  }

  MPI_Barrier(comm);

  //ipc connection setup
  if (mp_enable_ipc) {
      node_info_all = malloc(sizeof(struct node_info)*mpi_comm_size);
      if (!node_info_all) {
 	  mp_err_msg("Failed to allocate node info array \n");
	  return MP_FAILURE;
      }

      if(!gethostname(node_info_all[mpi_comm_rank].hname, 20)) {
  	  mp_err_msg("gethostname returned error \n");
	  return MP_FAILURE;
      }

      CUDA_CHECK(cudaGetDevice(&node_info_all[mpi_comm_rank].gpu_id));

      MPI_CHECK(MPI_Allgather(MPI_IN_PLACE, 0, MPI_DATATYPE_NULL,
                              node_info_all, sizeof(struct node_info), MPI_CHAR, comm));

      int cidx, can_access_peer; 
      for (i=0; i<mpi_comm_size; i++) {
	  can_access_peer = 0;
	  cidx = client_index[i];

	  if (i == mpi_comm_size) { 
              /*pick first rank on the node as the leader*/
              if (!smp_num_procs) {
                 smp_leader = i;
              }
              smp_local_rank = smp_num_procs;	      
              smp_num_procs++;
	      ipc_num_procs++;
	      continue;
	  }

	  if (!strcmp(node_info_all[i].hname, node_info_all[mpi_comm_rank].hname)) {
	      /*pick first rank on the node as the leader*/
	      if (!smp_num_procs) {
		 smp_leader = i; 
	      }
              clients[cidx].is_local = 1;
              clients[cidx].local_rank = smp_num_procs;
	      smp_num_procs++; 
	      CUDA_CHECK(cudaDeviceCanAccessPeer(&can_access_peer, node_info_all[mpi_comm_rank].gpu_id, node_info_all[i].gpu_id));
	  }

	  if (can_access_peer) { 
	      ipc_num_procs++;
              clients[cidx].can_use_ipc = 1;
	  } 
      }

      if (smp_num_procs > 1) {
	  shm_client_bufsize = sizeof(smp_buffer_t)*smp_depth;
	  shm_proc_bufsize = shm_client_bufsize*smp_num_procs;
	  shm_filesize = sizeof(smp_buffer_t)*smp_depth*smp_num_procs*smp_num_procs;

          //setup shared memory buffers 
          sprintf(shm_filename, "/dev/shm/libmp_shmem-%s-%d.tmp",
                  node_info_all[mpi_comm_rank].hname, getuid());
          mp_dbg_msg("shemfile %s\n", shm_filename);

	  shm_fd = open(shm_filename, O_RDWR | O_CREAT, S_IRWXU | S_IRWXG | S_IRWXO);
          if (shm_fd < 0) {
              mp_err_msg("opening shm file failed \n");
              return MP_FAILURE;
	  }

	  if (smp_leader == mpi_comm_rank) {
	      if (ftruncate(shm_fd, 0)) {
	          mp_err_msg("clearning up shm file failed \n");
                  /* to clean up tmp shared file */
	          return MP_FAILURE;
              }

              if (ftruncate(shm_fd, shm_filesize)) {
                  mp_err_msg("setting up shm file failed \n");
                  /* to clean up tmp shared file */
                  return MP_FAILURE;
       	      }
	  }
      }
 
      MPI_Barrier(MPI_COMM_WORLD);

      if (smp_num_procs > 1) {
	  struct stat file_status;

          /* synchronization between local processes */
          do {
             if (fstat(shm_fd, &file_status) != 0) {
                  mp_err_msg("fstat on shm file failed \n");
                  /* to clean up tmp shared file */
                  return MP_FAILURE;
             }
             usleep(1);
          } while (file_status.st_size != shm_filesize);

          /* mmap of the shared memory file */
          shm_mapptr = mmap(0, shm_filesize, (PROT_READ | PROT_WRITE), (MAP_SHARED), shm_fd, 0);
	  if (shm_mapptr == (void *) -1) {
              mp_err_msg("mmap on shm file failed \n");
              /* to clean up tmp shared file */
              return MP_FAILURE;
          }
      }

      for (i=0; i<mpi_comm_size; i++) {
          int j, cidx;
	  
          cidx = client_index[i]; 

          if (clients[cidx].is_local) {
	        assert(smp_local_rank >= 0);
 
		clients[cidx].smp.local_buffer = (void *)((char *)shm_mapptr 
					+ shm_proc_bufsize*smp_local_rank 
					+ shm_client_bufsize*clients[cidx].local_rank);

		memset(clients[cidx].smp.local_buffer, 0, shm_client_bufsize);

		for (j=0; j<smp_depth; j++) { 
		    clients[cidx].smp.local_buffer[j].free = 1;
		}

		clients[cidx].smp.remote_buffer = (void *)((char *)shm_mapptr 
					+ shm_proc_bufsize*clients[cidx].local_rank 
					+ shm_client_bufsize*smp_local_rank);
	  }
      }
  }

  free(qpinfo_all);

  return MP_SUCCESS;
}

void mp_finalize ()
{
  int i;
  mem_region_t *mem_region = NULL;

  MPI_Barrier(mpi_comm);

  /*destroy IB resources*/
  for (i=0; i<client_count; i++) {
      gds_destroy_qp (clients[i].qp);
  }
  ibv_dealloc_pd (ib_ctx->pd);
  ibv_close_device (ib_ctx->context);

  while (mem_region_list != NULL) {
    mem_region = mem_region_list;
    mem_region_list = mem_region_list->next;

    free(mem_region->region);
    free(mem_region);
  }

  /*free all buffers*/
  free(ib_ctx);
  free(client_index);
  free(clients);
}

//Progress RX_FLOW fix
//progress (remove) some requests on the RX flow if is not possible to queue a recv request
int mp_post_recv(client_t *client, struct mp_request *req)
{
    int progress_retry=0, ret=0, ret_progress=0;

    if(!client || !req)
        return MP_FAILURE;

    do
    {
        ret = gds_post_recv(client->qp, &req->in.rr, &req->out.bad_rr);
        if(ret == ENOMEM)
        {
            ret_progress = mp_progress_single_flow(RX_FLOW);
            if(ret_progress != MP_SUCCESS)
            {
                mp_err_msg("mp_progress_single_flow failed. Error: %d\n", ret_progress);
                break;
            }
            mp_warn_msg("RX_FLOW was full. mp_progress_single_flow called %d times (ret=%d)\n", (progress_retry+1), ret);
            progress_retry++;
        }
    } while(ret == ENOMEM && progress_retry <= MP_MAX_PROGRESS_FLOW_TRY);

    return ret;
}

int mp_irecv (void *buf, int size, int peer, mp_reg_t *reg_t, mp_request_t *req_t)
{
  int ret = 0;
  //int ret_progress = 0;
  struct mp_request *req = NULL;
  struct mp_reg *reg = (struct mp_reg *) *reg_t;
  client_t *client = &clients[client_index[peer]];

  req = new_request(client, MP_RECV, MP_PENDING_NOWAIT);
  assert(req);

  mp_dbg_msg("peer=%d req=%p buf=%p size=%d id=%d reg=%p key=%x\n", peer, req, buf, size, req->id, reg, reg->key);

  if (mp_enable_ipc && client->can_use_ipc) {
      track_ipc_stream_rreq(peer, req);
  } else { 
      req->in.rr.next = NULL;
      req->in.rr.wr_id = (uintptr_t) req;

      if (mp_enable_ud) { 
          struct mp_reg *ud_reg = (struct mp_reg *) ud_padding_reg;

          req->in.rr.num_sge = 2;
          req->in.rr.sg_list = req->ud_sg_entry;
          req->ud_sg_entry[0].length = UD_ADDITION;
          req->ud_sg_entry[0].lkey = ud_reg->key;
          req->ud_sg_entry[0].addr = (uintptr_t)(ud_padding);
          req->ud_sg_entry[1].length = size;
          req->ud_sg_entry[1].lkey = reg->key;
          req->ud_sg_entry[1].addr = (uintptr_t)(buf);	
      } else { 
          req->in.rr.num_sge = 1;
          req->in.rr.sg_list = &req->sg_entry;
          req->sg_entry.length = size;
          req->sg_entry.lkey = reg->key;
          req->sg_entry.addr = (uintptr_t)(buf);
      }
      //progress (remove) some request on the RX flow if is not possible to queue a recv request
      ret = mp_post_recv(client, req);
      if (ret) {
        mp_err_msg("posting recv failed ret: %d error: %s peer: %d index: %d \n", ret, strerror(errno), peer, client_index[peer]);
        goto out;
      }

      if (!use_event_sync) {
          ret = gds_prepare_wait_cq(client->recv_cq, &req->gds_wait_info, 0);
          if (ret) {
            mp_err_msg("gds_prepare_wait_cq failed: %s \n", strerror(errno));
            goto out;
          }
      }
  }

  *req_t = req; 

 out:
  return ret;
}

int mp_irecvv (struct iovec *v, int nvecs, int peer, mp_reg_t *reg_t, mp_request_t *req_t)
{
  int i, ret = 0;
  struct mp_request *req = NULL;
  struct mp_reg *reg = (struct mp_reg *) *reg_t;

  if (nvecs > ib_max_sge) {
      mp_err_msg("exceeding max supported vector size: %d \n", ib_max_sge);
      ret = MP_FAILURE;
      goto out;
  }

  client_t *client = &clients[client_index[peer]];

  req = new_request(client, MP_RECV, MP_PENDING_NOWAIT);
  assert(req);
  req->sgv = malloc(sizeof(struct ibv_sge)*nvecs);
  assert(req->sgv);

  mp_dbg_msg("req=%p id=%d\n", req, req->id);

  for (i=0; i < nvecs; ++i) {
    req->sgv[i].length = v[i].iov_len;
    req->sgv[i].lkey = reg->key;
    req->sgv[i].addr = (uint64_t)(v[i].iov_base);
  }

  req->in.rr.next = NULL;
  req->in.rr.wr_id = (uintptr_t) req;
  req->in.rr.num_sge = nvecs;
  req->in.rr.sg_list = req->sgv;

  ret = gds_post_recv(client->qp, &req->in.rr, &req->out.bad_rr);
  if (ret) {
    mp_err_msg("posting recvv failed ret: %d error: %s peer: %d index: %d \n", ret, strerror(errno), peer, client_index[peer]);
    goto out;
  }

  /*we are interested only in the last receive, retrieve repeatedly*/
  if (!use_event_sync) {
      ret = gds_prepare_wait_cq(client->recv_cq, &req->gds_wait_info, 0);
      if (ret) {
        mp_err_msg("gds_prepare_wait_cq failed: %s \n", strerror(errno));
        goto out;
      }
  }

  *req_t = req;

 out:
  return ret;
}

static int qp_query=0;

//Progress TX_FLOW fix
//progress (remove) some requests on the TX flow if is not possible to queue a send request
int mp_post_send(client_t *client, struct mp_request *req)
{
    int progress_retry=0, ret=0, ret_progress=0;

    if(!client || !req)
        return MP_FAILURE;

    do
    {
        ret = gds_post_send (client->qp, &req->in.sr, &req->out.bad_sr);
        //Note: ENOMEM is caused by a full QP or by an inline message size bigger than allowed?
        //mp_query_print_qp() print the current max inline size
        if(ret == ENOMEM)
        {
            if(qp_query == 0)
            {
                mp_query_print_qp(client->qp, req, 0);
                qp_query=1;
            }

            ret_progress = mp_progress_single_flow(TX_FLOW);
            if(ret_progress != MP_SUCCESS)
            {
                mp_err_msg("mp_progress_single_flow failed. Error: %d\n", ret_progress);
                break;
            }
            mp_warn_msg("TX_FLOW was full. mp_progress_single_flow called %d times (ret=%d)\n", (progress_retry+1), ret);
            progress_retry++;
        }
    } while(ret == ENOMEM && progress_retry <= MP_MAX_PROGRESS_FLOW_TRY);

    qp_query=0;
    return ret;
}

int mp_isend (void *buf, int size, int peer, mp_reg_t *reg_t, mp_request_t *req_t)
{
    int ret = 0;
    //int progress_retry = 0;
    //int ret_progress = 0;
    struct mp_request *req;
    struct mp_reg *reg = (struct mp_reg *) *reg_t;

    client_t *client = &clients[client_index[peer]];

    req = new_request(client, MP_SEND, MP_PENDING_NOWAIT);
    assert(req);

    mp_dbg_msg("req=%p id=%d\n", req, req->id);

    if (mp_enable_ipc && client->can_use_ipc)
    {
        ipc_handle_cache_entry_t *entry = NULL;
        smp_buffer_t *smp_buffer = NULL;

        //try to find in local handle cache
        ipc_handle_cache_find (buf, size, &entry, mpi_comm_rank);
        if (!entry) { 
            entry = malloc(sizeof(ipc_handle_cache_entry_t));
        if (!entry) { 
            mp_err_msg("cache entry allocation failed \n");	
            ret = MP_FAILURE;
            goto out;
        }
	  
          CU_CHECK(cuMemGetAddressRange((CUdeviceptr *)&entry->base, &entry->size, (CUdeviceptr) buf));
          CU_CHECK(cuIpcGetMemHandle (&entry->handle, (CUdeviceptr)entry->base));

          ipc_handle_cache_insert(entry, mpi_comm_rank);
      }

        assert(entry != NULL);
        smp_buffer = client->smp.remote_buffer + client->smp.remote_head;
        assert(smp_buffer->free == 1);	

        memcpy((void *)&smp_buffer->handle, (void *)&entry->handle, sizeof(CUipcMemHandle));  
        smp_buffer->base_addr = entry->base;
        smp_buffer->base_size = entry->size;
        smp_buffer->addr = buf;
        smp_buffer->size = size;
        smp_buffer->offset = (uintptr_t)buf - (uintptr_t)entry->base;
        smp_buffer->sreq = req; 
        smp_buffer->free = 0; 
        smp_buffer->busy = 1;
        client->smp.remote_head = (client->smp.remote_head + 1)%smp_depth;	 
    }
    else
    {
        req->in.sr.next = NULL;
        req->in.sr.exp_send_flags = IBV_EXP_SEND_SIGNALED;
        req->in.sr.exp_opcode = IBV_EXP_WR_SEND;
        req->in.sr.wr_id = (uintptr_t) req;
        req->in.sr.num_sge = 1;
        req->in.sr.sg_list = &req->sg_entry;

        if (mp_enable_ud) {
            req->in.sr.wr.ud.ah = client->ah;
            req->in.sr.wr.ud.remote_qpn = client->qpn; 
            req->in.sr.wr.ud.remote_qkey = 0;
        }

        req->sg_entry.length = size;
        req->sg_entry.lkey = reg->key;
        req->sg_entry.addr = (uintptr_t)(buf);
        // progress (remove) some request on the TX flow if is not possible to queue a send request
        ret = mp_post_send(client, req);
        if (ret) {
        mp_err_msg("posting send failed: %s \n", strerror(errno));
        goto out;
        }

        if (!use_event_sync) {
            ret = gds_prepare_wait_cq(client->send_cq, &req->gds_wait_info, 0);
            if (ret) {
                mp_err_msg("gds_prepare_wait_cq failed: %s \n", strerror(errno));
                goto out;
            }
        }
    }

    *req_t = req;

out:
    return ret;
}

int mp_isendv (struct iovec *v, int nvecs, int peer, mp_reg_t *reg_t, mp_request_t *req_t)
{
  int i, ret = 0;
  struct mp_request *req;
  struct mp_reg *reg = (struct mp_reg *) *reg_t;

  if (nvecs > ib_max_sge) {
      mp_err_msg("exceeding max supported vector size: %d \n", ib_max_sge);
      ret = MP_FAILURE;
      goto out;
  }

  client_t *client = &clients[client_index[peer]];

  req = new_request(client, MP_SEND, MP_PENDING_NOWAIT);
  assert(req);
  req->sgv = malloc(sizeof(struct ibv_sge)*nvecs);
  assert(req->sgv);

  mp_dbg_msg("req=%p id=%d\n", req, req->id);

  for (i=0; i < nvecs; ++i) {
    req->sgv[i].length = v[i].iov_len;
    req->sgv[i].lkey = reg->key;
    req->sgv[i].addr = (uint64_t)(v[i].iov_base);
  }

  if (mp_enable_ud) {
      req->in.sr.wr.ud.ah = client->ah;
      req->in.sr.wr.ud.remote_qpn = client->qpn;
      req->in.sr.wr.ud.remote_qkey = 0;
  }

  req->in.sr.next = NULL;
  req->in.sr.exp_send_flags = IBV_EXP_SEND_SIGNALED;
  req->in.sr.exp_opcode = IBV_EXP_WR_SEND;
  req->in.sr.wr_id = (uintptr_t) req;
  req->in.sr.num_sge = nvecs;
  req->in.sr.sg_list = req->sgv;

  ret = gds_post_send(client->qp, &req->in.sr, &req->out.bad_sr);
  if (ret) {
    mp_err_msg("posting send failed: %s \n", strerror(errno));
    goto out;
  }

  if (!use_event_sync) {
      ret = gds_prepare_wait_cq(client->send_cq, &req->gds_wait_info, 0);
      if (ret) {
        mp_err_msg("gds_prepare_wait_cq failed: %s \n", strerror(errno));
        goto out;
      }
  }

 out:
  return ret;
}

/*to enable opaque requests*/
void allocate_requests ()
{
  int i;
  mem_region_t *mem_region;
  struct mp_request *mp_requests;

  assert (mp_request_free_list == NULL);

  mem_region = (mem_region_t *) malloc (sizeof (mem_region_t));
  if (mem_region == NULL) {
    mp_err_msg("memory allocation for mem_region failed \n");
    exit(-1);
  }
  if (mem_region_list == NULL) {
    mem_region_list = mem_region;
    mem_region->next = NULL;
  } else {
    mem_region->next = mem_region_list;
  }

  mem_region->region = malloc (sizeof(struct mp_request)*mp_request_limit);
  if (mem_region == NULL) {
    mp_err_msg("memory allocation for request_region failed \n");
    exit(-1);
  }

  mp_requests = (struct mp_request *) mem_region->region;

  mp_request_free_list = mp_requests;
  for (i=0; i<mp_request_limit-1; i++) {
    mp_requests[i].next = mp_requests + i + 1;
  }
  mp_requests[i].next = NULL;
}

struct mp_request *get_request()
{
  struct mp_request *req = NULL;

  if (mp_request_free_list == NULL) {
    allocate_requests();
    assert(mp_request_free_list != NULL);
  }

  req = mp_request_free_list;
  mp_request_free_list = mp_request_free_list->next;

  req->next = NULL;
  req->prev = NULL;

  return req;
}

static int mp_get_request_id(client_t *client, mp_req_type_t type)
{
    assert(client->last_req_id < UINT_MAX);
    return ++client->last_req_id;
}

struct mp_request *new_stream_request(client_t *client, mp_req_type_t type, mp_state_t state, struct CUstream_st *stream)
{
  struct mp_request *req = get_request();
  //mp_dbg_msg("new req=%p\n", req);
  if (req) {
      req->peer = client->mpi_rank;
      req->flags = 0;
      req->sgv = NULL;
      req->next = NULL;
      req->prev = NULL;
      req->trigger = 0;
      req->type = type;
      req->status = state;
      req->id = mp_get_request_id(client, type);
  }

  return req;
}

void release_mp_request(struct mp_request *req)
{
  req->next = mp_request_free_list;
  req->prev = NULL;
  req->type = MP_NULL;
  req->status = MP_UNDEF;

  mp_request_free_list = req;
}

/*one-sided operations: window creation, put and get*/
int mp_window_create(void *addr, size_t size, mp_window_t *window_t) 
{
  int result = MP_SUCCESS;
  struct mp_window *window;
  typedef struct {
    void *base_addr;
    uint32_t rkey;
    int size;
  } exchange_win_info;
  exchange_win_info *exchange_win = NULL; 
  int i, peer;

  window = malloc (sizeof(struct mp_window));
  assert(window != NULL); 

  window->base_ptr = malloc (client_count*sizeof(void *));
  assert(window->base_ptr != NULL);
  window->rkey = malloc (client_count*sizeof(uint32_t));
  assert(window->rkey != NULL);
  window->rsize = malloc (client_count*sizeof(uint64_t));
  assert(window->rsize != NULL);

  exchange_win = malloc (mpi_comm_size*sizeof(exchange_win_info));
  assert(exchange_win != NULL); 

  window->reg=NULL;
  result = mp_register(addr, size, &window->reg, 0);  
  assert(result == MP_SUCCESS); 
  
  exchange_win[mpi_comm_rank].base_addr = addr; 
  exchange_win[mpi_comm_rank].rkey = window->reg->mr->rkey; 
  exchange_win[mpi_comm_rank].size = size;

  MPI_Allgather(MPI_IN_PLACE, sizeof(exchange_win_info),
               MPI_CHAR, exchange_win, sizeof(exchange_win_info),
               MPI_CHAR, mpi_comm);

  /*populate window address info*/
  for (i=0; i<client_count; i++) { 
      peer = clients[i].mpi_rank;
 
      window->base_ptr[i] = exchange_win[peer].base_addr;
      window->rkey[i] = exchange_win[peer].rkey;
      window->rsize[i] = exchange_win[peer].size;
  }

  *window_t = window;

  free(exchange_win);

  MPI_Barrier(mpi_comm);

  return result;
}

int mp_window_destroy(mp_window_t *window_t)
{
  struct mp_window *window = *window_t;
  int result = MP_SUCCESS;

  mp_deregister(&window->reg);
  
  free(window->base_ptr);
  free(window->rkey);

  free(window);

  return result;
}

int mp_iput (void *src, int size, mp_reg_t *reg_t, int peer, size_t displ, 
             mp_window_t *window_t, mp_request_t *req_t, int flags) 
{
  int ret = 0;
  struct mp_request *req;
  struct mp_reg *reg = *reg_t;
  struct mp_window *window = *window_t;

  if (mp_enable_ud) { 
	mp_err_msg("put/get not supported with UD \n");
	ret = MP_FAILURE;
	goto out;
  }

  int client_id = client_index[peer];
  client_t *client = &clients[client_id];

  assert(displ < window->rsize[client_id]);

  req = new_request(client, MP_RDMA, MP_PENDING_NOWAIT);
  assert(req);

  req->flags = flags;
  req->in.sr.next = NULL;
  if (flags & MP_PUT_NOWAIT)
      req->in.sr.exp_send_flags = 0;
  else
      req->in.sr.exp_send_flags = IBV_EXP_SEND_SIGNALED;
  if (flags & MP_PUT_INLINE)
      req->in.sr.exp_send_flags |= IBV_EXP_SEND_INLINE;
  req->in.sr.exp_opcode = IBV_EXP_WR_RDMA_WRITE;
  req->in.sr.wr_id = (uintptr_t) req;
  req->in.sr.num_sge = 1;
  req->in.sr.sg_list = &req->sg_entry;

  req->sg_entry.length = size;
  req->sg_entry.lkey = reg->key;
  req->sg_entry.addr = (uintptr_t)src;

  req->in.sr.wr.rdma.remote_addr = ((uint64_t)window->base_ptr[client_id]) + displ;
  req->in.sr.wr.rdma.rkey = window->rkey[client_id];

  ret = mp_post_send(client, req);
  if (ret) {
    mp_err_msg("posting send failed: %s \n", strerror(errno));
    goto out;
  }

  if (!(flags & MP_PUT_NOWAIT)) {
      ret = gds_prepare_wait_cq(client->send_cq, &req->gds_wait_info, 0);
      if (ret) {
          mp_err_msg("gds_prepare_wait_cq failed: %s \n", strerror(errno));
          goto out;
      }
  }

  *req_t = req;

 out:
  return ret;
}

int mp_iget (void *dst, int size, mp_reg_t *reg_t, int peer, size_t displ, 
	mp_window_t *window_t, mp_request_t *req_t) 
{
  int ret = 0;
  struct mp_request *req;
  struct mp_reg *reg = *reg_t;
  struct mp_window *window = *window_t;

  if (mp_enable_ud) { 
	mp_err_msg("put/get not supported with UD \n");
	ret = MP_FAILURE;
	goto out;
  }

  int client_id = client_index[peer];
  client_t *client = &clients[client_id];

  assert(displ < window->rsize[client_id]);

  req = new_request(client, MP_RDMA, MP_PENDING_NOWAIT);

  req->in.sr.next = NULL;
  req->in.sr.exp_send_flags = IBV_EXP_SEND_SIGNALED;
  req->in.sr.exp_opcode = IBV_WR_RDMA_READ;
  req->in.sr.wr_id = (uintptr_t) req;
  req->in.sr.num_sge = 1;
  req->in.sr.sg_list = &req->sg_entry;

  req->sg_entry.length = size;
  req->sg_entry.lkey = reg->key;
  req->sg_entry.addr = (uintptr_t)dst;

  req->in.sr.wr.rdma.remote_addr = ((uint64_t)window->base_ptr[client_id]) + displ;
  req->in.sr.wr.rdma.rkey = window->rkey[client_id];

  ret = gds_post_send(client->qp, &req->in.sr, &req->out.bad_sr);
  if (ret) {
    mp_err_msg("posting send failed: %s \n", strerror(errno));
    goto out;
  }

  ret = gds_prepare_wait_cq(client->send_cq, &req->gds_wait_info, 0);
  if (ret) {
    mp_err_msg("gds_prepare_wait_cq failed: %s \n", strerror(errno));
    goto out;
  }

  *req_t = req;

 out:
  return ret;
}

int mp_wait32(uint32_t *ptr, uint32_t value, int flags)
{
    int ret = MP_SUCCESS;
    int cond = 0;
    int cnt = 0;
    while (1) {
        switch(flags) {
        case MP_WAIT_EQ:   cond = (ACCESS_ONCE(*ptr) >  value); break;
        case MP_WAIT_GEQ:  cond = (ACCESS_ONCE(*ptr) >= value); break;
        case MP_WAIT_AND:  cond = (ACCESS_ONCE(*ptr) &  value); break;
        default: ret = EINVAL; goto out; break;
        }
        if (cond) break;
        arch_cpu_relax();
        ++cnt;
        if (cnt > 10000) {
            sched_yield();
            cnt = 0;
        }
    }
out:
    return ret;
}

int mp_query_param(mp_param_t param, int *value)
{
        int ret = 0;
        if (!value)
                return EINVAL;

        switch (param) {
        case MP_PARAM_VERSION:
                *value = (MP_API_MAJOR_VERSION << 16)|MP_API_MINOR_VERSION;
                break;
        default:
                ret = EINVAL;
                break;
        };
        return ret;
}

