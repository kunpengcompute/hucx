/**
* Copyright (C) Huawei Technologies Co., Ltd. 2026. ALL RIGHTS RESERVED.
*
* See file LICENSE for terms.
*/

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <uct/urma/rc_urma/rc_urma_iface.h>
#include <uct/urma/base/urma_device.h>
#include <uct/urma/rc_urma/rc_urma_ep.h>

#include <uct/api/uct.h>
#include <uct/base/uct_md.h>
#include <uct/base/uct_iface.h>
#include <ucs/arch/bitops.h>
#include <ucs/arch/cpu.h>
#include <ucs/type/class.h>
#include <ucs/type/cpu_set.h>
#include <ucs/debug/log.h>
#include <ucs/sys/string.h>

static void UCS_CLASS_DELETE_FUNC_NAME(uct_rc_urma_iface_t)(uct_iface_t*);

static const char *uct_rc_urma_flush_mode_names[] = {
    [UCT_RC_URMA_FLUSH_MODE_RDMA_WRITE_0] = "write0",
    [UCT_RC_URMA_FLUSH_MODE_FLOW_CONTROL] = "fc",
    [UCT_RC_URMA_FLUSH_MODE_AUTO]         = "auto",
    [UCT_RC_URMA_FLUSH_MODE_LAST]         = NULL
};

#ifdef ENABLE_STATS
static ucs_stats_class_t uct_rc_urma_iface_stats_class = {
    .name = "rc_urma_iface",
    .num_counters = UCT_RC_URMA_IFACE_STAT_LAST,
    .counter_names = {
        [UCT_RC_URMA_IFACE_STAT_RX_DL_CR_ERR]       = "rx_deal_cr_err",
        [UCT_RC_URMA_IFACE_STAT_TX_DL_CR_ERR]       = "tx_deal_cr_err",
        [UCT_RC_URMA_IFACE_STAT_RX_FC_REQ]          = "rx_fc_req",
        [UCT_RC_URMA_IFACE_STAT_TX_FC_GRANT]        = "tx_fc_grant",
        [UCT_RC_URMA_IFACE_STAT_TX_KA_SUCCESS]      = "tx_ka_success",
        [UCT_RC_URMA_IFACE_STAT_TX_KA_FAIL]         = "tx_ka_fail",
        [UCT_RC_URMA_IFACE_STAT_NO_CQE]             = "no_cqe"
    }
};
#endif /* ENABLE_STATS */

ucs_config_field_t uct_rc_urma_iface_config_table[] = {
    {"URMA_", "", NULL,
     ucs_offsetof(uct_rc_urma_iface_config_t, super),
     UCS_CONFIG_TYPE_TABLE(uct_urma_iface_config_table)},

    {"TX_JFC_LEN", "4096",
     "Length of send completion queue. This limits the total number of outstanding signaled sends.",
     ucs_offsetof(uct_rc_urma_iface_config_t, tx_jfc_len), UCS_CONFIG_TYPE_UINT},

    {"FLUSH_MODE", "auto",
     "Method to use for posting flush operation:\n"
     " - write0 : Post empty RDMA_WRITE\n"
     " - fc     : Send flow control message\n"
     " - auto   : Select automatically based on device support",
     ucs_offsetof(uct_rc_urma_iface_config_t, flush_mode),
     UCS_CONFIG_TYPE_ENUM(uct_rc_urma_flush_mode_names)},

    {"FC_SOFT_THRESH", "0.5",
     "Threshold for sending soft request for FC credits to the peer. This value\n"
     "refers to the percentage of the FC_WND_SIZE value. (must be > HARD_THRESH and < 1)",
     ucs_offsetof(uct_rc_urma_iface_config_t, soft_thresh), UCS_CONFIG_TYPE_DOUBLE},

    {NULL}
};

