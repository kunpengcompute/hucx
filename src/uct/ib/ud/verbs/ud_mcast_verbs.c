#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "ud_mcast_verbs.h"

#include <uct/api/uct.h>
#include <uct/ib/base/ib_iface.h>
#include <uct/base/uct_md.h>
#include <uct/base/uct_log.h>
#include <ucs/debug/log.h>
#include <ucs/debug/memtrack.h>
#include <ucs/type/class.h>
#include <string.h>
#include <arpa/inet.h> /* For htonl */

#include <uct/ib/ud/base/ud_iface.h>
#include <uct/ib/ud/base/ud_ep.h>
#include <uct/ib/ud/base/ud_def.h>
#include <uct/ib/ud/base/ud_inl.h>
#include <sys/socket.h>

extern uct_ud_iface_ops_t uct_ud_verbs_iface_ops;
static uct_ud_iface_ops_t super_ops;

extern ucs_config_field_t uct_ud_verbs_iface_config_table[];
ucs_config_field_t uct_ud_mcast_verbs_iface_config_table[] = {
  {"UD_", "", NULL,
   0, UCS_CONFIG_TYPE_TABLE(uct_ud_verbs_iface_config_table)},

  {NULL}
};

void UCS_CLASS_DELETE_FUNC_NAME(uct_ud_mcast_verbs_iface_t)(uct_iface_t*);

static ucs_status_t
uct_ud_mcast_iface_get_address(uct_iface_h tl_iface, uct_iface_addr_t *iface_addr)
{
    uct_ud_mcast_verbs_iface_t *iface = ucs_derived_of(tl_iface, uct_ud_mcast_verbs_iface_t);
    uct_ud_mcast_iface_addr_t *if_addr = (uct_ud_mcast_iface_addr_t *)iface_addr;
    super_ops.super.super.iface_get_address(tl_iface, iface_addr);
    if_addr->mgid = iface->mgid;
    if_addr->coll_id = iface->coll_id;
    return UCS_OK;
}

static ucs_status_t
uct_ud_mcast_verbs_iface_unpack_peer_address(uct_ud_iface_t *ud_iface,
                                            const uct_ib_address_t *ib_addr,
                                            const uct_ud_iface_addr_t *if_addr,
                                            int path_index, void *address_p)
{
    ucs_status_t status;
    uct_ud_mcast_verbs_iface_t *iface      = ucs_derived_of(ud_iface, uct_ud_mcast_verbs_iface_t);

    uct_ud_iface_addr_t if_addr_cpy;
    memcpy(&if_addr_cpy, if_addr, sizeof(if_addr_cpy)); 

    uct_ib_address_t *ib_addr_cpy;
    const uct_ib_address_t *ib_addr_used;

    if(iface->coll_id == 0) {
        uct_ib_address_pack_params_t params;
        uct_ib_address_unpack(ib_addr, &params);
        memcpy(&params.gid.raw, &iface->mgid.raw, sizeof(params.gid.raw)); 

        ib_addr_cpy = ucs_alloca(uct_ib_address_size(&params));
        params.flags &= ~UCT_IB_ADDRESS_PACK_FLAG_PKEY;
        uct_ib_address_pack(&params, ib_addr_cpy);
        uct_ib_pack_uint24(if_addr_cpy.qp_num, 0xFFFFFF);

        ib_addr_used = ib_addr_cpy;
    } else {
        ib_addr_used = ib_addr;
    }

    status = super_ops.unpack_peer_address(ud_iface, ib_addr_used, &if_addr_cpy, path_index, address_p);
    return status;
}

