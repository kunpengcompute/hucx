/**
* Copyright (C) Huawei Technologies Co., Ltd. 2026. ALL RIGHTS RESERVED.
*
* See file LICENSE for terms.
*/

#ifndef UCT_RC_URMA_EP_H
#define UCT_RC_URMA_EP_H

#include <uct/api/uct.h>
#include <ucs/datastruct/arbiter.h>
#include <ucs/sys/compiler_def.h>
#include <uct/base/uct_iface.h>
#include <uct/urma/base/urma_iface.h>
#include <uct/urma/base/urma_ep.h>
#include <uct/urma/rc_urma/rc_urma_iface.h>

#define RC_URMA_UNSIGNALED_INF UINT16_MAX

typedef struct uct_rc_urma_jetty {
    urma_jetty_t                    *jetty;
    int16_t                         available;
} uct_rc_urma_jetty_t;

struct uct_rc_urma_ep {
    uct_ub_ep_t                     super;
    uct_rc_urma_jetty_t             rc_urma_jetty;
    uint64_t                        ka_va_addr;
    urma_target_seg_t               *ka_tseg;
    ucs_arbiter_group_t             arb_group;
    urma_target_jetty_t             *tjetty;
    urma_tp_t                       tp;
};

typedef struct uct_rc_urma_ep_addr {
    uint32_t jetty_id;
    uint32_t tpn;
    /* Following fields appear in this order (if specified by jetty_idd).
     * - uint32_t ep_id
     * - uint64_t seg_va
     * - uint64_t seg_len
     * - uint32_t seg_flag
     * - uint32_t seg_token_id
     * - urma_transport_mode_t trans_mode;
     */
} UCS_S_PACKED uct_rc_urma_ep_addr_t;

typedef struct uct_rc_urma_ep_addr_params {
    uint32_t              jetty_id;
    uint32_t              ep_id;
    uint64_t              seg_va;
    uint64_t              seg_len;
    uint32_t              seg_flag;
    uint32_t              seg_token_id;
    urma_transport_mode_t trans_mode;
} uct_rc_urma_ep_addr_params_t;

/*
 * Auxillary AM ID bits used by FC protocol.
 */
enum {
    /* Keepalive Request scheduled: indicates that keepalive request
     * is scheduled in pending queue and no more keepalive actions
     * are needed */
    UCT_RC_URMA_EP_FLAG_KEEPALIVE_PENDING   = UCS_BIT(0),

    /* EP is connected to peer */
    UCT_RC_URMA_EP_FLAG_CONNECTED           = UCS_BIT(1),

    /* Flush cancel was executed on EP */
    UCT_RC_URMA_EP_FLAG_FLUSH_CANCEL        = UCS_BIT(2),

    /* Error handler already called or flush(CANCEL) disabled it */
    UCT_RC_URMA_EP_FLAG_ERR_HANDLER_INVOKED = UCS_BIT(3),

    /* Soft Credit Request: indicates that peer needs to piggy-back credits
     * grant to counter AM (if any). Can be bundled with
     * UCT_RC_EP_FLAG_FC_GRANT  */
    UCT_RC_URMA_EP_FLAG_FC_SOFT_REQ         = UCS_BIT(UCT_AM_ID_BITS),

    /* Hard Credit Request: indicates that wnd is close to be exhausted.
     * The peer must send separate AM with credit grant as soon as it
     * receives AM  with this bit set. Can be bundled with
     * UCT_RC_EP_FLAG_FC_GRANT */
    UCT_RC_URMA_EP_FLAG_FC_HARD_REQ         = UCS_BIT((UCT_AM_ID_BITS) + 1),

    /* Credit Grant: ep should update its FC wnd as soon as it receives AM with
     * this bit set. Can be bundled with either soft or hard request bits */
    UCT_RC_URMA_EP_FLAG_FC_GRANT            = UCS_BIT((UCT_AM_ID_BITS) + 2),

