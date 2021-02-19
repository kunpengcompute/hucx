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
                                     _is_loopback, _is_recv, _stride) \
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

static UCS_F_ALWAYS_INLINE uint8_t
uct_mm_coll_ep_centralized_check_buffer(uint8_t *slot_ptr, unsigned slot_size)
{
    uint8_t pending_counter;
    volatile uint8_t *slot_iter_ptr = slot_ptr + slot_size - 1;
    uint8_t slot_counter            = *slot_iter_ptr;

    if (slot_counter == 0) {
        return 0;
    }

    pending_counter = 0;

    do {
        *slot_iter_ptr   = 0;
        pending_counter += slot_counter;
        slot_iter_ptr   += slot_size * slot_counter;
        slot_counter     = *slot_iter_ptr;
    } while (slot_counter != 0);
    /*
     * Note: no need to check if slot_iter reaches the limit, because the last
     * slot is reserved - and is always empty.
     */

    /* prevent the pending from pointing to a zero-ed slot */
    ucs_memory_cpu_store_fence();

    return pending_counter;
}

static UCS_F_ALWAYS_INLINE void
uct_mm_coll_iface_centralized_set_slot_counter(uint8_t *slot_ptr,
                                               unsigned slot_size,
                                               uint8_t new_counter_value)
{
    uint8_t *slot_counter = slot_ptr + slot_size - 1;

    ucs_assert(*slot_counter == 0);

    *slot_counter = new_counter_value;

    ucs_share_cache(slot_counter);
}

static UCS_F_ALWAYS_INLINE size_t
uct_mm_coll_iface_centralized_get_slot_size(uct_mm_coll_ep_t *ep,
                                            size_t data_length,
                                            int is_short)
{
    return is_short ?
        ucs_align_up(data_length + 1, UCS_SYS_CACHE_LINE_SIZE) :
        (ep->seg_size / (ep->proc_cnt + 1));
}

static UCS_F_ALWAYS_INLINE size_t
uct_mm_coll_iface_centralized_get_slot_offset(uct_mm_coll_ep_t *ep,
                                              int is_incast,
                                              int is_short,
                                              size_t slot_size,
                                              unsigned slot_index)
{
    if (is_incast) {
        return slot_size * slot_index;
    }

    /* For broadcast - it's one cache-line per process indicating completion */
    size_t total_size = is_short ?
            ep->elem_size - sizeof(uct_mm_coll_fifo_element_t) : ep->seg_size;
    return total_size - (UCS_SYS_CACHE_LINE_SIZE * (ep->proc_cnt - slot_index));
}

static UCS_F_ALWAYS_INLINE ucs_status_t
uct_mm_coll_iface_centralized_get_ptr(uct_mm_coll_fifo_element_t *elem,
                                      uct_mm_coll_ep_t *ep,
                                      int is_incast,
                                      int is_short,
                                      int is_loopback,
                                      uint8_t **base_address)
{
    *base_address = UCT_MM_COLL_GET_BASE_ADDRESS(is_short, 1 /* _is_batch */,
            elem, ep, is_loopback, 1 /* _is_recv */, 0 /* _stride */);

    return UCS_OK;
}

static UCS_F_ALWAYS_INLINE uint8_t*
uct_mm_coll_iface_centralized_get_slot(uct_mm_coll_fifo_element_t *elem,
                                       uct_mm_coll_ep_t *ep,
                                       int is_incast,
                                       int is_short,
                                       int is_loopback,
                                       size_t slot_size,
                                       unsigned slot_index)
{
    uint8_t *base_address = NULL;

    (void) uct_mm_coll_iface_centralized_get_ptr(elem, ep,
            is_incast, is_short, is_loopback, &base_address);

    return base_address + uct_mm_coll_iface_centralized_get_slot_offset(ep,
            is_incast, is_short, slot_size, slot_index);
}

static UCS_F_ALWAYS_INLINE void
uct_mm_coll_ep_centralized_mark_done(uct_mm_coll_fifo_element_t *elem,
                                     uct_mm_coll_ep_t *ep,
                                     uint8_t *slot_ptr,
                                     unsigned slot_size,
                                     uct_incast_cb_t cb)
{
    uint8_t* next_slot = slot_ptr + slot_size;
    uint8_t my_offset  = ep->my_offset;
    uint8_t cnt        = uct_mm_coll_ep_centralized_check_buffer(next_slot,
                                                                 slot_size);

    ucs_memory_cpu_store_fence();

    ucs_assert(elem->pending <= my_offset);
    if (ucs_likely(elem->pending == my_offset)) {
        ucs_assert(slot_ptr[slot_size - 1] == 0);

        /* If enabled - reduce my data into the next data available */
        if ((cb) && (cnt)) {
            cb(slot_ptr + (cnt * slot_size), slot_ptr);
        }

        elem->pending = my_offset + cnt + 1;
    } else {
        uct_mm_coll_iface_centralized_set_slot_counter(slot_ptr, slot_size, cnt + 1);
    }
}

static UCS_F_ALWAYS_INLINE void
uct_mm_coll_ep_centralized_mark_incast_tx_done(uct_mm_coll_fifo_element_t *elem,
                                               uct_mm_coll_ep_t *ep,
                                               uint8_t *slot_ptr,
                                               unsigned slot_size,
                                               uct_incast_cb_t cb)
{
    uct_mm_coll_ep_centralized_mark_done(elem, ep, slot_ptr, slot_size, cb);
}

