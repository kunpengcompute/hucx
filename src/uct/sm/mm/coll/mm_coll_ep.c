/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2019.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#include "mm_coll_ep.h"

#include <ucs/arch/atomic.h>
#include <ucs/async/async.h>

#define UCT_MM_COLL_EP_GET_FIFO_ELEM(_ep, _index) \
        ((uct_mm_coll_fifo_element_t*)((char*)(_ep)->super.fifo_elems + \
                (((_index) & (_ep)->fifo_mask) * (_ep)->elem_size)))

#define UCT_MM_COLL_GET_BASE_ADDRESS(_is_short, _is_batch, _elem, _ep, \
                                     _iface, _is_loopback, _is_recv, _stride) \
({ \
    uint8_t *ret_address; \
    if (_is_short) { \
        ret_address = (uint8_t*)((_elem) + 1); \
    } else { \
        if (_is_loopback) { \
            ret_address = (_elem)->super.desc_data; \
        } else { \
            ucs_status_t status = uct_mm_ep_get_remote_seg(&(_ep)->super, \
                    (_elem)->super.desc.seg_id, (_elem)->super.desc.seg_size, \
                    (void**)&ret_address); \
            if (ucs_unlikely(status != UCS_OK)) { \
                return status; \
            } \
            VALGRIND_MAKE_MEM_DEFINED(ret_address, \
                                      (_elem)->super.desc.seg_size); \
            ret_address += (_elem)->super.desc.offset; \
        } \
    } \
    if ((_is_batch) && (!_is_loopback) && (!_is_recv)) { \
        ret_address += (_ep)->my_offset * (_stride); \
        ucs_assert(((_ep)->my_offset * (_stride)) < (_is_short ? \
                ((_ep)->elem_size - sizeof(uct_mm_coll_fifo_element_t)) : \
                (_ep)->seg_size)); \
    } \
    ret_address; \
})

/*
 * This function is the common send function for three types of shared-memory
 * collective interfaces: LOCKED, ATOMIC, BATCHED and CENTRALIZED.
 * The intent is to accommodate different kinds of constraints - resulting in
 * different performance profiles. For example, LOCKED should fit large buffers
 * reduced by a small amount of processes, but not for other cases.
 *
 * Basically, here's how the three work for reduce (1/2/3 are buffers from the
 * respective ranks, and 'p' stands for padding to cache-line size):
 *
 * 1. LOCKED mode, where the reduction is done by the sender:
 *
 *   | element->pending = 0 |             |
 *   | element->pending = 1 | 222         |
 *   | element->pending = 2 | 222+111     |
 *   | element->pending = 3 | 222+111+333 |
 *
 * 2. ATOMIC mode, same as LOCKED but using atomic operations to reduce:
 *
 *   | element->pending = 0 |             |
 *   | element->pending = 1 | 222         |
 *   | element->pending = 2 | 222+111     |
 *   | element->pending = 3 | 222+111+333 |
 *
 * 3.BATCHED mode, where buffers are written in separate cache-lines:
 *
 *   | element->pending = 0 |      |      |      |      |
 *   | element->pending = 1 |      |      | 222p |      |
 *   | element->pending = 2 |      | 111p | 222p |      |
 *   | element->pending = 3 |      | 111p | 222p | 333p |
 *
 * 4. CENTRALIZED mode, like "batched" but with recv-side completion:
 *
 *   | element->pending = 0 | ???-0 | ???-0 | ???-0 |
 *   | element->pending = 0 | ???-0 | 222-1 | ???-0 |
 *   | element->pending = 2 | 111-1 | 222-1 | ???-0 | < rank#0 "triggers" checks
 *   | element->pending = 3 | 111-1 | 222-1 | 333-1 |
 *                        ^       ^       ^       ^
 *                        ^      #1      #2      #3  -> the last byte is polled
 *                        ^                             by the receiver process.
 *                        ^
 *                        the receiver process polls all these last bytes, and
 *                        once all the bytes have been set - the receiver knows
 *                        this operation is complete (none of the senders know).
 *
 * To summarize the differences:
 *
 * name | does the reduction |     mutual exclusion     | typically good for
 * ----------------------------------------------------------------------------
 * LOCKED      |   sender    | element access uses lock | large size
 * ----------------------------------------------------------------------------
 * ATOMIC      |   sender    | element access is atomic | imbalance + some ops
 * ----------------------------------------------------------------------------
 * BATCHED     |   receiver  | "pending" is atomic      | small size, low PPN
 * ----------------------------------------------------------------------------
 * CENTRALIZED |   receiver  | not mutually excluding   | small size, high PPN
 *
 * Note: this function can also run in "broadcast mode" (see "is_bcast"), where
 * the sender is placing one message and all the other processes connected to
 * this FIFO are reading it. This also requires counting - to keep track of when
 * all the processes have seen this message and it can be released. This "bcast"
 * flow is the same for the three aforementioned methods.
 */
