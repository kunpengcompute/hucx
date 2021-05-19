/**
 * Copyright (C) Mellanox Technologies Ltd. 2001-2019.  ALL RIGHTS RESERVED.
 *
 * See file LICENSE for terms.
 */

#ifndef UD_VERBS_H
#define UD_VERBS_H

#include <uct/ib/base/ib_verbs.h>

#include <uct/ib/ud/base/ud_iface.h>
#include <uct/ib/ud/base/ud_ep.h>
#include <uct/ib/ud/base/ud_def.h>


typedef struct {
    uint32_t                          dest_qpn;
    struct ibv_ah                     *ah;
} uct_ud_verbs_ep_peer_address_t;


typedef struct {
    uct_ud_ep_t                       super;
    uct_ud_verbs_ep_peer_address_t    peer_address;
} uct_ud_verbs_ep_t;


typedef struct {
    uct_ud_iface_t                    super;
    struct {
        struct ibv_sge                sge[UCT_IB_MAX_IOV];
        struct ibv_send_wr            wr_inl;
        struct ibv_send_wr            wr_skb;
        uint16_t                      send_sn;
        uint16_t                      comp_sn;
    } tx;
    struct {
        size_t                        max_send_sge;
    } config;
} uct_ud_verbs_iface_t;

typedef struct uct_ud_verbs_iface_wrapper {
    uct_ud_verbs_iface_t super;
} uct_ud_verbs_iface_wrapper_t;

typedef struct uct_ud_mcast_verbs_iface {
    uct_ud_verbs_iface_t     super;
    uct_ud_mcast_iface_ctx_t mcast;
} uct_ud_mcast_verbs_iface_t;

UCS_CLASS_DECLARE(uct_ud_verbs_iface_t, uct_ud_iface_ops_t *, uct_iface_ops_t *,
                  uct_md_h, uct_worker_h, const uct_iface_params_t *,
                  const uct_iface_config_t *);

UCS_CLASS_DECLARE(uct_ud_verbs_iface_wrapper_t, uct_md_h, uct_worker_h,
                  const uct_iface_params_t *, const uct_iface_config_t *);

UCS_CLASS_DECLARE_NEW_FUNC(uct_ud_verbs_ep_t, uct_ep_t, const uct_ep_params_t*);

UCS_CLASS_DECLARE(uct_ud_verbs_ep_t, const uct_ep_params_t *)


ucs_status_t uct_ud_verbs_qp_max_send_sge(uct_ud_verbs_iface_t *iface,
                                          size_t *max_send_sge);

ucs_status_t uct_ud_verbs_iface_query(uct_iface_h tl_iface,
                                      uct_iface_attr_t *iface_attr);

ucs_status_t uct_ud_verbs_ep_am_short(uct_ep_h tl_ep, uint8_t id, uint64_t hdr,
                                      const void *buffer, unsigned length);

ucs_status_t uct_ud_verbs_ep_am_short_iov(uct_ep_h tl_ep, uint8_t id,
                                          const uct_iov_t *iov, size_t iovcnt);

ssize_t uct_ud_verbs_ep_am_bcopy(uct_ep_h tl_ep, uint8_t id,
                                 uct_pack_callback_t pack_cb, void *arg,
                                 unsigned flags);

#endif
