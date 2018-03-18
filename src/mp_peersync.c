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
#include <cuda.h>
#include <cuda_runtime.h>

#include "mp.h"
#include "mp_internal.h"
#include <gdsync.h>
#include <gdsync/tools.h>

#if (GDS_API_MAJOR_VERSION==2 && GDS_API_MINOR_VERSION>=2) || (GDS_API_MAJOR_VERSION>2)
#define HAS_GDS_DESCRIPTOR_API 1
#endif

#define __CUDACHECK(stmt, cond_str)					\
    do {								\
        cudaError_t result = (stmt);                                    \
        if (cudaSuccess != result) {                                    \
            mp_err_msg("Assertion \"%s != cudaSuccess\" failed at %s:%d error=%d(%s)\n", \
                       cond_str, __FILE__, __LINE__, result, cudaGetErrorString(result)); \
        }                                                               \
    } while (0)

#define CUDACHECK(stmt) __CUDACHECK(stmt, #stmt)

int mp_check_gpu_error()
{
    int ret = MP_SUCCESS;
    cudaError_t error = cudaGetLastError();
    if (error != cudaSuccess) {
        CUDACHECK(error);
        ret = MP_FAILURE;
    }
    return ret;
}

uint32_t *client_last_tracked_id_ptr(client_t *client, struct mp_request *req)
{
    return &client->last_tracked_id[mp_req_to_flow(req)];
}

void client_track_posted_stream_req(client_t *client, struct mp_request *req, mp_flow_t flow)
{    
    mp_dbg_msg("[%d] queuing request: %d req: %p \n", mpi_comm_rank, req->id, req);
    if (!client->posted_stream_req[flow]) {
        mp_dbg_msg("setting client[%d]->posted_stream_req[%s]=req=%p req->id=%d\n", 
                   client->mpi_rank, flow==TX_FLOW?"TX":"RX", req, req->id);
        assert(client->last_posted_stream_req[flow] == NULL);
        client->posted_stream_req[flow] = client->last_posted_stream_req[flow] = req;
    } else {
        // append req to stream list
        client->last_posted_stream_req[flow]->next = req;

	req->prev = client->last_posted_stream_req[flow];
        assert(req->next == NULL);

        client->last_posted_stream_req[flow] = req;
    }
}

void client_track_waited_stream_req(client_t *client, struct mp_request *req, mp_flow_t flow)
{
    const char *flow_str = flow==TX_FLOW?"TX":"RX";
    // init 1st pending req
    mp_dbg_msg("client[%d] req=%p req->id=%d flow=%s\n", client->mpi_rank, req, req->id, flow_str);
    if (!client->waited_stream_req[flow]) {
        mp_dbg_msg("setting client[%d]->waited_stream_req[%s]=req=%p req->id=%d\n", 
                   client->mpi_rank, flow_str, req, req->id);
        assert(client->last_waited_stream_req[flow] == NULL);
        client->waited_stream_req[flow] = client->last_waited_stream_req[flow] = req;
    } else {
        // append req to stream list
        client->last_waited_stream_req[flow]->next = req;

	req->prev = client->last_waited_stream_req[flow];
	assert(req->next == NULL);

        client->last_waited_stream_req[flow] = req;
    }
}

static int qp_query=0;
//Progress TX_FLOW fix:
//progress (remove) some requests on the TX flow if is not possible to queue a send request
int mp_post_send_on_stream(cudaStream_t stream, client_t *client, struct mp_request *req)
{
    int progress_retry=0;
    int ret = MP_SUCCESS;
    int ret_progress = MP_SUCCESS;

    if(!client || !req)
        return MP_FAILURE;

    us_t start = mp_get_cycles();
    us_t tmout = MP_PROGRESS_ERROR_CHECK_TMOUT_US;

    do
    {
        ret = gds_stream_queue_send(stream, client->qp, &req->in.sr, &req->out.bad_sr);
        if(ret == ENOMEM)
        {
            if(qp_query == 0)
            {
                mp_query_print_qp(client->qp, req, 1);
                qp_query=1;
            }


            ret_progress = mp_progress_single_flow(TX_FLOW);
            if(ret_progress != MP_SUCCESS)
            {
                mp_err_msg("mp_progress_single_flow failed. Error: %d\n", ret_progress);
                ret = ret_progress;
                break;
            }
            ret_progress = mp_progress_single_flow(RX_FLOW);
            if(ret_progress != MP_SUCCESS)
            {
                mp_err_msg("mp_progress_single_flow failed. Error: %d\n", ret_progress);
                ret = ret_progress;
                break;
            }
            us_t now = mp_get_cycles();
            long long dt = (long long)now-(long long)start;
            if (dt > (long long)tmout) {
                start = now;
                mp_warn_msg("TX_FLOW has been blocked for %lld secs, checking for GPU errors\n", dt/1000000);
                int retcode = mp_check_gpu_error();
                if (retcode) {
                    mp_err_msg("GPU error %d while progressing TX_FLOW\n", retcode);
                    ret = retcode;
                    mp_enable_dbg(1);
                    goto out;
                }        
                ++progress_retry;
            }
        }
    } while(ret == ENOMEM);
out:
    qp_query=0;
    return ret;
}

static int mp_prepare_send(client_t *client, struct mp_request *req)
{
    int progress_retry=0;
    int ret = MP_SUCCESS;
    int ret_progress = MP_SUCCESS;

    if(!client || !req)
        return MP_FAILURE;

    us_t start = mp_get_cycles();
    us_t tmout = MP_PROGRESS_ERROR_CHECK_TMOUT_US;

    do
    {
        ret = gds_prepare_send(client->qp, &req->in.sr, &req->out.bad_sr, &req->gds_send_info);
        if(ret == ENOMEM)
        {
            ret_progress = mp_progress_single_flow(TX_FLOW);
            if(ret_progress != MP_SUCCESS)
            {
                mp_err_msg("mp_progress_single_flow failed. Error: %d\n", ret_progress);
                ret = ret_progress;
                break;
            }
            ret_progress = mp_progress_single_flow(RX_FLOW);
            if(ret_progress != MP_SUCCESS)
            {
                mp_err_msg("mp_progress_single_flow failed. Error: %d\n", ret_progress);
                ret = ret_progress;
                break;
            }
            us_t now = mp_get_cycles();
            long long dt = (long long)now-(long long)start;
            if (dt > (long long)tmout) {
                start = now;
                mp_warn_msg("TX_FLOW has been blocked for %lld secs, checking for GPU errors\n", dt/1000000);
                int retcode = mp_check_gpu_error();
                if (retcode) {
                    mp_err_msg("GPU error %d while progressing TX_FLOW\n", retcode);
                    ret = retcode;
                    mp_enable_dbg(1);
                    goto out;
                }        
                ++progress_retry;
            }
        }
    } while(ret == ENOMEM);
out:
    return ret;
}


