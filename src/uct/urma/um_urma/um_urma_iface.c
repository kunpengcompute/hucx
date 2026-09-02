/**
* Copyright (C) Huawei Technologies Co., Ltd. 2026. ALL RIGHTS RESERVED.
*
* See file LICENSE for terms.
*/

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <uct/urma/base/urma_device.h>
#include <uct/urma/um_urma/um_urma_ep.h>
#include <uct/api/uct.h>
#include <uct/base/uct_md.h>
#include <uct/base/uct_iface.h>
#include <ucs/arch/bitops.h>
#include <ucs/arch/cpu.h>
#include <ucs/type/class.h>
#include <ucs/type/cpu_set.h>
#include <ucs/debug/log.h>
#include <ucs/sys/string.h>
#include "um_urma_iface.h"
#include "um_urma_ep.h"
#include "um_urma_def.h"
#include "um_urma_inl.h"

#define UCT_UM_URMA_TX_MODERATION    64
#define UCT_UM_URMA_MIN_TIMER_TIMER_BACKOFF 1.0
#define UCT_UM_URMA_IFACE_CEP_CONN_SN_MAX ((uct_um_urma_ep_conn_sn_t) - 1)

static urma_jfr_wr_t __thread g_wrs[UCT_IBP_MAX_RECV_WRS + 1] = {0};
static urma_sge_t __thread g_src_sge_buf[UCT_IBP_MAX_RECV_WRS] = {0};

#ifdef ENABLE_STATS
static ucs_stats_class_t uct_um_urma_iface_stats_class = {
    .name = "um_urma_iface",
    .num_counters = UCT_UM_URMA_IFACE_STAT_LAST,
    .counter_names = {
        [UCT_UM_URMA_IFACE_STAT_RX_DROP] = "rx_drop"
    }
};
#endif

static void UCS_CLASS_DELETE_FUNC_NAME(uct_um_urma_iface_t)(uct_iface_t*);
static void uct_um_urma_iface_free_async_comps(uct_um_urma_iface_t *iface);

ucs_config_field_t uct_um_urma_iface_config_table[] = {
    {"UB_", "", NULL,
    ucs_offsetof(uct_um_urma_iface_config_t, super),
    UCS_CONFIG_TYPE_TABLE(uct_ub_iface_config_table)},

    {"TIMEOUT", "5.0m", "Transport timeout",
     ucs_offsetof(uct_um_urma_iface_config_t, peer_timeout), UCS_CONFIG_TYPE_TIME},

    {"TX_JFC_LEN", "4096",
    "Length of send completion queue. This limits the total number of outstanding signaled sends.",
    ucs_offsetof(uct_um_urma_iface_config_t, tx_jfc_len), UCS_CONFIG_TYPE_UINT},

    {"TIMER_TICK", "10ms", "Initial timeout for retransmissions",
     ucs_offsetof(uct_um_urma_iface_config_t, timer_tick), UCS_CONFIG_TYPE_TIME},

    {"TIMER_BACKOFF", "2.0",
     "Timeout multiplier for resending trigger (must be >= "
     UCS_PP_MAKE_STRING(UCT_UM_URMA_MIN_TIMER_TIMER_BACKOFF) ")",
     ucs_offsetof(uct_um_urma_iface_config_t, timer_backoff), UCS_CONFIG_TYPE_DOUBLE},

    {"ASYNC_TIMER_TICK", "100ms", "Resolution for async timer",
     ucs_offsetof(uct_um_urma_iface_config_t, event_timer_tick), UCS_CONFIG_TYPE_TIME},

    {"MIN_POKE_TIME", "250ms",
     "Minimal interval to send ACK request with solicited flag, to wake up\n"
     "the remote peer in case it is not actively calling progress.\n"
     "Smaller values may incur performance overhead, while extermely large\n"
     "values can cause delays in presence of packet drops.",
     ucs_offsetof(uct_um_urma_iface_config_t, min_poke_time), UCS_CONFIG_TYPE_TIME},

    {"MAX_WINDOW", UCS_PP_MAKE_STRING(UCT_UM_URMA_CA_MAX_WINDOW),
     "Max congestion avoidance window. Should be >= "
      UCS_PP_MAKE_STRING(UCT_UM_URMA_CA_MIN_WINDOW) " and <= "
      UCS_PP_MAKE_STRING(UCT_UM_URMA_CA_MAX_WINDOW),
     ucs_offsetof(uct_um_urma_iface_config_t, max_window), UCS_CONFIG_TYPE_UINT},

    {"RX_ASYNC_MAX_POLL", "64",
     "Max number of receive completions to pick during asynchronous TX poll",
     ucs_offsetof(uct_um_urma_iface_config_t, rx_async_max_poll), UCS_CONFIG_TYPE_UINT},

    {NULL}
};

ucs_status_t uct_um_urma_iface_cep_get_peer_address(uct_um_urma_iface_t *iface, const uct_ub_address_t *dev_addr,
                                                    const uct_um_urma_iface_addr_t *if_addr, void *address_p)
{
    ucs_status_t status;

    status = uct_um_urma_iface_unpack_peer_address(iface, dev_addr, if_addr, address_p);
    if (status != UCS_OK) {
        ucs_diag("Failed to get peer address.");
    }

    return status;
}

static UCS_F_ALWAYS_INLINE ucs_conn_match_queue_type_t uct_um_urma_iface_cep_ep_queue_type(uct_um_urma_ep_t *ep)
{
    return (ep->flags & UCT_UM_URMA_EP_FLAG_PRIVATE) ? UCS_CONN_MATCH_QUEUE_UNEXP : UCS_CONN_MATCH_QUEUE_EXP;
}

