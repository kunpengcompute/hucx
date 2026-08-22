/**
* Copyright (C) Huawei Technologies Co., Ltd. 2026. ALL RIGHTS RESERVED.
* Description: Data plane interface
* See file LICENSE for terms.
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <ucs/time/time.h>
#include <uct/ub/base/ub_md.h>
#include <uct/api/uct.h>
#include <uct/base/uct_iov.inl>
#include <uct/base/uct_iface.h>
#include <ucs/arch/bitops.h>
#include <ucs/arch/cpu.h>
#include <ucs/type/class.h>
#include <ucs/type/cpu_set.h>
#include <ucs/debug/log.h>
#include <ucs/sys/string.h>
#include <stdint.h>
#include <infiniband/verbs.h>
#include <unistd.h>
#include "um_urma_ep.h"
#include "um_urma_iface.h"
#include "um_urma_def.h"
#include "um_urma_inl.h"

#define UCT_UM_URMA_SLOW_TIMER_MAX_TICK(_iface)  ((_iface)->config.peer_timeout / 3)
static void uct_um_urma_peer_name(uct_um_urma_peer_name_t *peer)
{
    ucs_strncpy_zero(peer->name, ucs_get_host_name(), sizeof(peer->name));
    peer->pid = getpid();
}

static void uct_um_urma_ep_set_state(uct_um_urma_ep_t *ep, uint32_t state)
{
    ep->flags |= state;
}

static void uct_um_urma_ep_resend_start(uct_um_urma_iface_t *iface, uct_um_urma_ep_t *ep)
{
    ep->resend.max_psn = ep->tx.psn - 1;
    ep->resend.psn     = ep->tx.acked_psn + 1;
    ep->resend.pos     = ucs_queue_iter_begin(&ep->tx.window);
    uct_um_urma_ep_ctl_op_add(iface, ep, UCT_UM_URMA_EP_OP_RESEND);
}

static void uct_um_urma_ep_resend_end(uct_um_urma_ep_t *ep)
{
    uct_um_urma_ep_ctl_op_del(ep, UCT_UM_URMA_EP_OP_RESEND);
    ep->flags &= ~UCT_UM_URMA_EP_FLAG_TX_NACKED;
}

static void uct_um_urma_ep_resend_ack(uct_um_urma_iface_t *iface, uct_um_urma_ep_t *ep)
{
    if (ucs_likely(UCT_UM_URMA_PSN_COMPARE(ep->resend.psn, >, ep->resend.max_psn))) {
        return;
    }

    if (UCT_UM_URMA_PSN_COMPARE(ep->tx.acked_psn, <, ep->resend.max_psn)) {
        if (UCT_UM_URMA_PSN_COMPARE(ep->resend.psn, <=, ep->tx.acked_psn)) {
            ucs_debug("Ep: ack received during resend resend.psn=%d tx.acked_psn=%d.",
                      ep->resend.psn, ep->tx.acked_psn);
            ep->resend.pos = ucs_queue_iter_begin(&ep->tx.window);
            ep->resend.psn = ep->tx.acked_psn + 1;
        }
        uct_um_urma_ep_ctl_op_add(iface, ep, UCT_UM_URMA_EP_OP_RESEND);
    } else {
        ep->resend.psn = ep->resend.max_psn + 1;
        uct_um_urma_ep_resend_end(ep);
    }
}

static void uct_um_urma_ep_ca_drop(uct_um_urma_ep_t *ep)
{
    ucs_debug("Ep ca drop@cwnd = %d in flight: %d.",
              ep->ca.cwnd, (int)ep->tx.psn - (int)ep->tx.acked_psn - 1);
    ep->ca.cwnd /= UCT_UM_URMA_CA_MD_FACTOR;
    if (ep->ca.cwnd < UCT_UM_URMA_CA_MIN_WINDOW) {
        ep->ca.cwnd = UCT_UM_URMA_CA_MIN_WINDOW;
    }
    ep->tx.max_psn    = ep->tx.acked_psn + ep->ca.cwnd;
    if (UCT_UM_URMA_PSN_COMPARE(ep->tx.max_psn, >, ep->tx.psn)) {
        uct_um_urma_ep_tx_stop(ep);
    }
}

static UCS_F_ALWAYS_INLINE void uct_um_urma_neth_init_data(uct_um_urma_ep_t *ep, uct_um_urma_neth_t *neth)
{
    neth->psn = ep->tx.psn;
    neth->ack_psn = ep->rx.acked_psn = ucs_frag_list_sn(&ep->rx.ooo_pkts);
}

static UCS_F_ALWAYS_INLINE int uct_um_urma_ep_ctl_op_check(uct_um_urma_ep_t *ep, uint32_t op)
{
    return ep->tx.pending.ops & op;
}

static UCS_F_ALWAYS_INLINE void uct_um_urma_ep_reset_max_psn(uct_um_urma_ep_t *ep)
{
    ep->tx.max_psn = ep->tx.psn + ep->ca.cwnd;
}

#if UCS_ENABLE_ASSERT
static UCS_F_ALWAYS_INLINE int uct_um_urma_ep_has_pending(uct_um_urma_ep_t *ep)
{
    return !ucs_arbiter_group_is_empty(&ep->tx.pending.group) &&
           !ucs_arbiter_elem_is_only(&ep->tx.pending.elem);
}
#endif

static UCS_F_ALWAYS_INLINE void uct_um_urma_neth_set_type_am(uct_um_urma_ep_t *ep, uct_um_urma_neth_t *neth, uint8_t id)
{
    neth->packet_type = (id << UCT_UM_URMA_PACKET_AM_ID_SHIFT) | ep->super.dep_id | UCT_UM_URMA_PACKET_FLAG_AM;
}

static UCS_F_ALWAYS_INLINE int uct_um_urma_ep_req_ack(uct_um_urma_ep_t *ep)
{
    return UCT_UM_URMA_PSN_COMPARE(ep->tx.psn, ==, ((ep->tx.acked_psn * 3 + ep->tx.max_psn) >> 2)) ||
           UCT_UM_URMA_PSN_COMPARE(ep->tx.psn + 1, ==, ep->tx.max_psn) ||
           uct_um_urma_ep_ctl_op_check(ep, UCT_UM_URMA_EP_OP_ACK_REQ);
}

static UCS_F_ALWAYS_INLINE void uct_um_urma_neth_ack_req(uct_um_urma_ep_t *ep, uct_um_urma_neth_t *neth)
{
    neth->packet_type |= uct_um_urma_ep_req_ack(ep) << UCT_UM_URMA_PACKET_ACK_REQ_SHIFT;
    uct_um_urma_ep_ctl_op_del(ep, UCT_UM_URMA_EP_OP_ACK | UCT_UM_URMA_EP_OP_ACK_REQ);
}

static uct_um_urma_send_skb_t *uct_um_urma_iface_get_tx_skb(uct_um_urma_iface_t *iface, uct_um_urma_ep_t *ep)
{
    uct_um_urma_send_skb_t *skb = NULL;

    if (ucs_unlikely(!uct_um_urma_iface_can_tx(iface))) {
        UCS_STATS_UPDATE_COUNTER(ep->super.super.stats, UCT_EP_STAT_NO_RES, 1);
        return NULL;
    }

    skb = iface->tx.skb;
    if (ucs_unlikely(skb == NULL)) {
        skb = ucs_mpool_get(&iface->tx.mp);
        if (skb == NULL) {
            ucs_trace_data("Iface out of tx skbs.");
            UCT_TL_IFACE_STAT_TX_NO_DESC(&iface->super.super);
            return NULL;
        }
        iface->tx.skb = skb;
    }

    VALGRIND_MAKE_MEM_DEFINED(&skb->tseg, sizeof(*skb->tseg));
    skb->flags = 0;
    ucs_read_prefetch(skb->neth);

    return skb;
}

static UCS_F_ALWAYS_INLINE uint32_t uct_um_urma_neth_get_am_id(uct_um_urma_neth_t *neth)
{
    return neth->packet_type >> UCT_UM_URMA_PACKET_AM_ID_SHIFT;
}

static UCS_F_ALWAYS_INLINE void uct_um_urma_ep_rx_put(uct_um_urma_neth_t *neth, unsigned byte_len)
{
    uct_um_urma_put_hdr_t *put_hdr = NULL;

    if (byte_len <= sizeof(*neth) + sizeof(*put_hdr)){
        ucs_warn("UM_URMA rx_put byte_len %u too short", byte_len);
        return;
    }
    put_hdr = (uct_um_urma_put_hdr_t *)(neth + 1);
    memcpy((void *)put_hdr->rva, put_hdr + 1, byte_len - sizeof(*neth) - sizeof(*put_hdr));
}

static UCS_F_ALWAYS_INLINE void uct_um_urma_ep_ca_ack(uct_um_urma_ep_t *ep)
{
    if (ep->ca.cwnd < ep->ca.wmax) {
        ep->ca.cwnd += UCT_UM_URMA_CA_AI_VALUE;
    }
    ep->tx.max_psn = ep->tx.acked_psn + ep->ca.cwnd;
}

static void uct_um_urma_ep_rx_ctl_drop_packet(uct_um_urma_ep_t *ep, uct_um_urma_neth_t *neth,
                                              uint16_t exp_flags, const char *packet_type_str)
{
    ucs_trace_data("Ep: drop %s with psn %u, head_sn %u", packet_type_str, neth->psn, ep->rx.ooo_pkts.head_sn);
    ucs_assertv_always(ep->flags & exp_flags,
                       "Conn_sn=%d ep_id=%d, dep_id=%d head_sn=%u"
                       " Neth_psn=%u ep_flags=0x%x exp_ep_flags=0x%x"
                       " Ctl_ops=0x%x rx_creq_count=%d",
                       ep->conn_sn, ep->super.ep_id, ep->super.dep_id, ep->rx.ooo_pkts.head_sn,
                       neth->psn, ep->flags, exp_flags,
                       ep->tx.pending.ops, ep->rx_creq_count);
}

static uct_um_urma_send_skb_t *uct_um_urma_ep_get_tx_skb(uct_um_urma_iface_t *iface, uct_um_urma_ep_t *ep)
{
    if (ucs_unlikely(!uct_um_urma_ep_is_connected_and_no_pending(ep) ||
                     uct_um_urma_ep_no_window(ep) ||
                     uct_um_urma_iface_has_pending_async_ev(iface))) {
        UCS_STATS_UPDATE_COUNTER(ep->super.super.stats, UCT_EP_STAT_NO_RES, 1);
        return NULL;
    }

    return uct_um_urma_iface_get_tx_skb(iface, ep);
}

static ucs_status_t uct_um_urma_am_skb_common(uct_um_urma_iface_t *iface, uct_um_urma_ep_t *ep,
                                              uint8_t id, uct_um_urma_send_skb_t **skb_p)
{
    uct_um_urma_send_skb_t *skb = NULL;
    uct_um_urma_neth_t *neth = NULL;

    UCT_CHECK_AM_ID(id);

    skb = uct_um_urma_ep_get_tx_skb(iface, ep);
    if (!skb) {
        return UCS_ERR_NO_RESOURCE;
    }

    ucs_assertv((ep->flags & UCT_UM_URMA_EP_FLAG_IN_PENDING) ||
                !uct_um_urma_ep_has_pending(ep),
                "Out-of-order send detected for ep am %d ep_pending %d",
                id, (ep->flags & UCT_UM_URMA_EP_FLAG_IN_PENDING));

    neth = skb->neth;
    uct_um_urma_neth_init_data(ep, neth);
    uct_um_urma_neth_set_type_am(ep, neth, id);
    uct_um_urma_neth_ack_req(ep, neth);

    *skb_p = skb;

    return UCS_OK;
}

static void uct_um_urma_iface_complete_tx(uct_um_urma_iface_t *iface, uct_um_urma_ep_t *ep,
                                          uct_um_urma_send_skb_t *skb, int has_data, void *data,
                                          const void *buffer, unsigned length)
{
    ucs_time_t now = uct_um_urma_iface_get_time(iface);
    iface->tx.skb = ucs_mpool_get(&iface->tx.mp);
    if (ucs_unlikely(iface->tx.skb == NULL)) {
        ucs_warn("Failed to allocate iface->tx.skb");
    }
    ep->tx.psn++;

    if (has_data) {
        skb->len += length;
        memcpy(data, buffer, length);
    }

    ucs_queue_push(&ep->tx.window, &skb->queue);
    ep->tx.tick = iface->tx.tick;
    if (!iface->async.disable) {
        ucs_wtimer_add(&iface->tx.timer, &ep->timer,
                       now - ucs_twheel_get_time(&iface->tx.timer) + ep->tx.tick);
    }

    ep->tx.send_time = now;
}

static UCS_F_ALWAYS_INLINE void uct_um_urma_iface_complete_tx_skb(uct_um_urma_iface_t *iface, uct_um_urma_ep_t *ep,
                                                                  uct_um_urma_send_skb_t *skb)
{
    uct_um_urma_iface_complete_tx(iface, ep, skb, 0, NULL, NULL, 0);
}

static UCS_F_ALWAYS_INLINE void uct_um_urma_iface_complete_tx_inl(uct_um_urma_iface_t *iface, uct_um_urma_ep_t *ep,
                                                                  uct_um_urma_send_skb_t *skb, void *data,
                                                                  const void *buffer, unsigned length)
{
    uct_um_urma_iface_complete_tx(iface, ep, skb, 1, data, buffer, length);
}

static void uct_um_urma_post_send(uct_um_urma_iface_t *iface, uct_um_urma_ep_t *ep,
                                         urma_jfs_wr_t *wr, unsigned send_flags)
{
    urma_jfs_wr_t *bad_wr = NULL;
    urma_status_t ret;

    if ((send_flags & UCT_UM_URMA_IFACE_SEND_CTL_FLAG_SIGNALED) ||
        iface->tx.unsignaled >= iface->super.config.tx_jfc_moderation - 1) {
        wr->flag.bs.complete_enable = URMA_COMPLETE_ENABLE;
        wr->user_ctx = iface->tx.unsignaled;
        iface->tx.unsignaled = 0;
        ep->super.status.uncompltes = 0;
    } else {
        wr->flag.bs.complete_enable = URMA_COMPLETE_DISABLE;
        ++iface->tx.unsignaled;
        ep->super.status.uncompltes++;
    }

    wr->flag.bs.inline_flag = (send_flags & UCT_UM_URMA_IFACE_SEND_CTL_FLAG_INLINE) ? URMA_INLINE_ENABLE:
                              URMA_INLINE_DISABLE;
    wr->flag.bs.solicited_enable = (send_flags & UCT_UM_URMA_IFACE_SEND_CTL_FLAG_SOLICITED) ? URMA_SOLICITED_ENABLE:
                                   URMA_SOLICITED_DISABLE;
    wr->tjetty = ep->peer_address.tjetty;

    ret = urma_post_jetty_send_wr(iface->um_urma_jetty.jetty, wr, &bad_wr);
    if (ret != URMA_SUCCESS) {
        (void)memset(wr, 0, sizeof(urma_jfs_wr_t));
        ucs_fatal("Post send return %d (%m).", ret);
    }

    ++ep->super.status.pi;
    --iface->um_urma_jetty.available;
    --iface->super.tx.jfc_available;
    ++iface->tx.send_sn;
}

static void uct_um_urma_ep_tx_inlv(uct_um_urma_iface_t *iface, uct_um_urma_ep_t *ep,
                                   const void *buffer, unsigned length)
{
    iface->tx.sge[1].len = length;
    iface->tx.sge[1].addr = (uintptr_t)buffer;
    ucs_assert(iface->tx.wr_inl.send.src.num_sge == 2);
    uct_um_urma_post_send(iface, ep, &iface->tx.wr_inl, UCT_UM_URMA_IFACE_SEND_CTL_FLAG_INLINE);
}


static void uct_um_urma_ep_tx_skb(uct_um_urma_iface_t *iface, uct_um_urma_ep_t *ep,
                                  uct_um_urma_send_skb_t *skb, unsigned send_flags)
{
    iface->tx.sge[0].tseg = skb->tseg;
    iface->tx.sge[0].len = skb->len;
    iface->tx.sge[0].addr = (uintptr_t)skb->neth;
    uct_um_urma_post_send(iface, ep, &iface->tx.wr_skb, send_flags);
}

ucs_status_t uct_um_urma_ep_am_short(uct_ep_h tl_ep, uint8_t id, uint64_t hdr, const void *buffer, unsigned length)
{
    uct_um_urma_iface_t *iface = ucs_derived_of(tl_ep->iface, uct_um_urma_iface_t);
    uct_um_urma_ep_t *ep = ucs_derived_of(tl_ep, uct_um_urma_ep_t);
    uct_um_urma_am_short_hdr_t *am_hdr = NULL;
    uct_um_urma_send_skb_t *skb = NULL;
    ucs_status_t ret = UCS_OK;

    UCT_CHECK_LENGTH(sizeof(uct_um_urma_neth_t) + sizeof(uct_um_urma_am_short_hdr_t) + length, 0,
                     iface->super.config.max_inline, "am_short");

    uct_um_urma_enter(iface);

    ret = uct_um_urma_am_skb_common(iface, ep, id, &skb);
    if (ret != UCS_OK) {
        uct_um_urma_leave(iface);
        return UCS_ERR_NO_RESOURCE;
    }

    am_hdr = (uct_um_urma_am_short_hdr_t *)(skb->neth + 1);
    am_hdr->hdr = hdr;
    iface->tx.sge[0].len = sizeof(uct_um_urma_neth_t) + sizeof(*am_hdr);
    iface->tx.sge[0].addr = (uintptr_t)skb->neth;

    uct_um_urma_ep_tx_inlv(iface, ep, buffer, length);
    skb->len = iface->tx.sge[0].len;

    uct_um_urma_iface_complete_tx_inl(iface, ep, skb, am_hdr + 1, buffer, length);
    UCT_TL_EP_STAT_OP(&ep->super.super, AM, SHORT, sizeof(hdr) + length);
    uct_um_urma_leave(iface);

    return UCS_OK;
}

ssize_t uct_um_urma_ep_am_bcopy(uct_ep_h tl_ep, uint8_t id, uct_pack_callback_t pack_cb, void *arg, unsigned flags)
{
    uct_um_urma_iface_t *iface = ucs_derived_of(tl_ep->iface, uct_um_urma_iface_t);
    uct_um_urma_ep_t *ep = ucs_derived_of(tl_ep, uct_um_urma_ep_t);
    uct_um_urma_send_skb_t *skb = NULL;
    ucs_status_t ret = UCS_OK;
    uint32_t payload_len;

    uct_um_urma_enter(iface);

    ret = uct_um_urma_am_skb_common(iface, ep, id, &skb);
    if (ret != UCS_OK) {
        uct_um_urma_leave(iface);
        return UCS_ERR_NO_RESOURCE;
    }

    payload_len = pack_cb(skb->neth + 1, arg);
    skb->len = sizeof(skb->neth[0]) + payload_len;
    UCT_CHECK_LENGTH(skb->len, 0, uct_ub_mtu_value(iface->super.config.mtu), "am_bcopy");

    ucs_assert(iface->tx.wr_skb.send.src.num_sge == 1);

    uct_um_urma_ep_tx_skb(iface, ep, skb, 0);
    uct_um_urma_iface_complete_tx_skb(iface, ep, skb);
    UCT_TL_EP_STAT_OP(&ep->super.super, AM, BCOPY, payload_len);
    uct_um_urma_leave(iface);

    return payload_len;
}


static UCS_F_ALWAYS_INLINE uct_um_urma_zcopy_desc_t *uct_um_urma_get_zcopy_desc(uct_um_urma_send_skb_t *skb)
{
    ucs_assert(skb->flags & UCT_UM_URMA_SEND_SKB_FLAG_ZCOPY);
    ucs_assert(!(skb->flags & UCT_UM_URMA_SEND_SKB_FLAG_INVALID));

    return (uct_um_urma_zcopy_desc_t*)((char*)skb->neth + skb->len);
}

static void uct_um_urma_skb_set_zcopy_desc(uct_um_urma_send_skb_t *skb, const uct_iov_t *iov,
                                           size_t iovcnt, uct_completion_t *comp)
{
    uct_um_urma_zcopy_desc_t *zdesc = NULL;
    uct_um_urma_iov_t *um_urma_iov = NULL;
    size_t iov_it_length;
    size_t iov_it;

    skb->flags |= UCT_UM_URMA_SEND_SKB_FLAG_ZCOPY;
    zdesc = uct_um_urma_get_zcopy_desc(skb);
    zdesc->iovcnt = 0;

    for (iov_it = 0; iov_it < iovcnt; ++iov_it) {
        iov_it_length = uct_iov_get_length(iov + iov_it);
        if (iov_it_length == 0) {
            continue;
        }

        ucs_assert(iov_it_length <= UINT16_MAX);

        um_urma_iov = &zdesc->iov[zdesc->iovcnt++];
        um_urma_iov->buffer = iov[iov_it].buffer;
        um_urma_iov->tseg = uct_ub_memh_get_tseg(iov[iov_it].memh);
        um_urma_iov->length = iov_it_length;
    }

    if (comp != NULL) {
        skb->flags |= UCT_UM_URMA_SEND_SKB_FLAG_COMP;
        zdesc->super.comp = comp;
    }
}

static uct_um_urma_send_skb_t *uct_um_urma_iface_ctl_skb_get(uct_um_urma_iface_t *iface)
{
    uct_um_urma_send_skb_t *skb = NULL;

    skb = ucs_mpool_get(&iface->tx.mp);
    if (skb == NULL) {
        ucs_fatal("Failed to allocate control skb.");
    }

    VALGRIND_MAKE_MEM_DEFINED(&skb->tseg, sizeof(*skb->tseg));
    skb->flags = 0;

    return skb;
}

static UCS_F_ALWAYS_INLINE void uct_um_urma_iface_add_ctl_desc(uct_um_urma_iface_t *iface,
                                                               uct_um_urma_ctl_desc_t *cdesc)
{
    ucs_queue_push(&iface->tx.outstanding_q, &cdesc->queue);
}


ucs_status_t uct_um_urma_ep_am_zcopy(uct_ep_h tl_ep, uint8_t id, const void *header, unsigned header_length,
                                     const uct_iov_t *iov, size_t iovcnt, unsigned flags, uct_completion_t *comp)
{
    uct_um_urma_iface_t *iface = ucs_derived_of(tl_ep->iface, uct_um_urma_iface_t);
    uct_um_urma_ep_t *ep = ucs_derived_of(tl_ep, uct_um_urma_ep_t);
    uct_um_urma_send_skb_t *skb = NULL;
    ucs_status_t ret = UCS_OK;

    UCT_CHECK_IOV_SIZE(iovcnt, iface->super.config.max_send_sge - 1, "uct_um_urma_ep_am_zcopy");

    UCT_CHECK_LENGTH(sizeof(uct_um_urma_neth_t) + sizeof(uct_um_urma_zcopy_desc_t) + header_length,
                     0, iface->super.config.seg_size, "am_zcopy header");

    uct_um_urma_enter(iface);

    ret = uct_um_urma_am_skb_common(iface, ep, id, &skb);
    if (ret != UCS_OK) {
        uct_um_urma_leave(iface);
        return UCS_ERR_NO_RESOURCE;
    }

    skb->neth->packet_type |= UCT_UM_URMA_PACKET_FLAG_ACK_REQ;
    memcpy(skb->neth + 1, header, header_length);
    skb->len = sizeof(uct_um_urma_neth_t) + header_length;

    iface->tx.wr_skb.send.src.num_sge = uct_ub_sge_fill_iov(iface->tx.sge + 1, iov, iovcnt) + 1;

    uct_um_urma_ep_tx_skb(iface, ep, skb, 0);
    iface->tx.wr_skb.send.src.num_sge = 1;

    uct_um_urma_skb_set_zcopy_desc(skb, iov, iovcnt, comp);
    uct_um_urma_iface_complete_tx_skb(iface, ep, skb);
    UCT_TL_EP_STAT_OP(&ep->super.super, AM, ZCOPY, header_length + uct_iov_total_length(iov, iovcnt));
    uct_um_urma_leave(iface);

    return UCS_INPROGRESS;
}

ucs_status_t uct_um_urma_ep_put_short(uct_ep_h tl_ep, const void *payload, unsigned length, uint64_t remote_addr,
                                      uct_rkey_t rkey)
{
    uct_um_urma_iface_t *iface = ucs_derived_of(tl_ep->iface, uct_um_urma_iface_t);
    uct_um_urma_ep_t *ep = ucs_derived_of(tl_ep, uct_um_urma_ep_t);
    uct_um_urma_put_hdr_t *put_hdr = NULL;
    uct_um_urma_send_skb_t *skb = NULL;
    uct_um_urma_neth_t *neth = NULL;

    UCT_CHECK_LENGTH(sizeof(uct_um_urma_neth_t) + sizeof(uct_um_urma_put_hdr_t) + length,
                     0, iface->super.config.max_inline, "put_short");

    uct_um_urma_enter(iface);

    skb = uct_um_urma_ep_get_tx_skb(iface, ep);
    if (!skb) {
        uct_um_urma_leave(iface);
        return UCS_ERR_NO_RESOURCE;
    }

    neth = skb->neth;
    uct_um_urma_neth_init_data(ep, neth);
    neth->packet_type = ep->super.dep_id | UCT_UM_URMA_PACKET_FLAG_PUT;
    uct_um_urma_neth_ack_req(ep, neth);

    put_hdr = (uct_um_urma_put_hdr_t *)(skb->neth + 1);
    put_hdr->rva = remote_addr;
    iface->tx.sge[0].addr = (uintptr_t)neth;
    iface->tx.sge[0].len = sizeof(*neth) + sizeof(*put_hdr);

    uct_um_urma_ep_tx_inlv(iface, ep, payload, length);

    skb->len = iface->tx.sge[0].len;
    uct_um_urma_iface_complete_tx_inl(iface, ep, skb, put_hdr + 1, payload, length);
    UCT_TL_EP_STAT_OP(&ep->super.super, PUT, SHORT, length);
    uct_um_urma_leave(iface);

    return UCS_OK;
}

ucs_status_t uct_um_urma_ep_pending_add(uct_ep_h tl_ep, uct_pending_req_t *req, unsigned flags)
{
    uct_um_urma_ep_t *ep = ucs_derived_of(tl_ep, uct_um_urma_ep_t);
    uct_um_urma_iface_t *iface = ucs_derived_of(ep->super.super.super.iface, uct_um_urma_iface_t);

    uct_um_urma_enter(iface);

    if (ucs_unlikely(uct_um_urma_iface_has_pending_async_ev(iface))) {
        goto add_req;
    }

    if (uct_um_urma_iface_can_tx(iface) &&
        uct_um_urma_iface_has_skbs(iface) &&
        uct_um_urma_ep_is_connected_and_no_pending(ep) &&
        !uct_um_urma_ep_no_window(ep)) {
        uct_um_urma_leave(iface);
        return UCS_ERR_BUSY;
    }

add_req:
    UCS_STATIC_ASSERT(sizeof(uct_um_urma_pending_req_priv_t) <= UCT_PENDING_REQ_PRIV_LEN);

    uct_um_urma_pending_req_priv(req)->flags = flags;

    ep->flags |= UCT_UM_URMA_EP_FLAG_HAS_PENDING;
    uct_pending_req_arb_group_push(&ep->tx.pending.group, req);

    ucs_arbiter_group_schedule(&iface->tx.pending_q, &ep->tx.pending.group);

    uct_um_urma_leave(iface);
    return UCS_OK;
}

static UCS_F_ALWAYS_INLINE int uct_um_urma_ep_is_last_pending_elem(uct_um_urma_ep_t *ep, ucs_arbiter_elem_t *elem)
{
    return ((ucs_arbiter_elem_is_only(elem)) || ((elem->next == &ep->tx.pending.elem) &&
            (ucs_arbiter_group_tail(&ep->tx.pending.group) == &ep->tx.pending.elem)));
}

static ucs_arbiter_cb_result_t uct_um_urma_ep_arbiter_purge_cb(ucs_arbiter_t *arbiter, ucs_arbiter_group_t *group,
                                                               ucs_arbiter_elem_t *elem, void *arg)
{
    uct_um_urma_ep_t *ep = ucs_container_of(group, uct_um_urma_ep_t, tx.pending.group);
    uct_pending_req_t *req = ucs_container_of(elem, uct_pending_req_t, priv);
    uct_purge_cb_args_t *cb_args = arg;
    uct_pending_purge_callback_t cb = cb_args->cb;
    int is_last_pending_elem;

    if (&ep->tx.pending.elem == elem) {
        return UCS_ARBITER_CB_RESULT_REMOVE_ELEM;
    }

    if (cb) {
        cb(req, cb_args->arg);
    } else {
        ucs_debug("Ep cancelling user pending request.");
    }

    is_last_pending_elem = uct_um_urma_ep_is_last_pending_elem(ep, elem);
    if (is_last_pending_elem) {
        uct_um_urma_ep_remove_has_pending_flag(ep);
    }

    return UCS_ARBITER_CB_RESULT_REMOVE_ELEM;
}

void uct_um_urma_ep_pending_purge(uct_ep_h tl_ep, uct_pending_purge_callback_t cb, void *arg)
{
    uct_um_urma_ep_t *ep = ucs_derived_of(tl_ep, uct_um_urma_ep_t);
    uct_um_urma_iface_t *iface = ucs_derived_of(ep->super.super.super.iface, uct_um_urma_iface_t);
    ucs_arbiter_group_t *group = &ep->tx.pending.group;
    uct_purge_cb_args_t args = { cb, arg };

    uct_um_urma_enter(iface);
    ucs_arbiter_group_purge(&iface->tx.pending_q, group, uct_um_urma_ep_arbiter_purge_cb, &args);
    if (uct_um_urma_ep_ctl_op_isany(ep)) {
        uct_um_urma_ep_ctl_op_schedule(iface, ep);
    }
    uct_um_urma_leave(iface);
}

ucs_status_t uct_um_urma_ep_get_address(uct_ep_h tl_ep, uct_ep_addr_t *addr)
{
    uct_um_urma_ep_t *ep = ucs_derived_of(tl_ep, uct_um_urma_ep_t);
    uct_um_urma_iface_t *iface = ucs_derived_of(ep->super.super.super.iface, uct_um_urma_iface_t);
    uct_ub_um_urma_ep_addr_t *ep_addr = (uct_ub_um_urma_ep_addr_t *)addr;

    ep_addr->iface_addr.jetty_id = iface->um_urma_jetty.jetty->jetty_id.id;
    ep_addr->iface_addr.trans_mode = iface->um_urma_jetty.jetty->jetty_cfg.jfs_cfg.trans_mode;
    ep_addr->ep_id = ep->super.ep_id;

    return UCS_OK;
}

static void uct_um_urma_ep_reset(uct_um_urma_ep_t *ep)
{
    ep->tx.psn         = UCT_UM_URMA_INITIAL_PSN;
    ep->ca.cwnd        = UCT_UM_URMA_CA_MIN_WINDOW;
    ep->ca.wmax        = ucs_derived_of(ep->super.super.super.iface, uct_um_urma_iface_t)->config.max_window;
    ep->tx.acked_psn   = UCT_UM_URMA_INITIAL_PSN - 1;
    ep->tx.pending.ops = UCT_UM_URMA_EP_OP_NONE;
    ep->tx.max_psn     = ep->tx.psn + ep->ca.cwnd;
    uct_um_urma_ep_reset_max_psn(ep);
    ucs_queue_head_init(&ep->tx.window);

    ep->resend.pos       = ucs_queue_iter_begin(&ep->tx.window);
    ep->resend.psn       = ep->tx.psn;
    ep->resend.max_psn   = ep->tx.acked_psn;
    ep->tx.resend_count  = 0;
    ep->rx_creq_count    = 0;

    ep->rx.acked_psn     = UCT_UM_URMA_INITIAL_PSN - 1;
    ucs_frag_list_init(ep->tx.psn - 1, &ep->rx.ooo_pkts, 0 /*TODO: ooo support */
                       UCS_STATS_ARG(ep->super.super.stats));
}