static UCS_F_ALWAYS_INLINE void
uct_mm_coll_ep_centralized_mark_bcast_rx_done(uct_mm_coll_fifo_element_t *elem,
                                              uct_mm_coll_ep_t *ep,
                                              int is_short)
{
    uint8_t *slot_ptr = uct_mm_coll_iface_centralized_get_slot(elem, ep, 0,
                                                               is_short, 0, 0,
                                                               ep->my_offset);

    uct_mm_coll_ep_centralized_mark_done(elem, ep, slot_ptr,
                                         UCS_SYS_CACHE_LINE_SIZE, NULL);

    /*
     * Note: no use calling uct_mm_progress_fifo_tail() here, as it is unlikely
     *       that all the slots have been completed - leave it to the receiver.
     */
}

static UCS_F_ALWAYS_INLINE int
uct_mm_coll_ep_centralized_is_elem_ready(uct_mm_coll_fifo_element_t *elem,
                                         uct_mm_coll_ep_t *ep, int is_incast,
                                         int is_short, int is_loopback,
                                         int use_cb, uct_incast_cb_t cb)
{
    uint32_t writers = ep->proc_cnt;
    uint32_t pending = elem->pending;
    if (ucs_likely(pending == writers)) {
        goto is_ready;
    }

    size_t slot_size = is_incast ? elem->super.length : UCS_SYS_CACHE_LINE_SIZE;
    ucs_assert((!is_short) || ((slot_size % UCS_SYS_CACHE_LINE_SIZE) == 0));

    uint8_t *slot_ptr = uct_mm_coll_iface_centralized_get_slot(elem, ep,
                                                               is_incast,
                                                               is_short,
                                                               is_loopback,
                                                               slot_size,
                                                               pending);

    uint8_t cnt = uct_mm_coll_ep_centralized_check_buffer(slot_ptr, slot_size);

    while (ucs_likely(cnt > 0)) {
        /* if enabled - reduce my data into the next data available */
        if (use_cb && cb) {
            if (ucs_likely(pending > 0)) {
                cb(UCS_PTR_BYTE_OFFSET(slot_ptr, -slot_size),
                   UCS_PTR_BYTE_OFFSET(slot_ptr, (cnt - 1) * slot_size));
            }
        }

        if ((pending = pending + cnt) == writers) {
            goto is_ready;
        }

        elem->pending = pending;

        slot_ptr = UCS_PTR_BYTE_OFFSET(slot_ptr, cnt * slot_size);

        cnt = uct_mm_coll_ep_centralized_check_buffer(slot_ptr, slot_size);
    }

    return 0;

is_ready:
    elem->pending = 0;
    return 1;
}

