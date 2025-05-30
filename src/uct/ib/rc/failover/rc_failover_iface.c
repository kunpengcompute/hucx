/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 */

#include <uct/ib/rc/verbs/rc_verbs.h>
#include <uct/ib/rc/verbs/rc_verbs_impl.h>
#include <uct/api/uct.h>
#include <uct/ib/rc/base/rc_iface.h>
#include <uct/ib/rc/base/rc_failover.h>
#include <uct/ib/base/ib_device.h>
#include <uct/ib/base/ib_log.h>
#include <uct/ib/base/ib_failover.h>
#include <uct/base/uct_md.h>
#include <ucs/arch/bitops.h>
#include <ucs/arch/cpu.h>
#include <ucs/debug/log.h>
#include <string.h>

#include "rc_failover.h"

static uct_rc_iface_ops_t uct_rc_failover_iface_ops;
static uct_iface_ops_t uct_rc_failover_iface_tl_ops;

static const char *uct_rc_failover_flush_mode_names[] = {
    [UCT_RC_VERBS_FLUSH_MODE_RDMA_WRITE_0] = "write0",
    [UCT_RC_VERBS_FLUSH_MODE_FLOW_CONTROL] = "fc",
    [UCT_RC_VERBS_FLUSH_MODE_AUTO]         = "auto",
    [UCT_RC_VERBS_FLUSH_MODE_LAST]         = NULL
};

static ucs_config_field_t uct_rc_failover_iface_config_table[] = {
  {"RC_", "", NULL,
   ucs_offsetof(uct_rc_verbs_iface_config_t, super),
   UCS_CONFIG_TYPE_TABLE(uct_rc_iface_config_table)},

  {"MAX_AM_HDR", "128",
   "Buffer size to reserve for active message headers. If set to 0, the transport will\n"
   "not support zero-copy active messages.",
   ucs_offsetof(uct_rc_verbs_iface_config_t, max_am_hdr), UCS_CONFIG_TYPE_MEMUNITS},

  {"TX_MAX_WR", "-1",
   "Limits the number of outstanding posted work requests. The actual limit is\n"
   "a minimum between this value and the TX queue length. -1 means no limit.",
   ucs_offsetof(uct_rc_verbs_iface_config_t, tx_max_wr), UCS_CONFIG_TYPE_UINT},

  {"FLUSH_MODE", "auto",
   "Method to use for posting flush operation:\n"
   " - write0 : Post empty RDMA_WRITE\n"
   " - fc     : Send flow control message\n"
   " - auto   : Select automatically based on device support",
   ucs_offsetof(uct_rc_verbs_iface_config_t, flush_mode),
   UCS_CONFIG_TYPE_ENUM(uct_rc_failover_flush_mode_names)},

  {NULL}
};

/* we're not worried about reversals here. we care about count between wr_id and ci */
static UCS_F_ALWAYS_INLINE unsigned
uct_rc_failover_get_tx_res_count(uct_rc_verbs_ep_t *ep,
                                 struct ibv_wc *wc)
{
    return wc->wr_id - ep->txcnt.ci;
}

static UCS_F_ALWAYS_INLINE void
uct_rc_falilover_update_tx_res(uct_rc_iface_t *iface, uct_rc_verbs_ep_t *ep,
                               unsigned count)
{
    ep->txcnt.ci += count;
    uct_rc_txqp_available_add(&ep->super.txqp, count);
    uct_rc_iface_update_reads(iface);
    uct_rc_iface_add_cq_credits(iface, count);
}