static ucs_status_t uct_um_urma_ep_free_by_timeout(uct_um_urma_ep_t *ep,
                                                   uct_um_urma_iface_t *iface)
{
    uct_um_urma_iface_ops_t *ops = NULL;
    ucs_time_t diff;

    diff = ucs_twheel_get_time(&iface->tx.timer) - ep->close_time;
    if (diff > iface->config.peer_timeout) {
        ucs_debug("Ur_urma_ep is destroyed after %fs with timeout %fs.", ucs_time_to_sec(diff),
                  ucs_time_to_sec(iface->config.peer_timeout));
        ops = ucs_derived_of(iface->ops, uct_um_urma_iface_ops_t);
        ops->ep_free(&ep->super.super.super);
        return UCS_OK;
    }
    return UCS_INPROGRESS;
}

static ucs_status_t uct_um_urma_ep_connect_to_iface(uct_um_urma_ep_t *ep)
{
    uct_um_urma_iface_t *iface = ucs_derived_of(ep->super.super.super.iface, uct_um_urma_iface_t);
    uct_ub_device_t UCS_V_UNUSED *dev = uct_ub_iface_device(&iface->super);

    ucs_frag_list_cleanup(&ep->rx.ooo_pkts);
    uct_um_urma_ep_reset(ep);

    return UCS_OK;
}