static UCS_F_ALWAYS_INLINE ssize_t
uct_mm_coll_ep_am_common_send(uct_coll_dtype_mode_t op_mode, int is_bcast,
                              int is_short, uct_ep_h tl_ep, uint8_t am_id,
                              size_t length, uint64_t header,
                              const void *payload, uct_pack_callback_t pack_cb,
                              void *arg, unsigned flags)
{
    uct_mm_coll_iface_t *iface = ucs_derived_of(tl_ep->iface, uct_mm_coll_iface_t);
    uct_mm_coll_ep_t *ep       = ucs_derived_of(tl_ep, uct_mm_coll_ep_t);
    uint8_t elem_flags;

    /* Sanity checks */
    UCT_CHECK_AM_ID(am_id);
    ucs_assert(is_bcast || (ep->coll_id != iface->my_coll_id));

    /* Grab the next cell I haven't yet written to */
    uint64_t head      = ep->tx_index;
    uct_mm_ep_t *mm_ep = &ep->super;

    /* Check if there is room in the remote process's receive FIFO to write */
    if (!UCT_MM_EP_IS_ABLE_TO_SEND(head, mm_ep->cached_tail,
                                   iface->super.config.fifo_size)) {
        if (!ucs_arbiter_group_is_empty(&mm_ep->arb_group)) {
            /* pending isn't empty. don't send now to prevent out-of-order sending */
            UCS_STATS_UPDATE_COUNTER(mm_ep->super.stats, UCT_EP_STAT_NO_RES, 1);
            return UCS_ERR_NO_RESOURCE;
        } else {
            /* pending is empty */
            /* update the local copy of the tail to its actual value on the remote peer */
            uct_mm_ep_update_cached_tail(mm_ep);
            if (!UCT_MM_EP_IS_ABLE_TO_SEND(head, mm_ep->cached_tail,
                                           iface->super.config.fifo_size)) {
                UCS_STATS_UPDATE_COUNTER(mm_ep->super.stats, UCT_EP_STAT_NO_RES, 1);
                return UCS_ERR_NO_RESOURCE;
            }
        }
    }

    /* Determine function parameters based on the collective operation type */
    int is_tight   = (op_mode != UCT_COLL_DTYPE_MODE_PADDED);
    int is_batched = !is_bcast && !is_tight;

    /* Calculate stride: distance between the start of two consecutive items */
    uint16_t stride;
    if (is_tight) {
        stride = length;
        ucs_assert(length > 0);
    } else {
        if (is_short) {
            stride = ep->elem_size - sizeof(uct_mm_coll_fifo_element_t);
        } else {
            stride = ep->seg_size;
        }
        if (!is_bcast) {
            stride /= ep->tx_cnt + 1;
        }
    }

    /* Check my "position" in the order of writers to this element */
    uint8_t *base_address;
    uint32_t previous_pending = 0;
    uct_mm_coll_fifo_element_t *elem = ep->tx_elem;
    int is_lock_needed = !is_short && !is_bcast && (flags & UCT_SEND_FLAG_PACK_LOCK);
    if (is_lock_needed) {
        ucs_spin_lock(&elem->lock);
        previous_pending = elem->pending++;
    }

    /* Write the buffer (or reduce onto an existing buffer) */
    base_address = UCT_MM_COLL_GET_BASE_ADDRESS(is_short, is_batched,
            elem, ep, iface, is_bcast, 0 /* _is_recv */, stride);
    if (is_short) {
        /* Last writer writes the header too, the rest - only payload */
        memcpy(base_address, payload, length);
    } else {
        /* For some reduce operations - ask the callback to do the reduction */
        if (!is_bcast &&
            !is_batched &&
            ucs_likely(previous_pending != 0)) {
            ucs_assert_always((((uintptr_t)arg) & UCT_PACK_CALLBACK_REDUCE) == 0);
            arg = (void*)((uintptr_t)arg | UCT_PACK_CALLBACK_REDUCE);
        }

        /* Write the portion of this process into the shared buffer */
        length = pack_cb(base_address, arg);
    }

    /* No need to mess with coordination if I'm the only writer (broadcast) */
    if (is_bcast) {
        ucs_assert(elem->pending == 0);
        elem->pending = 1;
        goto last_writer;
    }

    /* CENTRALIZED mode only: mark my slot as "written" */
    if (is_batched) {
        ucs_assert(length < stride);

        /* Make sure data is written before the "done" flag */
        ucs_memory_cpu_store_fence();

        /* Mark own slot as "done" */
        base_address[stride - 1] = UCT_MM_FIFO_ELEM_FLAG_OWNER;

        /* One process notifies the receiver (doesn't mean others are done) */
        if (ep->my_offset == 0) {
            goto last_writer;
        } else {
            goto trace_send;
        }
    } else if (is_tight) {
        ucs_memory_cpu_store_fence();
        ucs_assert(!is_batched);
        previous_pending = ucs_atomic_fadd32(&elem->pending, 1);
    } else if (is_lock_needed) {
        ucs_spin_unlock(&elem->lock);
    }

skip_payload:
    /* Check if this sender is the last expected sender for this element */
    if (previous_pending == ep->tx_cnt) {
last_writer:
        /* Change the owner bit to indicate that the writing is complete.
         * The owner bit flips after every FIFO wraparound */
        elem_flags = (elem->super.flags & UCT_MM_FIFO_ELEM_FLAG_OWNER) ^
                                          UCT_MM_FIFO_ELEM_FLAG_OWNER;
        if (is_short) {
            elem_flags  |= UCT_MM_FIFO_ELEM_FLAG_INLINE;
            elem->header = header;
        }

        elem->op_mode      = op_mode;
        elem->super.am_id  = am_id;
        elem->super.length = is_batched ? stride :
                             (!is_tight ? length + (is_short * sizeof(header)) :
                                          (length * (ep->tx_cnt + 1)) +
                                          (is_short * sizeof(header)));

        /* memory barrier - make sure that the memory is flushed before setting the
         * 'writing is complete' flag which the reader checks */
        ucs_memory_cpu_store_fence();

        /* Set this element as "written" - pass ownership to the receiver */
        elem->super.flags = elem_flags;

        /* update the remote head element */
        mm_ep->fifo_ctl->head = head;

        /* signal remote, if so requested */
        if (ucs_unlikely(flags & UCT_SEND_FLAG_SIGNALED)) {
            uct_mm_ep_signal_remote(mm_ep);
        }
    }

trace_send:
    uct_iface_trace_am(&iface->super.super.super, UCT_AM_TRACE_TYPE_SEND,
                       am_id, base_address, length, is_short ? "TX: AM_SHORT" :
                                                               "TX: AM_BCOPY");
    if (is_short) {
        UCT_TL_EP_STAT_OP(&mm_ep->super, AM, SHORT, length);
    } else {
        UCT_TL_EP_STAT_OP(&mm_ep->super, AM, BCOPY, length);
    }

    /* Update both the index and the pointer to the next element */
    ep->tx_elem = UCT_MM_COLL_EP_GET_FIFO_ELEM(ep, ++ep->tx_index);
    ucs_prefetch(ep->tx_elem);

    return UCS_OK;
}

