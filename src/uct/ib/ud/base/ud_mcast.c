/**
* Copyright (C) Huawei Technologies Co., Ltd. 2021.  ALL RIGHTS RESERVED.
*
* See file LICENSE for terms.
*/

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <ucs/sys/sock.h>
#include <uct/ib/base/ib_verbs.h>

#include "ud_ep.h"
#include "ud_mcast.h"
#include "ud_iface.h"

#define MCAST_PORT (1900)

/* multicast addresses are between 239.0.0.0 - 239.255.255.255 */
static int mcg_cnt = 0;
#define MCG_GID_INIT_VAL {0,0,0,0,0,0,0,0,0,0,255,255,0,0,0,0}
#define MCG_MAX_VAL (UCS_MASK(24))

static ucs_status_t uct_ud_mcast_init_group(uct_ud_mcast_iface_ctx_t *mcast_ctx,
                                            uct_ib_device_t *dev,
                                            uint8_t port_num,
                                            uct_ib_device_gid_info_t *gid_info)
{
    char ndev_name[IFNAMSIZ];
    ucs_status_t status;
    union ibv_gid mgid;
    struct ifreq ifr;

    struct sockaddr_in mcast_addr = {0};
    mcast_addr.sin_family         = AF_INET;
    mcast_addr.sin_addr.s_addr    = htonl(INADDR_ANY);
    mcast_addr.sin_port           = htons(MCAST_PORT);

    if ((mcg_cnt + 10 * mcast_ctx->coll_id) > MCG_MAX_VAL) {
        ucs_debug("init_multicast_group: reached maximum allowed multicast group ip.");
        return UCS_ERR_OUT_OF_RANGE;
    }

    uint8_t mcg_gid[16] = MCG_GID_INIT_VAL;
    uint32_t *mcg_gid_32 = (uint32_t*)mcg_gid;
    mcg_gid_32[3] = 239 | (mcg_cnt + 10 * mcast_ctx->coll_id) << 8;
    mcg_cnt++;
    memcpy(mgid.raw,mcg_gid,16);

    mcast_ctx->mgid = mgid;

    status = ucs_socket_server_init((struct sockaddr*)&mcast_addr,
                                    sizeof(mcast_addr), 0, 0, 1,
                                    SOCK_DGRAM, &mcast_ctx->listen_fd);
    if (status != UCS_OK) {
        return status;
    }

    status = uct_ib_device_get_roce_ndev_name(dev, port_num, gid_info->gid_index,
                                              ndev_name, sizeof(ndev_name));
    if (status != UCS_OK) {
        ucs_error("init_multicast_group: device name failed to load.");
        return UCS_ERR_INVALID_PARAM;
    }

    // TODO: used ucs_sockaddr_get_ifaddr() after #5581 is merged
    status = ucs_netif_ioctl(ndev_name, SIOCGIFADDR, &ifr);
    if (status != UCS_OK) {
        return UCS_ERR_INVALID_PARAM;
    }
    ucs_debug("device ip loaded %s.",
              inet_ntoa(((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr));

    char *tmp = inet_ntoa(((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr);
    char *localIF = malloc(strlen(tmp) + 1);
    strcpy(localIF, tmp);

    struct in_addr multicastIP;
    multicastIP.s_addr = mcg_gid[12] | mcg_gid[13] << 8 | mcg_gid[14] << 16 | mcg_gid[15] << 24;
    tmp = inet_ntoa(multicastIP);
    char *mcast = malloc(strlen(tmp) + 1);
    strcpy(mcast, tmp);

    struct ip_mreq multicastRequest;
    multicastRequest.imr_multiaddr.s_addr = inet_addr(mcast);
    multicastRequest.imr_interface.s_addr = inet_addr(localIF);

    status =  ucs_socket_setopt(mcast_ctx->listen_fd, IPPROTO_IP, IP_ADD_MEMBERSHIP,
                                (void *) &multicastRequest, sizeof(multicastRequest));
    if (status != UCS_OK) {
        return status;
    }

    // TODO: how to wait for the group to be created?

    return UCS_OK;
}

ucs_status_t uct_ud_mcast_iface_init(uct_ud_iface_t *iface,
                                     uct_ud_mcast_iface_ctx_t *mcast_ctx,
                                     const uct_iface_params_t *params)
{
    unsigned idx;
    size_t alloc_size;
    ucs_status_t status;

    iface->mcast_ctx    = mcast_ctx;
    mcast_ctx->coll_id  = params->global_info.proc_idx;
    mcast_ctx->coll_cnt = params->global_info.proc_cnt;

    status = uct_ud_mcast_init_group(mcast_ctx,
                                     uct_ib_iface_device(&iface->super),
                                     iface->super.config.port_num,
                                     &iface->super.gid_info);
    if (status != UCS_OK) {
        return status;
    }

    if (mcast_ctx->coll_id == 0) {
        mcast_ctx->num_of_peers     = mcast_ctx->coll_cnt;
        alloc_size                  = mcast_ctx->num_of_peers *
                                      sizeof(*mcast_ctx->acked_psn_by_src);
        mcast_ctx->acked_psn_by_src = UCS_ALLOC_CHECK(alloc_size,
                                                      "alloc acked_psn");

        for (idx = 0; idx < mcast_ctx->num_of_peers; idx++) {
            mcast_ctx->acked_psn_by_src[idx] = UCT_UD_INITIAL_PSN - 1;
        }
    } else {
        mcast_ctx->num_of_peers     = 0;
        mcast_ctx->acked_psn_by_src = NULL;
    }

    return UCS_OK;
}

ucs_status_t uct_ud_mcast_iface_attach(uct_ud_iface_t *iface,
                                       const uct_ud_mcast_iface_addr_t *addr)
{
    uct_ud_mcast_iface_ctx_t *mcast_ctx = iface->mcast_ctx;
    int is_coll_root                    = (mcast_ctx->coll_id == 0);

    char ib_gid_str[128];
    ucs_status_t status;

    if (is_coll_root) {
        return UCS_OK;
    }

    /* Ignore loopback connections outside the multicast root */
    if (addr->coll_id == mcast_ctx->coll_id) {
        return UCS_OK;
    }

    /* Attached to multicast group before proceeding to connecting */
    status = ibv_attach_mcast(iface->qp, &addr->mgid, 0);
    if (status != UCS_OK) {
        return status;
    }

    // TODO: find a way to detect when the attach has completed...
sleep(1);

    ucs_debug("multicast attachment on qp 0x%x, mgid %s", iface->qp->qp_num,
              uct_ib_gid_str(&addr->mgid, ib_gid_str, sizeof(ib_gid_str)));

    return UCS_OK;
}

void uct_ud_mcast_iface_query_update(uct_iface_attr_t *iface_attr)
{
    // TODO: disable zero-copy!

    iface_attr->iface_addr_len            = sizeof(uct_ud_mcast_iface_addr_t);
    iface_attr->ep_addr_len               = sizeof(uct_ud_mcast_ep_addr_t);
    iface_attr->cap.am.coll_mode_flags    = UCS_BIT(UCT_COLL_DTYPE_MODE_PADDED);
    iface_attr->cap.coll_mode.short_flags = UCS_BIT(UCT_COLL_DTYPE_MODE_PADDED);
    iface_attr->cap.coll_mode.bcopy_flags = UCS_BIT(UCT_COLL_DTYPE_MODE_PADDED);
    iface_attr->cap.coll_mode.zcopy_flags = 0; /* TODO: implement... */
    iface_attr->cap.flags                |= UCT_IFACE_FLAG_BCAST;
}
