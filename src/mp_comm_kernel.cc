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

#include "oob.hpp"
#include "tl.hpp"
#include "mp_external.hpp"
#include "mp.hpp"
#include "mp_common.hpp"

//================================== ASYNC KERNEL DESCRIPTOR =================================================
int mp_kernel_descriptors_send(mp_kernel_desc_send_t info, mp_request_t *mp_req)
{
        MP_CHECK_TL_OBJ();

        if(!info || !mp_req)
        {
                mp_err_msg(oob_rank, "info: %d, request: %d\n", (info) ? 1 : 0, (mp_req) ? 1 : 0);
                return MP_FAILURE;
        }

        return tl_comm->descriptors_kernel_send(&info, mp_req);
}

int mp_kernel_descriptors_wait(mp_kernel_desc_wait_t info, mp_request_t *mp_req)
{
        MP_CHECK_TL_OBJ();

        if(!info || !mp_req)
        {
                mp_err_msg(oob_rank, "info: %d, request: %d\n", (info) ? 1 : 0, (mp_req) ? 1 : 0);
                return MP_FAILURE;
        }

        return tl_comm->descriptors_kernel_wait(&info, mp_req);
}

int mp_prepare_kernel_send(void * buf, int size, int peer, mp_region_t * mp_reg, mp_request_t * mp_req, mp_kernel_desc_send_t sDesc, mp_kernel_desc_wait_t wDesc)
{
        MP_CHECK_TL_OBJ();
        MP_CHECK(mp_send_prepare((void *)((uintptr_t)buf), size, peer, mp_reg, mp_req));
        MP_CHECK(mp_kernel_descriptors_send(sDesc, mp_req));
        MP_CHECK(mp_kernel_descriptors_wait(wDesc, mp_req));

        return MP_SUCCESS;
}

int mp_prepare_kernel_recv(void * buf, int size, int peer, mp_region_t * mp_reg, mp_request_t * mp_req, mp_kernel_desc_wait_t wDesc)
{
        MP_CHECK_TL_OBJ();

        MP_CHECK(mp_irecv((void *)((uintptr_t)buf), size, peer, mp_reg, mp_req));
        MP_CHECK(mp_kernel_descriptors_wait(wDesc, mp_req));

        return MP_SUCCESS;
}



#if 0
namespace mp {

namespace mlx5 {

typedef gdsync::isem32_t isem32_t;

int get_descriptors(isem32_t *psem, uint32_t *ptr, uint32_t value);

typedef struct send_desc {
    gdsync::isem32_t dbrec;
    gdsync::isem64_t db;
} send_desc_t;

int get_descriptors(send_desc_t *sreq, mp_request_t *req);

typedef struct wait_desc {
    gdsync::wait_cond_t sema_cond;
    gdsync::isem32_t sema;
    gdsync::isem32_t flag;
} wait_desc_t ;

int get_descriptors(wait_desc_t *wreq, mp_request_t *req);
} // mlx5

#if defined(__CUDACC__)
namespace device {

namespace mlx5 {

    using namespace gdsync::device;

    __device__ inline void send(mp::mlx5::send_desc_t &info) {
        release(info.dbrec);
        __threadfence_system();
        release(info.db);
    }
    __device__ inline int wait(mp::mlx5::wait_desc_t &info) {
        return gdsync::device::wait(info.sema, info.sema_cond);
    }
    __device__ inline void signal(mp::mlx5::wait_desc_t &info) {
        release(info.flag);
        //__threadfence_system();
    }
}

} // device
#endif

} // mp
#endif