uct_um_urma_ep_t *uct_um_urma_iface_cep_get_ep(uct_um_urma_iface_t *iface, const uct_ub_address_t *dev_addr,
                                               const uct_um_urma_iface_addr_t *if_addr,
                                               uct_um_urma_ep_conn_sn_t conn_sn, int is_private)
{
    ucs_conn_match_queue_type_t queue_type = is_private ? UCS_CONN_MATCH_QUEUE_UNEXP : UCS_CONN_MATCH_QUEUE_ANY;
    ucs_conn_match_elem_t *conn_match = NULL;
    uct_um_urma_ep_t *ep = NULL;
    void *peer_address = NULL;
    ucs_status_t status;

    peer_address = ucs_alloca(iface->conn_match_ctx.address_length);
    status       = uct_um_urma_iface_cep_get_peer_address(iface, dev_addr, if_addr, peer_address);
    if (status != UCS_OK) {
        return NULL;
    }
    conn_match = ucs_conn_match_get_elem(&iface->conn_match_ctx, peer_address, conn_sn, queue_type, is_private);
    if (conn_match == NULL) {
        return NULL;
    }

    ep = ucs_container_of(conn_match, uct_um_urma_ep_t, conn_match);
    ucs_assert(ep->flags & UCT_UM_URMA_EP_FLAG_ON_CEP);

    if (is_private) {
        ep->flags &= ~UCT_UM_URMA_EP_FLAG_ON_CEP;
    }

    return ep;
}

ucs_status_t uct_um_urma_iface_cep_get_conn_sn(uct_um_urma_iface_t *iface, const uct_ub_address_t *dev_addr,
                                               const uct_um_urma_iface_addr_t *if_addr,
                                               uct_um_urma_ep_conn_sn_t *conn_sn_p)
{
    void *peer_address = ucs_alloca(iface->conn_match_ctx.address_length);
    ucs_status_t status;

    status = uct_um_urma_iface_cep_get_peer_address(iface, dev_addr, if_addr, peer_address);
    if (status != UCS_OK) {
        return status;
    }

    *conn_sn_p = ucs_conn_match_get_next_sn(&iface->conn_match_ctx, peer_address);

    return UCS_OK;
}

void uct_um_urma_iface_cep_insert_ep(uct_um_urma_iface_t *iface, const uct_ub_address_t *dev_addr,
                                     const uct_um_urma_iface_addr_t *if_addr, uct_um_urma_ep_conn_sn_t conn_sn,
                                     uct_um_urma_ep_t *ep)
{
    ucs_conn_match_queue_type_t queue_type;
    void *peer_address = NULL;

    queue_type = uct_um_urma_iface_cep_ep_queue_type(ep);
    peer_address = ucs_alloca(iface->conn_match_ctx.address_length);

    uct_um_urma_iface_cep_get_peer_address(iface, dev_addr, if_addr, peer_address);

    ucs_conn_match_insert(&iface->conn_match_ctx, peer_address, conn_sn, &ep->conn_match, queue_type);

    ep->flags |= UCT_UM_URMA_EP_FLAG_ON_CEP;
}

void uct_um_urma_iface_cep_remove_ep(uct_um_urma_iface_t *iface, uct_um_urma_ep_t *ep)
{
    if (!(ep->flags & UCT_UM_URMA_EP_FLAG_ON_CEP)) {
        return;
    }

    ucs_conn_match_remove_elem(&iface->conn_match_ctx, &ep->conn_match, uct_um_urma_iface_cep_ep_queue_type(ep));
    ep->flags &= ~UCT_UM_URMA_EP_FLAG_ON_CEP;
}

ucs_status_t uct_um_urma_iface_flush(uct_iface_h tl_iface, unsigned flags, uct_completion_t *comp)
{
    uct_um_urma_iface_t *iface = ucs_derived_of(tl_iface, uct_um_urma_iface_t);
    uct_um_urma_ep_t *ep = NULL;
    ucs_status_t status;
    int count = 0;
    int i;

    ucs_trace_func("");

    if (comp != NULL) {
        return UCS_ERR_UNSUPPORTED;
    }

    uct_um_urma_enter(iface);

    if (ucs_unlikely(uct_um_urma_iface_has_pending_async_ev(iface) ||
                     !ucs_queue_is_empty(&iface->tx.outstanding_q))) {
        UCT_TL_IFACE_STAT_FLUSH_WAIT(&iface->super.super);
        uct_um_urma_leave(iface);
        return UCS_INPROGRESS;
    }

    ucs_ptr_array_for_each(ep, i, &iface->super.eps) {
        status = uct_um_urma_ep_flush_nolock(iface, ep, NULL);
        if ((status == UCS_INPROGRESS) || (status == UCS_ERR_NO_RESOURCE)) {
            ++count;
        }
    }

    uct_um_urma_leave(iface);
    if (count != 0) {
        UCT_TL_IFACE_STAT_FLUSH_WAIT(&iface->super.super);
        return UCS_INPROGRESS;
    }

    UCT_TL_IFACE_STAT_FLUSH(&iface->super.super);

    return UCS_OK;
}

void uct_um_urma_iface_ctl_skb_complete(uct_um_urma_iface_t *iface,
                                        uct_um_urma_ctl_desc_t *cdesc, int is_async)
{
    uct_um_urma_send_skb_t *resent_skb = NULL;
    uct_um_urma_send_skb_t *skb = NULL;

    skb = cdesc->self_skb;
    ucs_assert(!(skb->flags & UCT_UM_URMA_SEND_SKB_FLAG_INVALID));

    resent_skb = cdesc->resent_skb;
    ucs_assert(uct_um_urma_get_ctl_desc(skb) == cdesc);

    if (resent_skb != NULL) {
        ucs_assert(skb->flags        & UCT_UM_URMA_SEND_SKB_FLAG_CTL_RESEND);
        ucs_assert(resent_skb->flags & UCT_UM_URMA_SEND_SKB_FLAG_RESENDING);

        resent_skb->flags &= ~UCT_UM_URMA_SEND_SKB_FLAG_RESENDING;
        --cdesc->ep->tx.resend_count;

        uct_um_urma_ep_window_release_completed(cdesc->ep, is_async);
    } else {
        ucs_assert(skb->flags & UCT_UM_URMA_SEND_SKB_FLAG_CTL_ACK);
    }

    uct_um_urma_skb_release(skb, 0);
}

