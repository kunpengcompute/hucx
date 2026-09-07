/**
* Copyright (C) Huawei Technologies Co., Ltd. 2026. ALL RIGHTS RESERVED.
*
* See file LICENSE for terms.
*/

#ifndef UCT_UM_URMA_EP_H
#define UCT_UM_URMA_EP_H

#include <uct/api/uct.h>
#include <ucs/datastruct/frag_list.h>
#include <ucs/datastruct/arbiter.h>
#include <ucs/datastruct/conn_match.h>
#include <ucs/time/timer_wheel.h>
#include <ucs/sys/compiler_def.h>
#include <uct/base/uct_iface.h>
#include <uct/urma/base/urma_iface.h>
#include <uct/urma/base/urma_ep.h>
#include <uct/urma/um_urma/um_urma_iface.h>
#include "um_urma_def.h"

#define UCT_UM_URMA_INITIAL_PSN             1   /* initial packet serial number */
#define UCT_UM_URMA_CA_MIN_WINDOW           2
#define UCT_UM_URMA_EP_NULL_ID              ((1<<24)-1)

#if ENABLE_DEBUG_DATA
#  define UCT_UM_URMA_EP_PEER_NAME_FMT        "%s:%d"
#  define UCT_UM_URMA_EP_PEER_NAME_ARG(_ep)   (_ep)->peer.name, (_ep)->peer.pid
#else
#  define UCT_UM_URMA_EP_PEER_NAME_FMT        "%s"
#  define UCT_UM_URMA_EP_PEER_NAME_ARG(_ep)   "<no debug data>"
#endif

typedef struct uct_um_urma_am_short_hdr {
    uint64_t hdr;
} uct_um_urma_am_short_hdr_t;

typedef struct uct_um_urma_put_hdr {
    uint64_t rva;
} uct_um_urma_put_hdr_t;

typedef struct uct_ub_um_urma_ep_addr {
    uct_um_urma_iface_addr_t    iface_addr;
    uint32_t                    ep_id;
} uct_ub_um_urma_ep_addr_t;

typedef uint16_t    uct_um_urma_psn_t;

typedef struct uct_um_urma_peer_name {
    char    name[16];
    int     pid;
} uct_um_urma_peer_name_t;

typedef struct uct_um_urma_ctl_hdr {
    uint8_t                             type;
    uint8_t                             reserved[3];
    union {
        struct {
            uct_ub_um_urma_ep_addr_t    ep_addr;
            uct_um_urma_ep_conn_sn_t    conn_sn;
        } conn_req;
        struct {
            uint32_t                    src_ep_id;
        } conn_rep;
        uint32_t                        data;
    };
    uct_um_urma_peer_name_t             peer;
    /* For CREQ packet, IB address follows */
} UCS_S_PACKED uct_um_urma_ctl_hdr_t;

typedef struct uct_um_urma_ep_pending_op {
    ucs_arbiter_group_t     group;
    uint32_t                ops;    /* bitmask that describes what control ops are sceduled */
    ucs_arbiter_elem_t      elem;
} uct_um_urma_ep_pending_op_t;