static ucs_status_t
uct_ud_mcast_verbs_iface_query(uct_iface_h tl_iface, uct_iface_attr_t *iface_attr)
{
    uct_ud_verbs_iface_t *iface = ucs_derived_of(tl_iface, uct_ud_verbs_iface_t);
    ucs_status_t status;
    size_t am_max_hdr;

    ucs_trace_func("");

    am_max_hdr = uct_ib_iface_hdr_size(iface->super.super.config.seg_size,
                                       sizeof(uct_ud_neth_t) +
                                       sizeof(uct_ud_zcopy_desc_t));
    status     = uct_ud_iface_query(&iface->super, iface_attr,
                                    iface->config.max_send_sge, am_max_hdr);
    if (status != UCS_OK) {
        return status;
    }

    iface_attr->overhead_short = 105e-9; /* Software overhead (short sends) */
    iface_attr->overhead_bcopy = 106e-9; /* Software overhead (bcopy sends) */

    iface_attr->iface_addr_len            = sizeof(uct_ud_mcast_iface_addr_t);
    iface_attr->ep_addr_len               = sizeof(uct_ud_mcast_verbs_ep_addr_t);
    iface_attr->cap.am.coll_mode_flags    = UCS_BIT(UCT_COLL_DTYPE_MODE_PADDED);
    iface_attr->cap.coll_mode.short_flags = UCS_BIT(UCT_COLL_DTYPE_MODE_PADDED);
    iface_attr->cap.coll_mode.bcopy_flags = UCS_BIT(UCT_COLL_DTYPE_MODE_PADDED);
    iface_attr->cap.coll_mode.zcopy_flags = 0; /* TODO: implement... */

    iface_attr->cap.flags |= UCT_IFACE_FLAG_BCAST;

    return UCS_OK;
}

static ucs_status_t uct_ud_mcast_ep_get_address(uct_ep_h tl_ep, uct_ep_addr_t *addr)
{
    ucs_status_t status = super_ops.super.super.ep_get_address(tl_ep, addr);
    if (status){
        return status;
    }
    uct_ud_ep_t *ep = ucs_derived_of(tl_ep, uct_ud_ep_t);
    uct_ud_mcast_verbs_iface_t *iface = ucs_derived_of(ep->super.super.iface, uct_ud_mcast_verbs_iface_t);
    uct_ud_mcast_verbs_ep_addr_t *ep_addr = (uct_ud_mcast_verbs_ep_addr_t *)addr;

    memcpy(ep_addr->mgid.raw, iface->mgid.raw, sizeof(iface->mgid.raw));
    return UCS_OK;
}

static ucs_status_t
uct_ud_mcast_verbs_ep_am_short(uct_ep_h tl_ep, uint8_t id, uint64_t hdr,
                        const void *buffer, unsigned length)
{
    unsigned orig_length = UCT_COLL_DTYPE_MODE_UNPACK_VALUE(length);
    return super_ops.super.super.ep_am_short(tl_ep, id, hdr, buffer, orig_length);
}

static ssize_t uct_ud_mcast_verbs_ep_am_bcopy(uct_ep_h tl_ep, uint8_t id,
                                       uct_pack_callback_t pack_cb, void *arg,
                                       unsigned flags)
{
    unsigned orig_flags = UCT_COLL_DTYPE_MODE_UNPACK_VALUE(flags);
    return super_ops.super.super.ep_am_bcopy(tl_ep, id, pack_cb, arg, orig_flags);
}

static ucs_status_t
uct_ud_mcast_verbs_ep_create(const uct_ep_params_t* params, uct_ep_h *ep_p)
{
    ucs_status_t status;

    uct_ud_mcast_verbs_iface_t *iface =
            ucs_derived_of(params->iface, uct_ud_mcast_verbs_iface_t);

    if (ucs_test_all_flags(params->field_mask, UCT_EP_PARAM_FIELD_DEV_ADDR |
                                               UCT_EP_PARAM_FIELD_IFACE_ADDR)) {
        if (iface->coll_id != 0) {
            const uct_ud_mcast_iface_addr_t *if_addr =
                    (const uct_ud_mcast_iface_addr_t*)params->iface_addr;

            /* Ignore loopback connections outside the multicast root */
            if (if_addr->coll_id == iface->coll_id) {
                return UCS_OK;
            }

            /* Attached to multicast group before proceeding to connecting */
            status = ibv_attach_mcast(iface->super.super.qp, &if_addr->mgid, 0);
            sleep(1);
            if (status != UCS_OK) {
                return status;
            }

            char p[128];
            ucs_debug("ibv_attach_mcast passed on qp 0x%x, mgid %s", iface->super.super.qp->qp_num, uct_ib_gid_str(&if_addr->mgid, p, sizeof(p)));
        }

        status = uct_ud_ep_create_connected_common(params, ep_p);
    } else {
        status = uct_ud_verbs_ep_t_new(params, ep_p);
    }
    if (status != UCS_OK) {
        return status;
    }

    /* On the multicast root - expect all CREPs to arrive before connecting */
    if (iface->coll_id == 0) {
        ((uct_ud_ep_t*)*ep_p)->rx_crep_count = iface->coll_cnt;
        sleep(1);
    }

    return UCS_OK;

}