int mp_send_on_stream (void *buf, int size, int peer, mp_reg_t *reg_t, 
		mp_request_t *req_t, cudaStream_t stream)
{
    int ret = 0;
    struct mp_request *req = NULL;
    struct mp_reg *reg = (struct mp_reg *)*reg_t;
    client_t *client = &clients[client_index[peer]];

    if (use_event_sync) {
        req = new_stream_request(client, MP_SEND, MP_PREPARED, stream);
        assert(req);

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

	client->last_posted_trigger_id[mp_type_to_flow(req->type)] = req->id;

	/*delay posting until stream has reached this id*/
#if HAS_GDS_DESCRIPTOR_API
        size_t n_descs = 0;
        gds_descriptor_t descs[2];
        descs[n_descs].tag = GDS_TAG_WRITE_VALUE32;
        ret = gds_prepare_write_value32(&descs[n_descs].write32,
                                        &client->last_trigger_id[mp_type_to_flow(req->type)],
                                        req->id,
                                        GDS_MEMORY_HOST);
        ++n_descs;
#else
        ret = gds_stream_post_poke_dword(stream,
                &client->last_trigger_id[mp_type_to_flow(req->type)],
                req->id,
                GDS_MEMORY_HOST);
#endif
        if (ret) {
            mp_err_msg("gds_stream_queue_send failed: %s \n", strerror(ret));
            // BUG: leaking req ??
            goto out;
        }
        
        client_track_posted_stream_req(client, req, TX_FLOW);

	/*block stream on completion of this req*/
        if (client->last_posted_tracked_id[TX_FLOW] < req->id) {
            req->trigger = 1;

            client->last_posted_tracked_id[TX_FLOW] = req->id;

#if HAS_GDS_DESCRIPTOR_API
            descs[n_descs].tag = GDS_TAG_WAIT_VALUE32;
            ret = gds_prepare_wait_value32(&descs[n_descs].wait32,
                                          &client->last_tracked_id[mp_type_to_flow(req->type)],
                                          req->id,
                                          GDS_WAIT_COND_GEQ,
                                          GDS_MEMORY_HOST);
            ++n_descs;        
#else
            ret = gds_stream_post_poll_dword(stream,
                &client->last_tracked_id[mp_type_to_flow(req->type)],
                req->id,
                GDS_WAIT_COND_GEQ,
                GDS_MEMORY_HOST);
#endif
            if (ret) {
                mp_err_msg("gds_stream_queue_send failed: %s \n", strerror(ret));
                goto out;
            }
        }

#if HAS_GDS_DESCRIPTOR_API
        ret = gds_stream_post_descriptors(stream, n_descs, descs, 0);
        if (ret) {
                mp_err_msg("gds_stream_post_descriptors failed: %s\n", strerror(ret));
                goto out;
        }
#endif
    } else {
        req = new_stream_request(client, MP_SEND, MP_PENDING, stream);

        assert(req);
        req->in.sr.next = NULL;
        req->in.sr.exp_send_flags = IBV_EXP_SEND_SIGNALED;
        req->in.sr.exp_opcode = IBV_EXP_WR_SEND;
        req->in.sr.wr_id = (uintptr_t) req;
        req->in.sr.num_sge = 1;
        req->in.sr.sg_list = &(req->sg_entry);

        if (mp_enable_ud) {
            req->in.sr.wr.ud.ah = client->ah;
            req->in.sr.wr.ud.remote_qpn = client->qpn;
            req->in.sr.wr.ud.remote_qkey = 0;
        }

        req->sg_entry.length = size;
        req->sg_entry.lkey = reg->key;
        req->sg_entry.addr = (uintptr_t)(buf);

        ret = gds_stream_queue_send(stream, client->qp, &req->in.sr,
                                    &req->out.bad_sr);
        if (ret) {
            mp_err_msg("gds_stream_queue_rsend failed: %s \n", strerror(ret));
            // BUG: leaking req ??
            goto out;
        }

        ret = gds_stream_wait_cq(stream, client->send_cq, 0);
        if (ret) {
            mp_err_msg("gds_stream_wait_cq failed: %s \n", strerror(ret));
            // BUG: leaking req ??
            goto out;
        }
    }

    *req_t = req; 

out:
    return ret; 
}

int mp_isend_on_stream (void *buf, int size, int peer, mp_reg_t *reg_t, 
		mp_request_t *req_t, cudaStream_t stream)
{
    int ret = 0;
    struct mp_request *req = NULL;
    struct mp_reg *reg = (struct mp_reg *)*reg_t;
    client_t *client = &clients[client_index[peer]];

    if (use_event_sync) {
        req = new_stream_request(client, MP_SEND, MP_PREPARED, stream);
        assert(req);

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

        client->last_posted_trigger_id[mp_type_to_flow(req->type)] = req->id;

#if HAS_GDS_DESCRIPTOR_API
        size_t n_descs = 0;
        gds_descriptor_t descs[2];
        descs[n_descs].tag = GDS_TAG_WRITE_VALUE32;
        ret = gds_prepare_write_value32(&descs[n_descs].write32,
                                        &client->last_trigger_id[mp_type_to_flow(req->type)],
                                        req->id,
                                        GDS_MEMORY_HOST);
        ++n_descs;
        if (ret) {
            mp_err_msg("gds_stream_queue_send failed: %s \n", strerror(ret));
            // BUG: leaking req ??
            goto out;
        }
        ret = gds_stream_post_descriptors(stream, n_descs, descs, 0);
#else
        ret = gds_stream_post_poke_dword(stream, 
		&client->last_trigger_id[mp_type_to_flow(req->type)], 
		req->id, 
		GDS_MEMORY_HOST);
#endif
        if (ret) {
            mp_err_msg("gds_stream_queue_send failed: %s \n", strerror(ret));
            // BUG: leaking req ??
            goto out;
        }

        client_track_posted_stream_req(client, req, TX_FLOW);
    } else {
        req = new_stream_request(client, MP_SEND, MP_PENDING_NOWAIT, stream);
        assert(req);

        mp_dbg_msg("req=%p id=%d\n", req, req->id);

        req->in.sr.next = NULL;
        req->in.sr.exp_send_flags = IBV_EXP_SEND_SIGNALED;
        req->in.sr.exp_opcode = IBV_EXP_WR_SEND;
        req->in.sr.wr_id = (uintptr_t) req;
        req->in.sr.num_sge = 1;
        req->in.sr.sg_list = &(req->sg_entry);

        if (mp_enable_ud) {
            req->in.sr.wr.ud.ah = client->ah;
            req->in.sr.wr.ud.remote_qpn = client->qpn;
            req->in.sr.wr.ud.remote_qkey = 0;
        }

        req->sg_entry.length = size;
        req->sg_entry.lkey = reg->key;
        req->sg_entry.addr = (uintptr_t)(buf);

        ret = mp_post_send_on_stream(stream, client, req);
        if (ret) {
            mp_err_msg("mp_post_send_on_stream failed: %s \n", strerror(ret));
            // BUG: leaking req ??
            goto out;
        }

        ret = gds_prepare_wait_cq(client->send_cq, &req->gds_wait_info, 0);
        if (ret) {
            mp_err_msg("gds_prepare_wait_cq failed: %s \n", strerror(ret));
            // BUG: leaking req ??
            goto out;
        }
    }

    *req_t = req;

out:
    return ret;
}