void uct_mm_coll_ep_centralized_reset_bcast_elem(uct_mm_coll_fifo_element_t* elem,
                                                 uct_mm_coll_ep_t *ep,
                                                 int is_short)
{
    uint8_t  slot_idx;
    uint8_t  slot_cnt = ep->proc_cnt;
    uint8_t* slot_ptr = uct_mm_coll_iface_centralized_get_slot(elem, ep, 0,
                                                               is_short, 1,
                                                               0, 0);

    for (slot_idx = 0; slot_idx < slot_cnt; slot_idx++) {
        slot_ptr[UCS_SYS_CACHE_LINE_SIZE - 1] = 0;
        slot_ptr += UCS_SYS_CACHE_LINE_SIZE;
    }
}

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
 * 4. CENTRALIZED mode, like "batched" but with root checking each slot:
 *
 *                                            Dummy slot, always empty
 *                                                    VVVVV
 *   | element->pending = 0 | ???-0 | ???-0 | ???-0 | ...-0 |
 *   | element->pending = 0 | ???-0 | 222-1 | ???-0 | ...-0 |
 *   | element->pending = 0 | 111-2 | 222-0 | ???-0 | ...-0 | < #1 sets #2 to 0
 *   | element->pending = 3 | 111-0 | 222-0 | 333-1 | ...-0 |
 *                        ^       ^       ^       ^
 *                        ^      #1      #2      #3  -> the last byte is polled
 *                        ^                             by the receiver process.
 *                        ^
 *                        the receiver process polls all these last bytes, and
 *                        once all the bytes have been set - the receiver knows
 *                        this operation is complete (none of the senders know).
 *
 *   The CENTRALIZED algorithm is slightly more complicated than the rest:
 *   - Each writer N checks if writer N+1 has a non-zero counter:
 *       > If writer N+1 has counter=0 - then set writer N counter to 1
 *       > If writer N+1 has counter=X - then set writer N+1 counter to 0 and
 *         writer N counter to X+1.
 *   - When polling, the reader checks counters starting from element->pending:
 *       > If element->pending is X - start by checking the counter of writer X
 *       > If writer X counter is Y - skip to writer X+Y (and continue checking
 *         from there), and also set the counter of writer X to 0.
 *       > If writer X counter is 0 - set element->pending=X
 *
 *   *Note: if element->pending is X - it means that all the writers 0,1,...,X-1
 *          have finished, and their counters should be 0 (so that those are
 *          ready for the next usage of this element).
 *
 * What is the size of each slot?
 * - Short messages have data-length-based slot size:
 *       ucs_align_up(data_length + 1, UCS_SYS_CACHE_LINE_SIZE);
 * - Bcopy messages have a fixed slot size: the segment_size / num_senders
 *   This is because you need to pass the slot offset to the packer callback -
 *   before you know the length actually written.
 * - Zcopy messages are not supported yet...
 *
 * The text above mostly focuses on incast (many-to-one communication), but
 * broadcast is also supported. In the broadcast case, each receiver needs to
 * indicate completion - which turns into incast again. For broadcast, the
 * layout is slightly different, and all the completion flags are grouped at the
 * end of the element (short/long sends) or segment (medium sends):
 *
 * 4. CENTRALIZED mode, like "batched" but with root checking each slot:
 *                            No flag     Padding   Dummy slot, always empty
 *                               V           V       VVVVV
 *   | element->pending = 0 | 0000 | ...-0 | ...-0 | ...-0 |
 *   | element->pending = 0 | 0000 | ...-0 | ...-1 | ...-0 | < #2 ACK-s
 *                                   ^^^^^   ^^^^^   ^^^^^
 *                                   Each ACK flag is in a separate cache-line
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
                              void *arg, unsigned flags, int is_centralized,
                              uct_incast_cb_t cb)
{
    uct_mm_coll_iface_t *iface = ucs_derived_of(tl_ep->iface, uct_mm_coll_iface_t);
    uct_mm_coll_ep_t *ep       = ucs_derived_of(tl_ep, uct_mm_coll_ep_t);
    uint8_t elem_flags;

    /* Sanity checks */
    UCT_CHECK_AM_ID(am_id);
    ucs_assert(is_bcast || (ep->remote_id != iface->my_coll_id));

    /* Grab the next cell I haven't yet written to */
    uint64_t head      = ep->tx_index;
    uct_mm_ep_t *mm_ep = &ep->super;
    unsigned fifo_size = iface->super.config.fifo_size;

    /* Check if there is room in the remote process's receive FIFO to write */
    if (!UCT_MM_EP_IS_ABLE_TO_SEND(head, mm_ep->cached_tail, fifo_size)) {
        if (!ucs_arbiter_group_is_empty(&mm_ep->arb_group)) {
            /* pending isn't empty. don't send now to prevent out-of-order sending */
            UCS_STATS_UPDATE_COUNTER(mm_ep->super.stats, UCT_EP_STAT_NO_RES, 1);
            return UCS_ERR_NO_RESOURCE;
        } else {
            /* pending is empty */
            /* update the local copy of the tail to its actual value on the remote peer */
            uct_mm_ep_update_cached_tail(mm_ep);
            if (!UCT_MM_EP_IS_ABLE_TO_SEND(head, mm_ep->cached_tail, fifo_size)) {
                UCS_STATS_UPDATE_COUNTER(mm_ep->super.stats, UCT_EP_STAT_NO_RES, 1);
                return UCS_ERR_NO_RESOURCE;
            }
        }
    }

    /* Determine function parameters based on the collective operation type */
    int is_tight       = (op_mode != UCT_COLL_DTYPE_MODE_PADDED);
    int is_batched     = !is_bcast && !is_tight;
    int is_zero_offset = (ep->my_offset == 0);
    uint8_t proc_cnt   = ep->proc_cnt;
    uint16_t stride    = is_tight ? length :
            uct_mm_coll_iface_centralized_get_slot_size(ep, length, is_short);

    ucs_assert(!is_tight || (length > 0));

    /* Get ready to write to the next element */
    uct_mm_coll_fifo_element_t *elem = ep->tx_elem;

    /* Write the buffer (or reduce onto an existing buffer) */
    uint8_t *base_address = UCT_MM_COLL_GET_BASE_ADDRESS(is_short, is_batched,
                                                         elem, ep, is_bcast,
                                                         0 /* _is_recv */,
                                                         stride);

    uint32_t previous_pending = 0;
    int is_lock_needed = !is_short && !is_bcast && (flags & UCT_SEND_FLAG_PACK_LOCK);
    if (ucs_unlikely(is_lock_needed)) {
        ucs_spin_lock(&elem->lock);
        previous_pending = elem->pending++;
    }

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
        goto last_writer;
    }

    if (is_batched) {
        /* BATCHED or CENTRALIZED modes only: mark my slot as "written" */
        if (is_centralized) {
            uct_mm_coll_ep_centralized_mark_incast_tx_done(elem, ep,
                                                           base_address,
                                                           stride, cb);
        } else {
            uct_mm_coll_iface_centralized_set_slot_counter(base_address, stride, 1);
        }

        /*
         * One process signals this element is "ready for inspection", but not
         * necessarily ready for the receiver to process (#0 did write his piece
         * but others may still be pending). Another important part of this step
         * is to notify the receiver what the stride is - so it can poll the
         * flags of each slot.
         */
        if (is_zero_offset) {
            ucs_prefetch_write(elem);
            goto last_writer;
        } else {
            goto trace_send;
        }
    } else if (is_tight) {
        /* BATCHED mode - update the central counter about my completion */
        ucs_memory_cpu_store_fence();
        previous_pending = ucs_atomic_fadd32(&elem->pending, 1);
    } else if (is_lock_needed) {
        /* LOCKED mode - just unlock before finishing */
        ucs_spin_unlock(&elem->lock);
        /* Note: 'previous_pending' was already updated when locking */
    }

