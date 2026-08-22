/**
* Copyright (C) Huawei Technologies Co., Ltd. 2026. ALL RIGHTS RESERVED.
* Description: Data plane interface
* See file LICENSE for terms.
*/


#include <ucs/time/time.h>
#include <uct/api/uct.h>
#include <uct/base/uct_iface.h>
#include <ucs/arch/bitops.h>
#include <ucs/arch/cpu.h>
#include <ucs/type/class.h>
#include <ucs/type/cpu_set.h>
#include <ucs/debug/log.h>
#include <uct/base/uct_iov.inl>
#include "ub_ep.h"
#include "ub_md.h"
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef ENABLE_STATS
static ucs_stats_class_t uct_ub_fc_stats_class = {
    .name = "ub_fc",
    .num_counters = UCT_UB_FC_STAT_LAST,
    .counter_names = {
        [UCT_UB_FC_STAT_NO_CRED]            = "no_wnd",
        [UCT_UB_FC_STAT_TX_GRANT]           = "tx_grant",
        [UCT_UB_FC_STAT_TX_PURE_GRANT]      = "tx_pure_grant",
        [UCT_UB_FC_STAT_TX_SOFT_REQ]        = "tx_soft_req",
        [UCT_UB_FC_STAT_TX_HARD_REQ]        = "tx_fc_req",
        [UCT_UB_FC_STAT_RX_PURE_GRANT]      = "rx_fc_grant",
        [UCT_UB_FC_STAT_RX_GRANT_CHECK]     = "rx_grant_err",
        [UCT_UB_FC_STAT_FC_WND]             = "fc_wnd_size",
        [UCT_UB_FC_STAT_RX_GRANT]           = "rx_grant",
        [UCT_UB_FC_STAT_RX_SOFT_REQ]        = "rx_soft_req",
        [UCT_UB_FC_STAT_RX_HARD_REQ]        = "rx_hard_req"
    }
};
#endif

#define UCT_UB_DEFINE_ATOMIC_LE_HANDLER(_bits) \
    void \
    uct_ub_common_atomic##_bits##_le_handler(uct_ub_iface_send_op_t *op, \
                                             const void *resp) \
    { \
        uct_ub_iface_send_desc_t *desc = ucs_derived_of(op, uct_ub_iface_send_desc_t); \
        uint##_bits##_t *dest = desc->super.buffer; \
        const uint##_bits##_t *value = resp; \
        \
        VALGRIND_MAKE_MEM_DEFINED(value, sizeof(*value)); \
        *dest = *value; /* response in desc buffer */ \
        uct_invoke_completion(desc->super.user_comp, UCS_OK); \
        ucs_mpool_put(desc); \
    }

UCT_UB_DEFINE_ATOMIC_LE_HANDLER(32)
UCT_UB_DEFINE_ATOMIC_LE_HANDLER(64)

void uct_ub_ep_flush_op_completion_handler(uct_ub_iface_send_op_t *op, const void *resp)
{
    uct_invoke_completion(op->user_comp, UCS_OK);
    ucs_mpool_put(op);
}

void uct_ub_ep_get_bcopy_handler(uct_ub_iface_send_op_t *op, const void *resp)
{
    uct_ub_iface_send_desc_t *desc = ucs_derived_of(op, uct_ub_iface_send_desc_t);

    VALGRIND_MAKE_MEM_DEFINED(resp, desc->super.length);

    desc->unpack_cb(desc->super.unpack_arg, resp, desc->super.length);

    uct_ub_op_release_get_bcopy(op);
    uct_invoke_completion(desc->super.user_comp, UCS_OK);
    ucs_mpool_put(desc);
}

void uct_ub_ep_get_bcopy_handler_no_completion(uct_ub_iface_send_op_t *op, const void *resp)
{
    uct_ub_iface_send_desc_t *desc = ucs_derived_of(op, uct_ub_iface_send_desc_t);

    VALGRIND_MAKE_MEM_DEFINED(resp, desc->super.length);

    desc->unpack_cb(desc->super.unpack_arg, resp, desc->super.length);
    uct_ub_op_release_get_bcopy(op);
    ucs_mpool_put(desc);
}

void uct_ub_ep_get_zcopy_completion_handler(uct_ub_iface_send_op_t *op, const void *resp)
{
    uct_ub_op_release_get_zcopy(op);
    uct_ub_send_op_completion_handler(op, resp);
}

void uct_ub_ep_am_zcopy_handler(uct_ub_iface_send_op_t *op, const void *resp)
{
    uct_ub_iface_send_desc_t *desc = ucs_derived_of(op, uct_ub_iface_send_desc_t);

    uct_invoke_completion(desc->super.user_comp, UCS_OK);
    ucs_mpool_put(desc);
}

ucs_status_t uct_ub_ep_basic_init(uct_ub_iface_t *iface, uct_ub_ep_t *ep)
{
    ucs_status_t status;

    ep->flags = 0;
    status = uct_ub_iface_add_ep(iface, ep);
    if (status != UCS_OK) {
        return status;
    }

    uct_ub_ep_tx_outstanding_queue_init(&ep->txqp);
    uct_ub_txcnt_init(&ep->status, 0, 0);
    ep->fi.fence_beat = 0;

    return UCS_OK;
}