static UCS_F_ALWAYS_INLINE ucs_status_t
uct_mm_coll_ep_am_short(uct_ep_h ep, uint8_t id, uint64_t header,
                        const void *payload, unsigned length, int is_bcast)
{
    unsigned orig_length          = UCT_COLL_DTYPE_MODE_UNPACK_VALUE(length);
    uct_coll_dtype_mode_t op_mode = UCT_COLL_DTYPE_MODE_UNPACK_MODE(length);

    ucs_assert((op_mode == UCT_COLL_DTYPE_MODE_PADDED) ||
               (op_mode == UCT_COLL_DTYPE_MODE_PACKED));

    ssize_t ret = uct_mm_coll_ep_am_common_send(op_mode, is_bcast, 1, ep, id,
            orig_length, header, payload, NULL, NULL, 0);

    return (ret > 0) ? UCS_OK : (ucs_status_t)ret;
}

ucs_status_t uct_mm_bcast_ep_am_short(uct_ep_h ep, uint8_t id, uint64_t header,
                                      const void *payload, unsigned length)
{
    return uct_mm_coll_ep_am_short(ep, id, header, payload, length, 1);
}

ucs_status_t uct_mm_incast_ep_am_short(uct_ep_h ep, uint8_t id, uint64_t header,
                                       const void *payload, unsigned length)
{
    return uct_mm_coll_ep_am_short(ep, id, header, payload, length, 0);
}

