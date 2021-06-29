/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2019.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#include <ucs/sys/string.h>
#include <uct/sm/base/sm_ep.h>
#include <uct/sm/base/sm_iface.h>
#include <uct/sm/mm/base/mm_ep.h>
#include <ucs/debug/memtrack_int.h>

#include "mm_coll_iface.h"
#include "mm_coll_ep.h"

ucs_config_field_t uct_mm_coll_iface_config_table[] = {
    {"SM_", "", NULL,
     ucs_offsetof(uct_mm_coll_iface_config_t, super),
     UCS_CONFIG_TYPE_TABLE(uct_mm_iface_config_table)},

    {NULL}
};

ucs_status_t uct_mm_coll_iface_query(uct_iface_h tl_iface,
                                     uct_iface_attr_t *iface_attr)
{
    uct_mm_base_iface_t *iface = ucs_derived_of(tl_iface, uct_mm_base_iface_t);
    uct_mm_md_t         *md    = ucs_derived_of(iface->super.super.md, uct_mm_md_t);

    ucs_status_t status = uct_mm_iface_query(tl_iface, iface_attr);
    if (status != UCS_OK) {
        return status;
    }

    iface_attr->cap.atomic32.op_flags     =
    iface_attr->cap.atomic32.fop_flags    =
    iface_attr->cap.atomic64.op_flags     =
    iface_attr->cap.atomic64.fop_flags    = 0; /* TODO: use in MPI_Accumulate */
    iface_attr->cap.event_flags           = UCT_IFACE_FLAG_EVENT_SEND_COMP;
    iface_attr->iface_addr_len            = sizeof(uct_mm_coll_iface_addr_t) +
                                            md->iface_addr_len;
    iface_attr->cap.flags                 = UCT_IFACE_FLAG_AM_SHORT          |
                                            UCT_IFACE_FLAG_AM_BCOPY          |
                                            UCT_IFACE_FLAG_PENDING           |
                                            UCT_IFACE_FLAG_CB_SYNC           |
                                            UCT_IFACE_FLAG_CONNECT_TO_IFACE;
    iface_attr->cap.am.coll_mode_flags    = UCS_BIT(UCT_COLL_DTYPE_MODE_PADDED) |
                                            UCS_BIT(UCT_COLL_DTYPE_MODE_PACKED);
    iface_attr->cap.coll_mode.short_flags = UCS_BIT(UCT_COLL_DTYPE_MODE_PADDED) |
                                            UCS_BIT(UCT_COLL_DTYPE_MODE_PACKED);
    iface_attr->cap.coll_mode.bcopy_flags = UCS_BIT(UCT_COLL_DTYPE_MODE_PADDED) |
                                            UCS_BIT(UCT_COLL_DTYPE_MODE_PACKED);
    iface_attr->cap.coll_mode.zcopy_flags = 0; /* TODO: implement... */

    return UCS_OK;
}

ucs_status_t uct_mm_coll_iface_get_address(uct_iface_t *tl_iface,
                                           uct_iface_addr_t *addr)
{
    uct_mm_coll_iface_addr_t *iface_addr = (void*)addr;
    uct_mm_coll_iface_t *coll_iface      = ucs_derived_of(tl_iface,
                                                          uct_mm_coll_iface_t);

    if (!coll_iface->super.super.super.worker) {
        uct_mm_md_t *md = ucs_derived_of(coll_iface->super.super.super.md,
                                         uct_mm_md_t);

        memset(addr, 0, sizeof(uct_mm_coll_iface_addr_t) + md->iface_addr_len);
        iface_addr->coll_id = (uint32_t)-1;

        return UCS_OK;
    }

    iface_addr->coll_id = coll_iface->my_coll_id;

    return uct_mm_iface_get_address(tl_iface,
                                    (uct_iface_addr_t*)&iface_addr->super);
}

int uct_mm_coll_iface_is_reachable(const uct_iface_h tl_iface,
                                   const uct_device_addr_t *dev_addr,
                                   const uct_iface_addr_t *tl_iface_addr)
{
    uct_mm_coll_iface_addr_t *iface_addr = (void*)tl_iface_addr;

    return uct_mm_iface_is_reachable(tl_iface, dev_addr,
                                     (uct_iface_addr_t*)&iface_addr->super);
}

UCS_CLASS_INIT_FUNC(uct_mm_coll_iface_t, uct_iface_ops_t *ops, uct_md_h md,
                    uct_worker_h worker, const uct_iface_params_t *params,
                    const uct_iface_config_t *tl_config)
{
    /* check the value defining the size of the FIFO element */
    uct_mm_iface_config_t *mm_config = ucs_derived_of(tl_config,
                                                      uct_mm_iface_config_t);
    if (mm_config->fifo_elem_size < sizeof(uct_mm_coll_fifo_element_t)) {
        ucs_error("The UCX_MM_FIFO_ELEM_SIZE parameter (%u) must be larger "
                  "than, or equal to, the FIFO element header size (%ld bytes).",
                  mm_config->fifo_elem_size, sizeof(uct_mm_coll_fifo_element_t));
        return UCS_ERR_INVALID_PARAM;
    }

    if (params->field_mask & UCT_IFACE_PARAM_FIELD_COLL_INFO) {
        self->my_coll_id  = params->host_info.proc_idx;
        self->sm_proc_cnt = params->host_info.proc_cnt;
    } else {
        self->my_coll_id  = 0;
        self->sm_proc_cnt = 1;
        worker = NULL;
    }

    UCS_CLASS_CALL_SUPER_INIT(uct_mm_base_iface_t, ops, md, worker, params,
                              tl_config);

    ucs_ptr_array_init(&self->ep_ptrs, "mm_coll_eps");

    self->loopback_ep = NULL;

    return UCS_OK;
}

UCS_CLASS_CLEANUP_FUNC(uct_mm_coll_iface_t)
{
    ucs_ptr_array_cleanup(&self->ep_ptrs);
}

UCS_CLASS_DEFINE(uct_mm_coll_iface_t, uct_mm_base_iface_t);

unsigned uct_mm_iface_progress_dummy(uct_iface_h tl_iface) { return 0; }

void uct_mm_iface_progress_enable_dummy(uct_iface_h tl_iface, unsigned flags) {}

void uct_mm_iface_progress_disable_dummy(uct_iface_h tl_iface, unsigned flags) {}
