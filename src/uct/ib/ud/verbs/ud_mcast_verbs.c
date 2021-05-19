/**
* Copyright (C) Huawei Technologies Co., Ltd. 2021.  ALL RIGHTS RESERVED.
*
* See file LICENSE for terms.
*/

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "ud_verbs.h"

#include <uct/ib/ud/base/ud_mcast.h>

extern uct_ud_iface_ops_t uct_ud_verbs_iface_ops;
extern uct_iface_ops_t    uct_ud_verbs_iface_tl_ops;
extern ucs_config_field_t uct_ud_verbs_iface_config_table[];

ucs_config_field_t uct_ud_mcast_verbs_iface_config_table[] = {
    {"MC_", "", NULL, 0,
     UCS_CONFIG_TYPE_TABLE(uct_ud_verbs_iface_config_table)},

    {NULL}
};

void UCS_CLASS_DELETE_FUNC_NAME(uct_ud_mcast_verbs_iface_t)(uct_iface_t*);

static ucs_status_t uct_ud_mcast_verbs_ep_am_short(uct_ep_h tl_ep,
                                                   uint8_t id,
                                                   uint64_t hdr,
                                                   const void *buffer,
                                                   unsigned length)
{
    unsigned orig_length = UCT_COLL_DTYPE_MODE_UNPACK_VALUE(length);

    return uct_ud_verbs_ep_am_short(tl_ep, id, hdr, buffer, orig_length);
}

static ucs_status_t uct_ud_mcast_verbs_ep_am_short_iov(uct_ep_h tl_ep,
                                                       uint8_t id,
                                                       const uct_iov_t *iov,
                                                       size_t iovcnt)
{
    size_t orig_iovcnt = UCT_COLL_DTYPE_MODE_UNPACK_VALUE(iovcnt);

    return uct_ud_verbs_ep_am_short_iov(tl_ep, id, iov, orig_iovcnt);
}

static ssize_t uct_ud_mcast_verbs_ep_am_bcopy(uct_ep_h tl_ep, uint8_t id,
                                              uct_pack_callback_t pack_cb,
                                              void *arg, unsigned flags)
{
    unsigned orig_flags = UCT_COLL_DTYPE_MODE_UNPACK_VALUE(flags);

    return uct_ud_verbs_ep_am_bcopy(tl_ep, id, pack_cb, arg, orig_flags);
}

static void uct_ud_mcast_verbs_init_ops(uct_iface_ops_t *tl_ops)
{
    *tl_ops                 = uct_ud_verbs_iface_tl_ops;
    tl_ops->ep_am_short     = uct_ud_mcast_verbs_ep_am_short;
    tl_ops->ep_am_short_iov = uct_ud_mcast_verbs_ep_am_short_iov;
    tl_ops->ep_am_bcopy     = uct_ud_mcast_verbs_ep_am_bcopy;
}

UCS_CLASS_INIT_FUNC(uct_ud_mcast_verbs_iface_t,
                    uct_md_h md, uct_worker_h worker,
                    const uct_iface_params_t *params,
                    const uct_iface_config_t *tl_config)
{
    uct_iface_ops_t mcast_ops;

    ucs_trace_func("");

    uct_ud_mcast_verbs_init_ops(&mcast_ops);
    
    UCS_CLASS_CALL_SUPER_INIT(uct_ud_verbs_iface_t, &uct_ud_verbs_iface_ops,
                              &mcast_ops, md, worker, params, tl_config)

    return uct_ud_mcast_iface_init(&self->super.super, &self->mcast, params);
}

UCS_CLASS_CLEANUP_FUNC(uct_ud_mcast_verbs_iface_t)
{
    ucs_trace_func("");
}

UCS_CLASS_DEFINE(uct_ud_mcast_verbs_iface_t, uct_ud_verbs_iface_t);

UCS_CLASS_DEFINE_NEW_FUNC(uct_ud_mcast_verbs_iface_t,
                          uct_iface_t,
                          uct_md_h, uct_worker_h,
                          const uct_iface_params_t*,
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

UCT_TL_DEFINE(&uct_ib_component, ud_mcast_verbs,
              uct_ud_mcast_verbs_query_tl_devices,
              uct_ud_mcast_verbs_iface_t, "MC_UD_VERBS_",
              uct_ud_mcast_verbs_iface_config_table,
              uct_ud_iface_config_t);
