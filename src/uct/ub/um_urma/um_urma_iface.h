/**
* Copyright (C) Huawei Technologies Co., Ltd. 2026. ALL RIGHTS RESERVED.
*
* See file LICENSE for terms.
*/

#ifndef UCT_UM_URMA_IFACE_H
#define UCT_UM_URMA_IFACE_H

#include <uct/api/uct.h>
#include <uct/base/uct_iface.h>
#include <uct/ub/base/ub_def.h>
#include <uct/ub/base/ub_md.h>
#include <uct/ub/base/ub_iface.h>
#include <uct/base/uct_iov.inl>
#include <ucs/async/async.h>
#include <ucs/time/timer_wheel.h>
#include <ucs/datastruct/ptr_array.h>
#include <ucs/datastruct/conn_match.h>
#include <ucs/datastruct/frag_list.h>
#include <urma_api.h>
#include "um_urma_def.h"

#define UCT_UB_MAX_IOV          8UL
#define UCT_UM_URMA_GRH_LEN     40  /* GLobal routing header */
#define UCT_UM_URMA_SKIP_SWEEP  8

typedef uint16_t                    uct_um_urma_psn_t;
typedef uint32_t                    uct_um_urma_ep_conn_sn_t;

enum {
    UCT_UM_URMA_SEND_SKB_FLAG_ACK_REQ    = UCS_BIT(0), /* ACK was requested for this skb */
    UCT_UM_URMA_SEND_SKB_FLAG_COMP       = UCS_BIT(1), /* This skb contains a completion */
    UCT_UM_URMA_SEND_SKB_FLAG_ZCOPY      = UCS_BIT(2), /* This skb contains a zero-copy segment */
    UCT_UM_URMA_SEND_SKB_FLAG_RESENDING  = UCS_BIT(3), /* An active control skb refers to this skb */

#if UCS_ENABLE_ASSERT
    UCT_UM_URMA_SEND_SKB_FLAG_CTL_ACK    = UCS_BIT(5), /* This is a control-ack skb */
    UCT_UM_URMA_SEND_SKB_FLAG_CTL_RESEND = UCS_BIT(6), /* This is a control-resend rsb */
    UCT_UM_URMA_SEND_SKB_FLAG_INVALID    = UCS_BIT(7)  /* skb is released */

#else
    UCT_UM_URMA_SEND_SKB_FLAG_CTL_ACK    = 0,
    UCT_UM_URMA_SEND_SKB_FLAG_CTL_RESEND = 0,
    UCT_UM_URMA_SEND_SKB_FLAG_INVALID    = 0
#endif
};

/* flags for uct_um_urma_iface_send_ctl() */
enum {
    UCT_UM_URMA_IFACE_SEND_CTL_FLAG_INLINE          = UCS_BIT(0),
    UCT_UM_URMA_IFACE_SEND_CTL_FLAG_SOLICITED       = UCS_BIT(1),
    UCT_UM_URMA_IFACE_SEND_CTL_FLAG_SIGNALED        = UCS_BIT(2)
};

enum {
    UCT_UM_URMA_IFACE_STAT_RX_DROP,
    UCT_UM_URMA_IFACE_STAT_LAST
};

typedef struct uct_um_urma_iface_ops {
    uct_iface_ops_t        super;
    void                   (*ep_free)(uct_ep_h ep);
} uct_um_urma_iface_ops_t;

typedef struct uct_um_urma_iface_config {
    uct_ub_iface_config_t   super;
    unsigned                tx_jfc_len;
    double                  peer_timeout;
    double                  min_poke_time;
    double                  timer_tick;
    double                  timer_backoff;
    double                  event_timer_tick;
    unsigned                max_window;
    unsigned                rx_async_max_poll;
} uct_um_urma_iface_config_t;

typedef struct uct_um_urma_jetty {
    urma_jetty_t    *jetty;
    int16_t         available;
} uct_um_urma_jetty_t;

typedef struct uct_um_urma_neth {
    uint32_t            packet_type;
    uct_um_urma_psn_t   psn;
    uct_um_urma_psn_t   ack_psn;
} UCS_S_PACKED uct_um_urma_neth_t;