static UCS_F_ALWAYS_INLINE ssize_t
uct_mm_coll_ep_am_bcopy(uct_ep_h ep, uint8_t id, uct_pack_callback_t pack_cb,
                        void *arg, unsigned flags, int is_bcast)
{
    unsigned orig_flags           = UCT_COLL_DTYPE_MODE_UNPACK_VALUE(flags);
    uct_coll_dtype_mode_t op_mode = UCT_COLL_DTYPE_MODE_UNPACK_MODE(flags);

    ucs_assert((op_mode == UCT_COLL_DTYPE_MODE_PADDED) ||
               (op_mode == UCT_COLL_DTYPE_MODE_PACKED));

    ssize_t ret = uct_mm_coll_ep_am_common_send(op_mode, is_bcast, 0, ep, id, 0,
            0, NULL, pack_cb, arg, orig_flags);

    return ret;
}

ssize_t uct_mm_bcast_ep_am_bcopy(uct_ep_h ep, uint8_t id,
                                 uct_pack_callback_t pack_cb,
                                 void *arg, unsigned flags)
{
    return uct_mm_coll_ep_am_bcopy(ep, id, pack_cb, arg, flags, 1);
}

ssize_t uct_mm_incast_ep_am_bcopy(uct_ep_h ep, uint8_t id,
                                  uct_pack_callback_t pack_cb,
                                  void *arg, unsigned flags)
{
    return uct_mm_coll_ep_am_bcopy(ep, id, pack_cb, arg, flags, 0);
}

static UCS_F_ALWAYS_INLINE ucs_status_t
uct_mm_coll_ep_am_zcopy(uct_ep_h ep, uint8_t id, const void *header,
                        unsigned header_length, const uct_iov_t *iov,
                        size_t iovcnt, unsigned flags, uct_completion_t *comp,
                        int is_bcast)
{
    uct_coll_dtype_mode_t op_mode = UCT_COLL_DTYPE_MODE_UNPACK_MODE(flags);
    unsigned orig_flags           = UCT_COLL_DTYPE_MODE_UNPACK_VALUE(flags);
    uint64_t header_value         = *(uint64_t*)header;
    void *payload                 = iov[0].buffer;
    size_t length                 = iov[0].length;
    size_t *offsets               = iov[1].buffer;

    ucs_assert(header_length == sizeof(header_value));
    ucs_assert(iov[1].length == (sizeof(size_t) *
            ucs_derived_of(ep->iface, uct_mm_coll_iface_t)->sm_proc_cnt));

    ssize_t ret = uct_mm_coll_ep_am_common_send(op_mode, is_bcast, 1, ep, id,
            length, header_value, payload, NULL, offsets, orig_flags);

    return (ret > 0) ? UCS_OK : (ucs_status_t)ret;
}

ucs_status_t uct_mm_bcast_ep_am_zcopy(uct_ep_h ep, uint8_t id, const void *header,
                                      unsigned header_length, const uct_iov_t *iov,
                                      size_t iovcnt, unsigned flags,
                                      uct_completion_t *comp)
{
    return uct_mm_coll_ep_am_zcopy(ep, id, header, header_length, iov, iovcnt,
                                   flags, comp, 1);
}

ucs_status_t uct_mm_incast_ep_am_zcopy(uct_ep_h ep, uint8_t id, const void *header,
                                       unsigned header_length, const uct_iov_t *iov,
                                       size_t iovcnt, unsigned flags,
                                       uct_completion_t *comp)
{
    return uct_mm_coll_ep_am_zcopy(ep, id, header, header_length, iov, iovcnt,
                                   flags, comp, 0);
}

static inline ucs_status_t uct_mm_coll_ep_add(uct_mm_coll_ep_t* ep)
{
    /* Block the asynchronous progress while adding this new endpoint
     * (ucs_ptr_array_locked_t not used - to avoid lock during progress). */
    uct_base_iface_t *base_iface = ucs_derived_of(ep->super.super.super.iface,
                                                  uct_base_iface_t);
    uct_mm_coll_iface_t *iface   = ucs_derived_of(base_iface,
                                                  uct_mm_coll_iface_t);
    ucs_async_context_t *async   = base_iface->worker->async;

    UCS_ASYNC_BLOCK(async);
    (void) ucs_ptr_array_insert(&iface->ep_ptrs, ep);
    UCS_ASYNC_UNBLOCK(async);

    return UCS_OK;
}