    /* Special FC AM with Credit Grant: Just an empty message indicating
     * credit grant. Can't be bundled with any other FC flag (as it consumes
     * all 3 FC bits). */
    UCT_RC_URMA_EP_FC_PURE_GRANT            = (UCT_RC_URMA_EP_FLAG_FC_HARD_REQ |
                                               UCT_RC_URMA_EP_FLAG_FC_SOFT_REQ |
                                               UCT_RC_URMA_EP_FLAG_FC_GRANT)
};

/*
 * FC protocol header mask
 */
#define UCT_RC_URMA_EP_FC_MASK UCT_RC_URMA_EP_FC_PURE_GRANT

static inline int16_t uct_rc_urma_outstanding_queue_available(uct_rc_urma_ep_t *ep)
{
    return ep->rc_urma_jetty.available;
}

static UCS_F_ALWAYS_INLINE uint8_t
uct_rc_urma_fc_req_moderation(uct_ub_fc_t *fc, uct_rc_urma_iface_t *iface)
{
    return (fc->fc_wnd == iface->super.config.fc_hard_thresh) ?
            UCT_RC_URMA_EP_FLAG_FC_HARD_REQ :
           (fc->fc_wnd == iface->config.fc_soft_thresh) ?
            UCT_RC_URMA_EP_FLAG_FC_SOFT_REQ : 0;
}

static UCS_F_ALWAYS_INLINE uint8_t uct_rc_urma_fc_get_fc_hdr(uint8_t id)
{
    return id & UCT_RC_URMA_EP_FC_MASK;
}

#define UCT_RC_URMA_CHECK_TX_QUEUE_RET(_ep, _ret) \
    if (ucs_unlikely(uct_rc_urma_outstanding_queue_available(_ep) <= 0)) { \
        UCS_STATS_UPDATE_COUNTER((_ep)->super.super.stats, UCT_EP_STAT_NO_RES, 1); \
        return _ret; \
    }

/*
 * Check for send resources
 */
#define UCT_RC_URMA_CHECK_JFC_RET(_iface, _ep, _ret) \
    /* jfc_moderation == 0 for TLs which don't support it */ \
    if (ucs_unlikely((_iface)->super.tx.jfc_available <= \
        (signed)(_iface)->super.config.tx_jfc_moderation)) { \
        if (!uct_rc_urma_have_jfc_avail(_iface)) { \
            UCS_STATS_UPDATE_COUNTER((_iface)->stats, UCT_RC_URMA_IFACE_STAT_NO_CQE, 1); \
            UCS_STATS_UPDATE_COUNTER((_ep)->super.super.stats, UCT_EP_STAT_NO_RES, 1); \
            return _ret; \
        } \
        (_ep)->super.status.uncompltes = RC_URMA_UNSIGNALED_INF; \
    }

#define UCT_RC_URMA_CHECK_RES(_iface, _ep) \
    { \
        ucs_assert(_ep->super.flags & UCT_RC_URMA_EP_FLAG_CONNECTED); \
        UCT_RC_URMA_CHECK_JFC_RET(_iface, _ep, UCS_ERR_NO_RESOURCE) \
        UCT_RC_URMA_CHECK_TX_QUEUE_RET(_ep, UCS_ERR_NO_RESOURCE) \
    }

/*
 * check for FC credits and add FC protocol bits (if any)
 */
#define UCT_RC_URMA_CHECK_FC_WND(_fc, _stats) \
    if ((_fc)->fc_wnd <= 0) { \
        UCS_STATS_UPDATE_COUNTER((_fc)->stats, UCT_UB_FC_STAT_NO_CRED, 1); \
        UCS_STATS_UPDATE_COUNTER(_stats, UCT_EP_STAT_NO_RES, 1); \
        return UCS_ERR_NO_RESOURCE; \
    } \

#define UCT_RC_URMA_CHECK_FC(_iface, _ep, _am_id) \
    { \
        if (ucs_unlikely((_ep)->super.fc.fc_wnd <= (_iface)->config.fc_soft_thresh)) { \
            if ((_iface)->super.config.fc_enabled) { \
                UCT_RC_URMA_CHECK_FC_WND(&(_ep)->super.fc, (_ep)->super.super.stats); \
                (_am_id) |= uct_rc_urma_fc_req_moderation(&(_ep)->super.fc, _iface); \
            } else { \
                /* Set fc_wnd to max, to send as much as possible without checks */ \
                (_ep)->super.fc.fc_wnd = INT16_MAX; \
            } \
        } \
        (_am_id) |= uct_rc_urma_fc_get_fc_hdr((_ep)->super.flags); /* take grant bit */ \
    }

