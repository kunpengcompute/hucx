/**
 * Copyright (C) Mellanox Technologies Ltd. 2001-2018.  ALL RIGHTS RESERVED.
 *
 * See file LICENSE for terms.
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "rma.h"
#include "rma.inl"

#include <ucp/dt/dt_contig.h>
#include <ucs/profile/profile.h>
#include <ucs/sys/stubs.h>

#include <ucp/core/ucp_rkey.inl>
#include <ucp/proto/proto_common.inl>


/* request can be released if
 *  - all fragments were sent (length == 0) (bcopy & zcopy mix)
 *  - all zcopy fragments are done (uct_comp.count == 0)
 *  - and request was allocated from the mpool
 *    (checked in ucp_request_complete_send)
 *
 * Request can be released either immediately or in the completion callback.
 * We must check req length in the completion callback to avoid the following
 * scenario:
 *  partial_send;no_resos;progress;
 *  send_completed;cb called;req free(ooops);
 *  next_partial_send; (oops req already freed)
 */
ucs_status_t ucp_rma_request_advance(ucp_request_t *req, ssize_t frag_length,
                                     ucs_status_t status,
                                     ucs_ptr_map_key_t req_id)
{
    ucs_assert(status != UCS_ERR_NOT_IMPLEMENTED);

    ucp_request_send_state_advance(req, NULL, UCP_REQUEST_SEND_PROTO_RMA,
                                   status);

    if (ucs_unlikely(UCS_STATUS_IS_ERR(status))) {
        if (status == UCS_ERR_NO_RESOURCE) {
            return UCS_ERR_NO_RESOURCE;
        }

        return UCS_OK;
    }

    ucs_assert(frag_length >= 0);
    ucs_assert(req->send.length >= frag_length);
    req->send.length -= frag_length;
    if (req->send.length == 0) {
        /* bcopy is the fast path */
        ucp_send_request_invoke_uct_completion(req);
        return UCS_OK;
    }
    req->send.buffer           = UCS_PTR_BYTE_OFFSET(req->send.buffer, frag_length);
    req->send.rma.remote_addr += frag_length;
    return UCS_INPROGRESS;
}

static void ucp_rma_request_bcopy_completion(uct_completion_t *self)
{
    ucp_request_t *req = ucs_container_of(self, ucp_request_t,
                                          send.state.uct_comp);

    if (ucs_likely(req->send.length == req->send.state.dt.offset)) {
        ucp_send_request_id_release(req);
        ucp_request_complete_send(req, self->status);
    }
}

static void ucp_rma_request_zcopy_completion(uct_completion_t *self)
{
    ucp_request_t *req = ucs_container_of(self, ucp_request_t,
                                          send.state.uct_comp);

    if (ucs_likely(req->send.length == req->send.state.dt.offset)) {
        ucp_send_request_id_release(req);
        ucp_request_send_buffer_dereg(req);
        ucp_request_complete_send(req, self->status);
    }
}

static UCS_F_ALWAYS_INLINE ucs_status_t
ucp_rma_request_init(ucp_request_t *req, ucp_ep_h ep, const void *buffer,
                     size_t length, uint64_t remote_addr, ucp_rkey_h rkey,
                     uct_pending_callback_t cb, size_t zcopy_thresh)
{
    req->flags                = 0;
    req->send.ep              = ep;
    req->send.buffer          = (void*)buffer;
    req->send.datatype        = ucp_dt_make_contig(1);
    req->send.mem_type        = UCS_MEMORY_TYPE_HOST;
    req->send.length          = length;
    req->send.rma.remote_addr = remote_addr;
    req->send.rma.rkey        = rkey;
    req->send.uct.func        = cb;
    req->send.lane            = rkey->cache.rma_lane;
    ucp_request_send_state_init(req, ucp_dt_make_contig(1), length);
    ucp_request_send_state_reset(req,
                                 (length < zcopy_thresh) ?
                                 ucp_rma_request_bcopy_completion :
                                 ucp_rma_request_zcopy_completion,
                                 UCP_REQUEST_SEND_PROTO_RMA);
#if UCS_ENABLE_ASSERT
    req->send.cb              = NULL;
#endif
    if (length < zcopy_thresh) {
        return UCS_OK;
    }

    return ucp_request_send_buffer_reg_lane(req, req->send.lane, 0);
}