UCS_CLASS_INIT_FUNC(uct_mm_coll_ep_t, const uct_ep_params_t *params)
{
    const uct_mm_coll_iface_addr_t *addr = (const void *)params->iface_addr;
    uct_mm_coll_iface_t *iface           = ucs_derived_of(params->iface,
                                                          uct_mm_coll_iface_t);

    ucs_assert(params->field_mask & UCT_EP_PARAM_FIELD_IFACE);
    ucs_assert(params->field_mask & UCT_EP_PARAM_FIELD_IFACE_ADDR);

    UCS_CLASS_CALL_SUPER_INIT(uct_mm_ep_t, params);

    self->coll_id    = addr->coll_id;
    self->my_offset  = iface->my_coll_id -
                      (uint32_t)(addr->coll_id < iface->my_coll_id);
    self->tx_cnt     = iface->sm_proc_cnt - 2;
    self->tx_index   = 0;
    self->tx_elem    = self->super.fifo_elems;
    self->elem_size  = iface->super.config.fifo_elem_size;
    self->fifo_mask  = iface->super.fifo_mask;
    self->seg_size   = iface->super.config.seg_size;
    self->ref_count  = 1;

    if (iface->my_coll_id == self->coll_id) {
        iface->loopback_ep = self;
    } else {
        uct_mm_coll_ep_add(self);
    }

    return UCS_OK;
}

UCS_CLASS_INIT_FUNC(uct_mm_bcast_ep_t, const uct_ep_params_t *params)
{
    uct_mm_coll_iface_t *iface = ucs_derived_of(params->iface, uct_mm_coll_iface_t);

    UCS_CLASS_CALL_SUPER_INIT(uct_mm_coll_ep_t, params);

    self->recv_check.is_flags_cached = 0;
    self->recv_check.flags_cache     = 0;
    self->recv_check.fifo_shift      = iface->super.recv_check.fifo_shift;
    self->recv_check.read_index      = 0;
    self->recv_check.read_elem       = self->super.super.fifo_elems;
    self->recv_check.fifo_ctl        = self->super.super.fifo_ctl;

    self->recv_check.fifo_release_factor_mask =
            iface->super.recv_check.fifo_release_factor_mask;

    ucs_debug("mm_bcast: ep connected: %p, src_coll_id: %u, dst_coll_id: %u",
              self, iface->my_coll_id, self->super.coll_id);

    return UCS_OK;
}

UCS_CLASS_INIT_FUNC(uct_mm_incast_ep_t, const uct_ep_params_t *params)
{
    uct_mm_coll_iface_t *iface = ucs_derived_of(params->iface, uct_mm_coll_iface_t);

    UCS_CLASS_CALL_SUPER_INIT(uct_mm_coll_ep_t, params);

    ucs_debug("mm_incast: ep connected: %p, src_coll_id: %u, dst_coll_id: %u",
              self, iface->my_coll_id, self->super.coll_id);

    return UCS_OK;
}

static UCS_CLASS_CLEANUP_FUNC(uct_mm_coll_ep_t) {}
static UCS_CLASS_CLEANUP_FUNC(uct_mm_bcast_ep_t) {}
static UCS_CLASS_CLEANUP_FUNC(uct_mm_incast_ep_t) {}

UCS_CLASS_DEFINE(uct_mm_coll_ep_t, uct_mm_ep_t)
UCS_CLASS_DEFINE(uct_mm_bcast_ep_t, uct_mm_coll_ep_t)
UCS_CLASS_DEFINE(uct_mm_incast_ep_t, uct_mm_coll_ep_t)

static UCS_F_ALWAYS_INLINE void
uct_mm_coll_ep_update_tail(uct_mm_fifo_ctl_t *fifo_ctl, uint64_t index)
{
    (void) ucs_atomic_cswap64(&fifo_ctl->tail, index, index + 1);
}

static UCS_F_ALWAYS_INLINE int
uct_mm_coll_ep_is_last_to_read(uct_mm_coll_fifo_element_t *elem,
                               uct_mm_fifo_ctl_t *fifo_ctl,
                               uint8_t proc_cnt,
                               uint64_t last_index)
{
    uint32_t pending = ucs_atomic_fadd32(&elem->pending, 1);
    if (pending == proc_cnt) {
        elem->pending = 0;
        uct_mm_coll_ep_update_tail(fifo_ctl, last_index);
        return 1;
    }
    return 0;
}