#define UCT_RC_URMA_CHECK_RES_AND_FC(_iface, _ep, _am_id) \
    UCT_RC_URMA_CHECK_RES(_iface, _ep) \
    UCT_RC_URMA_CHECK_FC(_iface, _ep, _am_id) \
    uct_rc_urma_iface_check_pending(_iface, &(_ep)->arb_group);

#define UCT_RC_URMA_UPDATE_FC_WND(_iface, _fc) \
    { \
        /* For performance reasons, prefer to update fc_wnd unconditionally */ \
        (_fc)->fc_wnd--; \
        \
        if ((_iface)->super.config.fc_enabled) { \
            UCS_STATS_SET_COUNTER((_fc)->stats, UCT_UB_FC_STAT_FC_WND, \
                                  (_fc)->fc_wnd); \
        } \
    }

#define UCT_RC_URMA_UPDATE_FC(_iface, _ep, _fc_hdr) \
    { \
        if ((_fc_hdr) & UCT_RC_URMA_EP_FLAG_FC_GRANT) { \
            UCS_STATS_UPDATE_COUNTER((_ep)->super.fc.stats, UCT_UB_FC_STAT_TX_GRANT, 1); \
        } \
        if ((_fc_hdr) & UCT_RC_URMA_EP_FLAG_FC_SOFT_REQ) { \
            UCS_STATS_UPDATE_COUNTER((_ep)->super.fc.stats, UCT_UB_FC_STAT_TX_SOFT_REQ, 1); \
        } else if ((_fc_hdr) & UCT_RC_URMA_EP_FLAG_FC_HARD_REQ) { \
            UCS_STATS_UPDATE_COUNTER((_ep)->super.fc.stats, UCT_UB_FC_STAT_TX_HARD_REQ, 1); \
        } \
        \
        (_ep)->super.flags &= ~UCT_RC_URMA_EP_FC_MASK; \
        \
        UCT_RC_URMA_UPDATE_FC_WND(_iface, &(_ep)->super.fc) \
    }

ucs_status_t uct_rc_urma_ep_am_short(uct_ep_h tl_ep, uint8_t id, uint64_t hdr,
                                     const void *buffer, unsigned length);
ucs_status_t uct_rc_urma_ep_am_short_iov(uct_ep_h tl_ep, uint8_t id,
                                         const uct_iov_t *iov, size_t iovcnt);
ssize_t uct_rc_urma_ep_am_bcopy(uct_ep_h tl_ep, uint8_t id,
                                uct_pack_callback_t pack_cb, void *arg,
                                unsigned flags);
ucs_status_t uct_rc_urma_ep_am_zcopy(uct_ep_h tl_ep, uint8_t id, const void *header,
                                     unsigned header_length, const uct_iov_t *iov,
                                     size_t iovcnt, unsigned flags,
                                     uct_completion_t *comp);
ucs_status_t uct_rc_urma_ep_put_short(uct_ep_h tl_ep, const void *payload,
                                      unsigned length, uint64_t remote_addr,
                                      uct_rkey_t rkey);
ssize_t uct_rc_urma_ep_put_bcopy(uct_ep_h tl_ep, uct_pack_callback_t pack_cb,
                                 void *arg, uint64_t remote_addr, uct_rkey_t rkey);
ucs_status_t uct_rc_urma_ep_put_zcopy(uct_ep_h tl_ep, const uct_iov_t *iov, size_t iovcnt,
                                      uint64_t remote_addr, uct_rkey_t rkey,
                                      uct_completion_t *comp);

ucs_status_t uct_rc_urma_ep_get_bcopy(uct_ep_h tl_ep,
                                      uct_unpack_callback_t unpack_cb,
                                      void *arg, size_t length,
                                      uint64_t remote_addr, uct_rkey_t rkey,
                                      uct_completion_t *comp);