int mp_irecv_on_stream (void *buf, int size, int peer, mp_reg_t *reg_t, 
                        mp_request_t *req_t, cudaStream_t stream)
{
    int ret = 0;
#if 1
    ret = -EINVAL;
#else
    struct mp_request *req = NULL;
    struct mp_reg *reg = (struct mp_reg *)*reg_t;
    client_t *client = &clients[client_index[peer]];

    req = new_stream_request(client, MP_RECV, MP_PENDING_NOWAIT, stream);
    assert(req);

    req->in.rr.next = NULL;
    req->in.rr.wr_id = (uintptr_t) req;
    req->in.rr.num_sge = 1;
    req->in.rr.sg_list = &req->sg_entry;

    req->sg_entry.length = size;
    req->sg_entry.lkey = reg->key;
    req->sg_entry.addr = (uintptr_t)(buf);

    ret = gds_stream_queue_send(stream, client->qp, &req->in.sr,
                                &req->out.bad_sr);
    if (ret) {
        mp_err_msg("gds_stream_queue_send failed: %s \n", strerror(ret));
        // BUG: leaking req ??
        goto out;
    }

    ret = gds_stream_queue_recv(stream, client->qp, &req->in.rr, &req->out.bad_rr);
    if (ret) {
        mp_err_msg("gds_stream_queue_recv failed ret: %d error: %s peer: %d index: %d \n", ret, strerror(ret), peer, client_index[peer]);
        goto out;
    }

    ret = gds_prepare_wait_cq(client->recv_cq, &req->gds_wait_info, 0);
    if (ret) {
        mp_err_msg("gds_prepare_wait_cq failed: %s \n", strerror(ret));
        // BUG: leaking req ??
        goto out;
    }

    *req_t = req;
#endif

    return ret;
}

int mp_isendv_on_stream (struct iovec *v, int nvecs, int peer, mp_reg_t *reg_t,
                         mp_request_t *req_t, cudaStream_t stream)
{
  int i, ret = 0;
  struct mp_request *req = NULL;
  struct mp_reg *reg = (struct mp_reg *)*reg_t;
  client_t *client = &clients[client_index[peer]];

  if (use_event_sync) {
      mp_dbg_msg(" Not Implemented \n");
      ret = MP_FAILURE;
      goto out;      	
  }	

  if (nvecs > ib_max_sge) {
      mp_err_msg("exceeding maxing vector size supported: %d \n", ib_max_sge);
      ret = MP_FAILURE;
      goto out;
  }

  req = new_stream_request(client, MP_SEND, MP_PENDING_NOWAIT, stream);
  assert(req);

  req->sgv = malloc(sizeof(struct ibv_sge)*nvecs);
  assert(req->sgv);

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

  ret = gds_stream_queue_send(stream, client->qp, &req->in.sr,
                              &req->out.bad_sr);
  if (ret) {
      mp_err_msg("gds_stream_queue_send failed: %s \n", strerror(ret));
      // BUG: leaking req ??
      goto out;
  }

  ret = gds_prepare_wait_cq(client->send_cq, &req->gds_wait_info, 0);
  if (ret) {
    mp_err_msg("gds_prepare_wait_cq failed: %s \n", strerror(ret));
    // BUG: leaking req ??
    goto out;
  }

  *req_t = req;

 out:
  return ret;
}

int mp_send_prepare(void *buf, int size, int peer, mp_reg_t *reg_t, mp_request_t *req_t)
{
    int ret = 0;
    struct mp_request *req;
    struct mp_reg *reg = (struct mp_reg *)*reg_t;
    client_t *client = &clients[client_index[peer]];
  
    req = new_stream_request(client, MP_SEND, MP_PREPARED, NULL);
    assert(req);

    mp_dbg_msg(" preparing send message, req->id=%d \n", req->id);

    if (use_event_sync) {
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
    } else {
        req->in.sr.next = NULL;
        req->in.sr.exp_send_flags = IBV_EXP_SEND_SIGNALED;
        req->in.sr.exp_opcode = IBV_EXP_WR_SEND;
        req->in.sr.wr_id = (uintptr_t) req;
        req->in.sr.num_sge = 1;
        req->in.sr.sg_list = &(req->sg_entry);

        if (mp_enable_ud) {
            req->in.sr.wr.ud.ah = client->ah;
            req->in.sr.wr.ud.remote_qpn = client->qpn;
            req->in.sr.wr.ud.remote_qkey = 0;

            mp_err_msg("[%d] posintg send to qpn %d size: %d req: %p \n", mpi_comm_rank, req->in.sr.wr.ud.remote_qpn, size, req);
        }

        req->sg_entry.length = size;
        req->sg_entry.lkey = reg->key;
        req->sg_entry.addr = (uintptr_t)(buf);
        
        ret = gds_prepare_send(client->qp, &req->in.sr,
                               &req->out.bad_sr, &req->gds_send_info);
        //ret = mp_prepare_send(client, req);
        if (ret) {
            mp_err_msg("mp_prepare_send failed: %s \n", strerror(ret));
            // BUG: leaking req ??
            goto out;
        }

        ret = gds_prepare_wait_cq(client->send_cq, &req->gds_wait_info, 0);
        if (ret) {
            mp_err_msg("gds_prepare_wait_cq failed: %s \n", strerror(ret));
            // BUG: leaking req ??
            goto out;
        }
    }

    *req_t = req;
out:
    return ret;
}