static ucs_status_t uct_um_urma_ep_disconnect_from_iface(uct_ep_h tl_ep)
{
    uct_um_urma_ep_t *ep = ucs_derived_of(tl_ep, uct_um_urma_ep_t);

    ucs_frag_list_cleanup(&ep->rx.ooo_pkts);
    uct_um_urma_ep_reset(ep);

    ep->super.dep_id = UCT_UM_URMA_EP_NULL_ID;
    ep->flags &= ~UCT_UM_URMA_EP_FLAG_CONNECTED;

    return UCS_OK;
}

void uct_um_urma_rjetty_get_peer_address(uct_um_urma_iface_t *iface, const uct_ub_address_t *dev_addr,
                                         const uct_um_urma_iface_addr_t *if_addr, void *address_p)
{
    uct_um_urma_peer_address_t *peer_address = (uct_um_urma_peer_address_t*)address_p;
    uct_ub_address_pack_params_t ubdev_addr_param = {0};
    urma_rjetty_t rjetty = {0};
    khiter_t kh_iter;

    uct_ub_address_unpack(dev_addr, &ubdev_addr_param);
    rjetty.jetty_id.eid   = ubdev_addr_param.eid;
    rjetty.jetty_id.uasid = ubdev_addr_param.uasid;
    rjetty.jetty_id.id    = if_addr->jetty_id;
    rjetty.trans_mode     = if_addr->trans_mode;
    rjetty.type           = URMA_JETTY;
    rjetty.tp_type        = URMA_UTP;
    kh_iter = kh_get(uct_um_urma_tjetty, &iface->peer_addr_hash, rjetty);
    if (kh_iter != kh_end(&iface->peer_addr_hash)) {
        *peer_address = kh_value(&iface->peer_addr_hash, kh_iter);
    }
}

