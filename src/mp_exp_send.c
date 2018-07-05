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

int mp_post_send_on_stream_exp(int peer, 
                                struct mp_request *req, 
                                cudaStream_t stream)
{
    int ret=0;
	client_t *client = &clients[client_index[peer]];

    if(!req)
        return MP_FAILURE;

    ret = gds_update_send_info(&req->gds_send_info, GDS_ASYNC, stream);
    if (ret) {
            mp_err_msg("gds_update_send_info failed: %s\n", strerror(ret));
            goto out;
    }

    ret = gds_stream_post_send(stream, &req->gds_send_info);
    if (ret) {
            mp_err_msg("gds_update_send_info failed: %s\n", strerror(ret));
            goto out;
    }

    ret = gds_prepare_wait_cq(client->send_cq, &req->gds_wait_info, 0);
    if (ret) {
        mp_err_msg("gds_prepare_wait_cq failed: %s \n", strerror(ret));
        // BUG: leaking req ??
        goto out;
    }

out:
    return ret;
}

int mp_prepare_send_exp(int peer,
                        struct mp_request *req, 
                        void * ptr_to_size_new,
                        void * ptr_to_lkey_new,
                        void * ptr_to_addr_new)
{
    int progress_retry=0;
    int ret = MP_SUCCESS;
    int ret_progress = MP_SUCCESS;    
    int ptr_to_size_flags=0, ptr_to_lkey_flags=0, ptr_to_addr_flags=0;
    unsigned int mem_type;
    CUresult curesult;
    const char *err_str = NULL;
	
	client_t *client = &clients[client_index[peer]];

    if(!req)
        return MP_FAILURE;


    //Useful to instruct libmlx5 to retrieve info
    req->in.sr.exp_send_flags |= IBV_EXP_SEND_GET_INFO;

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

    if(ptr_to_size_new != NULL)
    {
        curesult = cuPointerGetAttribute((void *)&mem_type, CU_POINTER_ATTRIBUTE_MEMORY_TYPE, (CUdeviceptr)ptr_to_size_new);
        if (curesult == CUDA_SUCCESS) {
            if (mem_type == CU_MEMORYTYPE_DEVICE) ptr_to_size_flags=GDS_MEMORY_GPU;
            else if (mem_type == CU_MEMORYTYPE_HOST) ptr_to_size_flags=GDS_MEMORY_HOST;
            else
            {
                mp_err_msg("error ptr size mem_type=%d\n", __FUNCTION__, mem_type);
                ret=MP_FAILURE;
                goto out;
            }
        }
        else {
            cuGetErrorString(ret, &err_str);        
            mp_err_msg("%s error ret=%d(%s)\n", __FUNCTION__, ret, err_str);
            ret=MP_FAILURE;
            goto out;
        }        
    }

    if(ptr_to_lkey_new != NULL)
    {
        curesult = cuPointerGetAttribute((void *)&mem_type, CU_POINTER_ATTRIBUTE_MEMORY_TYPE, (CUdeviceptr)ptr_to_lkey_new);
        if (curesult == CUDA_SUCCESS) {
            if (mem_type == CU_MEMORYTYPE_DEVICE) ptr_to_lkey_flags=GDS_MEMORY_GPU;
            else if (mem_type == CU_MEMORYTYPE_HOST) ptr_to_lkey_flags=GDS_MEMORY_HOST;
            else
            {
                mp_err_msg("error ptr lkey mem_type=%d\n", __FUNCTION__, mem_type);
                ret=MP_FAILURE;
                goto out;
            }
        }
        else {
            cuGetErrorString(ret, &err_str);        
            mp_err_msg("%s error ret=%d(%s)\n", __FUNCTION__, ret, err_str);
            ret=MP_FAILURE;
            goto out;
        }        
    }

    if(ptr_to_addr_new != NULL)
    {
        curesult = cuPointerGetAttribute((void *)&mem_type, CU_POINTER_ATTRIBUTE_MEMORY_TYPE, (CUdeviceptr)ptr_to_addr_new);
        if (curesult == CUDA_SUCCESS) {
            if (mem_type == CU_MEMORYTYPE_DEVICE) ptr_to_addr_flags=GDS_MEMORY_GPU;
            else if (mem_type == CU_MEMORYTYPE_HOST) ptr_to_addr_flags=GDS_MEMORY_HOST;
            else
            {
                mp_err_msg("error ptr addr mem_type=%d\n", __FUNCTION__, mem_type);
                ret=MP_FAILURE;
                goto out;
            }
        }
        else {
            cuGetErrorString(ret, &err_str);        
            mp_err_msg("%s error ret=%d(%s)\n", __FUNCTION__, ret, err_str);
            ret=MP_FAILURE;
            goto out;
        }        
    }

    ret = gds_prepare_send_info(&req->gds_send_info,
                            ptr_to_size_new, ptr_to_size_flags,
                            ptr_to_lkey_new, ptr_to_lkey_flags,
                            ptr_to_addr_new, ptr_to_addr_flags);
    if (ret) {
            mp_err_msg("gds_prepare_send_info failed: %s\n", strerror(ret));
            goto out;
    }
    
out:
    return ret;
}

int mp_isend_on_stream_exp(void *buf, int size, int peer, 
                            mp_reg_t *reg_t, mp_request_t *req_t, 
                            cudaStream_t stream,
                            void * ptr_to_size_new,
                            void * ptr_to_lkey_new,
                            void * ptr_to_addr_new)
{
    int ret = 0;
    struct mp_request *req = NULL;
    struct mp_reg *reg = (struct mp_reg *)*reg_t;
    client_t *client = &clients[client_index[peer]];

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

    ret = mp_prepare_send_exp(peer, req, 
                                ptr_to_size_new,
                                ptr_to_lkey_new,
                                ptr_to_addr_new);
    if (ret) {
    	mp_err_msg("mp_post_send_on_stream failed: %s \n", strerror(ret));
        // BUG: leaking req ??
    	goto out;
    }

    ret = mp_post_send_on_stream_exp(peer, req, stream);
    if (ret) {
        mp_err_msg("mp_post_send_on_stream failed: %s \n", strerror(ret));
    // BUG: leaking req ??
        goto out;
    }

    *req_t = req;

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