int mp_sendv_prepare(struct iovec *v, int nvecs, int peer, mp_reg_t *reg_t, mp_request_t *req_t)
{
  int i, ret = 0;
  struct mp_request *req = NULL;
  struct mp_reg *reg = (struct mp_reg *)*reg_t;
  client_t *client = &clients[client_index[peer]];

  if (use_event_sync) {
      mp_dbg_msg(" Not Implemented \n");
      ret = MP_FAILURE;
      goto out;
  }

  if (nvecs > ib_max_sge) {
      mp_err_msg("exceeding maxing vector size supported: %d \n", ib_max_sge);
      ret = MP_FAILURE;
      goto out;
  }

  req = new_stream_request(client, MP_SEND, MP_PREPARED, NULL);
  assert(req);

  req->sgv = malloc(sizeof(struct ibv_sge)*nvecs);
  assert(req->sgv);

  for (i=0; i < nvecs; ++i) {
    req->sgv[i].length = v[i].iov_len;
    req->sgv[i].lkey = reg->key;
    req->sgv[i].addr = (uintptr_t)(v[i].iov_base);
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

  ret = gds_prepare_send(client->qp, &req->in.sr,
                         &req->out.bad_sr, &req->gds_send_info);
  //ret = mp_prepare_send(client, req);
  if (ret) {
      mp_err_msg("mp_prepare_send failed: %s \n", strerror(ret));
      // BUG: leaking req ??
      goto out;
  }

  ret = gds_prepare_wait_cq(client->send_cq, &req->gds_wait_info, 0);
  if (ret) {
      mp_err_msg("gds_prepare_wait_cq failed: %s \n", strerror(ret));
      // BUG: leaking req ??
      goto out;
  }

  *req_t = req;

 out:
  return ret;
}


int mp_send_post_on_stream (mp_request_t *req_t, cudaStream_t stream)
{
    int ret = 0;
    struct mp_request *req = *req_t;

    assert(req->status == MP_PREPARED);

    client_t *client = &clients[client_index[req->peer]];
    req->stream = stream;

    if (use_event_sync) {

        if (req->id <= client->last_posted_trigger_id[mp_type_to_flow(req->type)]) {
             mp_err_msg("postd order is different from prepared order, last posted: %d being posted: %d \n",
                        client->last_posted_trigger_id[mp_type_to_flow(req->type)], req->id);
             ret = MP_FAILURE;
             goto out;
        }
        client->last_posted_trigger_id[mp_type_to_flow(req->type)] = req->id;

	/*delay posting until stream has reached this id*/
#if HAS_GDS_DESCRIPTOR_API
        size_t n_descs = 0;
        gds_descriptor_t descs[2];
        descs[n_descs].tag = GDS_TAG_WRITE_VALUE32;
        ret = gds_prepare_write_value32(&descs[n_descs].write32,
                                        &client->last_trigger_id[mp_type_to_flow(req->type)],
                                        req->id,
                                        GDS_MEMORY_HOST);
        ++n_descs;
#else
        ret = gds_stream_post_poke_dword(stream,
                &client->last_trigger_id[mp_type_to_flow(req->type)],
                req->id,
                GDS_MEMORY_HOST);
#endif
        if (ret) {
            mp_err_msg("gds_stream_queue_send failed: %s \n", strerror(ret));
            // BUG: leaking req ??
            goto out;
        }
        
        client_track_posted_stream_req(client, req, TX_FLOW);

	/*block stream on completion of this req*/
        if (client->last_posted_tracked_id[TX_FLOW] < req->id) {
            req->trigger = 1;
            client->last_posted_tracked_id[TX_FLOW] = req->id;
#if HAS_GDS_DESCRIPTOR_API
            descs[n_descs].tag = GDS_TAG_WAIT_VALUE32;
            ret = gds_prepare_wait_value32(&descs[n_descs].wait32,
                                          &client->last_tracked_id[mp_type_to_flow(req->type)],
                                          req->id,
                                          GDS_WAIT_COND_GEQ,
                                          GDS_MEMORY_HOST);
            ++n_descs;        
#else
            ret = gds_stream_post_poll_dword(stream,
                &client->last_tracked_id[mp_type_to_flow(req->type)],
                req->id,
                GDS_WAIT_COND_GEQ,
                GDS_MEMORY_HOST);
#endif
            if (ret) {
                mp_err_msg("gds_stream_queue_send failed: %s \n", strerror(ret));
                goto out;
            }
        }
#if HAS_GDS_DESCRIPTOR_API
        ret = gds_stream_post_descriptors(stream, n_descs, descs, 0);
        if (ret) {
                mp_err_msg("gds_stream_post_descriptors failed: %s\n", strerror(ret));
                goto out;
        }
#endif
    } else { 	
        req->status = MP_PENDING;

        ret = gds_stream_post_send(stream, &req->gds_send_info);
        if (ret) {
            mp_err_msg("gds_stream_post_send failed: %s \n", strerror(ret));
            // BUG: leaking req ??
            goto out;
        }

	    ret = gds_stream_post_wait_cq(stream, &req->gds_wait_info);
        if (ret) {
            mp_err_msg("gds_stream_post_wait failed: %s \n", strerror(ret));
            // bug: leaking req ??
            goto out;
        }
    }

out:
    return ret;
}

int mp_isend_post_on_stream (mp_request_t *req_t, cudaStream_t stream)
{
    int retcode;
    int ret = 0; 
    struct mp_request *req = *req_t;

    assert(req->status == MP_PREPARED);

    req->stream = stream;

    if (use_event_sync) {
        client_t *client = &clients[client_index[req->peer]];

        if (req->id <= client->last_posted_trigger_id[mp_type_to_flow(req->type)]) {
             mp_err_msg("postd order is different from prepared order, last posted: %d being posted: %d \n",
                        client->last_posted_trigger_id[mp_type_to_flow(req->type)], req->id);
             ret = MP_FAILURE;
             goto out;
        }
        client->last_posted_trigger_id[mp_type_to_flow(req->type)] = req->id;

	/*delay posting until stream has reached this id*/
#if HAS_GDS_DESCRIPTOR_API
        size_t n_descs = 0;
        gds_descriptor_t descs[2];
        descs[n_descs].tag = GDS_TAG_WRITE_VALUE32;
        retcode = gds_prepare_write_value32(&descs[n_descs].write32,
                                            &client->last_trigger_id[mp_type_to_flow(req->type)],
                                            req->id,
                                            GDS_MEMORY_HOST);
        ++n_descs;
        if (ret) {
                mp_err_msg("gds_prepare_write_value32 failed: %s\n", strerror(ret));
                goto out;
        }
        ret = gds_stream_post_descriptors(stream, n_descs, descs, 0);
#else
        retcode = gds_stream_post_poke_dword(stream,
                                             &client->last_trigger_id[mp_type_to_flow(req->type)],
                                             req->id,
                                             GDS_MEMORY_HOST);
#endif
        if (retcode) {
            mp_err_msg("error %s\n", strerror(retcode));
            ret = MP_FAILURE;
            // BUG: leaking req ??
            goto out;
        }
        
        client_track_posted_stream_req(client, req, TX_FLOW);
    } else {
        req->status = MP_PENDING_NOWAIT;

        ret = gds_stream_post_send(stream, &req->gds_send_info);
        if (ret) {
            mp_err_msg("gds_stream_post_send failed: %s \n", strerror(ret));
            goto out;
        }
    }

out:
    return ret;
}

int mp_send_post_all_on_stream (uint32_t count, mp_request_t *req_t, cudaStream_t stream)
{
    int i, ret = 0;

    if (use_event_sync) {
       size_t n_descs = 0;
       gds_descriptor_t descs[2*count];

       for (i=0; i<count; i++) {
  	   struct mp_request *req = req_t[i];
           client_t *client = &clients[client_index[req->peer]];

           /*BUG: can requests passed to post_all be differnt from the order they were prepared?*/
           if (req->id <= client->last_posted_trigger_id[mp_type_to_flow(req->type)]) {
               mp_err_msg("postd order is different from prepared order, last posted: %d being posted: %d \n",
                          client->last_posted_trigger_id[mp_type_to_flow(req->type)], req->id);
               ret = MP_FAILURE;
               goto out;
           }
           client->last_posted_trigger_id[mp_type_to_flow(req->type)] = req->id;
           
           mp_dbg_msg("[%d] posting dword on stream:%p id:%d \n", mpi_comm_rank, stream, req->id);

	   /*delay posting until stream has reached this id*/
#if HAS_GDS_DESCRIPTOR_API
           descs[n_descs].tag = GDS_TAG_WRITE_VALUE32;
           ret = gds_prepare_write_value32(&descs[n_descs].write32,
                                           &client->last_trigger_id[mp_type_to_flow(req->type)],
                                           req->id,
                                           GDS_MEMORY_HOST);
           ++n_descs;
#else
           ret = gds_stream_post_poke_dword(stream,
                                            &client->last_trigger_id[mp_type_to_flow(req->type)],
                                            req->id,
                                            GDS_MEMORY_HOST);
#endif
           if (ret) {
               mp_err_msg("gds_stream_queue_send failed: %s \n", strerror(ret));
               // BUG: leaking req ??
               goto out;
           }
           
           client_track_posted_stream_req(client, req, TX_FLOW);

	   /*block stream on completion of this req*/
           if (client->last_posted_tracked_id[TX_FLOW] < req->id) {
               req->trigger = 1;
               client->last_posted_tracked_id[TX_FLOW] = req->id;
               mp_dbg_msg("[%d] posting poll on stream:%p id:%d \n", mpi_comm_rank, stream, req->id);
#if HAS_GDS_DESCRIPTOR_API
               descs[n_descs].tag = GDS_TAG_WAIT_VALUE32;
               ret = gds_prepare_wait_value32(&descs[n_descs].wait32,
                                              &client->last_tracked_id[mp_type_to_flow(req->type)],
                                              req->id,
                                              GDS_WAIT_COND_GEQ,
                                              GDS_MEMORY_HOST);
               ++n_descs;        
#else
               ret = gds_stream_post_poll_dword(stream,
                   &client->last_tracked_id[mp_type_to_flow(req->type)],
                   req->id,
                   GDS_WAIT_COND_GEQ,
                   GDS_MEMORY_HOST);
#endif
               if (ret) {
                   mp_err_msg("gds_stream_queue_send failed: %s \n", strerror(ret));
                   goto out;
               }
           }
       } // for
#if HAS_GDS_DESCRIPTOR_API
        ret = gds_stream_post_descriptors(stream, n_descs, descs, 0);
        if (ret) {
                mp_err_msg("gds_stream_post_descriptors failed: %s\n", strerror(ret));
                goto out;
        }
#endif
    } else {	
       gds_send_request_t gds_send_request_local[8];
       gds_send_request_t *gds_send_request;
       gds_wait_request_t gds_wait_request_local[8];
       gds_wait_request_t *gds_wait_request;

       if (count <= 8) {
           gds_send_request = gds_send_request_local;
           gds_wait_request = gds_wait_request_local;
       } else {
           gds_send_request = malloc(count*sizeof(*gds_send_request));
           gds_wait_request = malloc(count*sizeof(*gds_wait_request));
       }

       for (i=0; i<count; i++) {
           struct mp_request *req = req_t[i];
           assert(req->status == MP_PREPARED);

           gds_send_request[i] = req->gds_send_info;
           gds_wait_request[i] = req->gds_wait_info;
       }

       ret = gds_stream_post_send_all(stream, count, gds_send_request);
       if (ret) {
           mp_err_msg("gds_stream_post_send_all failed: %s \n", strerror(ret));
           // BUG: leaking req ??
           goto out;
       }

       ret = gds_stream_post_wait_cq_all(stream, count, gds_wait_request);
       if (ret) {
           mp_err_msg("gds_stream_post_wait_all failed: %s \n", strerror(ret));
           // BUG: leaking req ??
           goto out;
       }

       if (count > 8) {
           free(gds_send_request);
           free(gds_wait_request);
       }
    }
out:
    return ret;
}

int mp_isend_post_all_on_stream (uint32_t count, mp_request_t *req_t, cudaStream_t stream)
{
    int ret = 0;
    int i;
    gds_send_request_t gds_send_request_local[8];
    gds_send_request_t *gds_send_request;

    mp_dbg_msg(" Entering \n");

    if (use_event_sync) {
       size_t n_descs = 0;
       gds_descriptor_t descs[2*count];

       for (i=0; i<count; i++) {
  	   struct mp_request *req = req_t[i];
           client_t *client = &clients[client_index[req->peer]];

	   /*BUG: can requests passed to post_all be differnt from the order they were prepared?*/
           if (req->id <= client->last_posted_trigger_id[mp_type_to_flow(req->type)]) {
                mp_err_msg("posted order is different from prepared order, last posted: %d being posted: %d \n",
                           client->last_posted_trigger_id[mp_type_to_flow(req->type)], req->id);
                ret = MP_FAILURE;
                goto out;
           }
           client->last_posted_trigger_id[mp_type_to_flow(req->type)] = req->id;

           mp_dbg_msg("[%d] posting dword on stream:%p id:%d \n", mpi_comm_rank, stream, req->id);

	   /*delay posting until stream has reached this id*/
#if HAS_GDS_DESCRIPTOR_API
           descs[n_descs].tag = GDS_TAG_WRITE_VALUE32;
           ret = gds_prepare_write_value32(&descs[n_descs].write32,
                                           &client->last_trigger_id[mp_type_to_flow(req->type)],
                                           req->id,
                                           GDS_MEMORY_HOST);
           ++n_descs;
#else
           ret = gds_stream_post_poke_dword(stream,
                   &client->last_trigger_id[mp_type_to_flow(req->type)],
                   req->id,
                   GDS_MEMORY_HOST);
#endif
           if (ret) {
               mp_err_msg("error while posting pokes %d/%s \n", ret, strerror(ret));
               // BUG: leaking req ??
               ret = MP_FAILURE;
               goto out;
           }
           
           client_track_posted_stream_req(client, req, TX_FLOW);
       }
#if HAS_GDS_DESCRIPTOR_API
       ret = gds_stream_post_descriptors(stream, n_descs, descs, 0);
       if (ret) {
               mp_err_msg("gds_stream_post_descriptors failed: %s\n", strerror(ret));
               goto out;
       }
#endif  
    } else { 	
        if (count <= 8) {
            gds_send_request = gds_send_request_local;
        } else {
            gds_send_request = malloc(count*sizeof(gds_send_request_t));
        }

        for (i=0; i<count; i++) {
            struct mp_request *req = req_t[i];

            assert(req->status == MP_PREPARED);

            req->stream = stream;
            req->status = MP_PENDING_NOWAIT;

            gds_send_request[i] = req->gds_send_info;
        }

        ret = gds_stream_post_send_all(stream, count, gds_send_request);
        if (ret) {
            mp_err_msg("gds_stream_post_send_all failed: %s \n", strerror(ret));
            // BUG: leaking req ??
            goto out;
        }

        if (count > 8) {
            free(gds_send_request);
        }
    }

    mp_dbg_msg(" Leaving \n");
 
out:
    return ret;
}

int mp_wait_on_stream (mp_request_t *req_t, cudaStream_t stream)
{
    assert(req_t);
    int ret = 0;
    struct mp_request *req = *req_t;
    client_t *client = &clients[client_index[req->peer]];

    mp_dbg_msg("req=%p status=%d id=%d\n", req, req->status, req->id);

    if (use_event_sync) {
        assert (req->type > 0);

        if (req->status == MP_COMPLETE) { 
	    //request_complete, nothing to do
	    goto out;
	}

	if (client->last_posted_tracked_id[mp_type_to_flow(req->type)] < req->id) { 
	    client->last_posted_tracked_id[mp_type_to_flow(req->type)] = req->id;
            req->trigger = 1;
#if HAS_GDS_DESCRIPTOR_API
            size_t n_descs = 0;
            gds_descriptor_t descs[1];
            descs[n_descs].tag = GDS_TAG_WAIT_VALUE32;
            ret = gds_prepare_wait_value32(&descs[n_descs].wait32,
                                           &client->last_tracked_id[mp_type_to_flow(req->type)],
                                           req->id,
                                           GDS_WAIT_COND_GEQ,
                                           GDS_MEMORY_HOST);
            if (ret) {
                    mp_err_msg("gds_prepare_wait_value32 failed: %s\n", strerror(ret));
                    goto out;
            }
            ++n_descs;        
            ret = gds_stream_post_descriptors(stream, n_descs, descs, 0);
#else
            ret = gds_stream_post_poll_dword(stream, 
	    	&client->last_tracked_id[mp_type_to_flow(req->type)], 
	    	req->id, 
	    	GDS_WAIT_COND_GEQ,
	    	GDS_MEMORY_HOST);
#endif
            if (ret) {
                mp_err_msg("gds_stream_queue_send failed: %s \n", strerror(ret));
                goto out;
            }
	}
    } else {
	//TODO:Can this be skipped if the last CQE that stream is waiting on is later than this?
	//     Similar to the event_sync case
        //assert(req->status == MP_PENDING_NOWAIT);
        // cannot check wait-for-end more than once

        if (!req_can_be_waited(req)) {
            mp_dbg_msg("cannot wait req:%p status:%d id=%d peer=%d type=%d flags=%08x\n", req, req->status, req->id, req->peer, req->type, req->flags);
            ret = EINVAL;
            goto out;
        }

        assert(req->status == MP_PENDING_NOWAIT || req->status == MP_COMPLETE);
	
        req->stream = stream;
        req->status = MP_PENDING;

        ret = gds_stream_post_wait_cq(stream, &req->gds_wait_info);
        if (ret) {
            mp_err_msg("error %d(%s) in gds_stream_post_wait_cq\n", ret, strerror(ret));
            // BUG: leaking req ??
            goto out;
        }
    }

out:
    return ret;
}

/*----------------------------------------------------------------------------*/

int mp_wait_all_on_stream (uint32_t count, mp_request_t *req_t, cudaStream_t stream)
{
    int i, ret = 0;

    if (!count)
        goto out;

    if (use_event_sync) {
        size_t n_descs = 0;
        gds_descriptor_t descs[count];
        for (i=0; i<count; i++) {
            struct mp_request *req = req_t[i];
            client_t *client = &clients[client_index[req->peer]];

            assert (req->type > 0);
            assert (req->status <= MP_PENDING);

            if (client->last_posted_tracked_id[mp_type_to_flow(req->type)] < req->id) {
                client->last_posted_tracked_id[mp_type_to_flow(req->type)] = req->id;

                req->trigger = 1;

#if HAS_GDS_DESCRIPTOR_API
                descs[n_descs].tag = GDS_TAG_WAIT_VALUE32;
                ret = gds_prepare_wait_value32(&descs[n_descs].wait32,
                                               &client->last_tracked_id[mp_type_to_flow(req->type)],
                                               req->id,
                                               GDS_WAIT_COND_GEQ,
                                               GDS_MEMORY_HOST);
                ++n_descs;        
#else
                ret = gds_stream_post_poll_dword(stream,
                    &client->last_tracked_id[mp_type_to_flow(req->type)],
                    req->id,
                    GDS_WAIT_COND_GEQ,
                    GDS_MEMORY_HOST);
#endif
                if (ret) {
                    mp_err_msg("gds_stream_queue_send failed: %s \n", strerror(ret));
                    goto out;
                }
            }
        }
#if HAS_GDS_DESCRIPTOR_API
        ret = gds_stream_post_descriptors(stream, n_descs, descs, 0);
        if (ret) {
                mp_err_msg("gds_stream_post_descriptors failed: %s\n", strerror(ret));
                goto out;
        }
#endif
    } else {
        gds_wait_request_t gds_wait_request_local[8];
        gds_wait_request_t *gds_wait_request;

        mp_dbg_msg("count=%d\n", count);

        if (count <= 8) {
            gds_wait_request = gds_wait_request_local;
        } else {
            gds_wait_request = malloc(count*sizeof(gds_wait_request_t));
        }

        for (i=0; i<count; i++) {
            struct mp_request *req = req_t[i];
            mp_dbg_msg("posting wait cq req=%p id=%d\n", req, req->id);

            if (!req_can_be_waited(req)) {
                mp_dbg_msg("cannot wait req:%p status:%d id=%d peer=%d type=%d flags=%08x\n", req, req->status, req->id, req->peer, req->type, req->flags);
                ret = EINVAL;
                goto out;
            }

            // cannot check wait-for-end more than once
            assert(req->status == MP_PENDING_NOWAIT || req->status == MP_COMPLETE);
            //assert(req->status == MP_PENDING_NOWAIT);

            req->stream = stream;
            req->status = MP_PENDING;

            gds_wait_request[i] = req->gds_wait_info;
        }

        ret = gds_stream_post_wait_cq_all(stream, count, gds_wait_request);
        if (ret) {
            mp_err_msg("gds_stream_post_wait_all failed: %s \n", strerror(ret));
            goto out;
        }

        if (count > 8) {
            free(gds_wait_request);
        }
    }

out:
    return ret;
}

/*----------------------------------------------------------------------------*/

int mp_put_prepare (void *src, int size, mp_reg_t *reg_t, int peer, size_t displ,
                    mp_window_t *window_t, mp_request_t *req_t, int flags)
{
  int ret = 0;
  struct mp_request *req = NULL;
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

  req = new_stream_request(client, MP_RDMA, MP_PREPARED, NULL);
  assert(req);

  mp_dbg_msg("req=%p id=%d\n", req, req->id);

  req->flags = flags;
  req->in.sr.next = NULL;
  if (flags & MP_PUT_NOWAIT) {
      mp_dbg_msg("MP_PUT_NOWAIT set\n");
      req->in.sr.exp_send_flags = 0;
  } else {
      req->in.sr.exp_send_flags = IBV_EXP_SEND_SIGNALED;
  }
  if (flags & MP_PUT_INLINE) {
      mp_dbg_msg("setting SEND_INLINE flag\n");
      req->in.sr.exp_send_flags |= IBV_EXP_SEND_INLINE;
  }
  req->in.sr.exp_opcode = IBV_EXP_WR_RDMA_WRITE;
  req->in.sr.wr_id = (uintptr_t) req;
  req->in.sr.num_sge = 1;
  req->in.sr.sg_list = &req->sg_entry;

  req->sg_entry.length = size;
  req->sg_entry.lkey = reg->key;
  req->sg_entry.addr = (uintptr_t)src;

  req->in.sr.wr.rdma.remote_addr = ((uint64_t)window->base_ptr[client_id]) + displ;
  req->in.sr.wr.rdma.rkey = window->rkey[client_id];

  ret = gds_prepare_send(client->qp, &req->in.sr,
                         &req->out.bad_sr, &req->gds_send_info);
  //ret = mp_prepare_send(client, req);
  if (ret) {
      mp_err_msg("error %d in mp_prepare_send: %s \n", ret, strerror(ret));
      goto out;
  }

  if (flags & MP_PUT_NOWAIT) {
      //req->status = MP_COMPLETE;
  } else {
      ret = gds_prepare_wait_cq(client->send_cq, &req->gds_wait_info, 0);
      if (ret) {
          mp_err_msg("error %d gds_prepare_wait_cq failed: %s \n", ret, strerror(ret));
          goto out;
      }
  }
  
  *req_t = req;
  
 out:
  if (ret) { 
      // free req
      if (req)
          release_mp_request(req);
  }
      
  return ret;
}

/*----------------------------------------------------------------------------*/

int mp_iput_on_stream (void *src, int size, mp_reg_t *reg_t, int peer, size_t displ,
                       mp_window_t *window_t, mp_request_t *req_t, int flags, cudaStream_t stream)
{
  int ret = 0;
  struct mp_request *req = NULL;
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

  req = new_stream_request(client, MP_RDMA, MP_PENDING_NOWAIT, stream);
  assert(req);

  mp_dbg_msg("req=%p id=%d\n", req, req->id);

  req->flags = flags;
  req->in.sr.next = NULL;
  if (flags & MP_PUT_NOWAIT) {
      mp_dbg_msg("MP_PUT_NOWAIT set\n");
      req->in.sr.exp_send_flags = 0;
  } else {
      req->in.sr.exp_send_flags = IBV_EXP_SEND_SIGNALED;
  }
  if (flags & MP_PUT_INLINE) {
      mp_dbg_msg("setting SEND_INLINE flag\n");
      req->in.sr.exp_send_flags |= IBV_EXP_SEND_INLINE;
  }
  req->in.sr.exp_opcode = IBV_EXP_WR_RDMA_WRITE;
  req->in.sr.wr_id = (uintptr_t) req;
  req->in.sr.num_sge = 1;
  req->in.sr.sg_list = &req->sg_entry;

  req->sg_entry.length = size;
  req->sg_entry.lkey = reg->key;
  req->sg_entry.addr = (uintptr_t)src;

  req->in.sr.wr.rdma.remote_addr = ((uint64_t)window->base_ptr[client_id]) + displ;
  req->in.sr.wr.rdma.rkey = window->rkey[client_id];

  ret = mp_post_send_on_stream(stream, client, req);
  if (ret) {
      mp_err_msg("gds_stream_queue_send failed: err=%d(%s) \n", ret, strerror(ret));
      // BUG: leaking req ??
      goto out;
  }

  if (flags & MP_PUT_NOWAIT) {
      req->status = MP_COMPLETE;
  } else {
      ret = gds_prepare_wait_cq(client->send_cq, &req->gds_wait_info, 0);
      if (ret) {
          mp_err_msg("gds_prepare_wait_cq failed: %s \n", strerror(ret));
          // BUG: leaking req ??
          goto out;
      }
  }
  *req_t = req;

 out:
  if (ret) { 
      // free req
      if (req)
          release_mp_request(req);
  }

  return ret;
}

/*----------------------------------------------------------------------------*/

int mp_iget_on_stream (void *dst, int size, mp_reg_t *reg_t, int peer, size_t displ,
        mp_window_t *window_t, mp_request_t *req_t, cudaStream_t stream)
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

  req = new_stream_request(client, MP_RDMA, MP_PENDING_NOWAIT, stream);
  assert(req);

  req->in.sr.next = NULL;
  req->in.sr.exp_send_flags = IBV_EXP_SEND_SIGNALED;
  req->in.sr.exp_opcode = IBV_EXP_WR_RDMA_READ;
  req->in.sr.wr_id = (uintptr_t) req;
  req->in.sr.num_sge = 1;
  req->in.sr.sg_list = &req->sg_entry;

  req->sg_entry.length = size;
  req->sg_entry.lkey = reg->key;
  req->sg_entry.addr = (uintptr_t)dst;

  req->in.sr.wr.rdma.remote_addr = ((uint64_t)window->base_ptr[client_id]) + displ;
  req->in.sr.wr.rdma.rkey = window->rkey[client_id];

  ret = gds_stream_queue_send(stream, client->qp, &req->in.sr,
                              &req->out.bad_sr);
  if (ret) {
      mp_err_msg("gds_stream_queue_send failed: %s \n", strerror(ret));
      goto out;
  }

  ret = gds_prepare_wait_cq(client->send_cq, &req->gds_wait_info, 0);
  if (ret) {
    mp_err_msg("gds_prepare_wait_cq failed: %s \n", strerror(ret));
    goto out;
  }

  *req_t = req;

 out:
  return ret;
}

