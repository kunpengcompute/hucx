/**
* Copyright (C) Huawei Technologies Co., Ltd. 2026. ALL RIGHTS RESERVED.
*
* See file LICENSE for terms.
*/

#ifndef UCT_RC_URMA_IFACE_H
#define UCT_RC_URMA_IFACE_H

#include <uct/api/uct.h>
#include <uct/base/uct_iface.h>
#include <uct/urma/base/urma_def.h>
#include <uct/urma/base/urma_md.h>
#include <uct/urma/base/urma_iface.h>
#include <uct/base/uct_iov.inl>
#include <ucs/datastruct/ptr_array.h>

#include <urma_api.h>

typedef struct uct_rc_urma_iface    uct_rc_urma_iface_t;

enum {
    UCT_RC_URMA_IFACE_STAT_RX_DL_CR_ERR,
    UCT_RC_URMA_IFACE_STAT_TX_DL_CR_ERR,
    UCT_RC_URMA_IFACE_STAT_RX_FC_REQ,
    UCT_RC_URMA_IFACE_STAT_TX_FC_GRANT,
    UCT_RC_URMA_IFACE_STAT_TX_KA_SUCCESS,
    UCT_RC_URMA_IFACE_STAT_TX_KA_FAIL,
    UCT_RC_URMA_IFACE_STAT_NO_CQE,
    UCT_RC_URMA_IFACE_STAT_LAST
};

enum {
    UCT_RC_URMA_FLUSH_MODE_RDMA_WRITE_0,
    UCT_RC_URMA_FLUSH_MODE_FLOW_CONTROL,
    UCT_RC_URMA_FLUSH_MODE_AUTO,
    UCT_RC_URMA_FLUSH_MODE_LAST
};

typedef struct uct_rc_urma_pending_req {
    uct_pending_req_t super;
    uct_ep_t          *ep;
} uct_rc_urma_pending_req_t;

struct uct_rc_urma_iface {
    uct_urma_iface_t             super;
    ucs_callback_t             progress;
    uct_ub_iface_send_desc_t   *fc_desc; /* used when max_inline is zero */
    struct {
        /* Threshold to send "soft" FC credit request. The peer will try to
         * piggy-back credits grant to the counter AM, if any. */
        int16_t                fc_soft_thresh;
        uint8_t                flush_by_fc;
    } config;
    struct {
        ucs_mpool_t            pending_mp; /* pool for FC grant and keepalive pending requests */
        ucs_arbiter_t          arbiter;
#if UCS_ENABLE_ASSERT
        int                    in_pending;
#endif
    } tx;

    UCS_STATS_NODE_DECLARE(stats)
};

typedef struct uct_rc_urma_iface_config {
    uct_urma_iface_config_t      super;
    unsigned                   flush_mode;
    unsigned                   tx_jfc_len;
    double                     soft_thresh;
} uct_rc_urma_iface_config_t;

static UCS_F_ALWAYS_INLINE int
uct_rc_urma_have_jfc_avail(uct_rc_urma_iface_t* iface)
{
    return iface->super.tx.jfc_available > 0;
}

static UCS_F_ALWAYS_INLINE ucs_status_t
uct_rc_urma_iface_invoke_pending_cb(uct_rc_urma_iface_t *iface, uct_pending_req_t *req)
{
    ucs_status_t status;

    ucs_trace_data("Progressing pending request");
#if UCS_ENABLE_ASSERT
    iface->tx.in_pending = 1;
#endif

    status = req->func(req);

#if UCS_ENABLE_ASSERT
    iface->tx.in_pending = 0;
#endif
    ucs_trace_data("Status returned from progress pending: %s",
                   ucs_status_string(status));

    return status;
}

static UCS_F_ALWAYS_INLINE void
uct_rc_urma_iface_check_pending(uct_rc_urma_iface_t *iface, ucs_arbiter_group_t *arb_group)
{
    ucs_assert(iface->tx.in_pending || ucs_arbiter_group_is_empty(arb_group));
}

void uct_rc_urma_available_set(uct_rc_urma_iface_t *iface, uint8_t jfs_idx, int16_t val);
#endif