struct uct_um_urma_ep {
    uct_ub_ep_t                     super;
    ucs_time_t                      send_time;
    ucs_time_t                      resend_time;
    ucs_time_t                      tick;
    uct_um_urma_peer_address_t      peer_address;
    uint64_t                        ka_va_addr;
    urma_target_seg_t               *ka_tseg;
    struct {
        uct_um_urma_psn_t           psn;          /* Next PSN to send */
        uct_um_urma_psn_t           max_psn;      /* Largest PSN that can be sent */
        uct_um_urma_psn_t           acked_psn;    /* last psn that was acked by remote side */
        uint16_t                    resend_count; /* number of in-flight resends on the ep */
        ucs_queue_head_t            window;       /* send window: [acked_psn+1, psn-1] */
        ucs_time_t                  send_time;
        ucs_time_t                  resend_time;
        ucs_time_t                  tick;
        uct_um_urma_ep_pending_op_t pending;      /* pending ops */
    } tx;
    struct {
        uct_um_urma_psn_t           acked_psn;    /* Last psn we acked */
        ucs_frag_list_t             ooo_pkts;     /* Out of order packets that can not be processed yet,
                                                   * also keeps last psn we successfully received and processed */
    } rx;
    struct {
        uct_um_urma_psn_t           wmax;
        uct_um_urma_psn_t           cwnd;
        ucs_wtimer_t                timer;
        ucs_time_t                  close_time;
    } ca;
    struct UCS_S_PACKED {
        ucs_queue_iter_t           pos;       /* points to the part of tx window that needs to be resent */
        uct_um_urma_psn_t          psn;       /* last psn that was retransmitted */
        uct_um_urma_psn_t          max_psn;   /* max psn that should be retransmitted */
    } resend;
    ucs_conn_match_elem_t           conn_match;
    uct_um_urma_ep_conn_sn_t        conn_sn;      /* connection sequence number. assigned in connect_to_iface() */
    uint16_t                        flags;
    uint8_t                         rx_creq_count; /* TODO: remove when reason for DUP/OOO CREQ is found */
    ucs_arbiter_group_t             arb_group;
    uct_um_urma_peer_name_t         peer;
    ucs_wtimer_t                    timer;
    ucs_time_t                      close_time;
};

/*
 * Call user completion handler
 */
typedef struct uct_um_urma_comp_desc {
    uct_completion_t            *comp;
    ucs_status_t                status;     /* used in case of failure */
} uct_um_urma_comp_desc_t;

typedef struct uct_um_urma_iov {
    void                        *buffer;    /**< Data buffer */
    urma_target_seg_t           *tseg;
    uint16_t                    length;     /**< Length of the buffer in bytes */
} UCS_S_PACKED uct_um_urma_iov_t;

typedef struct uct_um_urma_zcopy_desc {
    uct_um_urma_comp_desc_t     super;
    uct_um_urma_iov_t           iov[UCT_UB_MAX_IOV];
    uint16_t                    iovcnt;     /* Count of the iov[] array valid elements */
} uct_um_urma_zcopy_desc_t;

enum {
    UCT_UM_URMA_EP_OP_NONE       = 0,
    UCT_UM_URMA_EP_OP_ACK        = UCS_BIT(0),  /* ack data */
    UCT_UM_URMA_EP_OP_ACK_REQ    = UCS_BIT(1),  /* request ack of sent packets */
    UCT_UM_URMA_EP_OP_RESEND     = UCS_BIT(2),  /* resend un acked packets */
    UCT_UM_URMA_EP_OP_CREP       = UCS_BIT(3),  /* send connection reply */
    UCT_UM_URMA_EP_OP_CREQ       = UCS_BIT(4),  /* send connection request */
    UCT_UM_URMA_EP_OP_NACK       = UCS_BIT(5),  /* send NACK */
};

#define UCT_UM_URMA_EP_OP_CTL_ACK (UCT_UM_URMA_EP_OP_ACK|UCT_UM_URMA_EP_OP_ACK_REQ|UCT_UM_URMA_EP_OP_NACK)
#define UCT_UM_URMA_EP_OP_CTL_HI_PRIO  (UCT_UM_URMA_EP_OP_CREQ|UCT_UM_URMA_EP_OP_CREP|UCT_UM_URMA_EP_OP_RESEND)

/*
 * Auxillary AM ID bits used by FC protocol.
 */
enum {
    UCT_UM_URMA_EP_FLAG_DISCONNECTED      = UCS_BIT(0),  /* EP was disconnected */
    UCT_UM_URMA_EP_FLAG_PRIVATE           = UCS_BIT(1),  /* EP was created as internal */
    UCT_UM_URMA_EP_FLAG_HAS_PENDING       = UCS_BIT(2),  /* EP has some pending requests */
    UCT_UM_URMA_EP_FLAG_CONNECTED         = UCS_BIT(3),  /* EP was connected to the peer */
    UCT_UM_URMA_EP_FLAG_ON_CEP            = UCS_BIT(4),  /* EP was inserted to connection
                                                          * matching context */