ucs_status_t uct_rc_urma_iface_flush(uct_iface_h tl_iface, unsigned flags, uct_completion_t *comp)
{
    uct_rc_urma_iface_t *iface = ucs_derived_of(tl_iface, uct_rc_urma_iface_t);
    uct_rc_urma_ep_t *ep = NULL;
    ucs_status_t status;
    int count = 0;
    int i;

    if (comp != NULL) {
        return UCS_ERR_UNSUPPORTED;
    }

    for (i = 0; i < iface->super.eps.size; i++) {
        if (__ucs_ptr_array_is_free(iface->super.eps.start[i])) {
            continue;
        }
        ep = ucs_derived_of((uct_ub_ep_t *)iface->super.eps.start[i], uct_rc_urma_ep_t);
        status = uct_ep_flush(&ep->super.super.super, 0, NULL);
        if ((status == UCS_ERR_NO_RESOURCE) || (status == UCS_INPROGRESS)) {
            ++count;
        } else if (status != UCS_OK) {
            return status;
        }
    }

    if (count != 0) {
        UCT_TL_IFACE_STAT_FLUSH_WAIT(&iface->super.super);
        return UCS_INPROGRESS;
    }

    UCT_TL_IFACE_STAT_FLUSH(&iface->super.super);

    return UCS_OK;
}

static void uct_rc_urma_iface_common_progress_enable(uct_iface_h tl_iface, unsigned flags)
{
    uct_rc_urma_iface_t *iface = ucs_derived_of(tl_iface, uct_rc_urma_iface_t);

    uct_base_iface_progress_enable_cb(&iface->super.super, iface->progress, flags);
}

static ucs_status_t uct_rc_urma_iface_query(uct_iface_h tl_iface, uct_iface_attr_t *iface_attr)
{
    uct_rc_urma_iface_t *iface = ucs_derived_of(tl_iface, uct_rc_urma_iface_t);

    ucs_status_t status = uct_ub_base_iface_query(&(iface->super), iface_attr);
    if (status != UCS_OK) {
        return status;
    }

    status = uct_ub_iface_cap_query(&(iface->super), iface_attr);
    if (status != UCS_OK) {
        return status;
    }

    iface_attr->ep_addr_len = sizeof(uct_rc_urma_ep_addr_params_t);
    iface_attr->cap.flags |= UCT_IFACE_FLAG_CONNECT_TO_EP           |
                             UCT_IFACE_FLAG_GET_BCOPY               |
                             UCT_IFACE_FLAG_GET_ZCOPY;
    iface_attr->latency.m += 1e-9; /* 1 ns per each extra QP */
    iface_attr->overhead = 75e-9; /* Software overhead */

    return UCS_OK;
}

static UCS_F_ALWAYS_INLINE void uct_rc_urma_update_tx_res(uct_rc_urma_iface_t *iface, uct_rc_urma_ep_t *ep,
                                                          unsigned count)
{
    /*
     * PI (Previous Instruction) and CI (Current Instruction) values
     * simulate hardware-level instruction registers. These values are designed
     * to support single-wrap-around behavior (e.g., max_value + 1 = min_value),
     * thus explicit wrap-around checks are omitted for performance.
     *
     * Note: Values are expected to wrap at most once between state updates.
     *        No overflow/underflow validation is performed per design.
     */
    ep->super.status.ci += count;
    uct_ub_iface_update_reads(&(iface->super));
    ep->rc_urma_jetty.available += count;
    iface->super.tx.jfc_available += count;
}

static UCS_F_ALWAYS_INLINE uct_ub_iface_find_ep_t uct_rc_urma_get_ep_by_cr(uct_rc_urma_iface_t *iface,
                                                                           urma_cr_t *cr, uct_rc_urma_ep_t **ep)
{
    uct_ub_ep_t *find_ep = uct_ub_iface_lookup_ep(&iface->super, cr);
    uct_rc_urma_ep_t *rc_ep = ucs_derived_of(find_ep, uct_rc_urma_ep_t);

    if (ucs_unlikely(__ucs_ptr_array_is_free(find_ep))) {
        return UCT_UB_EP_NOT_EXIST;
    }

    *ep = rc_ep;
    return UCT_UB_FIND_DATA_EP;
}