static UCS_F_ALWAYS_INLINE ucs_status_ptr_t
ucp_rma_nonblocking(ucp_ep_h ep, const void *buffer, size_t length,
                    uint64_t remote_addr, ucp_rkey_h rkey,
                    uct_pending_callback_t progress_cb, size_t zcopy_thresh,
                    const ucp_request_param_t *param)
{
    ucs_status_t status;
    ucp_request_t *req;

    req = ucp_request_get_param(ep->worker, param,
                                {return UCS_STATUS_PTR(UCS_ERR_NO_MEMORY);});

    status = ucp_rma_request_init(req, ep, buffer, length, remote_addr, rkey,
                                  progress_cb, zcopy_thresh);
    if (ucs_unlikely(status != UCS_OK)) {
        return UCS_STATUS_PTR(status);
    }

    return ucp_rma_send_request(req, param);
}

ucs_status_t ucp_put_nbi(ucp_ep_h ep, const void *buffer, size_t length,
                         uint64_t remote_addr, ucp_rkey_h rkey)
{
    ucs_status_ptr_t status_ptr;

    status_ptr = ucp_put_nbx(ep, buffer, length, remote_addr, rkey,
                             &ucp_request_null_param);
    if (UCS_PTR_IS_PTR(status_ptr)) {
        ucp_request_free(status_ptr);
        return UCS_INPROGRESS;
    }

    /* coverity[overflow] */
    return UCS_PTR_STATUS(status_ptr);
}

ucs_status_ptr_t ucp_put_nb(ucp_ep_h ep, const void *buffer, size_t length,
                            uint64_t remote_addr, ucp_rkey_h rkey,
                            ucp_send_callback_t cb)
{
    ucp_request_param_t param = {
        .op_attr_mask = UCP_OP_ATTR_FIELD_CALLBACK,
        .cb.send      = (ucp_send_nbx_callback_t)cb
    };

    return ucp_put_nbx(ep, buffer, length, remote_addr, rkey, &param);
}

static UCS_F_ALWAYS_INLINE ucs_status_t
ucp_put_send_short(ucp_ep_h ep, const void *buffer, size_t length,
                   uint64_t remote_addr, ucp_rkey_h rkey,
                   const ucp_request_param_t *param)
{
    const ucp_rkey_config_t *rkey_config;
    uct_rkey_t tl_rkey;

    if (ucs_unlikely(param->op_attr_mask & (UCP_OP_ATTR_FIELD_DATATYPE |
                                            UCP_OP_ATTR_FLAG_NO_IMM_CMPL))) {
        return UCS_ERR_NO_RESOURCE;
    }

    rkey_config = ucp_rkey_config(ep->worker, rkey);
    if (ucs_unlikely(!ucp_proto_select_is_short(ep, &rkey_config->put_short,
                                                length))) {
        return UCS_ERR_NO_RESOURCE;
    }

    tl_rkey = rkey->tl_rkey[rkey_config->put_short.rkey_index].rkey.rkey;
    return UCS_PROFILE_CALL(uct_ep_put_short,
                            ep->uct_eps[rkey_config->put_short.lane],
                            buffer, length, remote_addr, tl_rkey);
}

static UCS_F_ALWAYS_INLINE ucs_status_ptr_t
ucp_put_common(ucp_ep_h ep, ucp_worker_h worker, const void *buffer,
               size_t count, uint64_t remote_addr, ucp_rkey_h rkey,
               const ucp_request_param_t *param);

ucs_status_ptr_t ucp_put_nbx(ucp_ep_h ep, const void *buffer, size_t count,
                             uint64_t remote_addr, ucp_rkey_h rkey,
                             const ucp_request_param_t *param)
{
    ucp_worker_h worker = ep->worker;
    ucs_status_ptr_t ret;

    UCP_RMA_CHECK_CONTIG1(param);
    UCP_RMA_CHECK_PTR(worker->context, buffer, count);
    UCP_WORKER_THREAD_CS_ENTER_CONDITIONAL(worker);

    ucs_trace_req("put_nbx buffer %p count %zu remote_addr %"PRIx64
                  " rkey %p to %s cb %p",
                  buffer, count, remote_addr, rkey, ucp_ep_peer_name(ep),
                  (param->op_attr_mask & UCP_OP_ATTR_FIELD_CALLBACK) ?
                  param->cb.send : NULL);

    ret = ucp_put_common(ep, worker, buffer, count, remote_addr, rkey, param);

    UCP_WORKER_THREAD_CS_EXIT_CONDITIONAL(worker);
    return ret;
}