static void
uct_rc_failover_handle_failure(uct_ib_iface_t *ib_iface, void *arg,
                               ucs_status_t ep_status)
{
    struct ibv_wc *wc       = arg;
    uct_rc_iface_t *iface   = ucs_derived_of(ib_iface, uct_rc_iface_t);
    ucs_log_level_t log_lvl = UCS_LOG_LEVEL_FATAL;
    char peer_info[128]     = { 0 };
    unsigned dest_qpn;
    uct_rc_verbs_ep_t *ep;
    ucs_status_t status;
    unsigned count;
    struct ibv_ah_attr ah_attr;

    uct_ib_device_t *dev  = uct_ib_iface_device(&iface->super);

    ep = ucs_derived_of(uct_rc_iface_lookup_ep(iface, wc->qp_num),
                        uct_rc_verbs_ep_t);
    if (ucs_unlikely(!ep)) {
        return;
    }

    count = uct_rc_failover_get_tx_res_count(ep, wc);
    uct_rc_txqp_purge_outstanding(iface, &ep->super.txqp, ep_status,
                                  ep->txcnt.ci + count, 0);
    ucs_arbiter_group_purge(&iface->tx.arbiter, &ep->super.arb_group,
                            uct_rc_ep_arbiter_purge_internal_cb, NULL);
    uct_rc_falilover_update_tx_res(iface, ep, count);

    if (ep->super.flags & (UCT_RC_EP_FLAG_ERR_HANDLER_INVOKED |
                           UCT_RC_EP_FLAG_FLUSH_CANCEL)) {
        goto out;
    }

    ep->super.flags |= UCT_RC_EP_FLAG_ERR_HANDLER_INVOKED;
    uct_rc_fc_restore_wnd(iface, &ep->super.fc);

    status  = uct_iface_handle_ep_err(&iface->super.super.super,
                                      &ep->super.super.super, ep_status);
    log_lvl = uct_base_iface_failure_log_level(&ib_iface->super, status,
                                               ep_status);
    status  = uct_ib_query_qp_peer_info(ep->qp, &ah_attr, &dest_qpn);
    if (ucs_likely(status == UCS_OK)) {
        uct_ib_log_dump_qp_peer_info(ib_iface, &ah_attr, dest_qpn, peer_info,
                                     sizeof(peer_info));
    }

    if (ep->super.lid == 0) {
        ucs_warn("RC unhandled timeout error with local dev name:%s remote dev gid:[subnet prefix:%llx interface id:%llx]",
                 uct_ib_device_name(dev),
                 ep->super.gid.global.subnet_prefix,
                 ep->super.gid.global.interface_id);
    } else {
        ucs_warn("RC unhandled timeout error with local dev name:%s remote dev lid:[%hu]",
                 uct_ib_device_name(dev),
                 ep->super.lid);
    }

    ucs_log(log_lvl,
            "send completion with error: %s [qpn 0x%x wrid 0x%lx"
            "vendor_err 0x%x]\n%s", ibv_wc_status_str(wc->status), wc->qp_num,
            wc->wr_id, wc->vendor_err, peer_info);

out:
    uct_rc_iface_arbiter_dispatch(iface);
    return;
}

static inline ucs_status_t
uct_failover_poll_cq(struct ibv_cq *cq, unsigned *count, struct ibv_wc *wcs)
{
    int ret;

    ret = ibv_poll_cq(cq, *count, wcs);
    if (ret <= 0) {
        if (ucs_likely(ret == 0)) {
            return UCS_ERR_NO_PROGRESS;
        }
        ucs_error("failed to poll receive CQ %d", ret);
        return UCS_ERR_REJECTED;
    }

    *count = ret;
    return UCS_OK;
}

static UCS_F_ALWAYS_INLINE unsigned
uct_rc_failover_iface_poll_tx(uct_rc_verbs_iface_t *iface)
{
    uct_rc_verbs_ep_t *ep;
    uint16_t count;
    int i;
    unsigned num_wcs = iface->super.super.config.tx_max_poll;
    struct ibv_wc wc[num_wcs];
    ucs_status_t status;
    uint16_t ts_cnt;

    status = uct_failover_poll_cq(iface->super.super.cq[UCT_IB_DIR_TX], &num_wcs, wc);
    if (status == UCS_ERR_NO_PROGRESS) {
        ucs_arbiter_dispatch(&iface->super.tx.arbiter, 1, uct_rc_failover_ep_process_pending,
                             NULL);
        return 0;
    }
    if (ucs_unlikely(status == UCS_ERR_REJECTED)) {
        ucs_error("iface %p failed to poll send tx CQ %d, device fault", iface, status);
        uct_ib_set_iface_fault_flag(&iface->super.super, DEV_FO_FLAG_IN_PROGRESS);
        return 0;
    }
    UCS_STATS_UPDATE_COUNTER(iface->super.super.stats,
                             UCT_IB_IFACE_STAT_TX_COMPLETION, num_wcs);
    for (i = 0; i < num_wcs; ++i) {
        ep = ucs_derived_of(uct_rc_iface_lookup_ep(&iface->super, wc[i].qp_num),
                            uct_rc_verbs_ep_t);
        if (ucs_unlikely((wc[i].status != IBV_WC_SUCCESS) || (ep == NULL))) {
            /*
            * we may not need to handle failover when we get wc errors,
            * failover handling still depends on dev events.
            */
            ucs_info("iface %p tx wr status error %d, wrid %lu depend on peer-end", iface, wc[i].status, wc[i].wr_id);
        }

        count = uct_rc_failover_get_tx_res_count(ep, &wc[i]);
        ucs_trace_poll("rc_verbs iface %p tx_wc wrid 0x%lx ep %p qpn 0x%x count %d",
                       iface, wc[i].wr_id, ep, wc[i].qp_num, count);

        uct_rc_failover_ep_completion_desc(ep, ep->txcnt.ci + count, &ts_cnt);
        ucs_arbiter_group_schedule(&iface->super.tx.arbiter,
                                   &ep->super.arb_group);
        uct_rc_falilover_update_tx_res(&iface->super, ep, count);
        uct_rc_failover_oscnt_update(ep, count - ts_cnt);
        ucs_arbiter_dispatch(&iface->super.tx.arbiter, 1, uct_rc_failover_ep_process_pending,
                             NULL);
    }

    return num_wcs;
}