static void uct_rc_urma_handle_failure(uct_urma_iface_t *ub_iface, urma_cr_t *cr)
{
    uct_rc_urma_iface_t *iface = ucs_derived_of(ub_iface, uct_rc_urma_iface_t);
    uct_rc_urma_ep_t *ep = NULL;
    uct_ub_iface_find_ep_t ret;
    ucs_log_level_t log_lvl;
    ucs_status_t ep_status;
    ucs_status_t status;
    unsigned desc_count;

    ret = uct_rc_urma_get_ep_by_cr(iface, cr, &ep);
    if (ret == UCT_UB_EP_NOT_EXIST) {
        return;
    }

    ep_status = uct_ub_iface_completion_err_proc(&iface->super, cr, ep->rc_urma_jetty.jetty, &(ep->super), NULL);
    desc_count = uct_ub_iface_get_tx_res_count(&ep->super, cr);
    uct_rc_urma_ep_txqp_purge_outstanding(iface, &ep->super.txqp,
                                          ep_status, ep->super.status.ci + desc_count, 0);

    /* Don't need to invoke UCT pending requests for a given UCT EP */
    ucs_arbiter_group_desched(&iface->tx.arbiter, &ep->arb_group);
    uct_rc_urma_update_tx_res(iface, ep, desc_count);

    if (ep->super.flags & (UCT_RC_URMA_EP_FLAG_ERR_HANDLER_INVOKED |
                           UCT_RC_URMA_EP_FLAG_FLUSH_CANCEL)) {
        return;
    }

    ep->super.flags |= UCT_RC_URMA_EP_FLAG_ERR_HANDLER_INVOKED;

    status  = uct_iface_handle_ep_err(&iface->super.super.super,
                                      &ep->super.super.super, ep_status);
    log_lvl = uct_urma_iface_failure_log_level(&iface->super.super, status,
                                             ep_status);

    ucs_log(log_lvl, "Send completion with error: %u", cr->status);
}