skip_payload:
    /* Check if this sender is the last expected sender for this element */
    if (previous_pending == proc_cnt - 1) {
last_writer:
        stride = is_batched ? stride :
                 (!is_tight ? (length + (is_short * sizeof(header))) :
                              (length * proc_cnt) + (is_short * sizeof(header)));

        /* Change the owner bit to indicate that the writing is complete.
         * The owner bit flips after every FIFO wrap-around */
        elem_flags = (head & fifo_size) && UCT_MM_FIFO_ELEM_FLAG_OWNER;
        if (is_short) {
            elem_flags    |= UCT_MM_FIFO_ELEM_FLAG_INLINE;
            elem->header   = header;
        }
        elem->op_mode      = op_mode;
        elem->super.am_id  = am_id;
        elem->super.length = stride;

        /* memory barrier - make sure that the memory is flushed before setting the
         * 'writing is complete' flag which the reader checks */
        ucs_memory_cpu_store_fence();

        /* Set this element as "written" - pass ownership to the receiver */
        elem->super.flags = elem_flags;

        ucs_share_cache(elem);

        /* update the remote head element */
        mm_ep->fifo_ctl->head = head;

        /* signal remote, if so requested */
        if (ucs_unlikely(flags & UCT_SEND_FLAG_SIGNALED)) {
            uct_mm_ep_signal_remote(mm_ep);
        } else {
            ucs_share_cache((void*)&mm_ep->fifo_ctl->head);
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

    ucs_share_cache(base_address);

    if (is_short && is_batched) {
        base_address = (void*)UCS_PTR_BYTE_DIFF(elem, base_address);
    }

    /* Update both the index and the pointer to the next element */
    elem = ep->tx_elem = UCT_MM_COLL_EP_NEXT_FIFO_ELEM(ep, elem, ep->tx_index);

    ucs_prefetch_write(elem);

    if (is_short && is_batched) {
        /* Prefetch the slot we will probably write to */
        ucs_prefetch_write(UCS_PTR_BYTE_OFFSET(elem, base_address));
    }

    return UCS_OK;
}

static UCS_F_ALWAYS_INLINE ucs_status_t
uct_mm_coll_ep_am_short(uct_ep_h ep, uint8_t id, uint64_t header,
                        const void *payload, unsigned length, int is_bcast,
                        int is_centralized, uct_incast_cb_t cb)
{
    unsigned orig_length          = UCT_COLL_DTYPE_MODE_UNPACK_VALUE(length);
    uct_coll_dtype_mode_t op_mode = UCT_COLL_DTYPE_MODE_UNPACK_MODE(length);

    ucs_assert((op_mode == UCT_COLL_DTYPE_MODE_PADDED) ||
               (op_mode == UCT_COLL_DTYPE_MODE_PACKED));

    ssize_t ret = uct_mm_coll_ep_am_common_send(op_mode, is_bcast, 1, ep, id,
        orig_length, header, payload, NULL, NULL, 0, is_centralized, cb);

    return (ret > 0) ? UCS_OK : (ucs_status_t)ret;
}

ucs_status_t uct_mm_bcast_ep_am_short_batched(uct_ep_h ep,
                                              uint8_t id,
                                              uint64_t header,
                                              const void *payload,
                                              unsigned length)
{
    return uct_mm_coll_ep_am_short(ep, id, header, payload, length, 1, 0, NULL);
}

ucs_status_t uct_mm_bcast_ep_am_short_centralized(uct_ep_h ep,
                                                  uint8_t id,
                                                  uint64_t header,
                                                  const void *payload,
                                                  unsigned length)
{
    return uct_mm_coll_ep_am_short(ep, id, header, payload, length, 1, 1, NULL);
}

ucs_status_t uct_mm_incast_ep_am_short_batched(uct_ep_h ep,
                                               uint8_t id,
                                               uint64_t header,
                                               const void *payload,
                                               unsigned length)
{
    return uct_mm_coll_ep_am_short(ep, id, header, payload, length, 0, 0, NULL);
}

ucs_status_t uct_mm_incast_ep_am_short_centralized(uct_ep_h ep,
                                                   uint8_t id,
                                                   uint64_t header,
                                                   const void *payload,
                                                   unsigned length)
{
    return uct_mm_coll_ep_am_short(ep, id, header, payload, length, 0, 1, NULL);
}

static UCS_F_ALWAYS_INLINE
ucs_status_t uct_mm_incast_ep_am_short_centralized_cb(uct_ep_h ep,
                                                      uint8_t id,
                                                      uint64_t header,
                                                      const void *payload,
                                                      unsigned length,
                                                      uct_incast_cb_t cb)
{
    return uct_mm_coll_ep_am_short(ep, id, header, payload, length, 0, 1, cb);
}

ucs_status_t uct_mm_incast_ep_am_short_centralized_ep_cb(uct_ep_h ep,
                                                         uint8_t id,
                                                         uint64_t header,
                                                         const void *payload,
                                                         unsigned length)
{
    uct_incast_cb_t cb = ucs_derived_of(ep, uct_mm_incast_ep_t)->cb;

    /* Note: in this case - cb would be checked to be non-zero during runtime */
    return uct_mm_coll_ep_am_short(ep, id, header, payload, length, 0, 1, cb);
}

static UCS_F_ALWAYS_INLINE ssize_t
uct_mm_coll_ep_am_bcopy(uct_ep_h ep, uint8_t id, uct_pack_callback_t pack_cb,
                        void *arg, unsigned flags, int is_bcast,
                        int is_centralized, uct_incast_cb_t cb)
{
    unsigned orig_flags           = UCT_COLL_DTYPE_MODE_UNPACK_VALUE(flags);
    uct_coll_dtype_mode_t op_mode = UCT_COLL_DTYPE_MODE_UNPACK_MODE(flags);

    ucs_assert((op_mode == UCT_COLL_DTYPE_MODE_PADDED) ||
               (op_mode == UCT_COLL_DTYPE_MODE_PACKED));

    ssize_t ret = uct_mm_coll_ep_am_common_send(op_mode, is_bcast, 0, ep, id, 0,
                                                0, NULL, pack_cb, arg, orig_flags,
                                                is_centralized, cb);

    return ret;
}

ssize_t uct_mm_bcast_ep_am_bcopy_batched(uct_ep_h ep, uint8_t id,
                                         uct_pack_callback_t pack_cb,
                                         void *arg, unsigned flags)
{
    return uct_mm_coll_ep_am_bcopy(ep, id, pack_cb, arg, flags, 1, 0, NULL);
}

ssize_t uct_mm_bcast_ep_am_bcopy_centralized(uct_ep_h ep, uint8_t id,
                                             uct_pack_callback_t pack_cb,
                                             void *arg, unsigned flags)
{
    return uct_mm_coll_ep_am_bcopy(ep, id, pack_cb, arg, flags, 1, 1, NULL);
}

ssize_t uct_mm_incast_ep_am_bcopy_batched(uct_ep_h ep, uint8_t id,
                                          uct_pack_callback_t pack_cb,
                                          void *arg, unsigned flags)
{
    return uct_mm_coll_ep_am_bcopy(ep, id, pack_cb, arg, flags, 0, 0, NULL);
}

ssize_t uct_mm_incast_ep_am_bcopy_centralized(uct_ep_h ep, uint8_t id,
                                              uct_pack_callback_t pack_cb,
                                              void *arg, unsigned flags)
{
    return uct_mm_coll_ep_am_bcopy(ep, id, pack_cb, arg, flags, 0, 1, NULL);
}

static UCS_F_ALWAYS_INLINE
ssize_t uct_mm_incast_ep_am_bcopy_centralized_cb(uct_ep_h ep, uint8_t id,
                                                 uct_pack_callback_t pack_cb,
                                                 void *arg, unsigned flags,
                                                 uct_incast_cb_t cb)
{
    return uct_mm_coll_ep_am_bcopy(ep, id, pack_cb, arg, flags, 0, 1, cb);
}

ssize_t uct_mm_incast_ep_am_bcopy_centralized_ep_cb(uct_ep_h ep, uint8_t id,
                                                    uct_pack_callback_t pack_cb,
                                                    void *arg, unsigned flags)
{
    uct_incast_cb_t cb = ucs_derived_of(ep, uct_mm_incast_ep_t)->cb;

    /* Note: in this case - cb would be checked to be non-zero during runtime */
    return uct_mm_coll_ep_am_bcopy(ep, id, pack_cb, arg, flags, 0, 1, cb);
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
            length, header_value, payload, NULL, offsets, orig_flags, 1, 0);

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

#define ucs_sum(_x, _y) ((_x)+(_y))

#define UCT_MM_INCAST_IFACE_CB_BODY(_operator, _operand) \
    void UCT_MM_INCAST_IFACE_CB_NAME(helper, _operator, _operand) \
        (void *dst, const void *src) { \
        *(_operand *)dst = ucs_##_operator (*(_operand *)dst, \
                                            *(_operand *)src); \
    } \
    static UCS_F_ALWAYS_INLINE \
    void UCT_MM_INCAST_IFACE_CB_NAME(inline_helper, _operator, _operand) \
        (void *dst, const void *src) { \
        *(_operand *)dst = ucs_##_operator (*(_operand *)dst, \
                                            *(_operand *)src); \
    } \
    ucs_status_t UCT_MM_INCAST_IFACE_CB_NAME(short, _operator, _operand) \
        (uct_ep_h ep, uint8_t id, uint64_t h, const void *p, unsigned l) { \
        return uct_mm_incast_ep_am_short_centralized_cb(ep, id, h, p, l, \
            UCT_MM_INCAST_IFACE_CB_NAME(inline_helper, _operator, _operand)); \
    } \
    ssize_t UCT_MM_INCAST_IFACE_CB_NAME(bcopy, _operator, _operand) \
        (uct_ep_h ep, uint8_t id, uct_pack_callback_t cb, void *a, unsigned f) { \
        return uct_mm_incast_ep_am_bcopy_centralized_cb(ep, id, cb, a, f, \
            UCT_MM_INCAST_IFACE_CB_NAME(inline_helper, _operator, _operand)); \
    }