ucs_status_t uct_rc_urma_ep_get_zcopy(uct_ep_h tl_ep, const uct_iov_t *iov,
                                      size_t iovcnt, uint64_t remote_addr,
                                      uct_rkey_t rkey, uct_completion_t *comp);
ucs_status_t uct_rc_urma_ep_atomic_cswap64(uct_ep_h tl_ep, uint64_t compare, uint64_t swap,
                                           uint64_t remote_addr, uct_rkey_t rkey,
                                           uint64_t *result, uct_completion_t *comp);
ucs_status_t uct_rc_urma_ep_atomic_cswap32(uct_ep_h tl_ep, uint32_t compare, uint32_t swap,
                                           uint64_t remote_addr, uct_rkey_t rkey,
                                           uint32_t *result, uct_completion_t *comp);
ucs_status_t uct_rc_urma_ep_atomic64_post(uct_ep_h ep, unsigned opcode, uint64_t value,
                                          uint64_t remote_addr, uct_rkey_t rkey);
ucs_status_t uct_rc_urma_ep_atomic32_post(uct_ep_h ep, unsigned opcode, uint32_t value,
                                          uint64_t remote_addr, uct_rkey_t rkey);
ucs_status_t uct_rc_urma_ep_atomic64_fetch(uct_ep_h ep, uct_atomic_op_t opcode,
                                           uint64_t value, uint64_t *result,
                                           uint64_t remote_addr, uct_rkey_t rkey,
                                           uct_completion_t *comp);
ucs_status_t uct_rc_urma_ep_atomic32_fetch(uct_ep_h ep, uct_atomic_op_t opcode,
                                           uint32_t value, uint32_t *result,
                                           uint64_t remote_addr, uct_rkey_t rkey,
                                           uct_completion_t *comp);
ucs_status_t uct_rc_urma_ep_pending_add(uct_ep_h tl_ep, uct_pending_req_t *r, unsigned flags);
void uct_rc_urma_ep_pending_purge(uct_ep_h tl_ep, uct_pending_purge_callback_t cb, void *arg);
ucs_status_t uct_rc_urma_ep_flush(uct_ep_h tl_ep, unsigned flags,
                                  uct_completion_t *comp);
ucs_status_t uct_rc_urma_ep_fence(uct_ep_h tl_ep, unsigned flags);
ucs_status_t uct_rc_urma_ep_check(uct_ep_h tl_ep, unsigned flags, uct_completion_t *comp);
ucs_status_t uct_rc_urma_ep_get_address(uct_ep_h tl_ep, uct_ep_addr_t *addr);
ucs_status_t uct_rc_urma_ep_connect_to_ep(uct_ep_h tl_ep,
                                          const uct_device_addr_t *dev_addr,
                                          const uct_ep_addr_t *ep_addr);
ucs_status_t uct_rc_urma_ep_fc_grant(uct_pending_req_t *self);
ucs_arbiter_cb_result_t uct_rc_urma_ep_process_pending(ucs_arbiter_t *arbiter, ucs_arbiter_group_t *group,
                                                       ucs_arbiter_elem_t *elem, void *arg);
void uct_rc_urma_ep_txqp_purge_outstanding(uct_rc_urma_iface_t *iface, uct_ub_tx_queue_t *txqp,
                                           ucs_status_t status, uint32_t sn, int warn);

ucs_status_t uct_rc_urma_ep_connect_to_ep_v2(uct_ep_h tl_ep, const uct_device_addr_t *dev_addr,
                                             const uct_ep_addr_t *ep_addr,
                                             const uct_ep_connect_to_ep_params_t *param);

UCS_CLASS_DECLARE(uct_rc_urma_ep_t, const uct_ep_params_t *);
UCS_CLASS_DECLARE_NEW_FUNC(uct_rc_urma_ep_t, uct_ep_t, const uct_ep_params_t *);
UCS_CLASS_DECLARE_DELETE_FUNC(uct_rc_urma_ep_t, uct_ep_t);

#endif