static unsigned
uct_rc_failover_iface_post_recv_always(uct_rc_verbs_iface_t *iface, unsigned max)
{
    struct ibv_recv_wr *bad_wr;
    uct_ib_recv_wr_t *wrs;
    unsigned count, i;
    int ret;

    wrs  = ucs_alloca(sizeof(*wrs) * max);

    count = uct_ib_iface_prepare_rx_wrs(&iface->super.super, &iface->super.rx.mp,
                                        wrs, max);
    if (ucs_unlikely(count == 0)) {
        return 0;
    }

    ret = ibv_post_srq_recv(iface->srq, &wrs[0].ibwr, &bad_wr);
    if (ucs_unlikely(ret != 0)) {
        if (ret == EFAULT) {
            ucs_error("iface %p ibv_post_srq_recv() returned %d", iface, ret);
            for (i = 0; i < max; i++) {
                ucs_mpool_put_inline((void*)(wrs[i].ibwr.wr_id));
            }
            uct_ib_set_iface_fault_flag(&iface->super.super, DEV_FO_FLAG_IN_PROGRESS);
            return 0;
        }
        ucs_fatal("ibv_post_srq_recv() returned %d: %m", ret);
    }
    iface->super.rx.srq.available -= count;

    return count;
}

static inline unsigned
uct_rc_failover_iface_post_recv_common(uct_rc_verbs_iface_t *iface,
                                                           int fill)
{
    unsigned batch = iface->super.super.config.rx_max_batch;
    unsigned count;

    if (iface->super.rx.srq.available < batch) {
        if (ucs_likely(fill == 0)) {
            return 0;
        } else {
            count = iface->super.rx.srq.available;
        }
    } else {
        count = batch;
    }
    return uct_rc_failover_iface_post_recv_always(iface, count);
}

static void
uct_rc_failover_iface_handle_am(uct_rc_iface_t *iface, uct_rc_hdr_t *hdr,
                                uint64_t wr_id, uint32_t qp_num, uint32_t length,
                                uint32_t imm_data, uint32_t slid)
{
    uct_ib_iface_recv_desc_t *desc;
    uct_rc_iface_ops_t *rc_ops;
    ucs_status_t status;
    void *udesc;
    uct_rc_failover_ep_t *ep;
    int need_skip_ack = 0;

    desc = (uct_ib_iface_recv_desc_t *)wr_id;
    if (ucs_unlikely(hdr->am_id & UCT_RC_EP_FC_MASK)) {
        rc_ops = ucs_derived_of(iface->super.ops, uct_rc_iface_ops_t);
        if (hdr->am_id == UCT_RC_EP_FC_PURE_GRANT) {
            need_skip_ack = 1;
            ucs_debug("need skip ack because pure grant fc");
        }
        status = rc_ops->fc_handler(iface, qp_num, hdr, length - sizeof(*hdr),
                                    imm_data, slid, UCT_CB_PARAM_FLAG_DESC);
    } else {
        status = uct_iface_invoke_am(&iface->super.super, hdr->am_id, hdr + 1,
                                     length - sizeof(*hdr), UCT_CB_PARAM_FLAG_DESC);
    }

    if (need_skip_ack == 0) {
        ep = ucs_derived_of(uct_rc_iface_lookup_ep(iface, qp_num), uct_rc_failover_ep_t);
        ucs_assert_always(ep != NULL);
        ep->rxcnt.ack++;
    }

    if (ucs_likely(status != UCS_INPROGRESS)) {
        ucs_mpool_put_inline(desc);
    } else {
        udesc = (char*)desc + iface->super.config.rx_headroom_offset;
        uct_recv_desc(udesc) = &iface->super.release_desc;
    }
}