static UCS_F_ALWAYS_INLINE ucs_status_ptr_t
ucp_put_common(ucp_ep_h ep, ucp_worker_h worker, const void *buffer,
               size_t count, uint64_t remote_addr, ucp_rkey_h rkey,
               const ucp_request_param_t *param)
{
    ucp_ep_rma_config_t *rma_config;
    ucs_status_ptr_t ret;
    ucs_status_t status;
    ucp_request_t *req;

    if (worker->context->config.ext.proto_enable) {
        status = ucp_put_send_short(ep, buffer, count, remote_addr, rkey, param);
        if (ucs_likely(status != UCS_ERR_NO_RESOURCE)) {
            return UCS_STATUS_PTR(status);
        }

        req = ucp_request_get_param(worker, param,
                                    return UCS_STATUS_PTR(UCS_ERR_NO_MEMORY));
        req->send.rma.rkey        = rkey;
        req->send.rma.remote_addr = remote_addr;

        ret = ucp_proto_request_send_op(ep,
                                        &ucp_rkey_config(worker, rkey)->proto_select,
                                        rkey->cfg_index, req, UCP_OP_ID_PUT,
                                        buffer, count, ucp_dt_make_contig(1),
                                        count, param);
    } else {
        status = UCP_RKEY_RESOLVE(rkey, ep, rma);
        if (status != UCS_OK) {
            return UCS_STATUS_PTR(status);
        }

        /* Fast path for a single short message */
        if (ucs_likely(!(param->op_attr_mask & UCP_OP_ATTR_FLAG_NO_IMM_CMPL) &&
                        ((ssize_t)count <= rkey->cache.max_put_short))) {
            status = UCS_PROFILE_CALL(uct_ep_put_short,
                                      ep->uct_eps[rkey->cache.rma_lane], buffer,
                                      count, remote_addr, rkey->cache.rma_rkey);
            if (ucs_likely(status != UCS_ERR_NO_RESOURCE)) {
                return UCS_STATUS_PTR(status);
            }
        }

        if (ucs_unlikely(param->op_attr_mask & UCP_OP_ATTR_FLAG_FORCE_IMM_CMPL)) {
            return UCS_STATUS_PTR(UCS_ERR_NO_RESOURCE);
        }

        rma_config = &ucp_ep_config(ep)->rma[rkey->cache.rma_lane];
        ret = ucp_rma_nonblocking(ep, buffer, count, remote_addr, rkey,
                                  rkey->cache.rma_proto->progress_put,
                                  rma_config->put_zcopy_thresh, param);
    }

    return ret;
}

void ucp_append_amo_cb(void *request, ucs_status_t status, void *user_data)
{
    ucp_request_t *req        = request;
    ucp_request_param_t param = {
        .op_attr_mask         = UCP_OP_ATTR_FIELD_REQUEST,
        .request              = request
    };

    /* Make sure this request ends up in the mpool */
    req->flags                = (req->flags & ~UCP_REQUEST_FLAG_COMPLETED) |
                                               UCP_REQUEST_FLAG_RELEASED;

    /* convert the same request from an append to a put */
    ucp_put_common(req->send.ep, req->send.ep->worker,
                   req->send.amo.append.buffer, req->send.amo.value,
                   *(uint64_t*)req->send.buffer, req->send.amo.append.rkey,
                   &param);
}

ucs_status_t ucp_get_nbi(ucp_ep_h ep, void *buffer, size_t length,
                         uint64_t remote_addr, ucp_rkey_h rkey)
{
    ucs_status_ptr_t status_ptr;

    status_ptr = ucp_get_nbx(ep, buffer, length, remote_addr, rkey,
                             &ucp_request_null_param);
    if (UCS_PTR_IS_PTR(status_ptr)) {
        ucp_request_free(status_ptr);
        return UCS_INPROGRESS;
    }

    /* coverity[overflow] */
    return UCS_PTR_STATUS(status_ptr);
}