ucs_status_t uct_rc_urma_iface_fc_handler(uct_urma_iface_t *ub_iface, void *arg, urma_cr_t *cr, unsigned flags)
{
    uct_rc_urma_iface_t *iface = ucs_derived_of(ub_iface, uct_rc_urma_iface_t);
    uct_rc_urma_pending_req_t *fc_req = NULL;
    uct_ub_hdr_t *hdr = (uct_ub_hdr_t *)arg;
    uct_rc_urma_ep_t *ep = NULL;
    uint32_t ep_id = hdr->ep_id;
    ucs_status_t status;
    int16_t cur_wnd;
    unsigned length;
    uint8_t fc_hdr;

    length = cr->completion_len - sizeof(*hdr);
    fc_hdr = uct_rc_urma_fc_get_fc_hdr(hdr->am_id);

    ucs_assert(iface->super.config.fc_enabled);

    ep = (uct_rc_urma_ep_t *)(iface->super.eps.start[ep_id]);
    if ((__ucs_ptr_array_is_free(ep)) || (ep->super.flags & UCT_RC_URMA_EP_FLAG_FLUSH_CANCEL)) {
        /* We get fc for ep which is being removed or canceled so should ignore it */
        goto out;
    }

    if (fc_hdr & UCT_RC_URMA_EP_FLAG_FC_GRANT) {
        UCS_STATS_UPDATE_COUNTER(ep->super.fc.stats, UCT_UB_FC_STAT_RX_GRANT, 1);

        /* Got either grant flag or special FC grant message */
        cur_wnd = ep->super.fc.fc_wnd;

        /* Peer granted resources, so update wnd */
        ep->super.fc.fc_wnd = iface->super.config.fc_wnd_size;
        UCS_STATS_SET_COUNTER(ep->super.fc.stats, UCT_UB_FC_STAT_FC_WND, ep->super.fc.fc_wnd);

        /* To preserve ordering we have to dispatch all pending
         * operations if current fc_wnd is <= 0
         * (otherwise it will be dispatched by tx progress) */
        if (cur_wnd <= 0) {
            ucs_arbiter_group_schedule(&iface->tx.arbiter, &ep->arb_group);
            ucs_arbiter_dispatch(&iface->tx.arbiter, 1,
                                 uct_rc_urma_ep_process_pending, NULL);
        }
        if (fc_hdr == UCT_RC_URMA_EP_FC_PURE_GRANT) {
            /* Special FC grant message can't be bundled with any other FC
             * request. Stop processing this AM and do not invoke AM handler */
            UCS_STATS_UPDATE_COUNTER(ep->super.fc.stats, UCT_UB_FC_STAT_RX_PURE_GRANT, 1);
            return UCS_OK;
        }
    }

    if (fc_hdr & UCT_RC_URMA_EP_FLAG_FC_SOFT_REQ) {
        UCS_STATS_UPDATE_COUNTER(ep->super.fc.stats, UCT_UB_FC_STAT_RX_SOFT_REQ, 1);

        /* Got soft credit request. Mark ep that it needs to grant
         * credits to the peer in outgoing AM (if any). */
        ep->super.flags |= UCT_RC_URMA_EP_FLAG_FC_GRANT;

    } else if (fc_hdr & UCT_RC_URMA_EP_FLAG_FC_HARD_REQ) {
        UCS_STATS_UPDATE_COUNTER(ep->super.fc.stats, UCT_UB_FC_STAT_RX_HARD_REQ, 1);
        fc_req = ucs_mpool_get(&iface->tx.pending_mp);
        if (ucs_unlikely(fc_req == NULL)) {
            ucs_error("Failed to allocate FC request. "
                      "Grant will not be sent on ep");
            return UCS_ERR_NO_MEMORY;
        }
        fc_req->ep = &ep->super.super.super;
        fc_req->super.func = uct_rc_urma_ep_fc_grant;

        /* Got hard credit request. Send grant to the peer immediately */
        status = uct_rc_urma_ep_fc_grant(&fc_req->super);

        if (status == UCS_ERR_NO_RESOURCE){
            /* force add request to group & schedule group to eliminate
             * FC deadlock */
            uct_pending_req_arb_group_push_head(&ep->arb_group, &fc_req->super);
            ucs_arbiter_group_schedule(&iface->tx.arbiter, &ep->arb_group);
        } else {
            ucs_assertv_always(status == UCS_OK, "Failed to send FC grant msg: %s",
                               ucs_status_string(status));
        }
    }

out:
    return uct_iface_invoke_am(&iface->super.super,
                               (hdr->am_id & ~UCT_RC_URMA_EP_FC_MASK),
                               hdr + 1, length, flags);
}

static void uct_rc_urma_iface_jfc_destroy(uct_urma_iface_t *iface, uint8_t direction)
{
    uct_ub_destroy_jfc(iface->jfc[direction]);
}

static uct_ub_iface_ops_t uct_rc_urma_iface_ops = {
    .super = {
        .iface_estimate_perf   = uct_base_iface_estimate_perf,
        .iface_vfs_refresh     = (uct_iface_vfs_refresh_func_t)ucs_empty_function,
        .ep_query              = (uct_ep_query_func_t)ucs_empty_function_return_unsupported,
        .ep_invalidate         = (uct_ep_invalidate_func_t)ucs_empty_function_return_unsupported,
        .ep_connect_to_ep_v2   = uct_rc_urma_ep_connect_to_ep_v2,
        .iface_is_reachable_v2 = uct_ub_iface_is_reachable
    },
    .create_jfc     = uct_ub_create_jfc,
    .arm_jfc        = uct_ub_iface_arm_jfc,
    .handle_failure = uct_rc_urma_handle_failure,
    .fc_handler     = uct_rc_urma_iface_fc_handler,
    .destroy_jfc    = uct_rc_urma_iface_jfc_destroy,
};

static unsigned uct_rc_urma_iface_do_progress(uct_iface_h tl_iface)
{
    uct_rc_urma_iface_t *iface = ucs_derived_of(tl_iface, uct_rc_urma_iface_t);
    return iface->progress(iface);
}