#define UCT_MM_INCAST_IFACE_CB_INSTANCES(_operator) \
        UCT_MM_INCAST_IFACE_CB_BODY(_operator, float) \
        UCT_MM_INCAST_IFACE_CB_BODY(_operator, double)

UCT_MM_INCAST_IFACE_CB_INSTANCES(sum)
UCT_MM_INCAST_IFACE_CB_INSTANCES(min)
UCT_MM_INCAST_IFACE_CB_INSTANCES(max)


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

    ucs_ptr_array_set(&iface->ep_ptrs, ep->remote_id, ep);

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
    // TODO: restore ucs_assert(addr->coll_id < iface->sm_proc_cnt);

    uct_ep_params_t super_params;
    memcpy(&super_params, params, sizeof(*params)); // TODO: fix ABI compatibility
    super_params.iface_addr =
            (void*)&((uct_mm_coll_iface_addr_t*)params->iface_addr)->super;

    UCS_CLASS_CALL_SUPER_INIT(uct_mm_ep_t, &super_params);

    if (addr->coll_id == (uint32_t)-1) {
        return UCS_OK;
    }

    ucs_assert(addr->coll_id == 0); // TODO: remove (debug only)

    self->remote_id  = addr->coll_id;
    self->my_offset  = iface->my_coll_id -
                      (uint32_t)(addr->coll_id < iface->my_coll_id);
    self->proc_cnt   = iface->sm_proc_cnt - 1;
    self->tx_index   = 0;
    self->tx_elem    = self->super.fifo_elems;
    self->elem_size  = iface->super.config.fifo_elem_size;
    self->fifo_mask  = iface->super.fifo_mask;
    self->seg_size   = iface->super.config.seg_size;
    self->ref_count  = 1;

    if (iface->my_coll_id == self->remote_id) {
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

    self->recv_check.flags_state = UCT_MM_FIFO_FLAG_STATE_UNCACHED;
    self->recv_check.fifo_shift  = iface->super.recv_check.fifo_shift;
    self->recv_check.read_index  = 0;
    self->recv_check.read_elem   = self->super.super.fifo_elems;
    self->recv_check.fifo_ctl    = self->super.super.fifo_ctl;

    self->recv_check.fifo_release_factor_mask =
            iface->super.recv_check.fifo_release_factor_mask;

    ucs_debug("mm_bcast: ep connected: %p, src_coll_id: %u, dst_coll_id: %u",
              self, iface->my_coll_id, self->super.remote_id);

    return UCS_OK;
}