ucs_status_ptr_t ucp_get_nb(ucp_ep_h ep, void *buffer, size_t length,
                            uint64_t remote_addr, ucp_rkey_h rkey,
                            ucp_send_callback_t cb)
{
    ucp_request_param_t param = {
        .op_attr_mask = UCP_OP_ATTR_FIELD_CALLBACK,
        .cb.send      = (ucp_send_nbx_callback_t)cb
    };

    return ucp_get_nbx(ep, buffer, length, remote_addr, rkey, &param);
}

ucs_status_ptr_t ucp_get_nbx(ucp_ep_h ep, void *buffer, size_t count,
                             uint64_t remote_addr, ucp_rkey_h rkey,
                             const ucp_request_param_t *param)
{
    ucp_worker_h worker = ep->worker;
    ucp_ep_rma_config_t *rma_config;
    ucs_status_ptr_t ret;
    ucs_status_t status;
    ucp_request_t *req;

    UCP_RMA_CHECK_CONTIG1(param);

    if (ucs_unlikely(param->op_attr_mask & UCP_OP_ATTR_FLAG_FORCE_IMM_CMPL)) {
        return UCS_STATUS_PTR(UCS_ERR_NO_RESOURCE);
    }

    UCP_RMA_CHECK_PTR(worker->context, buffer, count);
    UCP_WORKER_THREAD_CS_ENTER_CONDITIONAL(worker);

    ucs_trace_req("get_nbx buffer %p count %zu remote_addr %"PRIx64" rkey %p from %s cb %p",
                   buffer, count, remote_addr, rkey, ucp_ep_peer_name(ep),
                   (param->op_attr_mask & UCP_OP_ATTR_FIELD_CALLBACK) ?
                   param->cb.send : NULL);

    if (worker->context->config.ext.proto_enable) {
        req = ucp_request_get_param(worker, param,
                                    {ret = UCS_STATUS_PTR(UCS_ERR_NO_MEMORY);
                                    goto out_unlock;});

        req->send.rma.rkey        = rkey;
        req->send.rma.remote_addr = remote_addr;

        ret = ucp_proto_request_send_op(ep,
                                        &ucp_rkey_config(worker, rkey)->proto_select,
                                        rkey->cfg_index, req, UCP_OP_ID_GET,
                                        buffer, count, ucp_dt_make_contig(1),
                                        count, param);
    } else {
        status = UCP_RKEY_RESOLVE(rkey, ep, rma);
        if (status != UCS_OK) {
            ret = UCS_STATUS_PTR(status);
            goto out_unlock;
        }

        rma_config = &ucp_ep_config(ep)->rma[rkey->cache.rma_lane];
        ret        = ucp_rma_nonblocking(ep, buffer, count, remote_addr, rkey,
                                         rkey->cache.rma_proto->progress_get,
                                         rma_config->get_zcopy_thresh, param);
    }

out_unlock:
    UCP_WORKER_THREAD_CS_EXIT_CONDITIONAL(worker);
    return ret;
}

UCS_PROFILE_FUNC(ucs_status_t, ucp_put, (ep, buffer, length, remote_addr, rkey),
                 ucp_ep_h ep, const void *buffer, size_t length,
                 uint64_t remote_addr, ucp_rkey_h rkey)
{
    return ucp_rma_wait(ep->worker,
                        ucp_put_nb(ep, buffer, length, remote_addr, rkey,
                                   (ucp_send_callback_t)ucs_empty_function),
                        "put");
}

UCS_PROFILE_FUNC(ucs_status_t, ucp_get, (ep, buffer, length, remote_addr, rkey),
                 ucp_ep_h ep, void *buffer, size_t length,
                 uint64_t remote_addr, ucp_rkey_h rkey)
{
    return ucp_rma_wait(ep->worker,
                        ucp_get_nb(ep, buffer, length, remote_addr, rkey,
                                   (ucp_send_callback_t)ucs_empty_function),
                        "get");
}