static UCS_F_ALWAYS_INLINE uct_mm_coll_fifo_element_t*
uct_mm_coll_ep_get_next_rx_elem(uct_mm_coll_fifo_element_t* elem,
                                uct_mm_coll_ep_t *ep,
                                uint64_t *elem_index)
{
    if (ucs_likely(++(*elem_index) & ep->fifo_mask)) {
        return (uct_mm_coll_fifo_element_t*)((char*)elem + ep->elem_size);
    }

    return (uct_mm_coll_fifo_element_t*)ep->super.fifo_elems;
}

void uct_mm_coll_ep_release_desc(uct_mm_coll_ep_t *ep, void *desc)
{
    void *elem_desc;
    ucs_status_t UCS_V_UNUSED status;

    uint64_t elem_index              = ep->super.fifo_ctl->tail;
    uct_mm_coll_fifo_element_t *elem = UCT_MM_COLL_EP_GET_FIFO_ELEM(ep,
                                                                    elem_index);
    uct_mm_seg_id_t seg_id           = elem->super.desc.seg_id;
    uct_mm_base_iface_t *iface       = ucs_derived_of(ep->super.super.super.iface,
                                                      uct_mm_base_iface_t);
    size_t rx_headroom               = iface->rx_headroom;

new_segment:
    /* Find the base address for the remote segment of this element */
    status    = uct_mm_ep_get_remote_seg(&ep->super, seg_id,
                                         elem->super.desc.seg_size, &elem_desc);
    elem_desc = UCS_PTR_BYTE_OFFSET(elem_desc - 1, -rx_headroom);
    ucs_assert(status == UCS_OK); /* since it had to have been attached */

    while ((elem->super.flags & UCT_MM_FIFO_ELEM_FLAG_INLINE) || (desc != elem_desc)) {
        ucs_assert(elem_index <= ep->tx_index); /* should find eventually */
        elem = uct_mm_coll_ep_get_next_rx_elem(elem, ep, &elem_index);
        if (ucs_unlikely(seg_id != elem->super.desc.seg_id)) {
            seg_id = elem->super.desc.seg_id;
            goto new_segment;
        }
    }

    /* Check if this element has been released by all peers and can be re-used */
    int is_last = uct_mm_coll_ep_is_last_to_read(elem, ep->super.fifo_ctl,
                                                 ep->tx_cnt + 1, elem_index);
    if (is_last) {
        while ((elem_index < ep->tx_index) && (elem->pending == 0)) {
            uct_mm_coll_ep_update_tail(ep->super.fifo_ctl, elem_index);
            elem = uct_mm_coll_ep_get_next_rx_elem(elem, ep, &elem_index);
        }
    }
}

static UCS_F_ALWAYS_INLINE int
uct_mm_coll_iface_is_centralized_elem_ready(uct_mm_coll_fifo_element_t *elem,
                                            uint8_t *base_address,
                                            uint32_t num_slots,
                                            unsigned slot_size)
{
    uint32_t slot_iter     = elem->pending; /* start from last known position */
    uint8_t *slot_iter_ptr = base_address + ((slot_iter + 1) * slot_size) - 1;

    while ((slot_iter < num_slots) && (*slot_iter_ptr)) {
        ucs_assert(*slot_iter_ptr == UCT_MM_FIFO_ELEM_FLAG_OWNER);
        *slot_iter_ptr = 0;
        slot_iter_ptr += slot_size;
        slot_iter++;
    }

    elem->pending = slot_iter;
    return (slot_iter == num_slots);
}

/**
 * This function processes incoming messages (in elements). Specifically, this
 * function is used in "loopback mode" to check for incast, in which case the
 * passed endpoint is my own. After invoking the Active Message handler, the
 * return value may indicate that this message still needs to be kept
 * (UCS_INPROGRESS), and the appropriate callbacks are set for releasing it in
 * the future (by an upper layer calling @ref uct_iface_release_desc ).
 */