UCS_CLASS_INIT_FUNC(uct_mm_incast_ep_t, const uct_ep_params_t *params)
{
    uct_mm_coll_iface_t *iface = ucs_derived_of(params->iface, uct_mm_coll_iface_t);

    UCS_CLASS_CALL_SUPER_INIT(uct_mm_coll_ep_t, params);

    self->cb = ucs_derived_of(params->iface, uct_mm_incast_iface_t)->cb;

    ucs_debug("mm_incast: ep connected: %p, src_coll_id: %u, dst_coll_id: %u",
              self, iface->my_coll_id, self->super.remote_id);

    return UCS_OK;
}

static UCS_CLASS_CLEANUP_FUNC(uct_mm_coll_ep_t) {}
static UCS_CLASS_CLEANUP_FUNC(uct_mm_bcast_ep_t) {}
static UCS_CLASS_CLEANUP_FUNC(uct_mm_incast_ep_t) {}

UCS_CLASS_DEFINE(uct_mm_coll_ep_t, uct_mm_ep_t)
UCS_CLASS_DEFINE(uct_mm_bcast_ep_t, uct_mm_coll_ep_t)
UCS_CLASS_DEFINE(uct_mm_incast_ep_t, uct_mm_coll_ep_t)

static UCS_F_ALWAYS_INLINE void
uct_mm_coll_ep_elem_set_read(uct_mm_coll_fifo_element_t *elem,
                             uct_mm_fifo_ctl_t *fifo_ctl,
                             uint64_t index)
{
    if (fifo_ctl->tail == index) {
        fifo_ctl->tail = index + 1;
    }

    ucs_memory_cpu_store_fence();
}