void uct_um_urma_iface_send_completion(uct_um_urma_iface_t *iface, uint16_t sn,
                                       int is_async)
{
    uct_um_urma_ctl_desc_t *cdesc = NULL;

    ucs_queue_for_each_extract(cdesc, &iface->tx.outstanding_q, queue, UCS_CIRCULAR_COMPARE16(cdesc->sn, <=, sn)) {
        uct_um_urma_iface_ctl_skb_complete(iface, cdesc, is_async);
    }
}

static unsigned uct_um_urma_iface_poll_tx(uct_um_urma_iface_t *iface, int is_async)
{
    uint32_t num_completed;
    urma_cr_t cr = {0};
    int ret;

    ret = urma_poll_jfc(iface->super.jfc[UCT_UB_DIR_TX], 1, &cr);
    if (ucs_unlikely(ret < 0)) {
        ucs_fatal("Failed to poll send JFC.");
    }

    if (ret == 0) {
        return 0;
    }

    if (ucs_unlikely(cr.status != URMA_CR_SUCCESS)) {
        ucs_fatal("The send cr status is err: %m; status:%u.", cr.status);
    }

    num_completed = cr.user_ctx & UCT_UB_EP_TX_QUE_SN_MASK;
    iface->super.tx.jfc_available += num_completed + 1;
    iface->tx.comp_sn += num_completed + 1;

    uct_um_urma_iface_send_completion(iface, iface->tx.comp_sn, is_async);

    return ret;
}

static void uct_um_urma_iface_post_recv_always(uct_um_urma_iface_t *iface, unsigned count)
{
    uct_ub_iface_recv_desc_t *desc[count];
    urma_jfr_wr_t *bad_wr = NULL;
    unsigned tmp_count = 0;
    urma_status_t status;

    while (tmp_count < count) {
        UCT_TL_IFACE_GET_RX_DESC(&iface->super.super, &iface->super.rx.mp, desc[tmp_count], break);
        g_src_sge_buf[tmp_count].addr = (uint64_t)uct_ub_iface_recv_desc_hdr(&iface->super, desc[tmp_count]);
        g_src_sge_buf[tmp_count].len = (iface->super.config.rx_payload_offset - iface->super.config.rx_hdr_offset) +
                                        iface->super.config.seg_size;
        g_src_sge_buf[tmp_count].tseg = desc[tmp_count]->src_tseg;
        g_wrs[tmp_count].src.sge = &g_src_sge_buf[tmp_count];
        g_wrs[tmp_count].src.num_sge = 1;
        g_wrs[tmp_count].user_ctx = (uintptr_t)desc[tmp_count];
        g_wrs[tmp_count].next = &g_wrs[tmp_count + 1];
        tmp_count++;
    }
    if (ucs_likely(tmp_count > 0)) {
        g_wrs[tmp_count - 1].next = NULL;
    }

    status = urma_post_jetty_recv_wr(iface->um_urma_jetty.jetty, g_wrs, &bad_wr);
    if (ucs_unlikely(status != URMA_SUCCESS)) {
        for (tmp_count = 0; tmp_count < count; tmp_count++) {
            UCT_TL_IFACE_PUT_DESC(desc[tmp_count]);
        }
        ucs_fatal("urma_post_jetty_recv_wr failed failed status:%u.", status);
    }
    iface->rx.available -= count;
}

static UCS_F_ALWAYS_INLINE void uct_um_urma_iface_post_recv(uct_um_urma_iface_t *iface)
{
    if (iface->rx.available >= iface->super.config.rx_max_batch) {
        uct_um_urma_iface_post_recv_always(iface, iface->super.config.rx_max_batch);
    }
}

static unsigned uct_um_urma_iface_poll_rx(uct_um_urma_iface_t *iface, int is_async)
{
    unsigned num_crs = iface->super.config.rx_max_poll;
    uct_ub_iface_recv_desc_t *desc = NULL;
    uct_um_urma_neth_t *neth = NULL;
    urma_cr_t cr[num_crs];
    int count;
    int i;

    count = urma_poll_jfc(iface->super.jfc[UCT_UB_DIR_RX], num_crs, cr);
    if (ucs_unlikely(count <= 0)) {
        count = 0;
        goto out;
    }

    for (i = 0; i < count; i++) {
        if (ucs_unlikely(cr[i].status != URMA_CR_SUCCESS)) {
            UCS_STATS_UPDATE_COUNTER(iface->super.stats, UCT_UB_IFACE_STAT_RX_DL_CR_ERR, 1);
            ucs_error("The cr status is err: %m; status:%u.", cr[i].status);
            ucs_mpool_put_inline((void*)cr[i].user_ctx);
            continue;
        }

        desc = (uct_ub_iface_recv_desc_t *)cr[i].user_ctx;
        neth = (uct_um_urma_neth_t *)uct_ub_iface_recv_desc_hdr(&iface->super, desc);
        uct_um_urma_ep_process_rx(iface, neth, cr[i].completion_len,
                                  (uct_um_urma_recv_skb_t*)cr[i].user_ctx, is_async);
    }

    UCS_STATS_SET_COUNTER(iface->super.stats, UCT_UB_IFACE_STAT_RX_JFR_AVAILABLE, iface->super.rx.jfr_que_available);

    iface->rx.available += count;

out:
    uct_um_urma_iface_post_recv(iface);

    return count;
}

static inline uct_um_urma_comp_desc_t *uct_um_urma_comp_desc(uct_um_urma_send_skb_t *skb)
{
    ucs_assert(skb->flags & UCT_UM_URMA_SEND_SKB_FLAG_COMP);
    ucs_assert(!(skb->flags & UCT_UM_URMA_SEND_SKB_FLAG_INVALID));

    return (uct_um_urma_comp_desc_t*)((char*)skb->neth + skb->len);
}