static unsigned
uct_rc_failover_iface_poll_rx_common(uct_rc_verbs_iface_t *iface)
{
    uct_ib_iface_recv_desc_t *desc;
    uct_rc_hdr_t *hdr;
    unsigned i;
    ucs_status_t status;
    unsigned num_wcs = iface->super.super.config.rx_max_poll;
    struct ibv_wc wc[num_wcs];

    status = uct_failover_poll_cq(iface->super.super.cq[UCT_IB_DIR_RX], &num_wcs, wc);
    if (status == UCS_ERR_REJECTED) {
        ucs_error("iface %p failed to poll send rx CQ %d, device fault", iface, status);
        uct_ib_set_iface_fault_flag(&iface->super.super, DEV_FO_FLAG_IN_PROGRESS);
        return 0;
    } else if (status == UCS_ERR_NO_PROGRESS) {
        num_wcs = 0;
        goto out;
    }

    for (i = 0; i < num_wcs; i++) {
        desc = (uct_ib_iface_recv_desc_t *)(uintptr_t)wc[i].wr_id;
        hdr  = (uct_rc_hdr_t *)uct_ib_iface_recv_desc_hdr(&iface->super.super, desc);
        if (ucs_unlikely(wc[i].status != IBV_WC_SUCCESS)) {
            if (wc[i].status == IBV_WC_REM_ABORT_ERR) {
                continue;
            }
            /* we can get flushed messages during ep destroy */
            if (wc[i].status == IBV_WC_WR_FLUSH_ERR) {
                continue;
            }
            if (uct_ib_poll_cq_wr_status_need_failover(&wc[i]) != 0) {
                ucs_error("iface %p rx wr status error %d, device fault", iface, wc[i].status);
                uct_ib_set_iface_fault_flag(&iface->super.super, DEV_FO_FLAG_IN_PROGRESS);
                break;
            }
            UCT_IB_IFACE_VERBS_COMPLETION_ERR("receive", &iface->super.super, i, wc);
        }
        VALGRIND_MAKE_MEM_DEFINED(hdr, wc[i].byte_len);

        uct_ib_log_recv_completion(&iface->super.super, &wc[i], hdr, wc[i].byte_len,
                                   uct_rc_ep_packet_dump);
        uct_rc_failover_iface_handle_am(&iface->super, hdr, wc[i].wr_id, wc[i].qp_num,
                                        wc[i].byte_len, wc[i].imm_data, wc[i].slid);
    }
    iface->super.rx.srq.available += num_wcs;
    UCS_STATS_UPDATE_COUNTER(iface->super.super.stats,
                             UCT_IB_IFACE_STAT_RX_COMPLETION, num_wcs);

out:
    uct_rc_failover_iface_post_recv_common(iface, 0);
    return num_wcs;
}

static unsigned
uct_rc_failover_iface_progress(void *arg)
{
    uct_rc_verbs_iface_t *iface = arg;
    unsigned count;
    uct_dev_fault_status_t fstatus;

    fstatus = uct_ib_check_iface_fault_flag(&iface->super.super);
    if (ucs_unlikely(fstatus != DEV_FO_FLAG_NONE)) {
        if (fstatus == DEV_FO_FLAG_IN_PROGRESS) {
            count = uct_rc_failover_iface_poll_rx_common(iface);
            count += uct_rc_failover_iface_poll_tx(iface);
            if (count > 0) {
                return count;
            }
            if (iface->super.super.failover.ops.iface_failure_handle) {
                iface->super.super.failover.ops.iface_failure_handle(&iface->super.super);
            }
        }
        return 0;
    }

    count = uct_rc_failover_iface_poll_rx_common(iface);
    if (!uct_rc_iface_poll_tx(&iface->super, count)) {
        return count;
    }

    return uct_rc_failover_iface_poll_tx(iface);
}


