/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2019.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#include "mm_coll_iface.h"
#include "mm_coll_ep.h"

static ucs_status_t uct_mm_bcast_iface_query(uct_iface_h tl_iface,
                                             uct_iface_attr_t *iface_attr)
{
    ucs_status_t status = uct_mm_coll_iface_query(tl_iface, iface_attr);
     if (status != UCS_OK) {
         return status;
     }

    uct_mm_coll_iface_t *iface   = ucs_derived_of(tl_iface, uct_mm_coll_iface_t);
    size_t bcast_completions     = iface->sm_proc_cnt * UCS_SYS_CACHE_LINE_SIZE;
    iface_attr->cap.flags       |= UCT_IFACE_FLAG_BCAST;
    iface_attr->cap.am.max_short = iface->super.config.fifo_elem_size -
                                   sizeof(uct_mm_coll_fifo_element_t) -
                                   bcast_completions;
    iface_attr->cap.am.max_bcopy = iface->super.config.seg_size -
                                   bcast_completions;

    /* Update expected performance */
    // TODO: measure and update these numbers
    iface_attr->latency             = ucs_linear_func_make(80e-9, 0); /* 80 ns */
    iface_attr->bandwidth.dedicated = iface->super.super.config.bandwidth;
    iface_attr->bandwidth.shared    = 0;
    iface_attr->overhead_short      = 11e-9; /* 10 ns */
    iface_attr->overhead_bcopy      = 12e-9; /* 11 ns */

    return UCS_OK;
}

static int uct_mm_bcast_iface_release_shared_desc_func(uct_iface_h iface,
                                                       uct_recv_desc_t *self,
                                                       void *desc)
{
    *(uint8_t*)self = 1;
    return 1;
}

static UCS_CLASS_DECLARE_DELETE_FUNC(uct_mm_bcast_iface_t, uct_iface_t);

static uct_iface_ops_t uct_mm_bcast_iface_ops = {
    .ep_am_short               = uct_mm_bcast_ep_am_short,
    .ep_am_bcopy               = uct_mm_bcast_ep_am_bcopy,
    .ep_am_zcopy               = uct_mm_bcast_ep_am_zcopy,
    .ep_pending_add            = uct_mm_ep_pending_add,
    .ep_pending_purge          = uct_mm_ep_pending_purge,
    .ep_flush                  = uct_mm_ep_flush,
    .ep_fence                  = uct_sm_ep_fence,
    .ep_create                 = uct_mm_bcast_ep_create,
    .ep_destroy                = uct_mm_coll_ep_destroy,
    .iface_flush               = uct_mm_iface_flush,
    .iface_fence               = uct_sm_iface_fence,
    .iface_progress            = uct_mm_bcast_iface_progress,
    .iface_progress_enable     = uct_base_iface_progress_enable,
    .iface_progress_disable    = uct_base_iface_progress_disable,
    .iface_event_fd_get        = uct_mm_iface_event_fd_get,
    .iface_event_arm           = uct_mm_iface_event_fd_arm,
    .iface_close               = UCS_CLASS_DELETE_FUNC_NAME(uct_mm_bcast_iface_t),
    .iface_query               = uct_mm_bcast_iface_query,
    .iface_get_device_address  = uct_sm_iface_get_device_address,
    .iface_get_address         = uct_mm_coll_iface_get_address,
    .iface_is_reachable        = uct_mm_coll_iface_is_reachable,
    .iface_release_shared_desc = uct_mm_bcast_iface_release_shared_desc_func
};

UCS_CLASS_INIT_FUNC(uct_mm_bcast_iface_t, uct_md_h md, uct_worker_h worker,
                    const uct_iface_params_t *params,
                    const uct_iface_config_t *tl_config)
{
    uint32_t procs = ((params->field_mask & UCT_IFACE_PARAM_FIELD_COLL_INFO) &&
                      (params->host_info.proc_cnt > 2)) ?
                     params->host_info.proc_cnt : 2;

    uct_mm_iface_config_t *mm_config = ucs_derived_of(tl_config,
                                                      uct_mm_iface_config_t);
    mm_config->fifo_elem_size       += procs * UCS_SYS_CACHE_LINE_SIZE;

    UCS_CLASS_CALL_SUPER_INIT(uct_mm_coll_iface_t, &uct_mm_bcast_iface_ops, md,
                              worker, params, tl_config);

    UCT_MM_COLL_BCAST_EP_DUMMY(dummy, self);

    int i;
    uct_mm_coll_fifo_element_t *elem;
    for (i = 0, elem = self->super.super.recv_fifo_elems;
         i < self->super.super.config.fifo_size;
         i++, elem = UCS_PTR_BYTE_OFFSET(elem,
                 self->super.super.config.fifo_elem_size)) {
        uct_mm_coll_ep_centralized_reset_bcast_elem(elem, &dummy, 0);
        uct_mm_coll_ep_centralized_reset_bcast_elem(elem, &dummy, 1);

        elem->pending = 0;
    }

    mm_config->fifo_elem_size -= procs * UCS_SYS_CACHE_LINE_SIZE;

    return UCS_OK;
}


UCS_CLASS_CLEANUP_FUNC(uct_mm_bcast_iface_t) {}

UCS_CLASS_DEFINE(uct_mm_bcast_iface_t, uct_mm_coll_iface_t);

UCS_CLASS_DEFINE_NEW_FUNC(uct_mm_bcast_iface_t, uct_iface_t, uct_md_h,
                          uct_worker_h, const uct_iface_params_t*,
                          const uct_iface_config_t*);

static UCS_CLASS_DEFINE_DELETE_FUNC(uct_mm_bcast_iface_t, uct_iface_t);