static uct_iface_ops_t uct_rc_urma_iface_tl_ops = {
    .ep_put_short             = uct_rc_urma_ep_put_short,
    .ep_put_bcopy             = uct_rc_urma_ep_put_bcopy,
    .ep_put_zcopy             = uct_rc_urma_ep_put_zcopy,
    .ep_get_bcopy             = uct_rc_urma_ep_get_bcopy,
    .ep_get_zcopy             = uct_rc_urma_ep_get_zcopy,
    .ep_am_short              = uct_rc_urma_ep_am_short,
    .ep_am_bcopy              = uct_rc_urma_ep_am_bcopy,
    .ep_am_zcopy              = uct_rc_urma_ep_am_zcopy,
    .ep_atomic_cswap64        = uct_rc_urma_ep_atomic_cswap64,
    .ep_atomic_cswap32        = uct_rc_urma_ep_atomic_cswap32,
    .ep_atomic32_post         = uct_rc_urma_ep_atomic32_post,
    .ep_atomic64_post         = uct_rc_urma_ep_atomic64_post,
    .ep_atomic32_fetch        = uct_rc_urma_ep_atomic32_fetch,
    .ep_atomic64_fetch        = uct_rc_urma_ep_atomic64_fetch,
    .ep_pending_add           = uct_rc_urma_ep_pending_add,
    .ep_pending_purge         = uct_rc_urma_ep_pending_purge,
    .ep_flush                 = uct_rc_urma_ep_flush,
    .ep_fence                 = uct_rc_urma_ep_fence,
    .ep_check                 = uct_rc_urma_ep_check,
    .ep_create                = UCS_CLASS_NEW_FUNC_NAME(uct_rc_urma_ep_t),
    .ep_destroy               = UCS_CLASS_DELETE_FUNC_NAME(uct_rc_urma_ep_t),
    .ep_get_address           = uct_rc_urma_ep_get_address,
    .ep_connect_to_ep         = uct_rc_urma_ep_connect_to_ep,
    .iface_flush              = uct_rc_urma_iface_flush,
    .iface_fence              = uct_ub_iface_fence,
    .iface_progress_enable    = uct_rc_urma_iface_common_progress_enable,
    .iface_progress_disable   = uct_base_iface_progress_disable,
    .iface_progress           = uct_rc_urma_iface_do_progress,
    .iface_event_fd_get       = uct_ub_iface_event_fd_get,
    .iface_event_arm          = uct_ub_iface_event_arm,
    .iface_close              = UCS_CLASS_DELETE_FUNC_NAME(uct_rc_urma_iface_t),
    .iface_query              = uct_rc_urma_iface_query,
    .iface_get_device_address = uct_ub_iface_get_device_address,
    .iface_get_address        = ucs_empty_function_return_success,
    .iface_is_reachable       = uct_ub_base_iface_is_reachable
};

static ucs_status_t uct_rc_urma_iface_check_validity(const uct_iface_params_t *params,
                                                     const uct_rc_urma_iface_config_t *config,
                                                     const uct_worker_h worker)
{
    ucs_status_t ret;

    ret = uct_ub_iface_check_validity(params, worker);
    if (ret != UCS_OK) {
        return ret;
    }

    return UCS_OK;
}

static ucs_status_t uct_rc_urma_create_ub_resources(uct_rc_urma_iface_t *iface, const uct_ub_device_t *dev,
                                                    const uct_ub_iface_init_attr_t *init_attr)
{
    ucs_status_t status;

    urma_jfr_cfg_t jfr_cfg = {
        .id                     = 0,
        .depth                  = iface->super.config.rx_queue_depth,
        .flag = {
            .bs = {
                .tag_matching = URMA_NO_TAG_MATCHING,
                .lock_free = 1,
                .order_type = URMA_DEF_ORDER
            }
        },
        .trans_mode             = URMA_TM_RC,
        .max_sge                = 1,
        .min_rnr_timer          = URMA_TYPICAL_MIN_RNR_TIMER,
        .jfc                    = iface->super.jfc[UCT_UB_DIR_RX],
        .token_value            = dev->token
    };

    status = uct_ub_iface_create_jfr(&iface->super, &jfr_cfg);
    if (status != UCS_OK) {
        return status;
    }

    return UCS_OK;
}