static UCS_F_ALWAYS_INLINE int
uct_mm_ep_process_recv(uct_mm_coll_ep_t *ep, uct_mm_coll_iface_t *iface,
                       uct_mm_coll_fifo_element_t *elem, int is_incast)
{

    /* Detect incoming message parameters */
    int is_short          = elem->super.flags & UCT_MM_FIFO_ELEM_FLAG_INLINE;
    uint8_t *base_address = UCT_MM_COLL_GET_BASE_ADDRESS(is_short,
                            0 /* _is_batch */, elem, ep, iface, is_incast,
                            1 /* _is_recv */, 0 /* _stride */);
    uint16_t stride        = elem->super.length;
    uint8_t proc_cnt       = iface->sm_proc_cnt - 1;

    /* CENTRALIZED mode only - check if this is the last writer */
    int is_pending_batched = (elem->op_mode == UCT_COLL_DTYPE_MODE_PADDED);
    if (is_pending_batched && is_incast) {
        if (!uct_mm_coll_iface_is_centralized_elem_ready(elem,
                base_address, proc_cnt, stride)) {
            return 0; /* incast started, but not all peers have written yet */
        }

        ucs_memory_cpu_load_fence();
    }

    /* choose the flags for the Active Message callback argument */
    int am_cb_flags = is_incast * UCT_CB_PARAM_FLAG_STRIDE;
    ucs_assert((is_incast == 0) || (is_incast == 1));
    if (!is_short) {
        if (is_incast) {
            am_cb_flags |= UCT_CB_PARAM_FLAG_DESC;
        } else {
            am_cb_flags |= UCT_CB_PARAM_FLAG_DESC | UCT_CB_PARAM_FLAG_SHARED;
        }
    } else {
        base_address -= sizeof(elem->header);
    }

    uct_iface_trace_am(&iface->super.super.super, UCT_AM_TRACE_TYPE_RECV,
                       elem->super.am_id, base_address, elem->super.length,
                       is_short ? "RX: AM_SHORT" : "RX: AM_BCOPY");

    /* Process the incoming message using the active-message callback */
    ucs_assert(!is_incast || !is_pending_batched || (stride >= 64));
    ucs_status_t status = uct_iface_invoke_am(&iface->super.super.super,
                                              elem->super.am_id, base_address,
                                              stride, am_cb_flags);

    /*
     * This descriptor may resides on memory belonging to another process.
     * The consequence is that it can only be accessed for reading, not
     * writing (technically writing is possible, but would conflict with
     * other processes using this descriptor). UCT_CB_PARAM_FLAG_SHARED is
     * used to pass this information to upper layers.
     */
    uct_mm_fifo_check_t *recv_check;
    if (ucs_unlikely(status == UCS_INPROGRESS)) {
        void *desc = base_address - iface->super.rx_headroom;
        ucs_assert(!is_short);

        /* If I'm the owner of this memory - I can replace the element's segment */
        if (is_incast) {
            /* assign a new receive descriptor to this FIFO element.*/
            uct_mm_assign_desc_to_fifo_elem(&iface->super, &elem->super, 1);

            /* CENTRALIZED mode only - mark my slot as "written" */
            if (is_pending_batched) {
                uct_mm_coll_iface_init_centralized_desc(elem, stride, proc_cnt);
            }

            /* later release of this desc - the easy way */
            uct_recv_desc(desc) = (uct_recv_desc_t*)&iface->super.release_desc;

            /* Mark element as done (and re-usable) */
            recv_check = &iface->super.recv_check;
        } else {
            /* set information for @ref uct_mm_coll_iface_release_shared_desc */
            uct_recv_desc(desc) = (void*)((uintptr_t)ep->coll_id);
            return 1;
        }
    } else if (is_incast) {
        recv_check = &iface->super.recv_check;
    } else {
        recv_check = &ucs_derived_of(ep, uct_mm_bcast_ep_t)->recv_check;
    }

    /* Mark element as done (and re-usable) */
    uct_mm_coll_ep_is_last_to_read(elem, recv_check->fifo_ctl,
                                   proc_cnt, recv_check->read_index);

    return 1;
}

int uct_mm_ep_process_recv_loopback(uct_mm_coll_iface_t *iface,
                                    uct_mm_coll_fifo_element_t *elem)
{
    return uct_mm_ep_process_recv(NULL, iface, elem, 1);
}

static inline uct_mm_coll_ep_t* uct_mm_coll_ep_find(uct_mm_coll_iface_t *iface,
                                                    uint8_t coll_id)
{
    unsigned index;
    uct_mm_coll_ep_t *ep_iter;
    ucs_ptr_array_for_each(ep_iter, index, &iface->ep_ptrs) {
        if (coll_id == ep_iter->coll_id) {
            ep_iter->ref_count++;
            return ep_iter;
        }
    }

    return NULL;
}