unsigned uct_um_urma_iface_dispatch_async_comps_do(uct_um_urma_iface_t *iface)
{
    uct_um_urma_comp_desc_t *cdesc = NULL;
    uct_um_urma_send_skb_t *skb = NULL;
    unsigned count = 0;

    ucs_queue_for_each_extract(skb, &iface->tx.async_comp_q, queue, 1) {
        ucs_assert(!(skb->flags & UCT_UM_URMA_SEND_SKB_FLAG_RESENDING));
        cdesc = uct_um_urma_comp_desc(skb);
        uct_um_urma_iface_dispatch_comp(iface, cdesc->comp, cdesc->status);
        uct_um_urma_skb_release(skb, 0);
        ++count;
    }

    return count;
}

unsigned uct_um_urma_iface_dispatch_pending_rx_do(uct_um_urma_iface_t *iface)
{
    unsigned max_poll = iface->super.config.rx_max_poll;
    uct_ub_iface_recv_desc_t *desc = NULL;
    uct_um_urma_recv_skb_t *skb = NULL;
    uct_um_urma_neth_t *neth = NULL;
    int count = 0;

    do {
        skb  = ucs_queue_pull_elem_non_empty(&iface->rx.pending_q,
                                             uct_um_urma_recv_skb_t, u.am.queue);
        desc = (uct_ub_iface_recv_desc_t *)skb;
        neth  = uct_ub_iface_recv_desc_hdr(&iface->super,
                                           (uct_ub_iface_recv_desc_t*)skb);
        uct_um_urma_ep_deal_am(iface, neth, skb->u.am.len + sizeof(uct_um_urma_neth_t), desc);
        ++count;
    } while ((count < max_poll) && !ucs_queue_is_empty(&iface->rx.pending_q));

    return count;
}

static UCS_F_ALWAYS_INLINE unsigned
uct_um_urma_iface_dispatch_pending_rx(uct_um_urma_iface_t *iface)
{
    if (ucs_likely(ucs_queue_is_empty(&iface->rx.pending_q))) {
        return 0;
    }

    return uct_um_urma_iface_dispatch_pending_rx_do(iface);
}

static void uct_um_urma_iface_progress_pending(uct_um_urma_iface_t *iface, const uintptr_t is_async)
{
    uct_um_urma_iface_twheel_sweep(iface);

    if (!is_async) {
        iface->tx.async_before_pending = 0;
    }

    if (!uct_um_urma_iface_can_tx(iface)) {
        return;
    }

    ucs_arbiter_dispatch(&iface->tx.pending_q, 1, uct_um_urma_ep_do_pending,
                         (void *)is_async);
}

static unsigned uct_um_urma_iface_progress(uct_iface_h tl_iface)
{
    uct_um_urma_iface_t *iface = ucs_derived_of(tl_iface, uct_um_urma_iface_t);
    unsigned count;

    uct_um_urma_enter(iface);

    count = uct_um_urma_iface_dispatch_async_comps(iface);
    count += uct_um_urma_iface_dispatch_pending_rx(iface);
    if (ucs_likely(count == 0)) {
        count = uct_um_urma_iface_poll_rx(iface, 0);
        if (count == 0) {
            count += uct_um_urma_iface_poll_tx(iface, 0);
        }
    }

    uct_um_urma_iface_progress_pending(iface, 0);

    uct_um_urma_leave(iface);

    return count;
}

static ucs_status_t uct_um_urma_iface_query(uct_iface_h tl_iface, uct_iface_attr_t *iface_attr)
{
    uct_um_urma_iface_t *iface = ucs_derived_of(tl_iface, uct_um_urma_iface_t);

    ucs_status_t status = uct_ub_base_iface_query(&(iface->super), iface_attr);
    if (status != UCS_OK) {
        return status;
    }

    status = uct_ub_iface_cap_query(&(iface->super), iface_attr);
    if (status != UCS_OK) {
        return status;
    }

    iface_attr->iface_addr_len         = sizeof(uct_um_urma_iface_addr_t);
    iface_attr->ep_addr_len            = sizeof(uct_ub_um_urma_ep_addr_t);
    iface_attr->cap.flags             |= UCT_IFACE_FLAG_CONNECT_TO_EP;
    iface_attr->cap.flags             |= UCT_IFACE_FLAG_CONNECT_TO_IFACE;
    iface_attr->cap.flags             |= UCT_IFACE_FLAG_CB_ASYNC;
    iface_attr->cap.flags             &= ~UCT_IFACE_FLAG_PUT_BCOPY;
    iface_attr->cap.flags             &= ~UCT_IFACE_FLAG_PUT_ZCOPY;
    iface_attr->cap.am.max_short       = iface->super.config.max_inline -
                                         sizeof(uct_um_urma_neth_t) - sizeof(uct_um_urma_am_short_hdr_t);
    iface_attr->cap.am.max_bcopy       = iface->super.config.seg_size - sizeof(uct_um_urma_neth_t);
    iface_attr->cap.put.max_short      = iface->super.config.max_inline -
                                         sizeof(uct_um_urma_neth_t) - sizeof(uct_um_urma_put_hdr_t);
    iface_attr->cap.am.min_zcopy       = 0;
    iface_attr->cap.am.max_zcopy       = iface->super.config.seg_size - sizeof(uct_um_urma_neth_t);
    iface_attr->cap.atomic64.fop_flags = 0;
    iface_attr->cap.atomic32.fop_flags = 0;
    iface_attr->cap.atomic64.op_flags  = 0;
    iface_attr->cap.atomic32.op_flags  = 0;
    iface_attr->overhead               = 105e-9;
    iface_attr->latency.c             += 30e-9;

    return UCS_OK;
}

