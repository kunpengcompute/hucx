/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2019.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#include "mm_coll_iface.h"
#include "mm_coll_ep.h"

ucs_config_field_t uct_mm_incast_iface_config_table[] = {
    {"COLL_", "", NULL,
     ucs_offsetof(uct_mm_incast_iface_config_t, super),
     UCS_CONFIG_TYPE_TABLE(uct_mm_coll_iface_config_table)},

    {NULL}
};

static ucs_status_t uct_mm_incast_iface_query(uct_iface_h tl_iface,
                                              uct_iface_attr_t *iface_attr)
{
    ucs_status_t status = uct_mm_coll_iface_query(tl_iface, iface_attr);
     if (status != UCS_OK) {
         return status;
     }

    iface_attr->cap.flags |= UCT_IFACE_FLAG_INCAST;

    /* Set the message length limits */
    uct_mm_coll_iface_t *iface   = ucs_derived_of(tl_iface, uct_mm_coll_iface_t);
    iface_attr->cap.am.max_short = ((iface->super.config.fifo_elem_size -
                                     sizeof(uct_mm_coll_fifo_element_t)) /
                                    iface->sm_proc_cnt) - 1;
    iface_attr->cap.am.max_bcopy = (iface->super.config.seg_size /
                                    iface->sm_proc_cnt) - 1;

    /* Update expected performance */
    // TODO: measure and update these numbers
    iface_attr->latency             = ucs_linear_func_make(80e-9, 0); /* 80 ns */
    iface_attr->bandwidth.dedicated = iface->super.super.config.bandwidth;
    iface_attr->bandwidth.shared    = 0;
    iface_attr->overhead_short      = 11e-9; /* 10 ns */
    iface_attr->overhead_bcopy      = 12e-9; /* 11 ns */

    return UCS_OK;
}

/**
 * This function is used in some of the cases where a descriptor is being
 * released by the upper layers, during a call to @ref uct_iface_release_desc .
 * Specifically, it is used in cases where the descriptor actually belongs to
 * a remote peer (unlike in @ref uct_mm_ep_release_desc , where the descriptor
 * is guaranteed to be allocated by the same process). These cases are a result
 * of a broadcast, originating from a remote process (via shared memory), where
 * the incoming message had to be handled asynchronously (UCS_INPROGRESS).
 *
 * The function has a complicated task: finding the (MM_COLL_)element which uses
 * the given descriptor, and in this element mark the broadcast as completed
 * (for this worker at least, not necessarily on all the destination workers).
 * Basically, since each (MM_)endpoint keeps a hash-table mapping of segment IDs
 * to segment based addresses - we use that to find the right element (and the
 * rest is easy).
 *
 * @note This looks very inefficient, on first glance. If we have X outstanding
 * bcast elements (if I'm a slow reciever - those could all be waiting on me to
 * process them!) - we may end up checking each of those X, testing if the given
 * descriptor (to be released) belongs to one of them. But in fact, most cases
 * will belong to the first couple of elements, so this is in fact not so bad.
 */
static int uct_mm_incast_iface_release_shared_desc(uct_iface_h tl_iface,
                                                   uct_recv_desc_t *self,
                                                   void *desc)
{
    unsigned index;
    uct_mm_coll_ep_t *ep;
    uintptr_t src_coll_id        = (uintptr_t)self;
    uct_mm_incast_iface_t *iface = ucs_derived_of(tl_iface, uct_mm_incast_iface_t);

    /* Find the endpoint - based on the ID of the sender of this descriptor */
    ucs_ptr_array_for_each(ep, index, &iface->super.ep_ptrs) {
        if (ep->remote_id == src_coll_id) {
            uct_mm_coll_ep_release_desc(ep, desc);
            return 1;
        }
    }

    ucs_error("Failed to find the given shared descriptor in their list");
    ucs_assert(0); /* Should never happen... */
    return 0;
}

static UCS_CLASS_DECLARE_DELETE_FUNC(uct_mm_incast_iface_t, uct_iface_t);

