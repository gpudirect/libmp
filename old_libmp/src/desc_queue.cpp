/****
 * Copyright (c) 2011-2014, NVIDIA Corporation.  All rights reserved.
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

#include <vector>

#include <mp.h>
#include "mp_internal.h"

#if 1


/*----------------------------------------------------------------------------*/

struct mp_desc_queue {
    std::vector<gds_descriptor_t> descs;
    
    mp_desc_queue(size_t reserved_space = 128) {
        descs.reserve(reserved_space);
    }

    size_t num_descs() {
        size_t n_descs;
        try {
            n_descs = descs.size();
        } catch(...) {
            mp_err_msg("got C++ excepton in desc queue num_descs\n");
            exit(1);
        }
        return n_descs;
    }
        
    gds_descriptor_t *head() {
        gds_descriptor_t *ptr;
        if (descs.size() < 1) {
            mp_err_msg("desc_queue is empty\n");
            return NULL;
        }
        try {
            ptr = &descs.at(0);
        } catch(...) {
            mp_err_msg("got C++ excepton in desc queue head\n");
            exit(1);
        }
        return ptr;
    }

    void add(gds_descriptor_t &desc) {
        mp_dbg_msg("adding entry %lu\n", descs.size());
        try {
            descs.push_back(desc);
        } catch(...) {
            mp_err_msg("got C++ excepton in desc queue add\n");
            exit(1);
        }
    }

    void flush() {
        try {
            descs.clear();
        } catch(...) {
            mp_err_msg("got C++ excepton in desc queue flush\n");
            exit(1);
        }
    }
};

//typedef struct mp_desc_queue *mp_desc_queue_t;


int mp_desc_queue_alloc(mp_desc_queue_t *pdq)
{
    int ret = ENOMEM;
    assert(pdq);
    struct mp_desc_queue *dq = new struct mp_desc_queue;
    if (dq) {
        mp_dbg_msg("dq=%p n_descs=%zu\n", dq, dq->num_descs());
        ret = 0;
        *pdq = dq;
    }
    return ret;
}

int mp_desc_queue_free(mp_desc_queue_t *pdq)
{
    int ret = 0;
    assert(pdq);
    struct mp_desc_queue *dq = *pdq;
    assert(dq);

    if (dq->num_descs()) {
        mp_err_msg("destroying dq=%p which is not empty (%zd descs)\n", dq, dq->num_descs());
    }

    delete dq;
    *pdq = NULL;
    return ret;
}

int mp_desc_queue_add_send(mp_desc_queue_t *pdq, mp_request_t *req_t)
{
    int ret = 0;
    assert(pdq);
    struct mp_desc_queue *dq = *pdq;
    assert(dq);

    assert(req_t);
    struct mp_request *req = *req_t;
    assert(req);
    assert(req_type_tx(req->type));
    assert(req->status == MP_PREPARED);

    gds_descriptor_t desc;
    desc.tag = GDS_TAG_SEND;
    desc.send = &req->gds_send_info;
    dq->add(desc);

    req->status = MP_PENDING_NOWAIT;
    return ret;
}

int mp_desc_queue_add_wait_send(mp_desc_queue_t *pdq, mp_request_t *req_t)
{
    int ret = 0;
    assert(pdq);
    struct mp_desc_queue *dq = *pdq;
    assert(dq);

    assert(req_t);
    struct mp_request *req = *req_t;
    assert(req);
    assert(req_type_tx(req->type));
    assert(req_can_be_waited(req));
    assert(req->status == MP_PENDING_NOWAIT || req->status == MP_COMPLETE);

    gds_descriptor_t desc;
    desc.tag = GDS_TAG_WAIT;
    desc.wait = &req->gds_wait_info;
    dq->add(desc);

    // BUG: we cannot set the stream
    //req->stream = stream;
    req->status = MP_PENDING;

    return ret;
}

int mp_desc_queue_add_wait_recv(mp_desc_queue_t *pdq, mp_request_t *req_t)
{
    int ret = 0;
    assert(pdq);
    struct mp_desc_queue *dq = *pdq;
    assert(dq);

    assert(req_t);
    struct mp_request *req = *req_t;
    assert(req);
    assert(req_type_rx(req->type));
    assert(req->status == MP_PENDING_NOWAIT || req->status == MP_COMPLETE);

    gds_descriptor_t desc;
    desc.tag = GDS_TAG_WAIT;
    desc.wait = &req->gds_wait_info;
    dq->add(desc);

    // BUG: we cannot set the stream
    //req->stream = stream;
    req->status = MP_PENDING;

    return ret;
}

int mp_desc_queue_add_wait_value32(mp_desc_queue_t *pdq, uint32_t *ptr, uint32_t value, int flags)
{
    int ret = 0;
    assert(pdq);
    struct mp_desc_queue *dq = *pdq;
    assert(dq);
    gds_wait_cond_flag_t cond_flags = static_cast<gds_wait_cond_flag_t>(flags);
    gds_descriptor_t desc;

    desc.tag = GDS_TAG_WAIT_VALUE32;
    desc.wait32.ptr = ptr;
    desc.wait32.value = value;
    assert(flags >= (int)GDS_WAIT_COND_GEQ && flags <= (int)GDS_WAIT_COND_NOR);
    desc.wait32.cond_flags = cond_flags;
    desc.wait32.flags = GDS_MEMORY_HOST;
    dq->add(desc);
    return ret;
}

int mp_desc_queue_add_write_value32(mp_desc_queue_t *pdq, uint32_t *ptr, uint32_t value)
{
    int ret = 0;
    assert(pdq);
    struct mp_desc_queue *dq = *pdq;
    assert(dq);

    gds_descriptor_t desc;
    desc.tag = GDS_TAG_WRITE_VALUE32;
    desc.write32.ptr = ptr;
    desc.write32.value = value;
    desc.write32.flags = GDS_MEMORY_HOST;
    dq->add(desc);
    return ret;
}

int mp_desc_queue_post_on_stream(cudaStream_t stream, mp_desc_queue_t *pdq, int flags)
{
    int ret = 0;
    assert(pdq);
    struct mp_desc_queue *dq = *pdq;
    assert(dq);
    mp_dbg_msg("dq=%p n_descs=%zu\n", dq, dq->num_descs());
    if (dq->num_descs()) {
        ret = gds_stream_post_descriptors(stream, dq->num_descs(), dq->head(), 0);
        dq->flush();
    }
    return ret;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 *  tab-width: 4
 *  indent-tabs-mode: nil
 * End:
 */
#endif