ucs_status_t uct_um_urma_iface_get_address(uct_iface_h tl_iface, uct_iface_addr_t *iface_addr)
{
    uct_um_urma_iface_t *iface = ucs_derived_of(tl_iface, uct_um_urma_iface_t);
    uct_um_urma_iface_addr_t *addr = (uct_um_urma_iface_addr_t *)iface_addr;

    addr->jetty_id = iface->um_urma_jetty.jetty->jetty_id.id;
    addr->trans_mode = iface->um_urma_jetty.jetty->jetty_cfg.jfs_cfg.trans_mode;

    return UCS_OK;
}

static ucs_status_t uct_um_urma_ep_create(const uct_ep_params_t *params, uct_ep_h *ep_p)
{
    if (ucs_test_all_flags(params->field_mask, UCT_EP_PARAM_FIELD_DEV_ADDR | UCT_EP_PARAM_FIELD_IFACE_ADDR)) {
        return uct_um_urma_ep_create_connected_common(params, ep_p);
    }

    return uct_um_urma_ep_t_new(params, ep_p);
}

static unsigned uct_ur_iface_async_progress(uct_um_urma_iface_t *ur_iface)
{
    uct_um_urma_iface_t *iface = ucs_derived_of(ur_iface, uct_um_urma_iface_t);
    unsigned count, n;

    count = 0;
    do {
        n = uct_um_urma_iface_poll_rx(iface, 1);
        count += n;
    } while ((n > 0) && (count < iface->rx.async_max_poll));

    count += uct_um_urma_iface_poll_tx(iface, 1);

    uct_um_urma_iface_progress_pending(iface, 1);

    return count;
}

static void uct_um_urma_iface_async_progress(uct_um_urma_iface_t *iface)
{
    if (ucs_unlikely(iface->async.disable)) {
        return;
    }

    if (uct_ur_iface_async_progress(iface) > 0) {
        uct_um_urma_iface_raise_pending_async_ev(iface);
    }
}

static void uct_um_urma_iface_timer(int timer_id, ucs_event_set_types_t events, void *arg)
{
    uct_um_urma_iface_t *iface = arg;

    uct_um_urma_iface_async_progress(iface);
}

void uct_um_urma_iface_release_desc(uct_recv_desc_t *self, void *desc)
{
    uct_um_urma_iface_t *iface = ucs_container_of(self, uct_um_urma_iface_t, super.release_desc);

    uct_um_urma_enter(iface);
    uct_ub_iface_release_desc(self, desc);
    uct_um_urma_leave(iface);
}

ucs_status_t uct_um_urma_iface_event_arm(uct_iface_h tl_iface, unsigned events)
{
    uct_um_urma_iface_t *iface = ucs_derived_of(tl_iface, uct_um_urma_iface_t);
    ucs_status_t status;

    uct_um_urma_enter(iface);

    status = uct_ub_iface_event_arm(tl_iface, events);

    uct_um_urma_leave(iface);

    return status;
}

void uct_um_urma_iface_progress_enable(uct_iface_h tl_iface, unsigned flags)
{
    uct_um_urma_iface_t *iface = ucs_derived_of(tl_iface, uct_um_urma_iface_t);
    ucs_async_context_t *async = iface->super.super.worker->async;
    ucs_status_t status;

    uct_um_urma_enter(iface);

    if (iface->async.timer_id == 0) {
        status = ucs_async_add_timer(async->mode, iface->async.tick, uct_um_urma_iface_timer, iface, async,
                                     &iface->async.timer_id);
        if (status != UCS_OK) {
            ucs_fatal("Iface: unable to add iface timer handler - %s", ucs_status_string(status));
        }
        ucs_assert(iface->async.timer_id != 0);
    }

    uct_um_urma_leave(iface);
    uct_base_iface_progress_enable(tl_iface, flags);
}

void uct_um_urma_iface_progress_disable(uct_iface_h tl_iface, unsigned flags)
{
    uct_um_urma_iface_t *iface = ucs_derived_of(tl_iface, uct_um_urma_iface_t);
    ucs_status_t status;

    uct_um_urma_enter(iface);

    if (iface->async.timer_id != 0) {
        status = ucs_async_remove_handler(iface->async.timer_id, 1);
        if (status != UCS_OK) {
            ucs_fatal("Iface: unable to remove iface timer handler (%d) - %s.",
                      iface->async.timer_id, ucs_status_string(status));
        }

        iface->async.timer_id = 0;
    }

    uct_um_urma_leave(iface);

    uct_base_iface_progress_disable(tl_iface, flags);
}

static void uct_um_urma_iface_jfc_destroy(uct_ub_iface_t *iface, uint8_t direction)
{
    uct_ub_destroy_jfc(iface->jfc[direction]);
}

static uct_ub_iface_ops_t uct_um_urma_iface_ops = {
    .super = {
        .iface_estimate_perf = uct_base_iface_estimate_perf,
        .iface_vfs_refresh = (uct_iface_vfs_refresh_func_t)ucs_empty_function,
        .ep_query = (uct_ep_query_func_t)ucs_empty_function_return_unsupported,
        .ep_invalidate = (uct_ep_invalidate_func_t)ucs_empty_function_return_unsupported,
        .ep_connect_to_ep_v2 = uct_um_urma_ep_connect_to_ep_v2,
        .iface_is_reachable_v2 = uct_ub_iface_is_reachable
    },
    .create_jfc = uct_ub_create_jfc,
    .arm_jfc = uct_ub_iface_arm_jfc,
    .handle_failure = ucs_empty_function,
    .fc_handler = ucs_empty_function_return_success,
    .destroy_jfc = uct_um_urma_iface_jfc_destroy,
};