static uct_iface_ops_t uct_mm_incast_iface_ops = {
/*
 *  .ep_am_short               = uct_mm_incast_ep_am_short_batched/centralized,
 *  .ep_am_bcopy               = uct_mm_incast_ep_am_bcopy_batched/centralized,
 */
    .ep_am_zcopy               = uct_mm_incast_ep_am_zcopy,
    .ep_pending_add            = uct_mm_ep_pending_add,
    .ep_pending_purge          = uct_mm_ep_pending_purge,
    .ep_flush                  = uct_mm_coll_ep_flush,
    .ep_fence                  = uct_sm_ep_fence,
    .ep_create                 = uct_mm_incast_ep_create,
    .ep_destroy                = uct_mm_coll_ep_destroy,
    .iface_flush               = uct_mm_iface_flush,
    .iface_fence               = uct_sm_iface_fence,
/*
 *  .iface_progress            = uct_mm_incast_iface_progress,
 *  .iface_progress_enable     = uct_base_iface_progress_enable,
 *  .iface_progress_disable    = uct_base_iface_progress_disable,
 */
    .iface_close               = UCS_CLASS_DELETE_FUNC_NAME(uct_mm_incast_iface_t),
    .iface_query               = uct_mm_incast_iface_query,
    .iface_get_device_address  = uct_sm_iface_get_device_address,
    .iface_get_address         = uct_mm_coll_iface_get_address,
    .iface_is_reachable        = uct_mm_coll_iface_is_reachable,
    .iface_release_shared_desc = uct_mm_incast_iface_release_shared_desc
};

#define UCT_MM_INCAST_IFACE_CB_SUBCASE(_operator, _operand) \
    *cb = UCT_MM_INCAST_IFACE_CB_NAME(helper, _operator, _operand); \
    uct_mm_incast_iface_ops.ep_am_short = \
        UCT_MM_INCAST_IFACE_CB_NAME(short, _operator, _operand); \
    uct_mm_incast_iface_ops.ep_am_bcopy = \
        UCT_MM_INCAST_IFACE_CB_NAME(bcopy, _operator, _operand);

#define UCT_MM_INCAST_IFACE_CB_CASE(_operand, _operator) \
    switch (_operand) { \
    case UCT_INCAST_OPERAND_FLOAT: \
        UCT_MM_INCAST_IFACE_CB_SUBCASE(_operator, float) \
        break; \
    case UCT_INCAST_OPERAND_DOUBLE: \
        UCT_MM_INCAST_IFACE_CB_SUBCASE(_operator, double) \
        break; \
    default: \
        return UCS_ERR_UNSUPPORTED; \
    }

static ucs_status_t
uct_mm_incast_iface_choose_am_send(uct_incast_cb_t *cb,
                                   uct_ep_am_short_func_t *ep_am_short_p,
                                   uct_ep_am_bcopy_func_t *ep_am_bcopy_p)
{
    uct_incast_operand_t operand   = UCT_INCAST_CALLBACK_UNPACK_OPERAND(*cb);
    uct_incast_operator_t operator = UCT_INCAST_CALLBACK_UNPACK_OPERATOR(*cb);

    switch (operator) {
    case UCT_INCAST_OPERATOR_SUM:
        UCT_MM_INCAST_IFACE_CB_CASE(operand, sum)
        break;

    case UCT_INCAST_OPERATOR_MIN:
        UCT_MM_INCAST_IFACE_CB_CASE(operand, min)
        break;

    case UCT_INCAST_OPERATOR_MAX:
        UCT_MM_INCAST_IFACE_CB_CASE(operand, max)
        break;

    case UCT_INCAST_OPERATOR_CB:
        uct_mm_incast_iface_ops.ep_am_short =
                uct_mm_incast_ep_am_short_centralized_ep_cb;
        uct_mm_incast_iface_ops.ep_am_bcopy =
                uct_mm_incast_ep_am_bcopy_centralized_ep_cb;
        *cb = (uct_incast_cb_t)((uintptr_t)*cb & ~UCT_INCAST_OPERATOR_MASK);
        break;

    default:
        return UCS_ERR_UNSUPPORTED;
    };

    return UCS_OK;
}