ucs_status_t uct_um_urma_iface_unpack_peer_address(uct_um_urma_iface_t *iface, const uct_ub_address_t *dev_addr,
                                                   const uct_um_urma_iface_addr_t *if_addr, void *address_p)
{
    uct_um_urma_peer_address_t *peer_address = (uct_um_urma_peer_address_t*)address_p;
    uct_ub_address_pack_params_t ubdev_addr_param = {0};
    uct_ub_md_t *ub_md = uct_ub_iface_md(&iface->super);
    urma_rjetty_t rjetty = {0};
    urma_token_t token = {0};
    khiter_t kh_iter;
    int kh_ret;

    uct_ub_address_unpack(dev_addr, &ubdev_addr_param);
    rjetty.jetty_id.eid   = ubdev_addr_param.eid;
    rjetty.jetty_id.uasid = ubdev_addr_param.uasid;
    rjetty.jetty_id.id    = if_addr->jetty_id;
    rjetty.trans_mode     = if_addr->trans_mode;
    rjetty.type           = URMA_JETTY;
    rjetty.tp_type        = URMA_UTP;
    kh_iter = kh_get(uct_um_urma_tjetty, &iface->peer_addr_hash, rjetty);
    if (kh_iter == kh_end(&iface->peer_addr_hash)) {
        kh_iter = kh_put(uct_um_urma_tjetty, &iface->peer_addr_hash, rjetty, &kh_ret);
        if (kh_iter == kh_end(&iface->peer_addr_hash)) {
            ucs_error("Put hash error, eid="EID_FMT", uasid=%u, jetty_id=%u.",
                      EID_ARGS(rjetty.jetty_id.eid), rjetty.jetty_id.uasid, rjetty.jetty_id.id);
            return UCS_ERR_NO_MEMORY;
        }

        token.token = ub_md->dev.token.token;
        peer_address->tjetty_id = if_addr->jetty_id;
        peer_address->tjetty = urma_import_jetty(ub_md->dev.urma_ctx, &rjetty, &token);
        if (peer_address->tjetty == NULL) {
            ucs_error("Failed to urma_import_jetty, eid="EID_FMT", uasid=%u, jetty_id=%u.",
                      EID_ARGS(rjetty.jetty_id.eid), rjetty.jetty_id.uasid, rjetty.jetty_id.id);
            return UCS_ERR_IO_ERROR;
        }

        kh_value(&iface->peer_addr_hash, kh_iter) = *peer_address;
    } else {
        *peer_address = kh_value(&iface->peer_addr_hash, kh_iter);
    }

    return UCS_OK;
}

static uct_um_urma_ep_t *uct_um_urma_ep_create_passive(uct_um_urma_iface_t *iface, uct_um_urma_ctl_hdr_t *ctl)
{
    uct_ep_params_t params = {0};
    uct_um_urma_ep_t *ep = NULL;
    uct_ep_t *ep_h = NULL;
    ucs_status_t status;

    /* create new endpoint */
    params.field_mask = UCT_EP_PARAM_FIELD_IFACE;
    params.iface = &iface->super.super.super;
    status = uct_ep_create(&params, &ep_h);
    ucs_assert_always(status == UCS_OK);
    ep = ucs_derived_of(ep_h, uct_um_urma_ep_t);

    status = uct_ep_connect_to_ep(ep_h, (void*)uct_um_urma_creq_ub_addr(ctl), (void*)&ctl->conn_req.ep_addr);
    ucs_assert_always(status == UCS_OK);

    uct_um_urma_ep_set_state(ep, UCT_UM_URMA_EP_FLAG_PRIVATE);

    ep->conn_sn = ctl->conn_req.conn_sn;
    uct_um_urma_iface_cep_insert_ep(iface, uct_um_urma_creq_ub_addr(ctl), &ctl->conn_req.ep_addr.iface_addr,
                                    ctl->conn_req.conn_sn, ep);

    return ep;
}

static void uct_um_urma_ep_set_dest_ep_id(uct_um_urma_ep_t *ep, uint32_t dest_id)
{
    ucs_assert(dest_id != UCT_UM_URMA_EP_NULL_ID);
    if (dest_id >= UCT_UB_IFACE_MAX_EPS) {
        ucs_fatal("Invalid dest ep_id:%u received", dest_id);
    }
    ep->super.dep_id = dest_id;
    ep->flags |= UCT_UM_URMA_EP_FLAG_CONNECTED;
}

uct_um_urma_send_skb_t *uct_um_urma_ep_prepare_creq(uct_um_urma_ep_t *ep)
{
    uct_um_urma_iface_t *iface = ucs_derived_of(ep->super.super.super.iface, uct_um_urma_iface_t);
    uct_ub_address_pack_params_t ubdev_addr_param = {0};
    const uct_ub_address_t *ub_addr = NULL;
    uct_um_urma_send_skb_t *skb = NULL;
    uct_um_urma_ctl_hdr_t *creq = NULL;
    uct_um_urma_neth_t *neth = NULL;
    uct_device_addr_t *addr = NULL;

    ucs_assert_always(ep->super.dep_id == UCT_UM_URMA_EP_NULL_ID);
    ucs_assert_always(ep->super.ep_id != UCT_UM_URMA_EP_NULL_ID);

    skb = uct_um_urma_iface_get_tx_skb(iface, ep);
    if (!skb) {
        return NULL;
    }

    neth = skb->neth;
    neth->psn = ep->tx.psn;
    neth->ack_psn = ep->rx.acked_psn = ucs_frag_list_sn(&ep->rx.ooo_pkts);
    neth->packet_type  = UCT_UM_URMA_EP_NULL_ID;
    neth->packet_type |= UCT_UM_URMA_PACKET_FLAG_CTL;

    creq = (uct_um_urma_ctl_hdr_t *)(neth + 1);
    creq->type = UCT_UM_URMA_PACKET_CREQ;
    creq->conn_req.conn_sn = ep->conn_sn;
    uct_um_urma_ep_get_address(&ep->super.super.super, (void*)&creq->conn_req.ep_addr);
    uct_um_urma_peer_name(ucs_unaligned_ptr(&creq->peer));

    addr = (uct_device_addr_t *)uct_um_urma_creq_ub_addr(creq);
    uct_ub_iface_get_device_address(&iface->super.super.super, addr);

    ub_addr = (const uct_ub_address_t *)addr;
    uct_ub_address_unpack(ub_addr, &ubdev_addr_param);

    skb->len = sizeof(*neth) + sizeof(*creq) + iface->super.addr_size;

    return skb;
}

static uct_um_urma_send_skb_t *uct_um_urma_ep_prepare_crep(uct_um_urma_ep_t *ep)
{
    uct_um_urma_iface_t *iface = ucs_derived_of(ep->super.super.super.iface, uct_um_urma_iface_t);
    uct_um_urma_send_skb_t *skb = NULL;
    uct_um_urma_ctl_hdr_t *crep = NULL;
    uct_um_urma_neth_t *neth = NULL;

    ucs_assert_always(ep->super.dep_id != UCT_UM_URMA_EP_NULL_ID);
    ucs_assert_always(ep->super.ep_id != UCT_UM_URMA_EP_NULL_ID);

    skb = uct_um_urma_iface_get_tx_skb(iface, ep);
    if (!skb) {
        return NULL;
    }

    neth = skb->neth;
    neth->psn = ep->tx.psn;
    neth->ack_psn = ep->rx.acked_psn = ucs_frag_list_sn(&ep->rx.ooo_pkts);
    neth->packet_type = ep->super.dep_id | UCT_UM_URMA_PACKET_FLAG_ACK_REQ | UCT_UM_URMA_PACKET_FLAG_CTL;

    crep = (uct_um_urma_ctl_hdr_t *)(neth + 1);
    crep->type = UCT_UM_URMA_PACKET_CREP;
    crep->conn_rep.src_ep_id = ep->super.ep_id;
    uct_um_urma_peer_name(ucs_unaligned_ptr(&crep->peer));

    skb->len = sizeof(*neth) + sizeof(*crep);

    return skb;
}

static uint16_t uct_um_urma_ep_send_ctl(uct_um_urma_ep_t *ep, uct_um_urma_send_skb_t *skb,
                                        const uct_um_urma_iov_t *iov, uint16_t iovcnt, int flags)
{
    uct_um_urma_iface_t *iface = ucs_derived_of(ep->super.super.super.iface, uct_um_urma_iface_t);
    uint16_t iov_index;

    if ((skb->len <= iface->super.config.max_inline) && iovcnt == 0) {
        flags |= UCT_UM_URMA_IFACE_SEND_CTL_FLAG_INLINE;
    }

    for (iov_index = 0; iov_index < iovcnt; ++iov_index) {
        iface->tx.sge[iov_index + 1].addr = (uint64_t)iov[iov_index].buffer;
        iface->tx.sge[iov_index + 1].len = iov[iov_index].length;
        iface->tx.sge[iov_index + 1].tseg = iov[iov_index].tseg;
    }

    iface->tx.wr_skb.send.src.num_sge = iovcnt + 1;

    uct_um_urma_ep_tx_skb(iface, ep, skb, flags);
    iface->tx.wr_skb.send.src.num_sge = 1;

    return iface->tx.send_sn;
}

static void uct_um_urma_ep_send_creq_crep(uct_um_urma_iface_t *iface, uct_um_urma_ep_t *ep, uct_um_urma_send_skb_t *skb)
{
    uct_um_urma_ep_send_ctl(ep, skb, NULL, 0, UCT_UM_URMA_IFACE_SEND_CTL_FLAG_SOLICITED);

    uct_um_urma_iface_complete_tx_skb(iface, ep, skb);
}

void uct_um_urma_ep_deal_creq(uct_um_urma_iface_t *iface, uct_um_urma_neth_t *neth)
{
    uct_ub_address_pack_params_t ubdev_addr_param = {0};
    uct_um_urma_ctl_hdr_t *creq = NULL;
    uct_ub_address_t *addr = NULL;
    uct_um_urma_ep_t *ep = NULL;

    creq = (uct_um_urma_ctl_hdr_t *)(neth + 1);
    addr = (uct_ub_address_t *)(creq + 1);
    uct_ub_address_unpack(addr, &ubdev_addr_param);

    ucs_assert_always(creq->type == UCT_UM_URMA_PACKET_CREQ);

    ep = uct_um_urma_iface_cep_get_ep(iface, addr, &creq->conn_req.ep_addr.iface_addr, creq->conn_req.conn_sn, 0);
    if (ep == NULL) {
        ep = uct_um_urma_ep_create_passive(iface, creq);
        ucs_assert_always(ep != NULL);
        ep->rx.ooo_pkts.head_sn = neth->psn;
        uct_um_urma_ep_ctl_op_add(iface, ep, UCT_UM_URMA_EP_OP_CREP);
    } else if (ep->super.dep_id == UCT_UM_URMA_EP_NULL_ID) {
        uct_um_urma_ep_set_dest_ep_id(ep, creq->conn_req.ep_addr.ep_id);
        ep->rx.ooo_pkts.head_sn = neth->psn;
        if (UCT_UM_URMA_PSN_COMPARE(ep->tx.psn, >, UCT_UM_URMA_INITIAL_PSN)) {
            uct_um_urma_ep_process_ack(iface, ep, UCT_UM_URMA_INITIAL_PSN, 0);
        }
        uct_um_urma_ep_ctl_op_add(iface, ep, UCT_UM_URMA_EP_OP_CREP);
    }

    if (UCT_UM_URMA_PSN_COMPARE(neth->psn, <, ep->rx.ooo_pkts.head_sn)) {
        uct_um_urma_ep_rx_ctl_drop_packet(ep, neth, UCT_UM_URMA_EP_FLAG_CREQ_RCVD | UCT_UM_URMA_EP_FLAG_CREP_RCVD,
                                          "CREQ");
        return;
    }

    if (uct_um_urma_ep_ctl_op_check(ep, UCT_UM_URMA_EP_OP_CREQ)) {
        uct_um_urma_ep_set_state(ep, UCT_UM_URMA_EP_FLAG_CREQ_NOTSENT);
    }

    uct_um_urma_ep_ctl_op_del(ep, UCT_UM_URMA_EP_OP_CREQ);
    uct_um_urma_ep_set_state(ep, UCT_UM_URMA_EP_FLAG_CREQ_RCVD);
}