void uct_ub_err_delete_ep(uct_ub_iface_t *iface, uct_ub_ep_t *ep)
{
    ucs_ptr_array_remove(&iface->eps, ep->ep_id);
}

ucs_status_t uct_ub_fc_init(uct_ub_fc_t *fc, int16_t winsize
                            UCS_STATS_ARG(ucs_stats_node_t* stats_parent))
{
    ucs_status_t status;

    fc->fc_wnd = winsize;

    status = UCS_STATS_NODE_ALLOC(&fc->stats, &uct_ub_fc_stats_class, stats_parent, "");
    if (status != UCS_OK) {
       return status;
    }

    UCS_STATS_SET_COUNTER(fc->stats, UCT_UB_FC_STAT_FC_WND, fc->fc_wnd);

    return UCS_OK;
}

void uct_ub_fc_stats_cleanup(uct_ub_fc_t *fc)
{
    UCS_STATS_NODE_FREE(fc->stats);
}

ucs_status_t uct_ub_iface_txqp_purge_process(uct_ub_iface_t *iface, uct_ub_tx_queue_t *txqp,
                                              ucs_status_t status, uct_ub_iface_send_op_t *op, int warn)
{
    if (warn && (op->handler != uct_ub_ep_flush_op_completion_handler)) {
        ucs_warn("Destroying txqp with uncomplet operation handler %s",
                 ucs_debug_get_symbol_name((void *)op->handler));
    }

    op->flags &= ~(UCT_UB_IFACE_SEND_OP_FLAG_INUSE | UCT_UB_IFACE_SEND_OP_FLAG_ZCOPY);

    /* If the handler is ucs_mpool_put, the opration must have no user completion */
    if (op->handler == (uct_ub_send_handler_t)ucs_mpool_put) {
        ucs_mpool_put(op);
        goto out;
    }

    if (op->user_comp != NULL) {
        uct_invoke_completion(op->user_comp, status);
    }
    if ((op->handler == uct_ub_ep_get_bcopy_handler) ||
        (op->handler == uct_ub_ep_get_bcopy_handler_no_completion)) {
        uct_ub_iface_send_desc_t *desc = ucs_derived_of(op, uct_ub_iface_send_desc_t);
        uct_ub_op_release_get_bcopy(op);
        uct_ub_iface_update_reads(iface);
        ucs_mpool_put(desc);
        goto out;
    }

    if (op->handler == uct_ub_ep_get_zcopy_completion_handler) {
        uct_ub_op_release_get_zcopy(op);
        uct_ub_iface_update_reads(iface);
        uct_ub_iface_put_send_op(op);
        goto out;
    }

    // for put zcopy
    if (op->handler == uct_ub_send_op_completion_handler) {
        uct_ub_iface_put_send_op(op);
        goto out;
    }

    // for flush
    if (op->handler == uct_ub_ep_flush_op_completion_handler) {
        ucs_mpool_put(op);
        goto out;
    }

    // for atomic and am zcopy
    if ((op->handler == iface->config.atomic64_handler) ||
        (op->handler == iface->config.atomic32_handler) ||
        (op->handler == uct_ub_ep_am_zcopy_handler)) {
        uct_ub_iface_send_desc_t *desc = ucs_derived_of(op, uct_ub_iface_send_desc_t);
        ucs_mpool_put(desc);
    }

    return UCS_INPROGRESS;

out:
    return UCS_OK;
}

UCS_CLASS_INIT_FUNC(uct_ub_ep_t, uct_ub_iface_t *iface)
{
    ucs_status_t status;

    ucs_trace_func("");

    UCS_CLASS_CALL_SUPER_INIT(uct_base_ep_t, &iface->super);
    status = uct_ub_ep_basic_init(iface, self);
    if (status != UCS_OK) {
        return status;
    }

    status = uct_ub_fc_init(&self->fc, iface->config.fc_wnd_size
                            UCS_STATS_ARG(self->super.stats));
    if (status != UCS_OK) {
        ucs_warn("The uct_ub_fc_init failed");
        uct_ub_iface_remove_ep(iface, self);
        return status;
    }

    return UCS_OK;
}

static UCS_CLASS_CLEANUP_FUNC(uct_ub_ep_t)
{
    uct_ub_iface_t *iface = ucs_derived_of(self->super.super.iface, uct_ub_iface_t);

    uct_ub_iface_remove_ep(iface, self);
    uct_ub_fc_stats_cleanup(&self->fc);
}

UCS_CLASS_DEFINE(uct_ub_ep_t, uct_base_ep_t);
UCS_CLASS_DEFINE_NEW_FUNC(uct_ub_ep_t, uct_ep_t, uct_ub_iface_t *);
UCS_CLASS_DEFINE_DELETE_FUNC(uct_ub_ep_t, uct_ep_t);