    /* debug flags */
    UCT_UM_URMA_EP_FLAG_CREQ_RCVD         = UCS_BIT(5),  /* CREQ message was received */
    UCT_UM_URMA_EP_FLAG_CREP_RCVD         = UCS_BIT(6),  /* CREP message was received */
    UCT_UM_URMA_EP_FLAG_CREQ_SENT         = UCS_BIT(7),  /* CREQ message was sent */
    UCT_UM_URMA_EP_FLAG_CREP_SENT         = UCS_BIT(8),  /* CREP message was sent */
    UCT_UM_URMA_EP_FLAG_CREQ_NOTSENT      = UCS_BIT(9),  /* CREQ message is NOT sent, because
                                                          * connection establishment process
                                                          * is driven by remote side. */
    UCT_UM_URMA_EP_FLAG_TX_NACKED         = UCS_BIT(10), /* Last psn was acked with NAK */

    /* Endpoint is currently executing the pending queue */
#if UCS_ENABLE_ASSERT
    UCT_UM_URMA_EP_FLAG_IN_PENDING        = UCS_BIT(11),
#else
    UCT_UM_URMA_EP_FLAG_IN_PENDING        = 0,
#endif
};

enum {
    UCT_UM_URMA_PACKET_ACK_REQ_SHIFT   = 25,
    UCT_UM_URMA_PACKET_AM_ID_SHIFT     = 27,
    UCT_UM_URMA_PACKET_DEST_ID_SHIFT   = 24,
    UCT_UM_URMA_PACKET_PUT_SHIFT       = 28,
};

enum {
    UCT_UM_URMA_PACKET_FLAG_AM      = UCS_BIT(24),
    UCT_UM_URMA_PACKET_FLAG_ACK_REQ = UCS_BIT(25),
    UCT_UM_URMA_PACKET_FLAG_ECN     = UCS_BIT(26),
    UCT_UM_URMA_PACKET_FLAG_NAK     = UCS_BIT(27),
    UCT_UM_URMA_PACKET_FLAG_PUT     = UCS_BIT(28),
    UCT_UM_URMA_PACKET_FLAG_CTL     = UCS_BIT(29),

    UCT_UM_URMA_PACKET_AM_ID_MASK     = UCS_MASK(UCT_UM_URMA_PACKET_AM_ID_SHIFT),
    UCT_UM_URMA_PACKET_DEST_ID_MASK   = UCS_MASK(UCT_UM_URMA_PACKET_DEST_ID_SHIFT),
};

enum {
    UCT_UM_URMA_PACKET_CREQ = 1,
    UCT_UM_URMA_PACKET_CREP = 2,
};

ucs_status_t uct_um_urma_ep_am_short(uct_ep_h tl_ep, uint8_t id, uint64_t hdr, const void *buffer, unsigned length);

ssize_t uct_um_urma_ep_am_bcopy(uct_ep_h tl_ep, uint8_t id, uct_pack_callback_t pack_cb, void *arg, unsigned flags);

ucs_status_t uct_um_urma_ep_am_zcopy(uct_ep_h tl_ep, uint8_t id, const void *header, unsigned header_length,
                                     const uct_iov_t *iov, size_t iovcnt, unsigned flags, uct_completion_t *comp);

ucs_status_t uct_um_urma_ep_put_short(uct_ep_h tl_ep, const void *payload, unsigned length, uint64_t remote_addr,
                                      uct_rkey_t rkey);

ucs_status_t uct_um_urma_ep_pending_add(uct_ep_h tl_ep, uct_pending_req_t *r, unsigned flags);

void uct_um_urma_ep_pending_purge(uct_ep_h tl_ep, uct_pending_purge_callback_t cb, void *arg);

ucs_status_t uct_um_urma_ep_flush_nolock(uct_um_urma_iface_t *iface, uct_um_urma_ep_t *ep, uct_completion_t *comp);

ucs_status_t uct_um_urma_ep_flush(uct_ep_h tl_ep, unsigned flags, uct_completion_t *comp);