void uct_um_urma_ep_deal_crep(uct_um_urma_iface_t *iface, uct_um_urma_ep_t *ep, uct_um_urma_neth_t *neth)
{
    uct_um_urma_ctl_hdr_t *ctl = (uct_um_urma_ctl_hdr_t*)(neth + 1);

    ucs_assert_always(ctl->type == UCT_UM_URMA_PACKET_CREP);

    if (uct_um_urma_ep_is_connected(ep)) {
        ucs_assertv_always(ep->super.dep_id == ctl->conn_rep.src_ep_id,
                           "Ep [id=%d dest_ep_id=%d flags=0x%x] "
                           "Crep [dst_ep_id=%d src_ep_id=%d]",
                           ep->super.ep_id, ep->super.dep_id, ep->flags,
                           uct_um_urma_neth_get_dest_id(neth), ctl->conn_rep.src_ep_id);
    }

    if (UCT_UM_URMA_PSN_COMPARE(neth->psn, <, ep->rx.ooo_pkts.head_sn)) {
        uct_um_urma_ep_rx_ctl_drop_packet(ep, neth, UCT_UM_URMA_EP_FLAG_CREP_RCVD, "CREP");
        return;
    }

    ep->rx.ooo_pkts.head_sn = neth->psn;
    uct_um_urma_ep_set_dest_ep_id(ep, ctl->conn_rep.src_ep_id);
    ucs_arbiter_group_schedule(&iface->tx.pending_q, &ep->tx.pending.group);
    memcpy(&ep->peer, &ctl->peer, sizeof(ctl->peer));
    uct_um_urma_ep_set_state(ep, UCT_UM_URMA_EP_FLAG_CREP_RCVD);
}

void uct_um_urma_ep_deal_am(uct_um_urma_iface_t *iface, uct_um_urma_neth_t *neth,
                            unsigned byte_len, uct_ub_iface_recv_desc_t *desc)
{
    uint32_t am_id = neth->packet_type >> UCT_UM_URMA_PACKET_AM_ID_SHIFT;
    void *udesc = (char*)desc + iface->super.config.rx_headroom_offset;
    ucs_status_t status;

    if (uct_um_urma_neth_get_dest_id(neth) == UCT_UM_URMA_EP_NULL_ID) {
        return;
    }

    status = uct_iface_invoke_am(&iface->super.super, am_id, neth + 1,  byte_len - sizeof(uct_um_urma_neth_t),
                                 UCT_CB_PARAM_FLAG_DESC);
    if (ucs_unlikely(status == UCS_INPROGRESS)) {
        uct_recv_desc(udesc) = &iface->super.release_desc;
    } else {
        ucs_mpool_put_inline(desc);
    }
}

static void uct_um_urma_ep_resend(uct_um_urma_ep_t *ep)
{
    uct_um_urma_iface_t *iface = ucs_derived_of(ep->super.super.super.iface, uct_um_urma_iface_t);
    size_t max_len_without_nack = sizeof(uct_um_urma_neth_t) + sizeof(uct_um_urma_ctl_hdr_t) +
                                  iface->super.addr_size;
    uct_um_urma_send_skb_t *sent_skb = NULL;
    uct_um_urma_zcopy_desc_t *zdesc = NULL;
    uct_um_urma_ctl_desc_t *cdesc = NULL;
    uct_um_urma_send_skb_t *skb = NULL;
    ucs_queue_iter_t resend_pos = NULL;
    uct_um_urma_iov_t skb_iov = {0};
    uct_um_urma_iov_t *iov = NULL;
    uint16_t iovcnt = 0;

    if (UCT_UM_URMA_PSN_COMPARE(ep->resend.max_psn, <=, ep->tx.acked_psn)) {
        uct_um_urma_ep_resend_end(ep);
        return;
    }

    /* check window */
    resend_pos = ep->resend.pos;
    if (ucs_queue_iter_end(&ep->tx.window, resend_pos)) {
        uct_um_urma_ep_resend_end(ep);
        return;
    }

    sent_skb = ucs_queue_iter_elem(sent_skb, resend_pos, queue);
    ucs_assert(((uintptr_t)sent_skb % UCT_UM_URMA_SKB_ALIGN) == 0);
    if (UCT_UM_URMA_PSN_COMPARE(sent_skb->neth->psn, >=, ep->tx.max_psn)) {
        ucs_debug("Ep: out of window(psn=%d/max_psn=%d) - can not resend more.",
                  sent_skb ? sent_skb->neth->psn : -1, ep->tx.max_psn);
        uct_um_urma_ep_resend_end(ep);
        return;
    }

    if (!(ep->flags & UCT_UM_URMA_EP_FLAG_TX_NACKED) && (sent_skb->len > max_len_without_nack)) {
        uct_um_urma_ep_resend_end(ep);
        return;
    }

    ep->resend.pos = ucs_queue_iter_next(resend_pos);

    if (sent_skb->flags & UCT_UM_URMA_SEND_SKB_FLAG_RESENDING) {
        ucs_debug("Ep: skb already being resent.");
        return ;
    }

    if ((uct_um_urma_neth_get_dest_id(sent_skb->neth) == UCT_UM_URMA_EP_NULL_ID) &&
        !(sent_skb->neth->packet_type & UCT_UM_URMA_PACKET_FLAG_CTL)) {
        return ;
    }

    ucs_assertv_always(!(uct_um_urma_ep_is_connected(ep) &&
                       (uct_um_urma_neth_get_dest_id(sent_skb->neth) == UCT_UM_URMA_EP_NULL_ID) &&
                       !(sent_skb->neth->packet_type & UCT_UM_URMA_PACKET_FLAG_AM)),
                       "Ep: CREQ resend on endpoint which is already connected.");

    skb = uct_um_urma_iface_ctl_skb_get(iface);
    skb->flags = UCT_UM_URMA_SEND_SKB_FLAG_CTL_RESEND;
    sent_skb->flags |= UCT_UM_URMA_SEND_SKB_FLAG_RESENDING;
    ep->resend.psn = sent_skb->neth->psn;
    ep->tx.resend_time = uct_um_urma_iface_get_time(iface);

    if (sent_skb->flags & UCT_UM_URMA_SEND_SKB_FLAG_ZCOPY) {
        skb->len = sent_skb->len;
        zdesc = uct_um_urma_get_zcopy_desc(sent_skb);
        iov = zdesc->iov;
        iovcnt = zdesc->iovcnt;
    } else {
        ucs_assert(sent_skb->len >= sizeof(uct_um_urma_neth_t));
        skb->len = sizeof(uct_um_urma_neth_t);
        skb_iov.buffer = UCS_PTR_BYTE_OFFSET(sent_skb->neth, sizeof(uct_um_urma_neth_t));
        skb_iov.length = sent_skb->len - sizeof(uct_um_urma_neth_t);
        skb_iov.tseg = sent_skb->tseg;
        iov = &skb_iov;
        iovcnt = 1;
    }
    memcpy(skb->neth, sent_skb->neth, skb->len);
    skb->neth->ack_psn = ep->rx.acked_psn;
    cdesc              = uct_um_urma_get_ctl_desc(skb);
    cdesc->self_skb    = skb;
    cdesc->resent_skb  = sent_skb;
    cdesc->ep          = ep;

    if ((skb->neth->psn % UCT_UM_URMA_RESENDS_PER_ACK) == 0 ||
        UCT_UM_URMA_PSN_COMPARE(skb->neth->psn, ==, ep->tx.acked_psn + 1)) {
        skb->neth->packet_type |= UCT_UM_URMA_PACKET_FLAG_ACK_REQ;
    } else {
        skb->neth->packet_type &= ~UCT_UM_URMA_PACKET_FLAG_ACK_REQ;
    }

    if (UCT_UM_URMA_PSN_COMPARE(ep->resend.psn, ==, ep->resend.max_psn)) {
        ucs_debug("Ep: resending completed.");
        ep->resend.psn = ep->resend.max_psn + 1;
        uct_um_urma_ep_resend_end(ep);
    }

    cdesc->sn = uct_um_urma_ep_send_ctl(ep, skb, iov, iovcnt, UCT_UM_URMA_IFACE_SEND_CTL_FLAG_SIGNALED|
                                        UCT_UM_URMA_IFACE_SEND_CTL_FLAG_SOLICITED);
    uct_um_urma_iface_add_ctl_desc(iface, cdesc);
    ++ep->tx.resend_count;
}

static void uct_um_urma_ep_send_ack(uct_um_urma_iface_t *iface, uct_um_urma_ep_t *ep)
{
    uct_um_urma_ctl_desc_t *cdesc = NULL;
    uct_um_urma_send_skb_t *skb = NULL;
    int ctl_flags = 0;

    if (!uct_um_urma_ep_is_connected(ep)) {
        goto out;
    }
    if (sizeof(uct_um_urma_neth_t) <= iface->super.config.max_inline) {
        skb = ucs_alloca(sizeof(*skb) + sizeof(uct_um_urma_neth_t));
        ctl_flags |= UCT_UM_URMA_IFACE_SEND_CTL_FLAG_INLINE;
    } else {
        skb = uct_um_urma_iface_ctl_skb_get(iface);
    }
    uct_um_urma_neth_init_data(ep, skb->neth);
    skb->flags = UCT_UM_URMA_SEND_SKB_FLAG_CTL_ACK;
    skb->len = sizeof(uct_um_urma_neth_t);
    skb->neth->packet_type = ep->super.dep_id;
    if (uct_um_urma_ep_ctl_op_check(ep, UCT_UM_URMA_EP_OP_ACK_REQ)) {
        skb->neth->packet_type |= UCT_UM_URMA_PACKET_FLAG_ACK_REQ;
        if (ep->tx.tick >= iface->config.min_poke_time) {
            ctl_flags |= UCT_UM_URMA_IFACE_SEND_CTL_FLAG_SOLICITED;
        }
    }

    if (uct_um_urma_ep_ctl_op_check(ep, UCT_UM_URMA_EP_OP_NACK)) {
        skb->neth->packet_type |= UCT_UM_URMA_PACKET_FLAG_NAK;
    }
    if (ctl_flags & UCT_UM_URMA_IFACE_SEND_CTL_FLAG_INLINE) {
        uct_um_urma_ep_send_ctl(ep, skb, NULL, 0, ctl_flags);
    } else {
        cdesc = uct_um_urma_get_ctl_desc(skb);
        cdesc->sn = uct_um_urma_ep_send_ctl(ep, skb, NULL, 0, ctl_flags);
        cdesc->self_skb = skb;
        cdesc->resent_skb = NULL;
        cdesc->ep = NULL;
        uct_um_urma_iface_add_ctl_desc(iface, cdesc);
    }

out:
    uct_um_urma_ep_ctl_op_del(ep, UCT_UM_URMA_EP_OP_CTL_ACK);
}