static uct_um_urma_iface_ops_t uct_um_urma_iface_tl_ops = {
    {
    .ep_put_short             = uct_um_urma_ep_put_short,
    .ep_am_short              = uct_um_urma_ep_am_short,
    .ep_am_bcopy              = uct_um_urma_ep_am_bcopy,
    .ep_am_zcopy              = uct_um_urma_ep_am_zcopy,
    .ep_pending_add           = uct_um_urma_ep_pending_add,
    .ep_pending_purge         = uct_um_urma_ep_pending_purge,
    .ep_flush                 = uct_um_urma_ep_flush,
    .ep_fence                 = uct_base_ep_fence,
    .ep_create                = uct_um_urma_ep_create,
    .ep_destroy               = uct_um_urma_ep_disconnect,
    .ep_get_address           = uct_um_urma_ep_get_address,
    .ep_connect_to_ep         = uct_um_urma_ep_connect_to_ep,
    .iface_flush              = uct_um_urma_iface_flush,
    .iface_fence              = uct_ub_iface_fence,
    .iface_progress_enable    = uct_um_urma_iface_progress_enable,
    .iface_progress_disable   = uct_um_urma_iface_progress_disable,
    .iface_progress           = uct_um_urma_iface_progress,
    .iface_event_fd_get       = uct_ub_iface_event_fd_get,
    .iface_event_arm          = uct_um_urma_iface_event_arm,
    .iface_close              = UCS_CLASS_DELETE_FUNC_NAME(uct_um_urma_iface_t),
    .iface_query              = uct_um_urma_iface_query,
    .iface_get_device_address = uct_ub_iface_get_device_address,
    .iface_get_address        = uct_um_urma_iface_get_address,
    .iface_is_reachable       = uct_ub_base_iface_is_reachable
    },
    .ep_free                  = UCS_CLASS_DELETE_FUNC_NAME(uct_um_urma_ep_t),
};

static ucs_status_t uct_um_urma_create_ub_resources(uct_um_urma_iface_t *iface,
                                                    const uct_ub_device_t *dev)
{
    ucs_status_t status;
    urma_jfr_cfg_t jfr_cfg = {
        .id = 0,
        .depth = iface->super.config.rx_queue_depth,
        .flag = {.bs = {.tag_matching = URMA_NO_TAG_MATCHING}},
        .trans_mode = URMA_TM_UM,
        .max_sge = 1,
        .min_rnr_timer = URMA_TYPICAL_MIN_RNR_TIMER,
        .jfc = iface->super.jfc[UCT_UB_DIR_RX],
        .token_value = dev->token
    };
    urma_jetty_flag_t jetty_flag = {
        .bs = {.share_jfr = 1}
    };
    urma_jfs_cfg_t jfs_cfg = {
        .depth = iface->super.config.tx_max_wr,
        .trans_mode = URMA_TM_UM,
        .priority = UB_URMA_MAX_PRIORITY,
        .max_sge = iface->super.config.max_send_sge,
        .max_inline_data = iface->super.config.max_inline,
        .rnr_retry = URMA_TYPICAL_RNR_RETRY,
        .err_timeout = URMA_TYPICAL_ERR_TIMEOUT,
        .jfc = iface->super.jfc[UCT_UB_DIR_TX],
        .user_ctx = (uint64_t)NULL,
    };
    urma_jetty_cfg_t jetty_cfg = {
        .id = 0,
        .flag = jetty_flag,
        .jfs_cfg = jfs_cfg,
        .shared = {
            .jfr = iface->super.rx.jfr,
            .jfc = iface->super.jfc[UCT_UB_DIR_RX]
        }
    };

    status = uct_ub_iface_create_jfr(&iface->super, &jfr_cfg);
    if (status != UCS_OK) {
        return status;
    }

    jetty_cfg.shared.jfr = iface->super.rx.jfr,
    iface->um_urma_jetty.jetty = urma_create_jetty(dev->urma_ctx, &jetty_cfg);
    if (iface->um_urma_jetty.jetty == NULL) {
        ucs_error("Call urma_create_jetty failed.");
        uct_ub_destroy_jfr(iface->super.rx.jfr);
        return UCS_ERR_INVALID_PARAM;
    }
    iface->um_urma_jetty.available = iface->super.config.tx_max_wr - 1;

    return UCS_OK;
}

static void uct_um_urma_iface_free_async_comps(uct_um_urma_iface_t *iface)
{
    uct_um_urma_send_skb_t *skb = NULL;

    ucs_queue_for_each_extract(skb, &iface->tx.async_comp_q, queue, 1) {
        uct_um_urma_skb_release(skb, 0);
    }
}

static void uct_um_urma_destroy_ub_resources(uct_um_urma_iface_t *iface)
{
    if (urma_delete_jetty(iface->um_urma_jetty.jetty) != URMA_SUCCESS) {
        ucs_error("Failed to delete jetty.");
    }
    uct_ub_destroy_jfr(iface->super.rx.jfr);
}

static void uct_um_urma_iface_send_skb_init(uct_iface_h tl_iface, void *obj, uct_mem_h memh)
{
    uct_um_urma_send_skb_t *skb = obj;

    skb->tseg = uct_ub_memh_get_tseg(memh);
    skb->flags = 0; // UCT_UM_URMA_SEND_SKB_FLAG_INVALID;
}

static ucs_status_t uct_um_urma_iface_mempool_init(uct_um_urma_iface_t *iface,
                                                   const uct_um_urma_iface_config_t *config)
{
    ucs_status_t status;
    size_t data_size;

    data_size = sizeof(uct_um_urma_ctl_hdr_t) + iface->super.addr_size;
    data_size = ucs_max(data_size, iface->super.config.seg_size);
    data_size = ucs_max(data_size, sizeof(uct_um_urma_zcopy_desc_t) + iface->super.config.max_inline);
    data_size = ucs_max(data_size, sizeof(uct_um_urma_ctl_desc_t) + sizeof(uct_um_urma_neth_t));

    status = uct_iface_mpool_init(&iface->super.super, &iface->tx.mp, sizeof(uct_um_urma_send_skb_t) + data_size,
                                  sizeof(uct_um_urma_send_skb_t), UCS_SYS_CACHE_LINE_SIZE,
                                  &config->super.tx.mp, iface->super.config.tx_queue_depth,
                                  uct_um_urma_iface_send_skb_init, "um_urma_tx_skb");
    if (status != UCS_OK) {
        ucs_warn("Init um_urma_tx_skb memory pool, ret=%d.", status);
    }

    return status;
}