ucs_status_t uct_um_urma_ep_get_address(uct_ep_h tl_ep, uct_ep_addr_t *addr);

ucs_status_t uct_um_urma_ep_connect_to_ep(uct_ep_h tl_ep, const uct_device_addr_t *dev_addr,
                                          const uct_ep_addr_t *ep_addr);

ucs_status_t uct_um_urma_ep_create_connected_common(const uct_ep_params_t *params, uct_ep_h *new_ep_p);

ucs_status_t uct_um_urma_iface_unpack_peer_address(uct_um_urma_iface_t *iface, const uct_ub_address_t *dev_addr,
                                                   const uct_um_urma_iface_addr_t *if_addr, void *address_p);

uct_um_urma_send_skb_t *uct_um_urma_ep_prepare_creq(uct_um_urma_ep_t *ep);

void uct_um_urma_ep_deal_creq(uct_um_urma_iface_t *iface, uct_um_urma_neth_t *neth);

void uct_um_urma_ep_deal_crep(uct_um_urma_iface_t *iface, uct_um_urma_ep_t *ep, uct_um_urma_neth_t *neth);

void uct_um_urma_ep_deal_am(uct_um_urma_iface_t *iface, uct_um_urma_neth_t *neth, unsigned byte_len,
                            uct_ub_iface_recv_desc_t *desc);

ucs_arbiter_cb_result_t uct_um_urma_ep_do_pending(ucs_arbiter_t *arbiter, ucs_arbiter_group_t *group,
                                                  ucs_arbiter_elem_t *elem, void *arg);

void uct_um_urma_ep_process_ack(uct_um_urma_iface_t *iface, uct_um_urma_ep_t *ep,
                                uct_um_urma_psn_t ack_psn, int is_async);

void uct_um_urma_ep_disconnect(uct_ep_h tl_ep);

void uct_um_urma_ep_process_rx(uct_um_urma_iface_t *iface, uct_um_urma_neth_t *neth, unsigned byte_len,
                               uct_um_urma_recv_skb_t* skb, int is_async);

void uct_um_urma_ep_window_release_completed(uct_um_urma_ep_t *ep, int is_async);

ucs_status_t uct_um_urma_ep_connect_to_ep_v2(uct_ep_h tl_ep, const uct_device_addr_t *dev_addr,
                                             const uct_ep_addr_t *ep_addr,
                                             const uct_ep_connect_to_ep_params_t *param);

static UCS_F_ALWAYS_INLINE void uct_um_urma_ep_ctl_op_schedule(uct_um_urma_iface_t *iface, uct_um_urma_ep_t *ep)
{
    ucs_arbiter_group_push_elem(&ep->tx.pending.group, &ep->tx.pending.elem);
    ucs_arbiter_group_schedule(&iface->tx.pending_q, &ep->tx.pending.group);
}

static UCS_F_ALWAYS_INLINE void uct_um_urma_ep_ctl_op_add(uct_um_urma_iface_t *iface, uct_um_urma_ep_t *ep, int op)
{
    ep->tx.pending.ops |= op;
    uct_um_urma_ep_ctl_op_schedule(iface, ep);
}

static UCS_F_ALWAYS_INLINE void uct_um_urma_ep_ctl_op_del(uct_um_urma_ep_t *ep, uint32_t ops)
{
    ep->tx.pending.ops &= ~ops;
}

static UCS_F_ALWAYS_INLINE int uct_um_urma_ep_ctl_op_isany(uct_um_urma_ep_t *ep)
{
    return ep->tx.pending.ops;
}

static UCS_F_ALWAYS_INLINE int uct_um_urma_ep_ctl_op_check_ex(uct_um_urma_ep_t *ep, uint32_t ops)
{
    return (ep->tx.pending.ops & ops) && ((ep->tx.pending.ops & ~ops) == 0);
}

static UCS_F_ALWAYS_INLINE void uct_um_urma_ep_remove_has_pending_flag(uct_um_urma_ep_t *ep)
{
    ucs_assert(ep->flags & UCT_UM_URMA_EP_FLAG_HAS_PENDING);
    ep->flags &= ~UCT_UM_URMA_EP_FLAG_HAS_PENDING;
}

