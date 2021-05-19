/**
* Copyright (C) Huawei Technologies Co., Ltd. 2021.  ALL RIGHTS RESERVED.
*
* See file LICENSE for terms.
*/

#ifndef UCT_UD_MCAST_H
#define UCT_UD_MCAST_H

#include "ud_def.h"

typedef struct uct_ud_mcast_iface_ctx {
    int           listen_fd;
    uint32_t      coll_id;
    uint32_t      coll_cnt;
    union ibv_gid mgid;
    int           id;
    uint8_t       num_of_peers;     /* number of remote endpoints connected */
    uct_ud_psn_t *acked_psn_by_src; /* array of size num_of_peers - each cell represents last acked_psn from remote epid */
} uct_ud_mcast_iface_ctx_t;


ucs_status_t uct_ud_mcast_iface_init(uct_ud_iface_t *iface,
                                     uct_ud_mcast_iface_ctx_t *mcast_ctx,
                                     const uct_iface_params_t *params);

ucs_status_t uct_ud_mcast_iface_attach(uct_ud_iface_t *iface,
                                       const uct_ud_mcast_iface_addr_t *addr);

void uct_ud_mcast_iface_query_update(uct_iface_attr_t *iface_attr);

static UCS_F_ALWAYS_INLINE void
uct_ud_mcast_reset_acks(uct_ud_mcast_iface_ctx_t *mcast_ctx) {
    unsigned idx;
    for (idx = 0; idx < mcast_ctx->num_of_peers; idx++) {
        mcast_ctx->acked_psn_by_src[idx] = UCT_UD_INITIAL_PSN - 1;
    }
}

#endif