/*
 * When fault occurs, one-side req may have been sent to the peer-end
 * but local-end has not polled tx.
 * Therefore, here we update ci and oscc(one side complete counts).
 */
unsigned
uct_rc_verbs_iface_simply_poll_tx(uct_rc_verbs_iface_t *iface)
{
    uct_rc_verbs_ep_t *ep;
    uint16_t count;
    int i;
    unsigned num_wcs = iface->super.super.config.tx_max_poll;
    struct ibv_wc wc[num_wcs];
    ucs_status_t status;
    uint16_t ts_cnt;

    status = uct_failover_poll_cq(iface->super.super.cq[UCT_IB_DIR_TX], &num_wcs, wc);
    if (status != UCS_OK) {
        return 0;       // ignore err
    }
    UCS_STATS_UPDATE_COUNTER(iface->super.super.stats,
                             UCT_IB_IFACE_STAT_TX_COMPLETION, num_wcs);
    for (i = 0; i < num_wcs; ++i) {
        ep = ucs_derived_of(uct_rc_iface_lookup_ep(&iface->super, wc[i].qp_num),
                            uct_rc_verbs_ep_t);
        if (ucs_unlikely((wc[i].status != IBV_WC_SUCCESS) || (ep == NULL))) {
            continue;   // ignore err
        }

        count = wc[i].wr_id - ep->txcnt.ci;
        uct_rc_failover_ep_completion_desc(ep, wc[i].wr_id, &ts_cnt);
        ep->txcnt.ci += count;
        uct_rc_txqp_available_add(&ep->super.txqp, count);
        uct_rc_iface_update_reads(&iface->super);
        uct_rc_iface_add_cq_credits(&iface->super, count);
        uct_rc_failover_oscnt_update(ep, count - ts_cnt);
    }

    return num_wcs;
}

/*
 * When fault occurs, peer-end has sent request and peer data has been deleted from the outstanding_q,
 * but the local-end has not processed the request. so we need to poll rx in the fault context,
 * and we skip any error or post_recv. the aim of poll rx also change ack
 */
unsigned
uct_rc_verbs_iface_simply_poll_rx(uct_rc_verbs_iface_t *iface)
{
    uct_ib_iface_recv_desc_t *desc;
    uct_rc_hdr_t *hdr;
    unsigned i;
    ucs_status_t status;
    unsigned num_wcs = iface->super.super.config.rx_max_poll;
    struct ibv_wc wc[num_wcs];

    status = uct_failover_poll_cq(iface->super.super.cq[UCT_IB_DIR_RX], &num_wcs, wc);
    if (status != UCS_OK) {
        return 0;   // ignore err
    }

    for (i = 0; i < num_wcs; i++) {
        desc = (uct_ib_iface_recv_desc_t *)(uintptr_t)wc[i].wr_id;
        hdr  = (uct_rc_hdr_t *)uct_ib_iface_recv_desc_hdr(&iface->super.super, desc);
        if (ucs_unlikely(wc[i].status != IBV_WC_SUCCESS)) {
            continue;       // ignore err
        }
        VALGRIND_MAKE_MEM_DEFINED(hdr, wc[i].byte_len);

        uct_ib_log_recv_completion(&iface->super.super, &wc[i], hdr, wc[i].byte_len,
                                   uct_rc_ep_packet_dump);
        uct_rc_failover_iface_handle_am(&iface->super, hdr, wc[i].wr_id, wc[i].qp_num,
                                        wc[i].byte_len, wc[i].imm_data, wc[i].slid);
    }
    iface->super.rx.srq.available += num_wcs;
    UCS_STATS_UPDATE_COUNTER(iface->super.super.stats,
                             UCT_IB_IFACE_STAT_RX_COMPLETION, num_wcs);

out:
    // skip post recv
    return num_wcs;
}