/*----------------------------------------------------------------------------*/

int mp_iput_post_on_stream (mp_request_t *req_t, cudaStream_t stream)
{
    int ret = 0; 
    struct mp_request *req = *req_t;

    assert(req->status == MP_PREPARED);

    mp_dbg_msg("req=%p id=%d\n", req, req->id);
    
    ret = gds_stream_post_send(stream, &req->gds_send_info);
    if (ret) {
        mp_err_msg("gds_stream_post_send failed: %s \n", strerror(ret));
        goto out;
    }

    if (req->flags & MP_PUT_NOWAIT) {
        req->status = MP_COMPLETE;
    } else {
        req->status = MP_PENDING_NOWAIT;
    }

out:
    return ret;
}

/*----------------------------------------------------------------------------*/

int mp_iput_post_all_on_stream (uint32_t count, mp_request_t *req_t, cudaStream_t stream)
{
    int i, ret = 0;
    gds_send_request_t gds_send_request_local[8];
    gds_send_request_t *gds_send_request = NULL;

    mp_dbg_msg("count=%d\n", count);

    if (count <= 8) {
        gds_send_request = gds_send_request_local;
    } else {
        gds_send_request = malloc(count*sizeof(gds_send_request_t));
        if (!gds_send_request) {
            mp_err_msg("cannot allocate memory\n");
            ret = ENOMEM;
            goto out;
        }
    }

    for (i=0; i<count; i++) {
        struct mp_request *req = req_t[i];

        assert(req->status == MP_PREPARED);
        req->status = MP_PENDING_NOWAIT;

        mp_dbg_msg("posting send for req=%p id=%d\n", req, req->id);

        gds_send_request[i] = req->gds_send_info;
    }

    ret = gds_stream_post_send_all(stream, count, gds_send_request);
    if (ret) {
        mp_err_msg("error %d(%s) in gds_stream_post_send_all\n", ret, strerror(ret));
    }

    if (count > 8) {
        free(gds_send_request);
    }
out:
    return ret;
}