static UCS_F_ALWAYS_INLINE int
uct_mm_coll_ep_elem_is_last_to_read(uct_mm_coll_fifo_element_t *elem,
                                    uct_mm_fifo_ctl_t *fifo_ctl,
                                    int is_pending_batched,
                                    uint8_t proc_cnt,
                                    uint64_t last_index)
{
    uint32_t pending = is_pending_batched ? elem->pending :
                                            ucs_atomic_fadd32(&elem->pending, 1);

    if (pending == proc_cnt) {
        uct_mm_coll_ep_elem_set_read(elem, fifo_ctl, last_index);
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

ucs_status_t uct_mm_coll_ep_flush(uct_ep_h tl_ep, unsigned flags,
                                  uct_completion_t *comp)
{
    uct_mm_coll_ep_t *ep = ucs_derived_of(tl_ep, uct_mm_coll_ep_t);

    if (ucs_unlikely(ep->super.fifo_ctl == NULL)) {
        return UCS_OK;
    }

    return uct_mm_ep_flush(tl_ep, flags, comp);
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
    int is_pending_batched = (elem->op_mode == UCT_COLL_DTYPE_MODE_PADDED);
    while ((elem_index < ep->tx_index) &&
           (uct_mm_coll_ep_elem_is_last_to_read(elem, ep->super.fifo_ctl,
                                                is_pending_batched,
                                                ep->proc_cnt, elem_index))) {
        elem = uct_mm_coll_ep_get_next_rx_elem(elem, ep, &elem_index);
    }
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
uct_mm_coll_ep_process_recv(uct_mm_coll_ep_t *ep, uct_mm_coll_iface_t *iface,
                            uct_mm_coll_fifo_element_t *elem, int is_incast,
                            int is_loopback, int use_cb, uct_incast_cb_t cb)
{
    /* Detect incoming message parameters */
    int am_cb_flags;
    int is_short           = elem->super.flags & UCT_MM_FIFO_ELEM_FLAG_INLINE;
    int is_pending_batched = (elem->op_mode == UCT_COLL_DTYPE_MODE_PADDED);

    /* CENTRALIZED mode only - check if this is the last writer */
    if (ucs_likely(is_pending_batched) && is_incast) {
        if (!uct_mm_coll_ep_centralized_is_elem_ready(elem, ep, 1, is_short,
                                                      is_loopback, use_cb, cb)) {
            return 0; /* incast started, but not all peers have written yet */
        }
    }

    ucs_memory_cpu_load_fence();

    uint16_t stride       = elem->super.length;
    uint8_t *base_address = UCT_MM_COLL_GET_BASE_ADDRESS(is_short,
                            0 /* _is_batch */, elem, ep, is_incast,
                            1 /* _is_recv */, 0 /* _stride */);

    if (use_cb) {
        size_t shift  = uct_mm_coll_iface_centralized_get_slot_offset(ep,
                            is_incast, is_short, stride, ep->proc_cnt);
        base_address += shift - sizeof(uint64_t);
        if (is_short) {
            /* Place the header right before the resulting buffer */
            *(uint64_t*)base_address = elem->header;
        } else {
            /* Place the offset - so that UCT_CB_PARAM_FLAG_SHIFTED works */
            *(uint64_t*)base_address = shift;

            /* Set the callback to receive a pointer to the data itself*/
            base_address += sizeof(uint64_t);
        }

        am_cb_flags = 0;
    }

    /* choose the flags for the Active Message callback argument */
    if (!is_short) {
        if (is_incast) {
            if (use_cb) {
                am_cb_flags = UCT_CB_PARAM_FLAG_DESC | UCT_CB_PARAM_FLAG_SHIFTED;
            } else {
                am_cb_flags = UCT_CB_PARAM_FLAG_DESC | UCT_CB_PARAM_FLAG_STRIDE;
            }
        } else {
            am_cb_flags     = UCT_CB_PARAM_FLAG_DESC | UCT_CB_PARAM_FLAG_SHARED;
        }
    } else if (!use_cb) {
        am_cb_flags         = is_incast ? UCT_CB_PARAM_FLAG_STRIDE : 0;
        base_address       -= sizeof(elem->header);
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
     * This descriptor may reside on memory belonging to another process.
     * The consequence is that it can only be accessed for reading, not
     * writing (technically writing is possible, but would conflict with
     * other processes using this descriptor). UCT_CB_PARAM_FLAG_SHARED is
     * used to pass this information to upper layers.
     */
    if (ucs_unlikely(status == UCS_INPROGRESS)) {
        void *desc = base_address - iface->super.rx_headroom;
        ucs_assert(!is_short);

        /* If I'm the owner of this memory - I can replace the element's segment */
        if (is_incast) {
            /* assign a new receive descriptor to this FIFO element.*/
            uct_mm_assign_desc_to_fifo_elem(&iface->super, &elem->super, 1);

            /* later release of this desc - the easy way */
            uct_recv_desc(desc) = (uct_recv_desc_t*)&iface->super.release_desc;
        } else {
            /* set information for @ref uct_mm_bcast_iface_release_shared_desc_func */
            uint8_t* slot_ptr = uct_mm_coll_iface_centralized_get_slot(elem, ep, 0,
                                                                       is_short, 0, 0,
                                                                       ep->my_offset);
            /* each receviver has its own slot, No conflict. */
            slot_ptr[0] = iface->my_coll_id;

            ucs_assert(slot_ptr[UCS_SYS_CACHE_LINE_SIZE-1] == 0);
            /* all bcast receivers have the same slot start offset, overwirte is ok */
            size_t slot_offset = uct_mm_coll_iface_centralized_get_slot_offset(ep, 0, 
                                                                               is_short, 0, 0);
            uct_recv_desc(desc) = (void*)slot_offset;
        }
    } else if (is_pending_batched && !is_incast) {
        /* I finished reading the broadcast - let the sender know */
        uct_mm_coll_ep_centralized_mark_bcast_rx_done(elem, ep, is_short);
    }

    return 1;
}

static UCS_F_ALWAYS_INLINE unsigned
uct_mm_incast_iface_poll_fifo(uct_mm_incast_iface_t *iface, int use_cb,
                              uct_incast_cb_t cb)
{
    uct_mm_base_iface_t *mm_iface   = &iface->super.super;
    unsigned poll_count             = mm_iface->fifo_poll_count;
    unsigned poll_total             = mm_iface->fifo_poll_count;
    uct_mm_fifo_check_t *recv_check = &mm_iface->recv_check;

    uct_mm_coll_ep_t dummy = { .proc_cnt = iface->super.sm_proc_cnt - 1 };

    ucs_assert(poll_count >= UCT_MM_IFACE_FIFO_MIN_POLL);

    uct_mm_coll_fifo_element_t *elem = ucs_derived_of(recv_check->read_elem,
                                                      uct_mm_coll_fifo_element_t);

    while ((uct_mm_iface_fifo_has_new_data(recv_check, &elem->super, 0)) &&
           (uct_mm_coll_ep_process_recv(&dummy, &iface->super, elem, 1, 1, use_cb, cb))) {
        elem->pending = 0;

        UCT_MM_COLL_IFACE_NEXT_RECV_FIFO_ELEM(mm_iface, elem,
                                              recv_check->read_index);

        recv_check->read_elem  = &elem->super;

        uct_mm_progress_fifo_tail(recv_check);

        if (ucs_likely(--poll_count == 0)) {
            break;
        }
    }

    unsigned ret = poll_total - poll_count;

    uct_mm_iface_fifo_window_adjust(mm_iface, ret);

    if (ret == 0) {
        /* progress the pending sends (if there are any) */
        ucs_arbiter_dispatch(&mm_iface->arbiter, 1, uct_mm_ep_process_pending, &ret);
    }

    return ret;
}

unsigned uct_mm_incast_iface_progress_cb(uct_iface_h tl_iface)
{
    uct_mm_incast_iface_t *iface = ucs_derived_of(tl_iface,
                                                  uct_mm_incast_iface_t);

    return uct_mm_incast_iface_poll_fifo(iface, 1, iface->cb);
}

unsigned uct_mm_incast_iface_progress(uct_iface_h tl_iface)
{
    uct_mm_incast_iface_t *iface = ucs_derived_of(tl_iface,
                                                  uct_mm_incast_iface_t);

    return uct_mm_incast_iface_poll_fifo(iface, 0, NULL);
}

static inline uct_mm_coll_ep_t* uct_mm_coll_ep_find(uct_mm_coll_iface_t *iface,
                                                    uint8_t coll_id)
{
    unsigned index;
    uct_mm_coll_ep_t *ep_iter;
    ucs_ptr_array_for_each(ep_iter, index, &iface->ep_ptrs) {
        if (coll_id == ep_iter->remote_id) {
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
        if (iface->loopback_ep != NULL) {
            iface->loopback_ep->ref_count++;
        }
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
    uint8_t coll_id = ep->remote_id;

    ucs_ptr_array_for_each(ep_iter, index, &iface->ep_ptrs) {
        if (coll_id == ep_iter->remote_id) {
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
    if (ep->remote_id != iface->my_coll_id) {
        uct_mm_coll_ep_del(ucs_derived_of(tl_ep->iface, uct_mm_coll_iface_t), ep);
    } else {
        iface->loopback_ep = NULL;
    }

    UCS_CLASS_DELETE(uct_mm_coll_ep_t, ep);
}

void uct_mm_bcast_ep_destroy(uct_ep_h tl_ep)
{
    uct_mm_bcast_iface_t *iface = ucs_derived_of(tl_ep->iface, uct_mm_bcast_iface_t);
    if (iface->last_nonzero_ep == ucs_derived_of(tl_ep, uct_mm_bcast_ep_t)) {
        iface->last_nonzero_ep = NULL;
    }

    uct_mm_coll_ep_destroy(tl_ep);
}

static UCS_F_ALWAYS_INLINE void
uct_mm_bcast_ep_poll_tail(uct_mm_bcast_iface_t *iface)
{
    uct_mm_base_iface_t *mm_iface    = &iface->super.super;
    uct_mm_fifo_check_t *recv_check  = &mm_iface->recv_check;
    uct_mm_coll_fifo_element_t *elem = ucs_derived_of(recv_check->read_elem,
                                                      uct_mm_coll_fifo_element_t);
    uct_mm_fifo_ctl_t *fifo_ctl      = recv_check->fifo_ctl;
    uint64_t read_index              = recv_check->read_index;
    uint64_t read_limit              = fifo_ctl->head;
    int progress                     = 0;

    UCT_MM_COLL_BCAST_EP_DUMMY(dummy, iface);

    while ((ucs_unlikely(read_index < read_limit)) &&
           (!uct_mm_iface_fifo_flag_no_new_data(elem->super.flags, read_index,
                                                mm_iface->recv_check.fifo_shift)) &&
           (ucs_unlikely(uct_mm_coll_ep_centralized_is_elem_ready(elem, &dummy,
                   0, elem->super.flags & UCT_MM_FIFO_ELEM_FLAG_INLINE, 1, 0, NULL)))) {
        progress = 1;

        /* proceed to the next read_index/element */
        UCT_MM_COLL_IFACE_NEXT_RECV_FIFO_ELEM(mm_iface, elem, read_index);
    }

    if (progress) {
        fifo_ctl->tail          = read_index - 1;
        recv_check->read_index  = read_index;
        recv_check->read_elem   = &elem->super;
        recv_check->flags_state = UCT_MM_FIFO_FLAG_STATE_UNCACHED;
    }
}

static UCS_F_ALWAYS_INLINE unsigned
uct_mm_bcast_ep_poll_fifo(uct_mm_bcast_iface_t *iface, uct_mm_bcast_ep_t *ep)
{
    uct_mm_fifo_check_t *recv_check  = &ep->recv_check;
    uct_mm_coll_fifo_element_t *elem = ucs_container_of(ep->recv_check.read_elem,
                                                        uct_mm_coll_fifo_element_t,
                                                        super);

    if (!uct_mm_iface_fifo_has_new_data(recv_check, &elem->super, 0)) {
        return 0;
    }

    if (!uct_mm_coll_ep_process_recv(&ep->super, &iface->super, elem, 0, 0, 0, NULL)) {
        return 0;
    }

    /* Progress next reading position */
    ep->recv_check.flags_state = UCT_MM_FIFO_FLAG_STATE_UNCACHED;
    ep->recv_check.read_elem   = UCT_MM_COLL_EP_NEXT_FIFO_ELEM(&ep->super,
                                         elem, ep->recv_check.read_index);

    /*
     * Note: cannot call uct_mm_progress_fifo_tail() here, because I might have
     *       finished reading an element another peer hasn't (so it can't be
     *       marked as ready for re-use by incrementing the FIFO tail). This is
     *       what uct_mm_coll_ep_centralized_mark_bcast_rx_done() is for.
     */

    return 1;
}

static UCS_F_ALWAYS_INLINE unsigned
uct_mm_bcast_iface_progress_ep(uct_mm_bcast_iface_t *iface,
                               uct_mm_bcast_ep_t *ep,
                               unsigned count_limit)
{
    unsigned count, total_count = 0;

    do {
        count = uct_mm_bcast_ep_poll_fifo(iface, ep);
    } while (ucs_unlikely(count != 0) &&
             ((total_count = total_count + count) < count_limit));

    return total_count;
}

unsigned uct_mm_bcast_iface_progress(uct_iface_h tl_iface)
{
    uct_mm_bcast_iface_t *iface   = ucs_derived_of(tl_iface, uct_mm_bcast_iface_t);
    uct_mm_base_iface_t *mm_iface = ucs_derived_of(tl_iface, uct_mm_base_iface_t);
    uct_mm_bcast_ep_t *ep         = iface->last_nonzero_ep;
    unsigned count, count_limit   = mm_iface->fifo_poll_count;

    if (ucs_likely(ep != NULL)) {
        count = uct_mm_bcast_iface_progress_ep(iface, ep, count_limit);
        if (ucs_likely(count > 0)) {
            goto poll_done;
        }
    }

    if (ucs_ptr_array_lookup(&iface->super.ep_ptrs, iface->poll_ep_idx, ep)) {
        count = uct_mm_bcast_iface_progress_ep(iface, ep, count_limit);
    } else {
        count = 0;
    }

    if (ucs_likely(iface->super.ep_ptrs.size > 0)) {
        iface->poll_ep_idx = (iface->poll_ep_idx + 1) % iface->super.ep_ptrs.size;
    }

    if (ucs_likely(count == 0)) {
        /* use the time to check if the tail element has been released */
        uct_mm_bcast_ep_poll_tail(iface);

        /* progress the pending sends (if there are any) */
        ucs_arbiter_dispatch(&mm_iface->arbiter, 1, uct_mm_ep_process_pending, &count);
    } else {
        iface->last_nonzero_ep = ep;
    }

poll_done:
    uct_mm_iface_fifo_window_adjust(mm_iface, count);

    return count;
}