static UCS_F_ALWAYS_INLINE const void *uct_um_urma_iface_conn_match_get_address(const ucs_conn_match_elem_t *elem)
{
    uct_um_urma_ep_t *ep = ucs_container_of(elem, uct_um_urma_ep_t, conn_match);

    return &ep->peer_address;
}

static ucs_conn_sn_t uct_um_urma_iface_conn_match_get_conn_sn(const ucs_conn_match_elem_t *elem)
{
    uct_um_urma_ep_t *ep = ucs_container_of(elem, uct_um_urma_ep_t, conn_match);

    return ep->conn_sn;
}

static const char *uct_um_urma_iface_conn_match_address_str(const ucs_conn_match_ctx_t *conn_match_ctx,
                                                            const void *address, char *str, size_t max_size)
{
    const uct_um_urma_peer_address_t *peer_address = (const uct_um_urma_peer_address_t*)address;

    ucs_snprintf_zero(str, max_size, "Tjetty_id=%u", peer_address->tjetty_id);

    return str;
}

static void uct_um_urma_iface_conn_match_purge_cb(ucs_conn_match_ctx_t *conn_match_ctx, ucs_conn_match_elem_t *elem)
{
    uct_um_urma_iface_t *iface = ucs_container_of(conn_match_ctx, uct_um_urma_iface_t, conn_match_ctx);
    uct_um_urma_ep_t *ep = ucs_container_of(elem, uct_um_urma_ep_t, conn_match);
    uct_um_urma_iface_ops_t *ops = NULL;

    ep->flags &= ~UCT_UM_URMA_EP_FLAG_ON_CEP;

    ops = ucs_derived_of(iface->ops, uct_um_urma_iface_ops_t);
    ops->ep_free(&ep->super.super.super);
}

static ucs_status_t uct_um_urma_init_iface_attr(uct_um_urma_iface_t *iface, uct_um_urma_iface_config_t *config)
{
    ucs_status_t status;

    ucs_conn_match_ops_t conn_match_ops = {
        .get_address = uct_um_urma_iface_conn_match_get_address,
        .get_conn_sn = uct_um_urma_iface_conn_match_get_conn_sn,
        .address_str = uct_um_urma_iface_conn_match_address_str,
        .purge_cb    = uct_um_urma_iface_conn_match_purge_cb
    };

    if (config->timer_tick <= 0.) {
        ucs_error("The timer tick should be > 0 (%lf)", config->timer_tick);
        return UCS_ERR_INVALID_PARAM;
    } else {
        iface->tx.tick = ucs_time_from_sec(config->timer_tick);
    }

    if (config->timer_backoff < UCT_UM_URMA_MIN_TIMER_TIMER_BACKOFF) {
        ucs_error("The timer back off must be >= %lf (%lf)",
                  UCT_UM_URMA_MIN_TIMER_TIMER_BACKOFF, config->timer_backoff);
        return UCS_ERR_INVALID_PARAM;
    } else {
        iface->tx.timer_backoff = config->timer_backoff;
    }

    if (config->event_timer_tick <= 0.) {
        ucs_error("The event timer tick should be > 0 (%lf)", config->event_timer_tick);
        return UCS_ERR_INVALID_PARAM;
    } else {
        iface->async.tick = ucs_time_from_sec(config->event_timer_tick);
    }

    ucs_conn_match_init(&iface->conn_match_ctx, sizeof(uct_um_urma_peer_address_t),
                        UCT_UM_URMA_IFACE_CEP_CONN_SN_MAX, &conn_match_ops);

    status = ucs_twheel_init(&iface->tx.timer, iface->tx.tick / 4, uct_um_urma_iface_get_time(iface));
    if (status != UCS_OK) {
        return status;
    }

    ucs_arbiter_init(&iface->tx.pending_q);
    ucs_queue_head_init(&iface->tx.outstanding_q);
    ucs_queue_head_init(&iface->tx.async_comp_q);
    ucs_queue_head_init(&iface->rx.pending_q);
    kh_init_inplace(uct_um_urma_tjetty, &iface->peer_addr_hash);
    iface->rx.available = iface->super.config.rx_queue_depth;
    iface->config.peer_timeout = ucs_time_from_sec(config->peer_timeout);
    iface->config.min_poke_time = ucs_time_from_sec(config->min_poke_time);
    iface->async.timer_id = 0;
    iface->tx.async_before_pending = 0;
    iface->tx.timer_sweep_count = 0;
    iface->async.disable  = 0;
    iface->tx.unsignaled = 0;
    iface->rx.async_max_poll = config->rx_async_max_poll;
    iface->tx.send_sn = 0;
    iface->tx.comp_sn = 0;
    iface->tx.skb = NULL;
    iface->super.release_desc.cb = uct_um_urma_iface_release_desc;

    if ((config->max_window < UCT_UM_URMA_CA_MIN_WINDOW) || (config->max_window > UCT_UM_URMA_CA_MAX_WINDOW)) {
        ucs_conn_match_cleanup(&iface->conn_match_ctx);
        ucs_twheel_cleanup(&iface->tx.timer);
        kh_destroy_inplace(uct_um_urma_tjetty, &iface->peer_addr_hash);
        ucs_error("Max congestion avoidance window should be >= %d and <= %d (%d).",
                  UCT_UM_URMA_CA_MIN_WINDOW, UCT_UM_URMA_CA_MAX_WINDOW, config->max_window);
        return UCS_ERR_INVALID_PARAM;
    }

    iface->config.max_window = config->max_window;

    memset(&iface->tx.wr_inl, 0, sizeof(iface->tx.wr_inl));
    iface->tx.wr_inl.opcode = URMA_OPC_SEND;
    iface->tx.wr_inl.send.src.num_sge = 2;
    iface->tx.wr_inl.send.src.sge = iface->tx.sge;
    iface->tx.wr_inl.next = NULL;

    memset(&iface->tx.wr_skb, 0, sizeof(iface->tx.wr_skb));
    iface->tx.wr_skb.opcode = URMA_OPC_SEND;
    iface->tx.wr_skb.send.src.num_sge = 1;
    iface->tx.wr_skb.send.src.sge = iface->tx.sge;
    iface->tx.wr_skb.next = NULL;

    return UCS_OK;
}