static UCS_F_ALWAYS_INLINE void uct_rc_urma_update_tx_res_by_cr(uct_rc_urma_ep_t *ep, urma_cr_t *cr,
                                                                uct_rc_urma_iface_t *iface)
{
    uct_ub_tx_que_status_t *status = &ep->super.status;
    uct_ub_tx_queue_t *txqp = &ep->super.txqp;
    unsigned desc_count;

    desc_count = uct_ub_iface_get_tx_res_count(&ep->super, cr);

    uct_ub_iface_txqp_completion_desc(txqp, status->ci + desc_count);
    uct_rc_urma_update_tx_res(iface, ep, desc_count);
}

static UCS_F_ALWAYS_INLINE void uct_rc_urma_deal_with_send_cr(uct_rc_urma_iface_t *iface, urma_cr_t *cr, int count)
{
    uct_rc_urma_ep_t *ep = NULL;
    uct_ub_iface_find_ep_t ret;
    int i;

    for (i = 0; i < count; i++) {
        if (ucs_unlikely(cr[i].status != URMA_CR_SUCCESS)) {
            UCS_STATS_UPDATE_COUNTER(iface->super.stats, UCT_UB_IFACE_STAT_TX_DL_CR_ERR, 1);
            ucs_error("The send cr status is err: %m; status:%u.", cr[i].status);
            iface->super.ops->handle_failure(&iface->super, &cr[i]);
            continue;
        }

        ret = uct_rc_urma_get_ep_by_cr(iface, &cr[i], &ep);
        if (ucs_unlikely(ret == UCT_UB_EP_NOT_EXIST)) {
            continue;
        }

        uct_rc_urma_update_tx_res_by_cr(ep, &cr[i], iface);
        ucs_arbiter_group_schedule(&iface->tx.arbiter, &ep->arb_group);
        ucs_arbiter_dispatch(&iface->tx.arbiter, 1, uct_rc_urma_ep_process_pending, NULL);
    }
}

static UCS_F_ALWAYS_INLINE unsigned uct_rc_urma_iface_poll_tx(uct_rc_urma_iface_t *iface)
{
    int cr_cnt = iface->super.config.tx_max_poll;
    urma_cr_t cr[cr_cnt];
    int count;

    count = urma_poll_jfc(iface->super.jfc[UCT_UB_DIR_TX], cr_cnt, cr);
    if (ucs_likely(count <= 0)) {
        return 0;
    }

    UCS_STATS_UPDATE_COUNTER(iface->super.stats, UCT_UB_IFACE_STAT_TX_COMPLETION, count);
    uct_rc_urma_deal_with_send_cr(iface, cr, count);

    return (unsigned)count;
}

static UCS_F_ALWAYS_INLINE int uct_rc_urma_iface_check_if_poll_tx(const uct_rc_urma_iface_t *iface, unsigned count)
{
    return (count == 0) || iface->super.config.tx_poll_always;
}

static unsigned uct_rc_urma_iface_progress(void *arg)
{
    uct_rc_urma_iface_t *iface = arg;
    unsigned count;

    count = uct_ub_iface_poll_rx(&iface->super, UCT_RC_URMA_EP_FC_MASK);
    if (!uct_rc_urma_iface_check_if_poll_tx(iface, count)) {
        return count;
    }

    return uct_rc_urma_iface_poll_tx(iface);
}

