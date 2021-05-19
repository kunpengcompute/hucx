/**
 * Copyright (C) Mellanox Technologies Ltd. 2001-2014.  ALL RIGHTS RESERVED.
 *
 * See file LICENSE for terms.
 */
#ifndef UD_MLX5_H
#define UD_MLX5_H

#include "ud_mlx5_common.h"

#include <uct/ib/ud/base/ud_iface.h>
#include <uct/ib/ud/base/ud_ep.h>


typedef struct {
    uct_ib_mlx5_base_av_t               av;
    uint8_t                             is_global;
    struct mlx5_grh_av                  grh_av;
} uct_ud_mlx5_ep_peer_address_t;


typedef struct {
    uct_ud_ep_t                         super;
    uct_ud_mlx5_ep_peer_address_t       peer_address;
} uct_ud_mlx5_ep_t;


typedef struct {
    uct_ud_iface_config_t               super;
    uct_ib_mlx5_iface_config_t          mlx5_common;
    uct_ud_mlx5_iface_common_config_t   ud_mlx5_common;
} uct_ud_mlx5_iface_config_t;


typedef struct {
    uct_ud_iface_t                      super;
    struct {
        uct_ib_mlx5_txwq_t              wq;
        uct_ib_mlx5_mmio_mode_t         mmio_mode;
    } tx;
    struct {
        uct_ib_mlx5_rxwq_t              wq;
    } rx;
    uct_ib_mlx5_cq_t                    cq[UCT_IB_DIR_NUM];
    uct_ud_mlx5_iface_common_t          ud_mlx5_common;
} uct_ud_mlx5_iface_t;

typedef struct uct_ud_mlx5_iface_wrapper {
    uct_ud_mlx5_iface_t super; 
} uct_ud_mlx5_iface_wrapper_t;

typedef struct uct_ud_mcast_mlx5_iface {
    uct_ud_mlx5_iface_t      super;
    uct_ud_mcast_iface_ctx_t mcast;
} uct_ud_mcast_mlx5_iface_t;

UCS_CLASS_DECLARE(uct_ud_mlx5_iface_t, uct_ud_iface_ops_t *, uct_iface_ops_t *,
                  uct_md_h, uct_worker_h, const uct_iface_params_t *,
                  const uct_iface_config_t *);

UCS_CLASS_DECLARE(uct_ud_mlx5_iface_wrapper_t, uct_md_h, uct_worker_h,
                  const uct_iface_params_t *, const uct_iface_config_t *);

UCS_CLASS_DECLARE(uct_ud_mlx5_ep_t, const uct_ep_params_t*);
UCS_CLASS_DECLARE_NEW_FUNC(uct_ud_mlx5_ep_t, uct_ep_t, const uct_ep_params_t*);


ucs_status_t uct_ud_mlx5_iface_query(uct_iface_h tl_iface,
                                     uct_iface_attr_t *iface_attr);

ucs_status_t uct_ud_mlx5_ep_am_short(uct_ep_h tl_ep, uint8_t id, uint64_t hdr,
                                     const void *buffer, unsigned length);

ucs_status_t uct_ud_mlx5_ep_am_short_iov(uct_ep_h tl_ep, uint8_t id,
                                         const uct_iov_t *iov, size_t iovcnt);

ssize_t uct_ud_mlx5_ep_am_bcopy(uct_ep_h tl_ep, uint8_t id,
                                uct_pack_callback_t pack_cb, void *arg,
                                unsigned flags);


UCS_F_ALWAYS_INLINE unsigned
uct_ud_mlx5_tx_moderation(uct_ud_mlx5_iface_t *iface, uint8_t ce_se)
{
    if ((ce_se & MLX5_WQE_CTRL_CQ_UPDATE) ||
        (iface->super.tx.unsignaled >= (UCT_UD_TX_MODERATION - 1))) {
        iface->super.tx.unsignaled = 0;
        return ce_se | MLX5_WQE_CTRL_CQ_UPDATE;
    }

    iface->super.tx.unsignaled++;
    return ce_se;
}

#endif