typedef struct uct_um_urma_send_skb {
    ucs_queue_elem_t    queue;      /* in send window */
    urma_target_seg_t   *tseg;
    uint16_t            len;        /* data size */
    uint16_t            flags;
    uct_um_urma_neth_t  neth[0];
} UCS_S_PACKED UCS_V_ALIGNED(UCT_UM_URMA_SKB_ALIGN) uct_um_urma_send_skb_t;

typedef struct uct_um_urma_recv_skb {
    uct_ub_iface_recv_desc_t        super;
    union {
        struct {
            ucs_frag_list_elem_t    elem;
        } ooo;
        struct {
            ucs_queue_elem_t        queue;
            uint32_t                len;
        } am;
    } u;
} uct_um_urma_recv_skb_t;

typedef struct uct_um_urma_ctl_desc {
    ucs_queue_elem_t            queue;       /* Queue element in outstanding queue */
    uint16_t                    sn;          /* Sequence number in outstanding queue */
    uct_um_urma_send_skb_t      *self_skb;   /* Back-pointer to owner skb */
    uct_um_urma_send_skb_t      *resent_skb; /* For resend skb: points to a re-sent
                                               * skb in the window, can be NULL */
    uct_um_urma_ep_t            *ep;         /* For resend skb: points to the endpoint
                                               * on which the resend was made */
} uct_um_urma_ctl_desc_t;

typedef struct {
    uint32_t                          tjetty_id;
    urma_target_jetty_t               *tjetty;
} UCS_S_PACKED uct_um_urma_peer_address_t;

static UCS_F_ALWAYS_INLINE khint32_t uct_um_urma_tjetty_hash_func(urma_rjetty_t key)
{
    return kh_int_hash_func(key.jetty_id.eid.in4.addr | key.jetty_id.uasid | key.jetty_id.id);
}

static UCS_F_ALWAYS_INLINE int uct_um_urma_tjetty_hash_equal(urma_rjetty_t lh, urma_rjetty_t rh)
{
    return (lh.jetty_id.uasid == rh.jetty_id.uasid) &&
           (lh.jetty_id.id == rh.jetty_id.id) &&
           (lh.trans_mode == rh.trans_mode) &&
           (lh.jetty_id.eid.in4.addr == rh.jetty_id.eid.in4.addr);
}

KHASH_INIT(uct_um_urma_tjetty, urma_rjetty_t, uct_um_urma_peer_address_t, 1,
           uct_um_urma_tjetty_hash_func, uct_um_urma_tjetty_hash_equal)

typedef struct uct_um_urma_iface {
    uct_ub_iface_t                  super;
    uct_um_urma_jetty_t             um_urma_jetty;
    struct {
        unsigned                    available;
        unsigned                    async_max_poll;
        ucs_queue_head_t            pending_q;
    } rx;
    struct {
        uct_um_urma_send_skb_t      *skb; /* ready to use skb */
        ucs_mpool_t                 mp;   /* pool for send_op completions */
        urma_sge_t                  sge[UCT_UB_MAX_IOV];
        urma_jfs_wr_t               wr_inl;
        urma_jfs_wr_t               wr_skb;
        ucs_arbiter_t               pending_q;
        ucs_queue_head_t            outstanding_q;
        ucs_queue_head_t            async_comp_q;
        int16_t                     available;
        uint16_t                    send_sn;
        uint16_t                    comp_sn;
        uint8_t                     async_before_pending;
        ucs_twheel_t                timer;
        ucs_time_t                  tick;
        double                      timer_backoff;
        unsigned                    timer_sweep_count;
        unsigned                    unsignaled;
    } tx;
    struct {
        ucs_time_t                  tick;
        int                         timer_id;
        unsigned                    disable;
    } async;
    struct {
        unsigned                    max_inline;
        unsigned                    max_window;
        ucs_time_t                  peer_timeout;
        ucs_time_t                  min_poke_time;
    } config;
    ucs_conn_match_ctx_t            conn_match_ctx;
    uct_um_urma_iface_ops_t         *ops;
    khash_t(uct_um_urma_tjetty) peer_addr_hash;

    UCS_STATS_NODE_DECLARE(stats)
} uct_um_urma_iface_t;

typedef struct uct_um_urma_iface_addr {
    uint32_t                jetty_id;
    urma_transport_mode_t   trans_mode;
} UCS_S_PACKED uct_um_urma_iface_addr_t;