static UCS_F_ALWAYS_INLINE ucs_status_t uct_rc_urma_iface_fc_init(uct_rc_urma_iface_t *iface,
                                                                  const uct_rc_urma_iface_config_t *config)
{
    uct_ub_iface_fc_init(&iface->super, &config->super);

    /* Check FC parameters correctness */
    if ((config->soft_thresh <= config->super.fc.hard_thresh) ||
        (config->soft_thresh >= 1)) {
        ucs_error("The factor for soft FC threshold should be bigger"
                  " than FC_HARD_THRESH value and less than 1 (s=%f, h=%f)",
                  config->soft_thresh, config->super.fc.hard_thresh);
        return UCS_ERR_INVALID_PARAM;
    }

    if (config->super.fc.enable) {
        iface->config.fc_soft_thresh = ucs_max((int)(iface->super.config.fc_wnd_size *
                                               config->soft_thresh), 1);
    } else {
        iface->config.fc_soft_thresh = 0;
    }
    return UCS_OK;
}

static UCS_F_ALWAYS_INLINE ucs_status_t uct_rc_urma_init_iface_attr(uct_rc_urma_iface_t *iface,
                                                                    const uct_rc_urma_iface_config_t *config,
                                                                    const uct_iface_params_t *params)
{
    uct_ub_hdr_t *hdr = NULL;
    char *dev_name = NULL;
    ucs_status_t status;

#if UCS_ENABLE_ASSERT
    iface->tx.in_pending = 0;
#endif

    iface->progress = uct_rc_urma_iface_progress;
    ucs_arbiter_init(&iface->tx.arbiter);

    /* Flush mode */
    if (config->flush_mode == UCT_RC_URMA_FLUSH_MODE_AUTO) {
        /* Use flow control for flush on older devices */
        dev_name = &(uct_ub_iface_device(&iface->super)->dev_name[0]);
        iface->config.flush_by_fc = (strstr(dev_name, "mthca") == dev_name);
    } else {
        iface->config.flush_by_fc = (config->flush_mode == UCT_RC_URMA_FLUSH_MODE_FLOW_CONTROL);
    }

    status = uct_rc_urma_iface_fc_init(iface, config);
    if (status != UCS_OK) {
        return status;
    }

    if ((iface->super.config.max_inline < sizeof(*hdr)) || iface->config.flush_by_fc) {
        iface->fc_desc = ucs_mpool_get(&iface->super.tx.short_desc_mp);
        ucs_assert_always(iface->fc_desc != NULL);
        hdr = (uct_ub_hdr_t*)(iface->fc_desc + 1);
        hdr->am_id = UCT_RC_URMA_EP_FC_PURE_GRANT;
    } else {
        iface->fc_desc = NULL;
    }

    return UCS_OK;
}

static ucs_mpool_ops_t uct_rc_urma_pending_mpool_ops = {
    .chunk_alloc   = ucs_mpool_chunk_malloc,
    .chunk_release = ucs_mpool_chunk_free,
    .obj_init      = NULL,
    .obj_cleanup   = NULL
};

static ucs_status_t uct_rc_urma_iface_pending_mpool_init(uct_rc_urma_iface_t *iface)
{
    ucs_mpool_params_t mp_params;
    ucs_status_t status;

    /* Create memory pool for flush completions. Can't just alloc a certain
     * size buffer, because number of simultaneous flushes is not limited by
     * CQ or QP resources. */
    ucs_mpool_params_reset(&mp_params);
    mp_params.elem_size = sizeof(uct_rc_urma_pending_req_t);
    mp_params.elems_per_chunk = 128;
    mp_params.alignment = 1;
    mp_params.ops = &uct_rc_urma_pending_mpool_ops;
    mp_params.name = "pending-ops";
    status = ucs_mpool_init(&mp_params, &iface->tx.pending_mp);

    return status;
}