static ucs_status_t uct_rc_failover_iface_query(uct_iface_h tl_iface, uct_iface_attr_t *iface_attr)
{
    uct_rc_verbs_iface_t *iface = ucs_derived_of(tl_iface, uct_rc_verbs_iface_t);
    uct_ib_md_t *md             = uct_ib_iface_md(ucs_derived_of(iface, uct_ib_iface_t));
    uint8_t mr_id;
    ucs_status_t status;

    status = uct_rc_iface_query(&iface->super, iface_attr,
                                iface->config.max_inline,
                                iface->config.max_inline,
                                iface->config.short_desc_size,
                                iface->config.max_send_sge - 1,
                                sizeof(uct_rc_hdr_t),
                                iface->config.max_send_sge);
    if (status != UCS_OK) {
        return status;
    }

    iface_attr->cap.flags |= UCT_IFACE_FLAG_EP_CHECK;
    iface_attr->latency.m += 1e-9;  /* 1 ns per each extra QP */
    iface_attr->overhead   = 75e-9; /* Software overhead */

    iface_attr->ep_addr_len = (md->ops->get_atomic_mr_id(md, &mr_id) == UCS_OK) ?
                              sizeof(uct_rc_verbs_ep_flush_addr_t) :
                              sizeof(uct_rc_verbs_ep_addr_t);

    return UCS_OK;
}

static ucs_status_t
uct_rc_iface_failover_init_rx(uct_rc_iface_t *rc_iface,
                              const uct_rc_iface_common_config_t *config)
{
    uct_rc_failover_iface_t *iface = ucs_derived_of(rc_iface, uct_rc_failover_iface_t);

    return uct_rc_iface_init_rx(rc_iface, config, &iface->super.srq);
}

static void
uct_rc_iface_failover_cleanup_rx(uct_rc_iface_t *rc_iface)
{
    uct_rc_failover_iface_t *iface = ucs_derived_of(rc_iface, uct_rc_failover_iface_t);

    /* TODO flush RX buffers */
    uct_ib_destroy_srq(iface->super.srq);
}

static ucs_mpool_ops_t uct_rc_buf_info_mpool_ops = {
    .chunk_alloc   = ucs_mpool_chunk_malloc,
    .chunk_release = ucs_mpool_chunk_free,
    .obj_init      = NULL,          // no init
    .obj_cleanup   = NULL,
    .obj_str       = NULL
};

static ucs_status_t
uct_rc_iface_failover_init_buf_info_mp(uct_rc_failover_iface_t *iface)
{
    ucs_status_t status;
    ucs_mpool_params_t mp_params;
    ucs_mpool_params_reset(&mp_params);
    mp_params.elem_size       = sizeof(uct_rc_buf_info_t);
    mp_params.elems_per_chunk = iface->super.super.config.tx_moderation;
    mp_params.ops             = &uct_rc_buf_info_mpool_ops;
    mp_params.name            = "buf-info-mpool";
    status = ucs_mpool_init(&mp_params, &iface->buf_info_mp);
    return status;
}

static UCS_CLASS_INIT_FUNC(uct_rc_failover_iface_t, uct_md_h tl_md,
                           uct_worker_h worker, const uct_iface_params_t *params,
                           const uct_iface_config_t *tl_config)
{
    unsigned origin_buf_grow;
    ucs_status_t status;
    uct_rc_verbs_iface_config_t *config =
                    ucs_derived_of(tl_config, uct_rc_verbs_iface_config_t);
    uct_ib_iface_config_t *ib_config = &config->super.super.super;
    config->iface_op = &uct_rc_failover_iface_tl_ops;
    config->rc_iface_op = &uct_rc_failover_iface_ops;
    config->rc_iface_progress = uct_rc_failover_iface_progress;

    UCS_CLASS_CALL_SUPER_INIT(uct_rc_verbs_iface_t, tl_md, worker, params, tl_config);

    origin_buf_grow = ib_config->tx.mp.bufs_grow;
    ib_config->tx.mp.bufs_grow = self->super.super.config.tx_moderation;
    status = uct_iface_mpool_init(&self->super.super.super.super,
                                  &self->inl_mp,
                                  sizeof(uct_rc_iface_send_desc_t) + self->super.config.max_inline,
                                  sizeof(uct_rc_iface_send_desc_t),
                                  UCS_SYS_CACHE_LINE_SIZE,
                                  &ib_config->tx.mp,
                                  self->super.super.config.tx_moderation,
                                  NULL,
                                  "rc_verbs_inline");
    ib_config->tx.mp.bufs_grow = origin_buf_grow;
    if (status != UCS_OK) {
        ucs_error("init inline mpool failed");
        goto out;
    }

    status = uct_rc_iface_failover_init_buf_info_mp(self);
    if (status != UCS_OK) {
        ucs_error("init buf info mpool failed");
        ucs_mpool_cleanup(&self->inl_mp, 1);
    }

out:
    return status;
}