static UCS_CLASS_INIT_FUNC(uct_um_urma_iface_t, uct_md_h tl_md, uct_worker_h worker, const uct_iface_params_t *params,
                           const uct_iface_config_t *tl_config)
{
    uct_um_urma_iface_config_t *config = ucs_derived_of(tl_config, uct_um_urma_iface_config_t);
    uct_um_urma_iface_ops_t *ops = &uct_um_urma_iface_tl_ops;
    uct_ub_md_t *ub_md = ucs_derived_of(tl_md, uct_ub_md_t);
    uct_ub_iface_init_attr_t init_attr = {};
    uct_ub_device_t *dev = &ub_md->dev;
    ucs_status_t status;

    ucs_trace_func("");

    status = uct_ub_iface_check_validity(params, worker);
    if (status != UCS_OK) {
        goto out;
    }

    init_attr.jfc_depth[UCT_UB_DIR_TX] = ucs_min(config->super.tx.depth, dev->dev_attr.dev_cap.max_jfc_depth);
    init_attr.jfc_depth[UCT_UB_DIR_RX] = ucs_min(config->super.rx.depth, dev->dev_attr.dev_cap.max_jfc_depth);
    init_attr.rx_priv_len = sizeof(uct_um_urma_recv_skb_t) - sizeof(uct_ub_iface_recv_desc_t);
    init_attr.rx_hdr_len = sizeof(uct_um_urma_neth_t);
    init_attr.use_min_seg = 1;
    UCS_CLASS_CALL_SUPER_INIT(uct_ub_iface_t, &uct_um_urma_iface_ops, &ops->super, tl_md,
                              worker, params, tl_config, &init_attr);
    status = UCS_STATS_NODE_ALLOC(&self->stats, &uct_um_urma_iface_stats_class, self->super.stats, "-%p", self);
    if (status != UCS_OK) {
        goto out;
    }

    self->ops = ops;
    status = uct_um_urma_create_ub_resources(self, dev);
    if (status != UCS_OK) {
        goto err_create_ub_resources;
    }

    status = uct_um_urma_iface_mempool_init(self, config);
    if (status != UCS_OK) {
        goto err_mempool_init;
    }

    status = uct_um_urma_init_iface_attr(self, config);
    if (status != UCS_OK) {
        goto err_iface_attr_init;
    }

    while (self->rx.available >= self->super.config.rx_max_batch) {
        uct_um_urma_iface_post_recv(self);
    }

    return status;

err_iface_attr_init:
    ucs_mpool_cleanup(&self->tx.mp, 1);
err_mempool_init:
    uct_um_urma_destroy_ub_resources(self);
err_create_ub_resources:
    UCS_STATS_NODE_FREE(self->stats);
out:
    return status;
}

static UCS_CLASS_CLEANUP_FUNC(uct_um_urma_iface_t)
{
    uct_um_urma_peer_address_t addr;
    uct_um_urma_ep_t *ep = NULL;
    uint32_t i = 0;

    ucs_trace_func("");

    uct_um_urma_enter(self);

    uct_um_urma_iface_progress_disable(&self->super.super.super, UCT_PROGRESS_SEND | UCT_PROGRESS_RECV);
    uct_um_urma_iface_free_async_comps(self);
    ucs_conn_match_cleanup(&self->conn_match_ctx);
    ucs_twheel_cleanup(&self->tx.timer);
    ucs_ptr_array_for_each(ep, i, &self->super.eps) {
        if (ep->flags & UCT_UM_URMA_EP_FLAG_PRIVATE) {
            self->super.super.super.ops.ep_destroy(&ep->super.super.super);
        }
    }

    kh_foreach_value(&self->peer_addr_hash, addr, {
        if (addr.tjetty != NULL) {
            if (urma_unimport_jetty(addr.tjetty) != URMA_SUCCESS) {
                ucs_warn("Urma_unimport_jetty failed, eid="EID_FMT", uasid=%u, jetty_id=%u.",
                         EID_ARGS(addr.tjetty->id.eid), addr.tjetty->id.uasid, addr.tjetty->id.id);
            }
            addr.tjetty = NULL;
        }
    });
    kh_destroy_inplace(uct_um_urma_tjetty, &self->peer_addr_hash);

    uct_um_urma_destroy_ub_resources(self);

    ucs_arbiter_cleanup(&self->tx.pending_q);
    ucs_mpool_cleanup(&self->tx.mp, 0);
    UCS_STATS_NODE_FREE(self->stats);
    uct_um_urma_leave(self);
}

UCS_CLASS_DEFINE(uct_um_urma_iface_t, uct_ub_iface_t);

static UCS_CLASS_DEFINE_NEW_FUNC(uct_um_urma_iface_t, uct_iface_t, uct_md_h, uct_worker_h, const uct_iface_params_t*,
                                 const uct_iface_config_t*);

static UCS_CLASS_DEFINE_DELETE_FUNC(uct_um_urma_iface_t, uct_iface_t);

static ucs_status_t uct_um_urma_query_tl_devices(uct_md_h md, uct_tl_device_resource_t **tl_devices_p,
                                                 unsigned *num_tl_devices_p)
{
    uct_ub_md_t *ub_md = ucs_derived_of(md, uct_ub_md_t);
    int flags = 0;

    return uct_ub_device_query_ports(&ub_md->dev, flags, tl_devices_p, num_tl_devices_p);
}

UCT_TL_DEFINE(&uct_ub_component, um_urma, uct_um_urma_query_tl_devices, uct_um_urma_iface_t,
              "UM_URMA_", uct_um_urma_iface_config_table, uct_um_urma_iface_config_t);