static UCS_F_ALWAYS_INLINE int uct_um_urma_iface_can_tx(uct_um_urma_iface_t *iface)
{
    return iface->super.tx.jfc_available > 0;
}

ucs_status_t uct_um_urma_iface_cep_get_conn_sn(uct_um_urma_iface_t *iface, const uct_ub_address_t *dev_addr,
                                               const uct_um_urma_iface_addr_t *if_addr,
                                               uct_um_urma_ep_conn_sn_t *conn_sn_p);

uct_um_urma_ep_t *uct_um_urma_iface_cep_get_ep(uct_um_urma_iface_t *iface, const uct_ub_address_t *dev_addr,
                                               const uct_um_urma_iface_addr_t *if_addr,
                                               uct_um_urma_ep_conn_sn_t conn_sn, int is_private);

void uct_um_urma_iface_cep_insert_ep(uct_um_urma_iface_t *iface, const uct_ub_address_t *dev_addr,
                                     const uct_um_urma_iface_addr_t *if_addr, uct_um_urma_ep_conn_sn_t conn_sn,
                                     uct_um_urma_ep_t *ep);

void uct_um_urma_iface_cep_remove_ep(uct_um_urma_iface_t *iface, uct_um_urma_ep_t *ep);

void uct_um_urma_iface_release_desc(uct_recv_desc_t *self, void *desc);

ucs_status_t uct_um_urma_iface_get_address(uct_iface_h tl_iface, uct_iface_addr_t *addr);

void uct_um_urma_iface_progress_enable(uct_iface_h tl_iface, unsigned flags);

void uct_um_urma_iface_progress_disable(uct_iface_h tl_iface, unsigned flags);

unsigned uct_um_urma_iface_dispatch_async_comps_do(uct_um_urma_iface_t *iface);

void uct_um_urma_iface_ctl_skb_complete(uct_um_urma_iface_t *iface, uct_um_urma_ctl_desc_t *cdesc, int is_async);

static UCS_F_ALWAYS_INLINE void uct_um_urma_enter(uct_um_urma_iface_t *iface)
{
    UCS_ASYNC_BLOCK(iface->super.super.worker->async);
}

static UCS_F_ALWAYS_INLINE void uct_um_urma_leave(uct_um_urma_iface_t *iface)
{
    UCS_ASYNC_UNBLOCK(iface->super.super.worker->async);
}

static UCS_F_ALWAYS_INLINE ucs_time_t uct_um_urma_iface_get_time(uct_um_urma_iface_t *iface)
{
    return ucs_get_time();
}

static UCS_F_ALWAYS_INLINE void uct_um_urma_iface_twheel_sweep(uct_um_urma_iface_t *iface)
{
    if (iface->tx.timer_sweep_count++ % UCT_UM_URMA_SKIP_SWEEP) {
        return;
    }
    if (ucs_twheel_is_empty(&iface->tx.timer)) {
        return;
    }
    ucs_twheel_sweep(&iface->tx.timer, uct_um_urma_iface_get_time(iface));
}

static UCS_F_ALWAYS_INLINE void uct_um_urma_iface_raise_pending_async_ev(uct_um_urma_iface_t *iface)
{
    if (!ucs_arbiter_is_empty(&iface->tx.pending_q)) {
        iface->tx.async_before_pending = 1;
    }
}

static UCS_F_ALWAYS_INLINE int uct_um_urma_iface_has_pending_async_ev(uct_um_urma_iface_t *iface)
{
    return iface->tx.async_before_pending;
}

static UCS_F_ALWAYS_INLINE void uct_um_urma_iface_dispatch_comp(uct_um_urma_iface_t *iface, uct_completion_t *comp,
                                                                ucs_status_t status)
{
    uct_um_urma_iface_raise_pending_async_ev(iface);
    uct_invoke_completion(comp, status);
}

static UCS_F_ALWAYS_INLINE unsigned uct_um_urma_iface_dispatch_async_comps(uct_um_urma_iface_t *iface)
{
    if (ucs_likely(ucs_queue_is_empty(&iface->tx.async_comp_q))) {
        return 0;
    }

    return uct_um_urma_iface_dispatch_async_comps_do(iface);
}

#endif