static void
uct_rc_failover_iface_qp_cleanup(uct_rc_iface_qp_cleanup_ctx_t *rc_cleanup_ctx)
{
    uct_rc_verbs_iface_qp_cleanup_ctx_t *cleanup_ctx =
            ucs_derived_of(rc_cleanup_ctx, uct_rc_verbs_iface_qp_cleanup_ctx_t);
    uct_ib_destroy_qp(cleanup_ctx->qp);
}

static UCS_CLASS_CLEANUP_FUNC(uct_rc_failover_iface_t)
{
    ucs_mpool_cleanup(&self->inl_mp, 1);
    ucs_mpool_cleanup(&self->buf_info_mp, 1);
    return;         // chain
}

UCS_CLASS_DEFINE(uct_rc_failover_iface_t, uct_rc_verbs_iface_t);
static UCS_CLASS_DEFINE_NEW_FUNC(uct_rc_failover_iface_t, uct_iface_t, uct_md_h,
                                 uct_worker_h, const uct_iface_params_t*,
                                 const uct_iface_config_t*);
static UCS_CLASS_DEFINE_DELETE_FUNC(uct_rc_failover_iface_t, uct_iface_t);

static uct_iface_ops_t uct_rc_failover_iface_tl_ops = {
    .ep_am_short              = uct_rc_failover_ep_am_short,
    .ep_am_short_iov          = uct_rc_failover_ep_am_short_iov,
    .ep_am_bcopy              = uct_rc_failover_ep_am_bcopy,
    .ep_am_zcopy              = uct_rc_failover_ep_am_zcopy,
    .ep_put_short             = uct_rc_failover_ep_put_short,
    .ep_put_bcopy             = uct_rc_failover_ep_put_bcopy,
    .ep_put_zcopy             = uct_rc_failover_ep_put_zcopy,
    .ep_get_bcopy             = uct_rc_failover_ep_get_bcopy,
    .ep_get_zcopy             = uct_rc_failover_ep_get_zcopy,
    .ep_atomic_cswap64        = uct_rc_failover_ep_atomic_cswap64,
    .ep_atomic64_post         = uct_rc_failover_ep_atomic64_post,
    .ep_atomic64_fetch        = uct_rc_failover_ep_atomic64_fetch,
    .ep_atomic_cswap32        = (uct_ep_atomic_cswap32_func_t)ucs_empty_function_return_unsupported,
    .ep_atomic32_post         = (uct_ep_atomic32_post_func_t)ucs_empty_function_return_unsupported,
    .ep_atomic32_fetch        = (uct_ep_atomic32_fetch_func_t)ucs_empty_function_return_unsupported,
    .ep_pending_add           = uct_rc_failover_ep_pending_add,
    .ep_pending_purge         = uct_rc_ep_pending_purge,
    .ep_flush                 = uct_rc_failover_ep_flush,
    .ep_fence                 = uct_rc_verbs_ep_fence,
    .ep_check                 = uct_rc_ep_check,
    .ep_create                = UCS_CLASS_NEW_FUNC_NAME(uct_rc_failover_ep_t),
    .ep_destroy               = UCS_CLASS_DELETE_FUNC_NAME(uct_rc_failover_ep_t),
    .ep_get_address           = uct_rc_verbs_ep_get_address,
    .ep_connect_to_ep         = uct_base_ep_connect_to_ep,
    .iface_flush              = uct_rc_iface_flush,
    .iface_fence              = uct_rc_iface_fence,
    .iface_progress_enable    = uct_rc_verbs_iface_common_progress_enable,
    .iface_progress_disable   = uct_base_iface_progress_disable,
    .iface_progress           = uct_rc_iface_do_progress,
    .iface_event_fd_get       = uct_ib_iface_event_fd_get,
    .iface_event_arm          = uct_rc_iface_event_arm,
    .iface_close              = UCS_CLASS_DELETE_FUNC_NAME(uct_rc_failover_iface_t),
    .iface_query              = uct_rc_failover_iface_query,
    .iface_get_address        = ucs_empty_function_return_success,
    .iface_get_device_address = uct_ib_iface_get_device_address,
    .iface_is_reachable       = uct_ib_iface_is_reachable,
    .ep_get_private_date      = uct_rc_failover_ep_get_private_data,
    .ep_resend                = uct_rc_failover_ep_failover_resend_progress,
    .ep_pending_transfer      = uct_rc_ep_failover_pending_transfer_progress,
    .iface_set_fault_flag     = uct_rc_set_iface_fault_flag,
    .iface_get_device_guid    = uct_ib_iface_get_device_guid,
    .ep_pre_handle            = uct_rc_failover_pre_handle,
    .ep_set_fault             = uct_rc_failover_set_fault,
};