UCS_CLASS_INIT_FUNC(uct_mm_incast_iface_t, uct_md_h md, uct_worker_h worker,
                    const uct_iface_params_t *params,
                    const uct_iface_config_t *tl_config)
{
    uint32_t procs = (params->field_mask & UCT_IFACE_PARAM_FIELD_COLL_INFO) ?
                     params->host_info.proc_cnt : 0;

    uct_mm_incast_iface_config_t *cfg = ucs_derived_of(tl_config,
                                                       uct_mm_incast_iface_config_t);
    int is_centralized                = procs > cfg->super.batched_thresh;
    unsigned orig_fifo_elem_size      = cfg->super.super.fifo_elem_size;
    size_t short_stride               = ucs_align_up(orig_fifo_elem_size -
                                                     sizeof(uct_mm_coll_fifo_element_t),
                                                     UCS_SYS_CACHE_LINE_SIZE);
    cfg->super.super.fifo_elem_size   = sizeof(uct_mm_coll_fifo_element_t) +
                                        (procs * short_stride);

    if (procs == 0) {
        worker                                         = NULL;
        uct_mm_incast_iface_ops.iface_progress         = uct_mm_iface_progress_dummy;
        uct_mm_incast_iface_ops.iface_progress_enable  = uct_mm_iface_progress_enable_dummy;
        uct_mm_incast_iface_ops.iface_progress_disable = uct_mm_iface_progress_disable_dummy;
    } else if (is_centralized) {
        if (params->field_mask & UCT_IFACE_PARAM_FIELD_INCAST_CB) {
            self->cb = params->incast_cb;

            ucs_status_t status = uct_mm_incast_iface_choose_am_send(&self->cb,
                    &uct_mm_incast_iface_ops.ep_am_short,
                    &uct_mm_incast_iface_ops.ep_am_bcopy);
            if (status != UCS_OK) {
                return status;
            }

            uct_mm_incast_iface_ops.iface_progress     = uct_mm_incast_iface_progress_cb;
        } else {
            uct_mm_incast_iface_ops.ep_am_short        = uct_mm_incast_ep_am_short_centralized;
            uct_mm_incast_iface_ops.ep_am_bcopy        = uct_mm_incast_ep_am_bcopy_centralized;
            uct_mm_incast_iface_ops.iface_progress     = uct_mm_incast_iface_progress;
            self->cb = NULL;
        }
        uct_mm_incast_iface_ops.iface_progress_enable  = uct_base_iface_progress_enable;
        uct_mm_incast_iface_ops.iface_progress_disable = uct_base_iface_progress_disable;
    } else {
        uct_mm_incast_iface_ops.ep_am_short            = uct_mm_incast_ep_am_short_batched;
        uct_mm_incast_iface_ops.ep_am_bcopy            = uct_mm_incast_ep_am_bcopy_batched;
        uct_mm_incast_iface_ops.iface_progress         = uct_mm_incast_iface_progress;
        uct_mm_incast_iface_ops.iface_progress_enable  = uct_base_iface_progress_enable;
        uct_mm_incast_iface_ops.iface_progress_disable = uct_base_iface_progress_disable;
    }

    UCS_CLASS_CALL_SUPER_INIT(uct_mm_coll_iface_t, &uct_mm_incast_iface_ops,
                              md, worker, params, tl_config);

    if (procs == 0) {
        return UCS_OK;
    }

    uct_mm_coll_fifo_element_t *elem = self->super.super.recv_fifo_elems;
    ucs_assert_always(((uintptr_t)elem % UCS_SYS_CACHE_LINE_SIZE) == 0);
    ucs_assert_always((sizeof(*elem)   % UCS_SYS_CACHE_LINE_SIZE) == 0);

    int i;
    ucs_status_t status;
    for (i = 0; i < self->super.super.config.fifo_size; i++) {
        elem->pending = 0;

        status = ucs_spinlock_init(&elem->lock, UCS_SPINLOCK_FLAG_SHARED);
        if (status != UCS_OK) {
            goto destory_elements;
        }

        elem = UCS_PTR_BYTE_OFFSET(elem, self->super.super.config.fifo_elem_size);
    }

    cfg->super.super.fifo_elem_size = orig_fifo_elem_size;

    return UCS_OK;

destory_elements:
    while (i--) {
        ucs_spinlock_destroy(&UCT_MM_COLL_IFACE_GET_FIFO_ELEM(self, i)->lock);
    }

    UCS_CLASS_CLEANUP(uct_mm_iface_t, self);
    return status;
}

UCS_CLASS_CLEANUP_FUNC(uct_mm_incast_iface_t)
{
    int i;

    if (self->super.super.super.super.worker == NULL) {
        return;
    }

    uct_mm_coll_fifo_element_t *fifo_elem_p;
    for (i = 0; i < self->super.super.config.fifo_size; i++) {
        fifo_elem_p = UCT_MM_COLL_IFACE_GET_FIFO_ELEM(self, i);
        ucs_spinlock_destroy(&fifo_elem_p->lock);
    }
}

UCS_CLASS_DEFINE(uct_mm_incast_iface_t, uct_mm_coll_iface_t);

UCS_CLASS_DEFINE_NEW_FUNC(uct_mm_incast_iface_t, uct_iface_t, uct_md_h,
                          uct_worker_h, const uct_iface_params_t*,
                          const uct_iface_config_t*);

static UCS_CLASS_DEFINE_DELETE_FUNC(uct_mm_incast_iface_t, uct_iface_t);