static void uct_um_urma_ep_do_pending_ctl(uct_um_urma_ep_t *ep, uct_um_urma_iface_t *iface)
{
    uct_um_urma_send_skb_t *skb = NULL;

    if (ep->tx.pending.ops & UCT_UM_URMA_EP_OP_CREQ) {
        skb = uct_um_urma_ep_prepare_creq(ep);
        if (skb) {
            uct_um_urma_ep_ctl_op_del(ep, UCT_UM_URMA_EP_OP_CREQ);
            uct_um_urma_ep_send_creq_crep(iface, ep, skb);
        }
        goto out;
    }

    if (ep->tx.pending.ops & UCT_UM_URMA_EP_OP_CREP) {
        skb = uct_um_urma_ep_prepare_crep(ep);
        if (skb) {
            uct_um_urma_ep_ctl_op_del(ep, UCT_UM_URMA_EP_OP_CREP);
            uct_um_urma_ep_send_creq_crep(iface, ep, skb);
        }
        goto out;
    }

    if (ep->tx.pending.ops & UCT_UM_URMA_EP_OP_RESEND) {
        uct_um_urma_ep_resend(ep);
        goto out;
    }

    if (ep->tx.pending.ops & UCT_UM_URMA_EP_OP_CTL_ACK) {
        uct_um_urma_ep_send_ack(iface, ep);
        goto out;
    }
    ucs_assertv(!uct_um_urma_ep_ctl_op_isany(ep), "Unsupported pending op mask: %x", ep->tx.pending.ops);
out:
    return;
}

static inline ucs_arbiter_cb_result_t uct_um_urma_ep_ctl_op_next(uct_um_urma_ep_t *ep)
{
    if (uct_um_urma_ep_ctl_op_isany(ep)) {
        /* can send more control - come here later */
        return UCS_ARBITER_CB_RESULT_NEXT_GROUP;
    }
    /* no more control - nothing to do in
     * this dispatch cycle. */
    return UCS_ARBITER_CB_RESULT_RESCHED_GROUP;
}

ucs_arbiter_cb_result_t uct_um_urma_ep_do_pending(ucs_arbiter_t *arbiter, ucs_arbiter_group_t *group,
                                                  ucs_arbiter_elem_t *elem, void *arg)
{
    uct_um_urma_iface_t *iface = ucs_container_of(arbiter, uct_um_urma_iface_t, tx.pending_q);
    uct_um_urma_ep_t *ep = ucs_container_of(group, uct_um_urma_ep_t, tx.pending.group);
    intptr_t in_async_progress = (uintptr_t)arg;
    uct_pending_req_t *req = NULL;
    int async_before_pending;
    int is_last_pending_elem;
    ucs_status_t status;
    int allow_callback;

    if (!uct_um_urma_iface_can_tx(iface)) {
        return UCS_ARBITER_CB_RESULT_STOP;
    }

    if (!uct_um_urma_iface_has_skbs(iface) && !uct_um_urma_ep_ctl_op_isany(ep)) {
        return UCS_ARBITER_CB_RESULT_STOP;
    }

    if (!uct_um_urma_ep_ctl_op_isany(ep) &&
        (!uct_um_urma_ep_is_connected(ep) ||
        uct_um_urma_ep_no_window(ep))) {
        return UCS_ARBITER_CB_RESULT_DESCHED_GROUP;
    }

    if (&ep->tx.pending.elem == elem) {
        uct_um_urma_ep_do_pending_ctl(ep, iface);
        if (uct_um_urma_ep_ctl_op_isany(ep)) {
            /* there is still some ctl left. go to next group */
            return UCS_ARBITER_CB_RESULT_NEXT_GROUP;
        } else {
            /* no more ctl - dummy elem can be removed */
            return UCS_ARBITER_CB_RESULT_REMOVE_ELEM;
        }
    }
    req = ucs_container_of(elem, uct_pending_req_t, priv);
    allow_callback = !in_async_progress ||
                     (uct_um_urma_pending_req_priv(req)->flags & UCT_CB_FLAG_ASYNC);
    if (allow_callback && !uct_um_urma_ep_ctl_op_check(ep, UCT_UM_URMA_EP_OP_CTL_HI_PRIO)) {
        ucs_assert(!(ep->flags & UCT_UM_URMA_EP_FLAG_IN_PENDING));
        ep->flags |= UCT_UM_URMA_EP_FLAG_IN_PENDING;
        async_before_pending = iface->tx.async_before_pending;
        if (uct_um_urma_pending_req_priv(req)->flags & UCT_CB_FLAG_ASYNC) {
            /* temporary reset the flag to unblock sends from async context */
            iface->tx.async_before_pending = 0;
        }
        /* temporary reset `UCT_UM_URMA_EP_HAS_PENDING` flag to unblock sends */
        uct_um_urma_ep_remove_has_pending_flag(ep);

        is_last_pending_elem = uct_um_urma_ep_is_last_pending_elem(ep, elem);

        status = req->func(req);
#if UCS_ENABLE_ASSERT
            /* do not touch the request (or the arbiter element) after
             * calling the callback if UCS_OK is returned from the callback */
        if (status == UCS_OK) {
            req  = NULL;
            elem = NULL;
        }
#endif
        uct_um_urma_ep_set_has_pending_flag(ep);
        iface->tx.async_before_pending = async_before_pending;
        ep->flags &= ~UCT_UM_URMA_EP_FLAG_IN_PENDING;

        if (status == UCS_INPROGRESS) {
            return UCS_ARBITER_CB_RESULT_NEXT_GROUP;
        } else if (status != UCS_OK) {
            /* avoid deadlock: send low priority ctl if user cb failed
             * no need to check for low prio here because we
             * already checked above.
             */
            uct_um_urma_ep_do_pending_ctl(ep, iface);
            return uct_um_urma_ep_ctl_op_next(ep);
        }

        if (is_last_pending_elem) {
            uct_um_urma_ep_remove_has_pending_flag(ep);
        }

        return UCS_ARBITER_CB_RESULT_REMOVE_ELEM;
    }
    /* try to send ctl messages */
    uct_um_urma_ep_do_pending_ctl(ep, iface);
    if (in_async_progress) {
        return uct_um_urma_ep_ctl_op_next(ep);
    } else {
        /* we still didn't process the current pending request because of hi-prio
         * control messages, so cannot stop sending yet. If we stop, not all
         * resources will be exhausted and out-of-order with pending can occur.
         * (pending control ops may be cleared by uct_um_urma_ep_do_pending_ctl)
         */
        return UCS_ARBITER_CB_RESULT_NEXT_GROUP;
    }
}

ucs_status_t uct_um_urma_ep_create_connected_common(const uct_ep_params_t *ep_params, uct_ep_h *new_ep_p)
{
    uct_um_urma_iface_t *iface = ucs_derived_of(ep_params->iface, uct_um_urma_iface_t);
    const uct_um_urma_iface_addr_t *if_addr = NULL;
    uct_um_urma_peer_address_t address_p = {0};
    const uct_ub_address_t *dev_addr = NULL;
    uct_um_urma_send_skb_t *skb = NULL;
    uct_um_urma_ep_conn_sn_t conn_sn;
    uct_ep_params_t params = {};
    uct_um_urma_ep_t *ep = NULL;
    uct_ep_h new_ep_h = NULL;
    ucs_status_t status;
    ucs_trace_func("");

    uct_um_urma_enter(iface);

    UCT_EP_PARAMS_CHECK_DEV_IFACE_ADDRS(ep_params);
    dev_addr = (const uct_ub_address_t *)ep_params->dev_addr;
    if_addr = (const uct_um_urma_iface_addr_t *)ep_params->iface_addr;

    *new_ep_p = NULL;
    status = uct_um_urma_iface_cep_get_conn_sn(iface, dev_addr, if_addr, &conn_sn);
    if (status != UCS_OK) {
        goto out;
    }

    ep = uct_um_urma_iface_cep_get_ep(iface, dev_addr, if_addr, conn_sn, 1);
    if (ep != NULL) {
        uct_um_urma_ep_set_state(ep, UCT_UM_URMA_EP_FLAG_CREQ_NOTSENT);
        ep->flags &= ~UCT_UM_URMA_EP_FLAG_PRIVATE;
        status = UCS_OK;
        uct_um_urma_iface_cep_insert_ep(iface, dev_addr, if_addr, conn_sn, ep);
        goto out_set_ep;
    }

    params.field_mask = UCT_EP_PARAM_FIELD_IFACE;
    params.iface = &iface->super.super.super;

    status = uct_ep_create(&params, &new_ep_h);
    if (status != UCS_OK) {
        goto ep_err;
    }

    ep = ucs_derived_of(new_ep_h, uct_um_urma_ep_t);
    ep->conn_sn = conn_sn;
    status = uct_um_urma_ep_connect_to_iface(ep);
    if (status != UCS_OK) {
        goto ep_err;
    }

    uct_um_urma_iface_cep_insert_ep(iface, dev_addr, if_addr, conn_sn, ep);
    status = uct_um_urma_iface_unpack_peer_address(iface, dev_addr, if_addr, &ep->peer_address);
    if (status != UCS_OK) {
        if (ep->peer_address.tjetty != NULL) {
            if (urma_unimport_jetty(ep->peer_address.tjetty) != URMA_SUCCESS) {
                ucs_warn("Urma_unimport_jetty failed, eid="EID_FMT", uasid=%u, jetty_id=%u.",
                         EID_ARGS(ep->peer_address.tjetty->id.eid), ep->peer_address.tjetty->id.uasid, ep->peer_address.tjetty->id.id);
            }
            ep->peer_address.tjetty = NULL;
        }
        uct_um_urma_ep_disconnect_from_iface(&ep->super.super.super);
        goto out;
    }

    skb = uct_um_urma_ep_prepare_creq(ep);
    if (skb != NULL) {
        uct_um_urma_ep_send_ctl(ep, skb, NULL, 0, UCT_UM_URMA_IFACE_SEND_CTL_FLAG_SOLICITED);
        uct_um_urma_iface_complete_tx_skb(iface, ep, skb);
        uct_um_urma_ep_set_state(ep, UCT_UM_URMA_EP_FLAG_CREQ_SENT);
    } else {
        uct_um_urma_ep_ctl_op_add(iface, ep, UCT_UM_URMA_EP_OP_CREQ);
    }

out_set_ep:
    /* cppcheck-suppress autoVariables */
    *new_ep_p = &ep->super.super.super;
out:
    uct_um_urma_leave(iface);

    return status;
ep_err:
    uct_um_urma_rjetty_get_peer_address(iface, dev_addr, if_addr, &address_p);
    if (address_p.tjetty != NULL) {
        if (urma_unimport_jetty(address_p.tjetty) != URMA_SUCCESS) {
            ucs_warn("Urma_unimport_jetty failed, eid="EID_FMT", uasid=%u, jetty_id=%u.",
                     EID_ARGS(address_p.tjetty->id.eid), address_p.tjetty->id.uasid, address_p.tjetty->id.id);
        }
        address_p.tjetty = NULL;
    }
    uct_um_urma_leave(iface);

    return status;
}

ucs_status_t uct_um_urma_ep_connect_to_ep(uct_ep_h tl_ep, const uct_device_addr_t *dev_addr,
                                          const uct_ep_addr_t *uct_ep_addr)
{
    uct_um_urma_ep_t *ep = ucs_derived_of(tl_ep, uct_um_urma_ep_t);
    uct_um_urma_iface_t *iface = ucs_derived_of(ep->super.super.super.iface, uct_um_urma_iface_t);
    const uct_ub_um_urma_ep_addr_t *ep_addr = (const uct_ub_um_urma_ep_addr_t*)uct_ep_addr;
    const uct_ub_address_t *ub_addr = (const uct_ub_address_t*)dev_addr;

    ucs_assert_always(ep->super.dep_id == UCT_UM_URMA_EP_NULL_ID);
    ucs_trace_func("");

    uct_um_urma_ep_set_dest_ep_id(ep, ep_addr->ep_id);

    ucs_frag_list_cleanup(&ep->rx.ooo_pkts);
    uct_um_urma_ep_reset(ep);

    return uct_um_urma_iface_unpack_peer_address(iface, ub_addr, &ep_addr->iface_addr, (void *)&ep->peer_address);
}

