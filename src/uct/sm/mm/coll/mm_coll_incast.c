/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2019.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#include "mm_coll_iface.h"
#include "mm_coll_ep.h"

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

static UCS_F_ALWAYS_INLINE unsigned
uct_mm_incast_iface_poll_fifo(uct_mm_incast_iface_t *iface)
{
    uct_mm_base_iface_t *mm_iface = &iface->super.super;
    if (!uct_mm_iface_fifo_has_new_data(&mm_iface->recv_check)) {
        return 0;
    }

    uct_mm_coll_fifo_element_t *elem = ucs_derived_of(mm_iface->recv_check.read_elem,
                                                      uct_mm_coll_fifo_element_t);
    if (!uct_mm_ep_process_recv_loopback(&iface->super, elem)) {
        return 0;
    }

    /* raise the read_index */
    uint64_t read_index = ++mm_iface->recv_check.read_index;

    /* the next fifo_element which the read_index points to */
    mm_iface->recv_check.read_elem =
        UCT_MM_IFACE_GET_FIFO_ELEM(mm_iface, mm_iface->recv_fifo_elems,
                                   (read_index & mm_iface->fifo_mask));

    uct_mm_progress_fifo_tail(&mm_iface->recv_check);

    return 1;
}

unsigned uct_mm_incast_iface_progress(uct_iface_h tl_iface)
{
    uct_mm_incast_iface_t *iface  = ucs_derived_of(tl_iface, uct_mm_incast_iface_t);
    uct_mm_base_iface_t *mm_iface = ucs_derived_of(tl_iface, uct_mm_base_iface_t);
    unsigned total_count  = 0;
    unsigned count;

    ucs_assert(mm_iface->fifo_poll_count >= UCT_MM_IFACE_FIFO_MIN_POLL);

    /* progress receive */
    do {
        count = uct_mm_incast_iface_poll_fifo(iface);
        ucs_assert(count < 2);
        total_count += count;
        ucs_assert(total_count < UINT_MAX);
    } while ((count != 0) && (total_count < mm_iface->fifo_poll_count));

    uct_mm_iface_fifo_window_adjust(mm_iface, total_count);

    /* progress the pending sends (if there are any) */
    ucs_arbiter_dispatch(&mm_iface->arbiter, 1, uct_mm_ep_process_pending,
                         &total_count);

    return total_count;
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

    printf("uct_mm_incast_iface_release_shared_desc!\n");
    unsigned index;
    uct_mm_coll_ep_t *ep;
    uintptr_t src_coll_id        = (uintptr_t)self;
    uct_mm_incast_iface_t *iface = ucs_derived_of(tl_iface, uct_mm_incast_iface_t);

    /* Find the endpoint - based on the ID of the sender of this descriptor */
    ucs_ptr_array_for_each(ep, index, &iface->super.ep_ptrs) {
        if (ep->coll_id == src_coll_id) {
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
    .ep_am_short               = uct_mm_incast_ep_am_short,
    .ep_am_bcopy               = uct_mm_incast_ep_am_bcopy,
    .ep_am_zcopy               = uct_mm_incast_ep_am_zcopy,
    .ep_pending_add            = uct_mm_ep_pending_add,
    .ep_pending_purge          = uct_mm_ep_pending_purge,
    .ep_flush                  = uct_mm_ep_flush,
    .ep_fence                  = uct_sm_ep_fence,
    .ep_create                 = uct_mm_incast_ep_create,
    .ep_destroy                = uct_mm_coll_ep_destroy,
    .iface_flush               = uct_mm_iface_flush,
    .iface_fence               = uct_sm_iface_fence,
    .iface_progress            = uct_mm_incast_iface_progress,
    .iface_progress_enable     = uct_base_iface_progress_enable,
    .iface_progress_disable    = uct_base_iface_progress_disable,
    .iface_event_fd_get        = uct_mm_iface_event_fd_get,
    .iface_event_arm           = uct_mm_iface_event_fd_arm,
    .iface_close               = UCS_CLASS_DELETE_FUNC_NAME(uct_mm_incast_iface_t),
    .iface_query               = uct_mm_incast_iface_query,
    .iface_get_device_address  = uct_sm_iface_get_device_address,
    .iface_get_address         = uct_mm_coll_iface_get_address,
    .iface_is_reachable        = uct_mm_iface_is_reachable,
    .iface_release_shared_desc = uct_mm_incast_iface_release_shared_desc
};

UCS_CLASS_INIT_FUNC(uct_mm_incast_iface_t, uct_md_h md, uct_worker_h worker,
                    const uct_iface_params_t *params,
                    const uct_iface_config_t *tl_config)
{
    uint32_t procs = ((params->field_mask & UCT_IFACE_PARAM_FIELD_COLL_INFO) &&
                      (params->host_info.proc_cnt > 2)) ?
                     params->host_info.proc_cnt : 2;

    uct_mm_iface_config_t *mm_config = ucs_derived_of(tl_config,
                                                      uct_mm_iface_config_t);
    unsigned orig_fifo_elem_size     = mm_config->fifo_elem_size;
    size_t short_stride              = ucs_align_up(orig_fifo_elem_size -
                                                    sizeof(uct_mm_coll_fifo_element_t),
                                                    UCS_SYS_CACHE_LINE_SIZE);
    mm_config->fifo_elem_size        = sizeof(uct_mm_coll_fifo_element_t) +
                                       (procs * short_stride);

    UCS_CLASS_CALL_SUPER_INIT(uct_mm_coll_iface_t, &uct_mm_incast_iface_ops,
                              md, worker, params, tl_config);

    int i;
    ucs_status_t status;
    uct_mm_coll_fifo_element_t *elem;
    for (i = 0; i < self->super.super.config.fifo_size; i++) {
        elem = UCT_MM_COLL_IFACE_GET_FIFO_ELEM(self, i);
        ucs_assert(elem->super.flags & UCT_MM_FIFO_ELEM_FLAG_OWNER);

        elem->pending = 0;

        status = ucs_spinlock_init(&elem->lock, UCS_SPINLOCK_FLAG_SHARED);
        if (status != UCS_OK) {
            goto destory_elements;
        }
    }

    mm_config->fifo_elem_size = orig_fifo_elem_size;

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