static uct_rc_iface_ops_t uct_rc_failover_iface_ops = {
    .super = {
        .super = {
            .iface_estimate_perf   = uct_rc_iface_estimate_perf,
            .iface_vfs_refresh     = uct_rc_iface_vfs_refresh,
            .ep_query              = (uct_ep_query_func_t)ucs_empty_function_return_unsupported,
            .ep_invalidate         = (uct_ep_invalidate_func_t)ucs_empty_function_return_unsupported,
            .ep_connect_to_ep_v2   = uct_rc_verbs_ep_connect_to_ep_v2,
            .iface_is_reachable_v2 = uct_ib_iface_is_reachable_v2
        },
        .create_cq      = uct_ib_verbs_create_cq,
        .destroy_cq     = uct_ib_verbs_destroy_cq,
        .event_cq       = (uct_ib_iface_event_cq_func_t)ucs_empty_function,
        .handle_failure = uct_rc_failover_handle_failure,
    },
    .init_rx         = uct_rc_iface_failover_init_rx,
    .cleanup_rx      = uct_rc_iface_failover_cleanup_rx,
    .fc_ctrl         = uct_rc_failover_ep_fc_ctrl,
    .fc_handler      = uct_rc_iface_fc_handler,
    .cleanup_qp      = uct_rc_failover_iface_qp_cleanup,
    .ep_post_check   = uct_rc_verbs_ep_post_check,
    .ep_vfs_populate = uct_rc_verbs_ep_vfs_populate
};

static ucs_status_t
uct_rc_failover_can_create_qp(struct ibv_context *ctx, struct ibv_pd *pd)
{
    struct ibv_qp_init_attr qp_init_attr = {
        .qp_type             = IBV_QPT_RC,
        .sq_sig_all          = 0,
        .cap.max_send_wr     = 1,
        .cap.max_recv_wr     = 1,
        .cap.max_send_sge    = 1,
        .cap.max_recv_sge    = 1,
        .cap.max_inline_data = 0
    };
    struct ibv_cq *cq;
    struct ibv_qp *qp;
    ucs_status_t status = UCS_OK;

    cq = ibv_create_cq(ctx, 1, NULL, NULL, 0);
    if (cq == NULL) {
        uct_ib_check_memlock_limit_msg(UCS_LOG_LEVEL_DEBUG, "ibv_create_cq()");
        status = UCS_ERR_IO_ERROR;
        goto err;
    }

    qp_init_attr.send_cq = cq;
    qp_init_attr.recv_cq = cq;

    qp = ibv_create_qp(pd, &qp_init_attr);
    if (qp == NULL) {
        uct_ib_check_memlock_limit_msg(UCS_LOG_LEVEL_DEBUG, "ibv_create_qp()");
        status = UCS_ERR_UNSUPPORTED;
        goto err_destroy_cq;
    }

    ibv_destroy_qp(qp);
err_destroy_cq:
    ibv_destroy_cq(cq);
err:
    return status;
}

static ucs_status_t
uct_rc_failover_query_tl_devices(uct_md_h md,
                                 uct_tl_device_resource_t **tl_devices_p,
                                 unsigned *num_tl_devices_p)
{
    uct_ib_md_t *ib_md = ucs_derived_of(md, uct_ib_md_t);
    ucs_status_t status;

    /* device does not support RC if we cannot create an RC QP */
    status = uct_rc_failover_can_create_qp(ib_md->dev.ibv_context, ib_md->pd);
    if (status != UCS_OK) {
        return status;
    }

    return uct_ib_device_query_ports(&ib_md->dev, 0, tl_devices_p,
                                     num_tl_devices_p);
}

UCT_TL_DEFINE_ENTRY(&uct_ib_component, rc_fo, uct_rc_failover_query_tl_devices,
                    uct_rc_failover_iface_t, "RC_F_",
                    uct_rc_failover_iface_config_table,
                    uct_rc_verbs_iface_config_t);