/*----------------------------------------------------------------------------*/

int mp_wait32_on_stream(uint32_t *ptr, uint32_t value, int flags, cudaStream_t stream)
{
    int ret = MP_SUCCESS;
    int cond_flags = 0;
    size_t n_descs = 0;
    gds_descriptor_t descs[1];

    mp_dbg_msg("ptr=%p value=%d\n", ptr, value);

    switch(flags) {
    case MP_WAIT_EQ:  cond_flags |= GDS_WAIT_COND_GEQ; break;
    case MP_WAIT_GEQ: cond_flags |= GDS_WAIT_COND_GEQ; break;
    case MP_WAIT_AND: cond_flags |= GDS_WAIT_COND_GEQ; break;
    default: ret = EINVAL; goto out; break;
    }
#if HAS_GDS_DESCRIPTOR_API
    descs[n_descs].tag = GDS_TAG_WAIT_VALUE32;
    ret = gds_prepare_wait_value32(&descs[n_descs].wait32,
                                   ptr,
                                   value,
                                   cond_flags,
                                   GDS_MEMORY_HOST|GDS_WAIT_POST_FLUSH);
    ++n_descs;
    if (ret) {
            mp_err_msg("gds_prepare_wait_value32 failed: %s\n", strerror(ret));
            goto out;
    }
    ret = gds_stream_post_descriptors(stream, n_descs, descs, 0);
#else
    ret = gds_stream_post_poll_dword(stream, ptr, value, cond_flags, GDS_MEMORY_HOST);//|GDS_WAIT_POST_FLUSH);
#endif
    if (ret) {
        mp_err_msg("error %d while posting poll on ptr=%p value=%08x flags=%08x\n", ret, ptr, value, flags);
    }
out:
    return ret;
}

/*----------------------------------------------------------------------------*/

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 *  tab-width: 4
 *  indent-tabs-mode: nil
 * End:
 */