static void init_ops(uct_ud_mcast_verbs_iface_t *self, uct_ud_iface_ops_t *ops) {
    *ops      = uct_ud_verbs_iface_ops;
    super_ops = uct_ud_verbs_iface_ops;

    /* Control path - adding mcast address */
    ops->super.super.iface_get_address = uct_ud_mcast_iface_get_address;
    ops->super.super.ep_get_address    = uct_ud_mcast_ep_get_address;
    ops->super.super.ep_create         = uct_ud_mcast_verbs_ep_create;
    ops->super.super.iface_query       = uct_ud_mcast_verbs_iface_query;
    ops->unpack_peer_address           = uct_ud_mcast_verbs_iface_unpack_peer_address;
    ops->super.super.ep_am_short       = uct_ud_mcast_verbs_ep_am_short;
    ops->super.super.ep_am_bcopy       = uct_ud_mcast_verbs_ep_am_bcopy;
}

//multicast addresses are between 239.0.0.0 - 239.255.255.255
static int mcg_cnt = 0;
#define MCG_GID_INIT_VAL {0,0,0,0,0,0,0,0,0,0,255,255,0,0,0,0}
#define MCG_MAX_VAL (255 | 255 << 8 | 255 << 16)

static int init_multicast_group(uct_ud_mcast_verbs_iface_t *self) {
    ucs_status_t status;
    union ibv_gid mgid;
    if ((mcg_cnt + 10*self->coll_id) > MCG_MAX_VAL) {
        ucs_debug("init_multicast_group: reached maximum allowed multicast group ip.");
        return UCS_ERR_OUT_OF_RANGE;
    }
    uint8_t mcg_gid[16] = MCG_GID_INIT_VAL;
    uint32_t *mcg_gid_32 = (uint32_t*)mcg_gid;
    mcg_gid_32[3] = 239 | (mcg_cnt + 10*self->coll_id) << 8;
    mcg_cnt++;
    memcpy(mgid.raw,mcg_gid,16);

    self->mgid = mgid;

    if ((self->listen_fd = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
        ucs_debug("init_multicast_group: socket create failed.");
        return UCS_ERR_INVALID_PARAM;
    }
    int enable = 1;
    if (setsockopt(self->listen_fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) {
        ucs_debug("init_multicast_group: sock enable reuse failed to load.");
        return UCS_ERR_INVALID_PARAM;
    }

    struct sockaddr_in multicastAddr; /* Multicast Address */
    memset(&multicastAddr, 0, sizeof(multicastAddr));   /* Zero out structure */
    multicastAddr.sin_family = AF_INET;                 /* Internet address family */
    multicastAddr.sin_addr.s_addr =  htonl(INADDR_ANY);  /* Any incoming interface */
    multicastAddr.sin_port = htons(1900);      /* Multicast port */

    /* Bind to the multicast port */
    int ret = bind(self->listen_fd, (struct sockaddr *) &multicastAddr, sizeof(multicastAddr));
    if (ret < 0) {
        ucs_debug("init_multicast_group: bind socket failed.");
        return UCS_ERR_INVALID_PARAM;
    }

    char ndev_name[IFNAMSIZ];
    uct_ib_device_t *dev = uct_ib_iface_device(&self->super.super.super);
    uint8_t port_num     = self->super.super.super.config.port_num;
    uint8_t gid_index    = self->super.super.super.gid_info.gid_index;

    status = uct_ib_device_get_roce_ndev_name(dev, port_num, gid_index,
                                              ndev_name, sizeof(ndev_name));
    if (status != UCS_OK) {
        ucs_debug("init_multicast_group: device name failed to load.");
        return UCS_ERR_INVALID_PARAM;
    }
    struct ifreq ifr;

    status = ucs_netif_ioctl(ndev_name, SIOCGIFADDR, &ifr);
    ucs_debug("device ip loaded %s.", inet_ntoa(((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr));

    if (status != UCS_OK) {
        return UCS_ERR_INVALID_PARAM;
    }
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

    status =  ucs_socket_setopt(self->listen_fd, IPPROTO_IP, IP_ADD_MEMBERSHIP,(void *) &multicastRequest,
            sizeof(multicastRequest));
    if (status != UCS_OK) {
        return UCS_ERR_INVALID_PARAM;
    }
    sleep(1);
    return UCS_OK;
}

static uct_ud_iface_ops_t ops;

UCS_CLASS_INIT_FUNC(uct_ud_mcast_verbs_iface_t,
                    uct_md_h md, uct_worker_h worker,
                    const uct_iface_params_t *params,
                    const uct_iface_config_t *tl_config)
{
    ucs_status_t status = UCS_OK;

    ucs_trace_func("");

    init_ops(self, &ops);
    
    UCS_CLASS_CALL_SUPER_INIT(uct_ud_verbs_iface_t, &ops,
                              md, worker, params,tl_config)
                              
    self->super.super.is_mcast_iface = 1;

    self->coll_id  = params->global_info.proc_idx;
    self->coll_cnt = params->global_info.proc_cnt;

    status = init_multicast_group(self);

    if (self->coll_id == 0) {
        self->num_of_peers     = self->coll_cnt;
        self->acked_psn_by_src = ucs_malloc(sizeof(*self->acked_psn_by_src)*self->num_of_peers, "alloc acked_psn");
        if(!self->acked_psn_by_src) {
            return UCS_ERR_NO_MEMORY;
        }
        for (int i = 0; i < self->num_of_peers; i++) {
            self->acked_psn_by_src[i] = UCT_UD_INITIAL_PSN - 1;
        }
    } else {
        self->num_of_peers     = 0;
        self->acked_psn_by_src = NULL;
    }

    return status;
}


UCS_CLASS_CLEANUP_FUNC(uct_ud_mcast_verbs_iface_t)
{
    ucs_trace_func("");
}

UCS_CLASS_DEFINE(uct_ud_mcast_verbs_iface_t, uct_ud_verbs_iface_t);

UCS_CLASS_DEFINE_NEW_FUNC(uct_ud_mcast_verbs_iface_t, uct_iface_t, uct_md_h,
                                 uct_worker_h, const uct_iface_params_t*,
                                 const uct_iface_config_t*);

UCS_CLASS_DEFINE_DELETE_FUNC(uct_ud_mcast_verbs_iface_t, uct_iface_t);

static ucs_status_t
uct_ud_mcast_verbs_query_tl_devices(uct_md_h md,
                              uct_tl_device_resource_t **tl_devices_p,
                              unsigned *num_tl_devices_p)
{
    uct_ib_md_t *ib_md = ucs_derived_of(md, uct_ib_md_t);
    return uct_ib_device_query_ports(&ib_md->dev, 0, tl_devices_p,
                                     num_tl_devices_p);
}

UCT_TL_DEFINE(&uct_ib_component, ud_mcast_verbs, uct_ud_mcast_verbs_query_tl_devices,
              uct_ud_mcast_verbs_iface_t, "UD_MV_", uct_ud_mcast_verbs_iface_config_table,
              uct_ud_iface_config_t);
