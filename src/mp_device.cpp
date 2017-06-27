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
#include "mp/device.cuh"
#include "mp_internal.h"

#include <gdsync/mlx5.h>

namespace mp {
    namespace mlx5 {

        int get_descriptors(isem32_t *psem, uint32_t *ptr, uint32_t value)
        {
            int ret = 0;
            struct gds_mlx5_dword_wait_info mlx5_i;
            int flags = GDS_MEMORY_HOST;
            int retcode = gds_mlx5_get_dword_wait_info(ptr, value, flags, &mlx5_i);
            if (retcode) {
                mp_err_msg("got error %d/%s \n", retcode, strerror(retcode));
                ret = MP_FAILURE;
                goto out;
            }
            mp_dbg_msg("wait_info ptr=%p value=%08x\n", mlx5_i.ptr, mlx5_i.value);
            psem->ptr   = mlx5_i.ptr;
            psem->value = mlx5_i.value;
        out:
            return ret;
        }

        int get_descriptors(send_desc_t *sinfo, mp_request_t *req_t)
        {
            int retcode;
            int ret = 0; 
            struct mp_request *req = *req_t;

            mp_dbg_msg("req=%p status=%d id=%d\n", req, req->status, req->id);

            // track req
            assert(req->status == MP_PREPARED);
            if (use_event_sync) {
                mp_err_msg("unsupported call in async event mode\n"); 
                ret = MP_FAILURE;
                goto out;
            }
            req->status = MP_PENDING_NOWAIT;
            req->stream = 0;

            // get_descriptors in sinfo
            struct gds_mlx5_send_info mlx5_i;
            retcode = gds_mlx5_get_send_info(1, &req->gds_send_info, &mlx5_i);
            if (retcode) {
                mp_err_msg("got error %d/%s \n", retcode, strerror(retcode));
                ret = MP_FAILURE;
                goto out;
            }
            sinfo->dbrec.ptr   = mlx5_i.dbrec_ptr;
            sinfo->dbrec.value = mlx5_i.dbrec_value;
            sinfo->db.ptr      = mlx5_i.db_ptr;
            sinfo->db.value    = mlx5_i.db_value;

        out:
            return ret;
        }

        int get_descriptors(wait_desc_t *winfo, mp_request_t *req_t)
        {
            int retcode;
            int ret = 0;
            struct mp_request *req = *req_t;
            client_t *client = &clients[client_index[req->peer]];

            mp_dbg_msg("req=%p status=%d id=%d\n", req, req->status, req->id);

            assert(req->status == MP_PENDING_NOWAIT || req->status == MP_COMPLETE);
	
            req->stream = 0;
            req->status = MP_PENDING;
            
            gds_mlx5_wait_info_t mlx5_i;
            retcode = gds_mlx5_get_wait_info(1, &req->gds_wait_info, &mlx5_i);
            if (retcode) {
                mp_err_msg("error %d\n", retcode);
                ret = MP_FAILURE;
                // BUG: leaking req ??
                goto out;
            }
            // BUG: need a switch() here
            winfo->sema_cond  = mlx5_i.cond;
            winfo->sema.ptr   = mlx5_i.cqe_ptr;
            winfo->sema.value = mlx5_i.cqe_value;
            winfo->flag.ptr   = mlx5_i.flag_ptr;
            winfo->flag.value = mlx5_i.flag_value;

        out:
            return ret;
        }

    }
}