ucs_status_t uct_um_urma_ep_connect_to_ep_v2(uct_ep_h tl_ep, const uct_device_addr_t *dev_addr,
                                             const uct_ep_addr_t *ep_addr,
                                             const uct_ep_connect_to_ep_params_t *param)
{
    return uct_um_urma_ep_connect_to_ep(tl_ep, dev_addr, ep_addr);
}

static UCS_F_ALWAYS_INLINE uct_um_urma_comp_desc_t *uct_um_urma_get_comp_desc(uct_um_urma_send_skb_t *skb)
{
    ucs_assert(skb->flags & UCT_UM_URMA_SEND_SKB_FLAG_COMP);
    ucs_assert(!(skb->flags & UCT_UM_URMA_SEND_SKB_FLAG_INVALID));

    return (uct_um_urma_comp_desc_t*)((char*)skb->neth + skb->len);
}

static UCS_F_ALWAYS_INLINE void uct_um_urma_iface_add_async_comp(uct_um_urma_iface_t *iface,
                                                                 uct_um_urma_send_skb_t *skb, ucs_status_t status)
{
    uct_um_urma_comp_desc_t *cdesc = uct_um_urma_get_comp_desc(skb);

    cdesc->status = status;
    ucs_queue_push(&iface->tx.async_comp_q, &skb->queue);
}

static UCS_F_ALWAYS_INLINE int uct_um_urma_skb_is_completed(uct_um_urma_send_skb_t *skb, uct_um_urma_psn_t ack_psn)
{
    ucs_assert(!(skb->flags & UCT_UM_URMA_SEND_SKB_FLAG_INVALID));

    return UCT_UM_URMA_PSN_COMPARE(skb->neth->psn, <=, ack_psn) && !(skb->flags & UCT_UM_URMA_SEND_SKB_FLAG_RESENDING);
}

static void uct_um_urma_ep_window_release_inline(uct_um_urma_iface_t *iface, uct_um_urma_ep_t *ep,
                                                 uct_um_urma_psn_t ack_psn, ucs_status_t status,
                                                 int is_async, int invalidate_resend)
{
    uct_um_urma_send_skb_t *skb = NULL;

    ucs_queue_for_each_extract(skb, &ep->tx.window, queue, uct_um_urma_skb_is_completed(skb, ack_psn)) {
        if (invalidate_resend && (ep->resend.pos == &skb->queue.next)) {
            ep->resend.pos = ucs_queue_iter_begin(&ep->tx.window);
            ep->resend.psn = ep->tx.acked_psn + 1;
        }

        if (ucs_likely(!(skb->flags & UCT_UM_URMA_SEND_SKB_FLAG_COMP))) {
            uct_um_urma_skb_release(skb, 1);
        } else if (ucs_likely(!is_async)) {
            uct_um_urma_iface_dispatch_comp(iface, uct_um_urma_get_comp_desc(skb)->comp, status);
            uct_um_urma_skb_release(skb, 1);
        } else {
            uct_um_urma_iface_add_async_comp(iface, skb, status);
        }
    }
}

void uct_um_urma_ep_process_ack(uct_um_urma_iface_t *iface, uct_um_urma_ep_t *ep,
                                uct_um_urma_psn_t ack_psn, int is_async)
{
    if (ucs_unlikely(UCT_UM_URMA_PSN_COMPARE(ack_psn, <=, ep->tx.acked_psn))) {
        return;
    }

    ep->tx.acked_psn = ack_psn;
    uct_um_urma_ep_window_release_inline(iface, ep, ack_psn, UCS_OK, is_async, 0);
    uct_um_urma_ep_ca_ack(ep);
    uct_um_urma_ep_resend_ack(iface, ep);

    ucs_arbiter_group_schedule(&iface->tx.pending_q, &ep->tx.pending.group);

    ep->tx.tick = iface->tx.tick;
    ep->tx.send_time = uct_um_urma_iface_get_time(iface);
}

void uct_um_urma_ep_window_release_completed(uct_um_urma_ep_t *ep, int is_async)
{
    uct_um_urma_iface_t *iface = ucs_derived_of(ep->super.super.super.iface, uct_um_urma_iface_t);

    uct_um_urma_ep_window_release_inline(iface, ep, ep->tx.acked_psn, UCS_OK, is_async, 1);
}

void uct_um_urma_ep_window_release(uct_um_urma_ep_t *ep, ucs_status_t status, int is_async)
{
    uct_um_urma_iface_t *iface = ucs_derived_of(ep->super.super.super.iface, uct_um_urma_iface_t);

    uct_um_urma_ep_window_release_inline(iface, ep, ep->tx.acked_psn, status, is_async, 0);
}

static void uct_um_urma_ep_purge_outstanding(uct_um_urma_ep_t *ep)
{
    uct_um_urma_iface_t *iface = ucs_derived_of(ep->super.super.super.iface, uct_um_urma_iface_t);
    uct_um_urma_ctl_desc_t *cdesc = NULL;
    ucs_queue_iter_t iter = NULL;

    ucs_queue_for_each_safe(cdesc, iter, &iface->tx.outstanding_q, queue) {
        if (cdesc->ep == ep) {
            ucs_queue_del_iter(&iface->tx.outstanding_q, iter);
            uct_um_urma_iface_ctl_skb_complete(iface, cdesc, 0);
        }
    }

    ucs_assert_always(ep->tx.resend_count == 0);
}

static void uct_um_urma_ep_purge(uct_um_urma_ep_t *ep, ucs_status_t status)
{
    uct_um_urma_ep_reset_max_psn(ep);
    uct_um_urma_ep_purge_outstanding(ep);
    ep->tx.acked_psn = (uct_um_urma_psn_t)(ep->tx.psn - 1);
    uct_um_urma_ep_window_release(ep, status, 0);
    ucs_assert(ucs_queue_is_empty(&ep->tx.window));
}

void uct_um_urma_ep_disconnect(uct_ep_h tl_ep)
{
    uct_um_urma_iface_t *iface = ucs_derived_of(tl_ep->iface, uct_um_urma_iface_t);
    uct_um_urma_ep_t *ep = ucs_derived_of(tl_ep, uct_um_urma_ep_t);

    uct_um_urma_enter(iface);

    uct_um_urma_ep_pending_purge(tl_ep, NULL, NULL);
    uct_um_urma_ep_flush(tl_ep, 0, NULL);

    ep->close_time = ucs_twheel_get_time(&iface->tx.timer);
    ep->flags |= UCT_UM_URMA_EP_FLAG_DISCONNECTED;
    ucs_wtimer_add(&iface->tx.timer, &ep->timer, UCT_UM_URMA_SLOW_TIMER_MAX_TICK(iface));
    uct_um_urma_leave(iface);
}

void uct_um_urma_ep_process_rx(uct_um_urma_iface_t *iface, uct_um_urma_neth_t *neth, unsigned byte_len,
                               uct_um_urma_recv_skb_t* skb, int is_async)
{
    ucs_frag_list_ooo_type_t ooo_type;
    uct_um_urma_ep_t *ep = NULL;
    uint32_t dest_id;
    uint32_t is_am;
    uint32_t am_id;

    dest_id = uct_um_urma_neth_get_dest_id(neth);
    am_id = uct_um_urma_neth_get_am_id(neth);
    is_am = neth->packet_type & UCT_UM_URMA_PACKET_FLAG_AM;

    if (ucs_unlikely(dest_id == UCT_UM_URMA_EP_NULL_ID)) {
        uct_um_urma_ep_deal_creq(iface, neth);
        goto out;
    } else if (ucs_unlikely(!ucs_ptr_array_lookup(&iface->super.eps, dest_id, ep) || (ep->super.ep_id != dest_id))) {
        ucs_trace("RX: failed to find ep %u, dropping packet.", dest_id);
        goto out;
    }

    ucs_assert(ep->super.ep_id != UCT_UM_URMA_EP_NULL_ID);

    uct_um_urma_ep_process_ack(iface, ep, neth->ack_psn, is_async);

    if (ucs_unlikely(neth->packet_type & UCT_UM_URMA_PACKET_FLAG_ACK_REQ)) {
        uct_um_urma_ep_ctl_op_add(iface, ep, UCT_UM_URMA_EP_OP_ACK);
        ucs_trace_data("ACK_REQ - schedule ack, head_sn=%u sn=%u", ep->rx.ooo_pkts.head_sn, neth->psn);
    }

    if (ucs_unlikely(UCT_UM_URMA_PSN_COMPARE(neth->psn, >, ep->rx.ooo_pkts.head_sn +1))) {
        uct_um_urma_ep_ctl_op_add(iface, ep, UCT_UM_URMA_EP_OP_NACK);
    }

    if (ucs_unlikely(!is_am)) {
        if (neth->packet_type & UCT_UM_URMA_PACKET_FLAG_NAK) {
            uct_um_urma_ep_set_state(ep, UCT_UM_URMA_EP_FLAG_TX_NACKED);
            goto out;
        }

        if ((size_t)byte_len == sizeof(*neth)) {
            goto out;
        }

        if (neth->packet_type & UCT_UM_URMA_PACKET_FLAG_CTL) {
            uct_um_urma_ep_deal_crep(iface, ep, neth);
            goto out;
        }
    }

    ooo_type = ucs_frag_list_insert(&ep->rx.ooo_pkts, &skb->u.ooo.elem, neth->psn);
    if (ucs_unlikely(ooo_type != UCS_FRAG_LIST_INSERT_FAST)) {
        if ((ooo_type != UCS_FRAG_LIST_INSERT_DUP) &&
            (ooo_type != UCS_FRAG_LIST_INSERT_FAIL)) {
            ucs_fatal("Out of order is not implemented: got %d.", ooo_type);
        }
        ucs_trace_data("DUP/OOB - schedule ack, head_sn=%d sn=%d.", ep->rx.ooo_pkts.head_sn, neth->psn);
        goto out;
    }

    if (ucs_unlikely(!is_am && (neth->packet_type & UCT_UM_URMA_PACKET_FLAG_PUT))) {
        uct_um_urma_ep_rx_put(neth, byte_len);
        goto out;
    }

    if (ucs_unlikely(is_async && !(iface->super.super.am[am_id].flags & UCT_CB_FLAG_ASYNC))) {
        skb->u.am.len = byte_len - sizeof(*neth);
        ucs_queue_push(&iface->rx.pending_q, &skb->u.am.queue);
    } else {
        uct_um_urma_iface_raise_pending_async_ev(iface);
        uct_um_urma_ep_deal_am(iface, neth, byte_len, &skb->super);
    }

    return;
out:
    ucs_mpool_put(skb);
}


ucs_status_t uct_um_urma_ep_put_comp(uct_um_urma_ep_t *ep, uct_um_urma_iface_t *iface, uct_completion_t *comp)
{
    uct_um_urma_send_skb_t *skb = NULL;

    if (comp == NULL) {
        return UCS_INPROGRESS;
    }

    ucs_assert(comp->count > 0);

    skb = ucs_mpool_get(&iface->tx.mp);
    if (skb == NULL) {
        return UCS_ERR_NO_RESOURCE;
    }

    skb->flags = UCT_UM_URMA_SEND_SKB_FLAG_COMP;
    skb->len = sizeof(skb->neth[0]);
    skb->neth->packet_type = 0;
    skb->neth->psn = (uct_um_urma_psn_t)(ep->tx.psn - 1);
    uct_um_urma_neth_set_dest_id(skb->neth, UCT_UM_URMA_EP_NULL_ID);
    uct_um_urma_get_comp_desc(skb)->comp = comp;

    if (!ucs_queue_is_empty(&ep->tx.window)) {
        ucs_queue_push(&ep->tx.window, &skb->queue);
    } else {
        ucs_assert(ep->tx.resend_count == 0);
        uct_um_urma_iface_add_async_comp(iface, skb, UCS_OK);
    }
    ucs_trace_data("Added dummy flush skb psn %d.", skb->neth->psn);
    return UCS_INPROGRESS;
}

