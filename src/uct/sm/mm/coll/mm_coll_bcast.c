/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2019.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#include "mm_coll_iface.h"
#include "mm_coll_ep.h"

ucs_config_field_t uct_mm_bcast_iface_config_table[] = {
    {"COLL_", "", NULL,
     ucs_offsetof(uct_mm_bcast_iface_config_t, super),
     UCS_CONFIG_TYPE_TABLE(uct_mm_coll_iface_config_table)},

    {NULL}
};

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
    uct_mm_bcast_iface_t* biface = ucs_derived_of(iface, uct_mm_bcast_iface_t);
    uint8_t my_coll_id = biface->super.my_coll_id;
    uint8_t slot_idx;
    uint8_t slot_cnt = biface->super.sm_proc_cnt;
    size_t slot_offset = (size_t)self;
    uint8_t* slot_ptr = (uint8_t*)desc + biface->super.super.rx_headroom + slot_offset;
    for (slot_idx = 0; slot_idx < slot_cnt; slot_idx++) {
        if (slot_ptr[0] == my_coll_id) {
            /* mark done */
            slot_ptr[UCS_SYS_CACHE_LINE_SIZE - 1] = 1;
            return 1;
        }
        slot_ptr += UCS_SYS_CACHE_LINE_SIZE;
    }

    /* Should never happen */
    ucs_assert(0); 
    return 1;
}

static UCS_CLASS_DECLARE_DELETE_FUNC(uct_mm_bcast_iface_t, uct_iface_t);

static uct_iface_ops_t uct_mm_bcast_iface_ops = {
/*
 *  .ep_am_short               = uct_mm_bcast_ep_am_short_batched/centralized,
 *  .ep_am_bcopy               = uct_mm_bcast_ep_am_bcopy_batched/centralized,
 */
    .ep_am_zcopy               = uct_mm_bcast_ep_am_zcopy,
    .ep_pending_add            = uct_mm_ep_pending_add,
    .ep_pending_purge          = uct_mm_ep_pending_purge,
    .ep_flush                  = uct_mm_coll_ep_flush,
    .ep_fence                  = uct_sm_ep_fence,
    .ep_create                 = uct_mm_bcast_ep_create,
    .ep_destroy                = uct_mm_bcast_ep_destroy,
    .iface_flush               = uct_mm_iface_flush,
    .iface_fence               = uct_sm_iface_fence,
/*
 *  .iface_progress            = uct_mm_bcast_iface_progress,
 *  .iface_progress_enable     = uct_base_iface_progress_enable,
 *  .iface_progress_disable    = uct_base_iface_progress_disable,
 */
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
    uint32_t procs = (params->field_mask & UCT_IFACE_PARAM_FIELD_COLL_INFO) ?
                     params->host_info.proc_cnt : 1;

    uct_mm_bcast_iface_config_t *cfg = ucs_derived_of(tl_config,
                                                      uct_mm_bcast_iface_config_t);
    int is_centralized               = procs > cfg->super.batched_thresh;
    cfg->super.super.fifo_elem_size += procs * UCS_SYS_CACHE_LINE_SIZE;

    self->last_nonzero_ep = NULL;
    self->poll_ep_idx     = 0;

    if (procs == 1) {
        worker                                        = NULL;
        uct_mm_bcast_iface_ops.iface_progress         = uct_mm_iface_progress_dummy;
        uct_mm_bcast_iface_ops.iface_progress_enable  = uct_mm_iface_progress_enable_dummy;
        uct_mm_bcast_iface_ops.iface_progress_disable = uct_mm_iface_progress_disable_dummy;
    } else if (is_centralized) {
        uct_mm_bcast_iface_ops.ep_am_short            = uct_mm_bcast_ep_am_short_centralized;
        uct_mm_bcast_iface_ops.ep_am_bcopy            = uct_mm_bcast_ep_am_bcopy_centralized;
        uct_mm_bcast_iface_ops.iface_progress         = uct_mm_bcast_iface_progress;
        uct_mm_bcast_iface_ops.iface_progress_enable  = uct_base_iface_progress_enable;
        uct_mm_bcast_iface_ops.iface_progress_disable = uct_base_iface_progress_disable;
    } else {
        uct_mm_bcast_iface_ops.ep_am_short            = uct_mm_bcast_ep_am_short_batched;
        uct_mm_bcast_iface_ops.ep_am_bcopy            = uct_mm_bcast_ep_am_bcopy_batched;
        uct_mm_bcast_iface_ops.iface_progress         = uct_mm_bcast_iface_progress;
        uct_mm_bcast_iface_ops.iface_progress_enable  = uct_base_iface_progress_enable;
        uct_mm_bcast_iface_ops.iface_progress_disable = uct_base_iface_progress_disable;
    }

    UCS_CLASS_CALL_SUPER_INIT(uct_mm_coll_iface_t, &uct_mm_bcast_iface_ops, md,
                              worker, params, tl_config);

    if (procs == 1) {
        return UCS_OK;
    }

    UCT_MM_COLL_EP_DUMMY(dummy, self);

    uct_mm_coll_fifo_element_t *elem = self->super.super.recv_fifo_elems;
    ucs_assert_always(((uintptr_t)elem % UCS_SYS_CACHE_LINE_SIZE) == 0);
    ucs_assert_always((sizeof(*elem)   % UCS_SYS_CACHE_LINE_SIZE) == 0);

    int i;
    for (i = 0; i < self->super.super.config.fifo_size; i++) {
        uct_mm_coll_ep_centralized_reset_elem(elem, &dummy, 0, 0);
        uct_mm_coll_ep_centralized_reset_elem(elem, &dummy, 1, 0);

        elem->pending = 0;

        elem = UCS_PTR_BYTE_OFFSET(elem, self->super.super.config.fifo_elem_size);
    }


    cfg->super.super.fifo_elem_size -= procs * UCS_SYS_CACHE_LINE_SIZE;

    return UCS_OK;
}


UCS_CLASS_CLEANUP_FUNC(uct_mm_bcast_iface_t) {}

UCS_CLASS_DEFINE(uct_mm_bcast_iface_t, uct_mm_coll_iface_t);

UCS_CLASS_DEFINE_NEW_FUNC(uct_mm_bcast_iface_t, uct_iface_t, uct_md_h,
                          uct_worker_h, const uct_iface_params_t*,
                          const uct_iface_config_t*);

static UCS_CLASS_DEFINE_DELETE_FUNC(uct_mm_bcast_iface_t, uct_iface_t);