static inline uct_ep_h uct_mm_coll_ep_check_existing(const uct_ep_params_t *params)
{
    uint8_t coll_id = ((uct_mm_coll_iface_addr_t*)params->iface_addr)->coll_id;
    uct_mm_coll_iface_t *iface = ucs_derived_of(params->iface, uct_mm_coll_iface_t);

    ucs_assert(params->field_mask & UCT_EP_PARAM_FIELD_IFACE);
    ucs_assert(params->field_mask & UCT_EP_PARAM_FIELD_IFACE_ADDR);

    if (coll_id == iface->my_coll_id) {
        return (uct_ep_h)iface->loopback_ep;
    }

    /* look for the identifier among the existing endpoints (for re-use) */
    return (uct_ep_h)uct_mm_coll_ep_find(iface, coll_id);
}

ucs_status_t uct_mm_bcast_ep_create(const uct_ep_params_t *params, uct_ep_h *ep_p)
{
    uct_ep_h ep = uct_mm_coll_ep_check_existing(params);

    if (ucs_likely(ep != NULL)) {
        *ep_p = ep;
        return UCS_OK;
    }

    return UCS_CLASS_NEW(uct_mm_bcast_ep_t, ep_p, params);
}

ucs_status_t uct_mm_incast_ep_create(const uct_ep_params_t *params, uct_ep_h *ep_p)
{
    uct_ep_h ep = uct_mm_coll_ep_check_existing(params);

    if (ucs_likely(ep != NULL)) {
        *ep_p = ep;
        return UCS_OK;
    }

    return UCS_CLASS_NEW(uct_mm_incast_ep_t, ep_p, params);
}

static inline void uct_mm_coll_ep_del(uct_mm_coll_iface_t *iface,
                                      uct_mm_coll_ep_t* ep)
{
    unsigned index;
    uct_mm_coll_ep_t *ep_iter;
    uint8_t coll_id = ep->coll_id;

    ucs_ptr_array_for_each(ep_iter, index, &iface->ep_ptrs) {
        if (coll_id == ep_iter->coll_id) {
            /* Block the asynchronous progress while adding this new endpoint
             * (ucs_ptr_array_locked_t not used - to avoid lock during progress). */
            uct_base_iface_t *base_iface = ucs_derived_of(iface, uct_base_iface_t);
            ucs_async_context_t *async   = base_iface->worker->async;
            UCS_ASYNC_BLOCK(async);
            ucs_ptr_array_remove(&iface->ep_ptrs, index);
            UCS_ASYNC_UNBLOCK(async);

            return;
        }
    }

    ucs_error("failed to find the endpoint in its array (id=%u)", coll_id);
}

void uct_mm_coll_ep_destroy(uct_ep_h tl_ep)
{
    uct_mm_coll_ep_t *ep = ucs_derived_of(tl_ep, uct_mm_coll_ep_t);
    if (--ep->ref_count) {
        return;
    }

    uct_mm_coll_iface_t *iface = ucs_derived_of(tl_ep->iface, uct_mm_coll_iface_t);
    if (ep->coll_id != iface->my_coll_id) {
        uct_mm_coll_ep_del(ucs_derived_of(tl_ep->iface, uct_mm_coll_iface_t), ep);
    } else {
        iface->loopback_ep = NULL;
    }

    UCS_CLASS_DELETE(uct_mm_coll_ep_t, ep);
}

unsigned uct_mm_bcast_ep_poll_fifo(uct_mm_bcast_iface_t *iface,
                                   uct_mm_bcast_ep_t *ep)
{
    if (!uct_mm_iface_fifo_has_new_data(&ep->recv_check)) {
        return 0;
    }

    uct_mm_coll_fifo_element_t *elem = ucs_container_of(ep->recv_check.read_elem,
                                                        uct_mm_coll_fifo_element_t,
                                                        super);
    if (!uct_mm_ep_process_recv(&ep->super, &iface->super, elem, 0)) {
        return 0;
    }

    /* raise the read_index */
    uint64_t read_index = ++ep->recv_check.read_index;

    /* the next fifo_element which the read_index points to */
    ep->recv_check.read_elem =
            UCT_MM_IFACE_GET_FIFO_ELEM(&iface->super.super,
                                       ep->super.super.fifo_elems,
                                       (read_index & ep->super.fifo_mask));


    ucs_prefetch(ep->recv_check.read_elem);

    /*
     * Note: cannot call uct_mm_progress_fifo_tail() here, because I might have
     *       finished reading an element another peer hasn't (so it can't be
     *       marked as ready for re-use by incrementing the FIFO tail).
     */

    return 1;
}