static UCS_F_ALWAYS_INLINE void uct_um_urma_skb_release(uct_um_urma_send_skb_t *skb, int is_inline)
{
    ucs_assert(!(skb->flags & UCT_UM_URMA_SEND_SKB_FLAG_INVALID));
    skb->flags = UCT_UM_URMA_SEND_SKB_FLAG_INVALID;

    if (is_inline) {
        ucs_mpool_put_inline(skb);
    } else {
        ucs_mpool_put(skb);
    }
}

static UCS_F_ALWAYS_INLINE int uct_um_urma_iface_has_skbs(uct_um_urma_iface_t *iface)
{
    return iface->tx.skb || !ucs_mpool_is_empty(&iface->tx.mp);
}

static UCS_F_ALWAYS_INLINE int uct_um_urma_ep_no_window(uct_um_urma_ep_t *ep)
{
    return UCT_UM_URMA_PSN_COMPARE(ep->tx.psn, >=, ep->tx.max_psn);
}

static UCS_F_ALWAYS_INLINE int uct_um_urma_ep_is_last_ack_received(uct_um_urma_ep_t *ep)
{
    return UCT_UM_URMA_PSN_COMPARE(ep->tx.acked_psn, ==, ep->tx.psn - 1);
}

static UCS_F_ALWAYS_INLINE void uct_um_urma_neth_set_dest_id(uct_um_urma_neth_t *neth, uint32_t id)
{
    neth->packet_type |= id;
}

static UCS_F_ALWAYS_INLINE uint32_t uct_um_urma_neth_get_dest_id(uct_um_urma_neth_t *neth)
{
    return neth->packet_type & UCT_UM_URMA_PACKET_DEST_ID_MASK;
}

static UCS_F_ALWAYS_INLINE uct_ub_address_t* uct_um_urma_creq_ub_addr(uct_um_urma_ctl_hdr_t *conn_req)
{
    ucs_assert(conn_req->type == UCT_UM_URMA_PACKET_CREQ);

    return (uct_ub_address_t*)(conn_req + 1);
}

static UCS_F_ALWAYS_INLINE int uct_um_urma_ep_is_connected(uct_um_urma_ep_t *ep)
{
    ucs_assert((ep->super.dep_id == UCT_UM_URMA_EP_NULL_ID) == !(ep->flags & UCT_UM_URMA_EP_FLAG_CONNECTED));

    return ep->flags & UCT_UM_URMA_EP_FLAG_CONNECTED;
}

typedef struct {
    uct_pending_req_priv_arb_t arb;
    unsigned                   flags;
} uct_um_urma_pending_req_priv_t;

static UCS_F_ALWAYS_INLINE uct_um_urma_pending_req_priv_t *uct_um_urma_pending_req_priv(uct_pending_req_t *req)
{
    return (uct_um_urma_pending_req_priv_t *)&(req)->priv;
}

static UCS_F_ALWAYS_INLINE int uct_um_urma_ep_is_connected_and_no_pending(uct_um_urma_ep_t *ep)
{
    return (ep->flags & (UCT_UM_URMA_EP_FLAG_CONNECTED | UCT_UM_URMA_EP_FLAG_HAS_PENDING))
        == UCT_UM_URMA_EP_FLAG_CONNECTED;
}

static UCS_F_ALWAYS_INLINE void uct_um_urma_ep_set_has_pending_flag(uct_um_urma_ep_t *ep)
{
    ep->flags |= UCT_UM_URMA_EP_FLAG_HAS_PENDING;
}

UCS_CLASS_DECLARE(uct_um_urma_ep_t, const uct_ep_params_t *);
UCS_CLASS_DECLARE_NEW_FUNC(uct_um_urma_ep_t, uct_ep_t, const uct_ep_params_t *);
UCS_CLASS_DECLARE_DELETE_FUNC(uct_um_urma_ep_t, uct_ep_t);

#endif