static UCS_CLASS_INIT_FUNC(uct_rc_urma_iface_t, uct_md_h tl_md, uct_worker_h worker,
                           const uct_iface_params_t *params,
                           const uct_iface_config_t *tl_config)
{
    uct_rc_urma_iface_config_t *config = ucs_derived_of(tl_config, uct_rc_urma_iface_config_t);
    uct_ub_md_t *ub_md = ucs_derived_of(tl_md, uct_ub_md_t);
    uct_ub_iface_ops_t *ops = &uct_rc_urma_iface_ops;
    uct_ub_iface_init_attr_t init_attr = {};
    uct_ub_device_t *dev = &ub_md->dev;
    ucs_status_t status = UCS_OK;

    ucs_trace_func("");

    status = uct_rc_urma_iface_check_validity(params, config, worker);
    if (status != UCS_OK) {
        goto err;
    }

    init_attr.jfc_depth[UCT_UB_DIR_TX] = ucs_min(config->tx_jfc_len, dev->dev_attr.dev_cap.max_jfc_depth);
    init_attr.jfc_depth[UCT_UB_DIR_RX] = ucs_min(config->super.rx.depth, dev->dev_attr.dev_cap.max_jfc_depth);
    init_attr.rx_hdr_len               = sizeof(uct_ub_hdr_t);
    init_attr.use_min_seg              = 0;

    UCS_CLASS_CALL_SUPER_INIT(uct_urma_iface_t, ops, &uct_rc_urma_iface_tl_ops, tl_md, worker,
                              params, tl_config, &init_attr);

    status = UCS_STATS_NODE_ALLOC(&self->stats, &uct_rc_urma_iface_stats_class, self->super.stats, "-%p", self);
    if (status != UCS_OK) {
        goto err;
    }

    status = uct_rc_urma_init_iface_attr(self, config, params);
    if (status != UCS_OK) {
        goto err_destroy_stats;
    }

    status = uct_rc_urma_create_ub_resources(self, dev, &init_attr);
    if (status != UCS_OK) {
        ucs_error("Rc urma create ub resources failed");
        goto err_destroy_stats;
    }

    status = uct_rc_urma_iface_pending_mpool_init(self);
    if (status != UCS_OK) {
        ucs_warn("Init pending memory pool err:%m");
        goto err_destroy_mempool;
    }

    uct_ub_iface_init_recv_queue(&self->super, self->super.config.rx_queue_depth);

    return UCS_OK;

err_destroy_mempool:
    uct_ub_destroy_jfr(self->super.rx.jfr);
err_destroy_stats:
    UCS_STATS_NODE_FREE(self->stats);
err:
    return status;
}

static UCS_CLASS_CLEANUP_FUNC(uct_rc_urma_iface_t)
{
    ucs_trace_func("");

    uct_base_iface_progress_disable(&self->super.super.super, UCT_PROGRESS_SEND | UCT_PROGRESS_RECV);
    uct_ub_destroy_jfr(self->super.rx.jfr);
    UCS_STATS_NODE_FREE(self->stats);
    ucs_mpool_cleanup(&self->tx.pending_mp, 1);
    if (self->fc_desc != NULL) {
        ucs_mpool_put(self->fc_desc);
    }
}

UCS_CLASS_DEFINE(uct_rc_urma_iface_t, uct_urma_iface_t);
static UCS_CLASS_DEFINE_NEW_FUNC(uct_rc_urma_iface_t, uct_iface_t, uct_md_h,
                                 uct_worker_h, const uct_iface_params_t*,
                                 const uct_iface_config_t*);

static UCS_CLASS_DEFINE_DELETE_FUNC(uct_rc_urma_iface_t, uct_iface_t);

static ucs_status_t uct_rc_urma_query_tl_devices(uct_md_h md,
                                                 uct_tl_device_resource_t **tl_devices_p,
                                                 unsigned *num_tl_devices_p)
{
    uct_ub_md_t *ub_md = ucs_derived_of(md, uct_ub_md_t);
    int flags = 0;

    return uct_ub_device_query_ports(&ub_md->dev, flags, tl_devices_p,
                                     num_tl_devices_p);
}

UCT_TL_DEFINE(&uct_ub_component, rc_urma, uct_rc_urma_query_tl_devices, uct_rc_urma_iface_t,
              "RC_URMA_", uct_rc_urma_iface_config_table,
              uct_rc_urma_iface_config_t);
