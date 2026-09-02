/**
* Copyright (C) Huawei Technologies Co., Ltd. 2026. ALL RIGHTS RESERVED.
*
* See file LICENSE for terms.
*/

#ifndef UCT_UM_URMA_INL_H
#define UCT_UM_URMA_INL_H

static UCS_F_ALWAYS_INLINE void uct_um_urma_ep_tx_stop(uct_um_urma_ep_t *ep)
{
    ep->tx.max_psn = ep->tx.psn;
}

static UCS_F_ALWAYS_INLINE uct_um_urma_ctl_desc_t *uct_um_urma_get_ctl_desc(uct_um_urma_send_skb_t *skb)
{
    ucs_assert(skb->flags & (UCT_UM_URMA_SEND_SKB_FLAG_CTL_ACK | UCT_UM_URMA_SEND_SKB_FLAG_CTL_RESEND));
    ucs_assert(!(skb->flags & UCT_UM_URMA_SEND_SKB_FLAG_INVALID));

    return (uct_um_urma_ctl_desc_t *)((char*)skb->neth + skb->len);
}

#endif