ucs_status_t uct_um_urma_ep_flush_nolock(uct_um_urma_iface_t *iface, uct_um_urma_ep_t *ep,
                                         uct_completion_t *comp)
{
    uct_um_urma_send_skb_t *skb = NULL;

    if (ucs_unlikely(!uct_um_urma_ep_is_connected(ep))) {
        if (uct_um_urma_ep_ctl_op_check(ep, UCT_UM_URMA_EP_OP_CREQ) || !ucs_queue_is_empty(&ep->tx.window)) {
            return UCS_ERR_NO_RESOURCE;
        }
        return UCS_OK;
    }

    if (!uct_um_urma_iface_can_tx(iface) || !uct_um_urma_iface_has_skbs(iface) || uct_um_urma_ep_no_window(ep)) {
        return UCS_ERR_NO_RESOURCE;
    }

    if (ucs_queue_is_empty(&ep->tx.window) && ucs_queue_is_empty(&iface->tx.async_comp_q)) {
        ucs_assert(ep->tx.resend_count == 0);
        return UCS_OK;
    }

    if (uct_um_urma_ep_is_last_ack_received(ep)) {
        uct_um_urma_ep_ctl_op_del(ep, UCT_UM_URMA_EP_OP_ACK_REQ);
    } else {
        ucs_assert(!ucs_queue_is_empty(&ep->tx.window));
        skb = ucs_queue_tail_elem_non_empty(&ep->tx.window, uct_um_urma_send_skb_t, queue);
        if (!(skb->flags & UCT_UM_URMA_SEND_SKB_FLAG_ACK_REQ)) {
            ep->tx.pending.ops |= UCT_UM_URMA_EP_OP_ACK_REQ;
            if (uct_um_urma_ep_ctl_op_check_ex(ep, UCT_UM_URMA_EP_OP_ACK_REQ)) {
                uct_um_urma_ep_do_pending_ctl(ep, iface);
            }
            skb->flags |= UCT_UM_URMA_SEND_SKB_FLAG_ACK_REQ;
        }
    }

    return uct_um_urma_ep_put_comp(ep, iface, comp);
}

ucs_status_t uct_um_urma_ep_flush(uct_ep_h tl_ep, unsigned flags, uct_completion_t *comp)
{
    uct_um_urma_ep_t *ep = ucs_derived_of(tl_ep, uct_um_urma_ep_t);
    uct_um_urma_iface_t *iface = ucs_derived_of(ep->super.super.super.iface, uct_um_urma_iface_t);
    ucs_status_t status;

    uct_um_urma_enter(iface);

    if (ucs_unlikely(flags & UCT_FLUSH_FLAG_CANCEL)) {
        uct_ep_pending_purge(tl_ep, NULL, 0);
        uct_um_urma_iface_dispatch_async_comps(iface);
        uct_um_urma_ep_purge(ep, UCS_ERR_CANCELED);
        status = UCS_OK;
        goto out;
    }

    if (ucs_unlikely(uct_um_urma_iface_has_pending_async_ev(iface))) {
        status = UCS_ERR_NO_RESOURCE;
        goto out;
    }

    status = uct_um_urma_ep_flush_nolock(iface, ep, comp);
    if (status == UCS_OK) {
        UCT_TL_EP_STAT_FLUSH(&ep->super.super);
    } else if (status == UCS_INPROGRESS) {
        UCT_TL_EP_STAT_FLUSH_WAIT(&ep->super.super);
    }

out:
    uct_um_urma_leave(iface);

    return status;
}

static unsigned uct_um_urma_ep_deferred_timeout_handler(void *arg)
{
    uct_um_urma_ep_t *ep = arg;
    uct_um_urma_iface_t *iface = ucs_derived_of(ep->super.super.super.iface, uct_um_urma_iface_t);
    ucs_status_t status;

    if (ep->flags & UCT_UM_URMA_EP_FLAG_DISCONNECTED) {
        uct_um_urma_ep_purge(ep, UCS_ERR_ENDPOINT_TIMEOUT);
        return 0;
    }

    if (ep->flags & UCT_UM_URMA_EP_FLAG_PRIVATE) {
        ucs_assert(ucs_queue_is_empty(&ep->tx.window));
        uct_ep_destroy(&ep->super.super.super);
        return 0;
    }

    uct_um_urma_ep_purge(ep, UCS_ERR_ENDPOINT_TIMEOUT);

    status = uct_iface_handle_ep_err(&iface->super.super.super, &ep->super.super.super, UCS_ERR_ENDPOINT_TIMEOUT);
    if (status != UCS_OK) {
        ucs_fatal("Ur_urma endpoint to "UCT_UM_URMA_EP_PEER_NAME_FMT": "
                  "unhandled timeout error.", UCT_UM_URMA_EP_PEER_NAME_ARG(ep));
    }

    return 1;
}

static void uct_um_urma_ep_timer_backoff(uct_um_urma_ep_t *ep)
{
    uct_um_urma_iface_t *iface = ucs_derived_of(ep->super.super.super.iface, uct_um_urma_iface_t);

    ep->tx.tick = ucs_min(ep->tx.tick * iface->tx.timer_backoff, UCT_UM_URMA_SLOW_TIMER_MAX_TICK(iface));
    ucs_wtimer_add(&iface->tx.timer, &ep->timer, ep->tx.tick);
}

static void uct_um_urma_ep_timer(ucs_wtimer_t *self)
{
    uct_um_urma_ep_t *ep = ucs_container_of(self, uct_um_urma_ep_t, timer);
    uct_um_urma_iface_t *iface = ucs_derived_of(ep->super.super.super.iface, uct_um_urma_iface_t);
    ucs_time_t last_send;
    ucs_status_t status;
    ucs_time_t diff;
    ucs_time_t now;

    if (uct_um_urma_ep_is_last_ack_received(ep)) {
        if (ep->flags & UCT_UM_URMA_EP_FLAG_DISCONNECTED) {
            status = uct_um_urma_ep_free_by_timeout(ep, iface);
            if (status == UCS_INPROGRESS) {
                uct_um_urma_ep_timer_backoff(ep);
            }
        }
        return;
    }

    ucs_assert(!ucs_queue_is_empty(&ep->tx.window));

    now  = ucs_twheel_get_time(&iface->tx.timer);
    diff = now - ep->tx.send_time;
    if (diff > iface->config.peer_timeout) {
        ucs_debug("Ep: timeout of %.2f sec, config::peer_timeout - %.2f sec.", ucs_time_to_sec(diff),
                  ucs_time_to_sec(iface->config.peer_timeout));
        ucs_callbackq_add_safe(&iface->super.super.worker->super.progress_q, uct_um_urma_ep_deferred_timeout_handler,
                               ep, UCS_CALLBACKQ_FLAG_ONESHOT);
        return;
    }

    if ((ep->tx.pending.ops & (UCT_UM_URMA_EP_OP_ACK_REQ|UCT_UM_URMA_EP_OP_RESEND)) || (ep->tx.resend_count > 0)) {
        ucs_trace("Ep: resend still in progress, ops 0x%x tx_count %d.",
                  ep->tx.pending.ops, ep->tx.resend_count);
        uct_um_urma_ep_timer_backoff(ep);
        return;
    }

    last_send = ucs_max(ep->tx.send_time, ep->tx.resend_time);
    diff      = now - last_send;

    if (diff > iface->tx.tick) {
        if (diff > 3 * iface->tx.tick) {
            ucs_trace("Scheduling resend now: %lu last_send: %lu diff: %lu tick: %lu.",
                      now, last_send, diff, ep->tx.tick);
            uct_um_urma_ep_ca_drop(ep);
            uct_um_urma_ep_resend_start(iface, ep);
        }

        if (uct_um_urma_ep_is_connected(ep)) {
            uct_um_urma_ep_ctl_op_add(iface, ep, UCT_UM_URMA_EP_OP_ACK_REQ);
        }
    }

    uct_um_urma_ep_timer_backoff(ep);
}

UCS_CLASS_INIT_FUNC(uct_um_urma_ep_t, const uct_ep_params_t *params)
{
    uct_um_urma_iface_t *iface = ucs_derived_of(params->iface, uct_um_urma_iface_t);

    memset(self, 0, sizeof(*self));
    UCS_CLASS_CALL_SUPER_INIT(uct_ub_ep_t, &iface->super);

    uct_um_urma_enter(iface);

    uct_um_urma_ep_reset(self);
    self->tx.tick = iface->tx.tick;
    ucs_wtimer_init(&self->timer, uct_um_urma_ep_timer);
    ucs_arbiter_group_init(&self->tx.pending.group);
    ucs_arbiter_elem_init(&self->tx.pending.elem);

    self->super.dep_id = UCT_UM_URMA_EP_NULL_ID;
    ucs_arbiter_group_init(&self->arb_group);
    self->ka_tseg = NULL;
    self->ka_va_addr = 0;
    self->peer_address.tjetty = NULL;

    uct_um_urma_leave(iface);

    return UCS_OK;
}

static ucs_arbiter_cb_result_t uct_um_urma_ep_pending_cancel_cb(ucs_arbiter_t *arbiter, ucs_arbiter_group_t *group,
                                                                ucs_arbiter_elem_t *elem, void *arg)
{
    uct_um_urma_ep_t *ep = ucs_container_of(group, uct_um_urma_ep_t, tx.pending.group);

    if (&ep->tx.pending.elem == elem) {
        return UCS_ARBITER_CB_RESULT_REMOVE_ELEM;
    }

    if (uct_um_urma_ep_is_last_pending_elem(ep, elem)) {
        uct_um_urma_ep_remove_has_pending_flag(ep);
    }

    return UCS_ARBITER_CB_RESULT_REMOVE_ELEM;
}

static int uct_um_urma_ep_remove_timeout_filter(const ucs_callbackq_elem_t *elem, void *arg)
{
    return (elem->cb == uct_um_urma_ep_deferred_timeout_handler) && (elem->arg == arg);
}

static UCS_CLASS_CLEANUP_FUNC(uct_um_urma_ep_t)
{
    uct_um_urma_iface_t *iface = ucs_derived_of(self->super.super.super.iface, uct_um_urma_iface_t);
    uct_um_urma_enter(iface);

    iface->super.tx.jfc_available += self->super.status.pi - self->super.status.ci;
    ucs_callbackq_remove_if(&iface->super.super.worker->super.progress_q,
                            uct_um_urma_ep_remove_timeout_filter, self);
    uct_um_urma_ep_purge(self, UCS_ERR_CANCELED);

    ucs_wtimer_remove(&iface->tx.timer, &self->timer);
    uct_um_urma_iface_cep_remove_ep(iface, self);
    ucs_frag_list_cleanup(&self->rx.ooo_pkts);

    ucs_arbiter_group_purge(&iface->tx.pending_q, &self->tx.pending.group, uct_um_urma_ep_pending_cancel_cb, 0);

    if (!ucs_queue_is_empty(&self->tx.window)) {
        ucs_debug("Ep id=%d conn_sn=%d has %d unacked packets.", self->super.ep_id, self->conn_sn,
                  (int)ucs_queue_length(&self->tx.window));
    }

    ucs_arbiter_group_cleanup(&self->tx.pending.group);

    uct_um_urma_leave(iface);
}

UCS_CLASS_DEFINE(uct_um_urma_ep_t, uct_ub_ep_t);
UCS_CLASS_DEFINE_NEW_FUNC(uct_um_urma_ep_t, uct_ep_t, const uct_ep_params_t *);
UCS_CLASS_DEFINE_DELETE_FUNC(uct_um_urma_ep_t, uct_ep_t);
