/**
* Copyright (C) Mellanox Technologies Ltd. 2001-2019.  ALL RIGHTS RESERVED.
* Copyright (C) UT-Battelle, LLC. 2015. ALL RIGHTS RESERVED.
* Copyright (C) Huawei Technologies Co., Ltd. 2021.  ALL RIGHTS RESERVED.
*
* See file LICENSE for terms.
*/

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "uct_iface.h"
#include "uct_cm.h"
#include "uct_iov.inl"

#include <uct/api/uct.h>
#include <uct/api/v2/uct_v2.h>
#include <ucs/async/async.h>
#include <ucs/sys/string.h>
#include <ucs/time/time.h>
#include <ucs/debug/debug_int.h>
#include <ucs/vfs/base/vfs_obj.h>


#ifdef ENABLE_STATS
static ucs_stats_class_t uct_ep_stats_class = {
    .name          = "uct_ep",
    .num_counters  = UCT_EP_STAT_LAST,
    .class_id      = UCS_STATS_CLASS_ID_INVALID,
    .counter_names = {
        [UCT_EP_STAT_AM]          = "am",
        [UCT_EP_STAT_PUT]         = "put",
        [UCT_EP_STAT_GET]         = "get",
        [UCT_EP_STAT_ATOMIC]      = "atomic",
#if IBV_HW_TM
        [UCT_EP_STAT_TAG]         = "tag",
#endif
        [UCT_EP_STAT_BYTES_SHORT] = "bytes_short",
        [UCT_EP_STAT_BYTES_BCOPY] = "bytes_bcopy",
        [UCT_EP_STAT_BYTES_ZCOPY] = "bytes_zcopy",
        [UCT_EP_STAT_NO_RES]      = "no_res",
        [UCT_EP_STAT_FLUSH]       = "flush",
        [UCT_EP_STAT_FLUSH_WAIT]  = "flush_wait",
        [UCT_EP_STAT_PENDING]     = "pending",
        [UCT_EP_STAT_FENCE]       = "fence"
    }
};

static ucs_stats_class_t uct_iface_stats_class = {
    .name          = "uct_iface",
    .num_counters  = UCT_IFACE_STAT_LAST,
    .class_id      = UCS_STATS_CLASS_ID_INVALID,
    .counter_names = {
        [UCT_IFACE_STAT_RX_AM]       = "rx_am",
        [UCT_IFACE_STAT_RX_AM_BYTES] = "rx_am_bytes",
        [UCT_IFACE_STAT_TX_NO_DESC]  = "tx_no_desc",
        [UCT_IFACE_STAT_FLUSH]       = "flush",
        [UCT_IFACE_STAT_FLUSH_WAIT]  = "flush_wait",
        [UCT_IFACE_STAT_FENCE]       = "fence"
    }
};
#endif


static ucs_status_t uct_iface_stub_am_handler(void *arg, void *data,
                                              size_t length, unsigned flags)
{
    const size_t dump_len = 64;
    uint8_t id            = (uintptr_t)arg;
    char dump_str[(dump_len * 4) + 1]; /* 1234:5678\n\0 */

    ucs_warn("got active message id %d, but no handler installed", id);
    ucs_warn("payload %zu of %zu bytes:\n%s", ucs_min(length, dump_len), length,
             ucs_str_dump_hex(data, ucs_min(length, dump_len),
                              dump_str, sizeof(dump_str), 16));
    ucs_log_print_backtrace(UCS_LOG_LEVEL_WARN);
    return UCS_OK;
}

static void uct_iface_set_stub_am_handler(uct_base_iface_t *iface, uint8_t id)
{
    iface->am[id].cb    = uct_iface_stub_am_handler;
    iface->am[id].arg   = (void*)(uintptr_t)id;
    iface->am[id].flags = UCT_CB_FLAG_ASYNC;
}

ucs_status_t uct_iface_set_am_handler(uct_iface_h tl_iface, uint8_t id,
                                      uct_am_callback_t cb, void *arg,
                                      uint32_t flags)
{
    uct_base_iface_t *iface = ucs_derived_of(tl_iface, uct_base_iface_t);
    ucs_status_t status;
    uct_iface_attr_t attr;

    if (id >= UCT_AM_ID_MAX) {
        ucs_error("active message id out-of-range (got: %d max: %d)", id,
                  (int)UCT_AM_ID_MAX);
        return UCS_ERR_INVALID_PARAM;
    }

    if (cb == NULL) {
        uct_iface_set_stub_am_handler(iface, id);
        return UCS_OK;
    }

    status = uct_iface_query(tl_iface, &attr);
    if (status != UCS_OK) {
        return status;
    }

    UCT_CB_FLAGS_CHECK(flags);

    /* If user wants a synchronous callback, it must be supported, or the
     * callback could be called from another thread.
     */
    if (!(flags & UCT_CB_FLAG_ASYNC) && !(attr.cap.flags & UCT_IFACE_FLAG_CB_SYNC)) {
        ucs_error("Synchronous callback requested, but not supported");
        return UCS_ERR_INVALID_PARAM;
    }

    iface->am[id].cb    = cb;
    iface->am[id].arg   = arg;
    iface->am[id].flags = flags;
    return UCS_OK;
}

ucs_status_t uct_iface_set_am_tracer(uct_iface_h tl_iface, uct_am_tracer_t tracer,
                                     void *arg)
{
    uct_base_iface_t *iface = ucs_derived_of(tl_iface, uct_base_iface_t);

    iface->am_tracer     = tracer;
    iface->am_tracer_arg = arg;
    return UCS_OK;
}

void uct_iface_dump_am(uct_base_iface_t *iface, uct_am_trace_type_t type,
                       uint8_t id, const void *data, size_t length,
                       char *buffer, size_t max)
{
    if (iface->am_tracer != NULL) {
        iface->am_tracer(iface->am_tracer_arg, type, id, data, length, buffer, max);
    }
}

void uct_iface_mpool_empty_warn(uct_base_iface_t *iface, ucs_mpool_t *mp)
{
    static ucs_time_t warn_time = 0;
    ucs_time_t now = ucs_get_time();

    /* Limit the rate of warning to once in 30 seconds. This gives reasonable
     * indication about a deadlock without flooding with warnings messages. */
    if (warn_time == 0) {
        warn_time = now;
    }
    if (now - warn_time > ucs_time_from_sec(30)) {
        ucs_warn("Memory pool %s is empty", ucs_mpool_name(mp));
        warn_time = now;
    }
}

void uct_iface_set_async_event_params(const uct_iface_params_t *params,
                                      uct_async_event_cb_t *event_cb,
                                      void **event_arg)
{
    *event_cb  = UCT_IFACE_PARAM_VALUE(params, async_event_cb, ASYNC_EVENT_CB,
                                       NULL);                                       
    *event_arg = UCT_IFACE_PARAM_VALUE(params, async_event_arg, ASYNC_EVENT_ARG,
                                       NULL);
}


ucs_status_t uct_iface_query(uct_iface_h iface, uct_iface_attr_t *iface_attr)
{
    return iface->ops.iface_query(iface, iface_attr);
}

ucs_status_t
uct_iface_estimate_perf(uct_iface_h tl_iface, uct_perf_attr_t *perf_attr)
{
    uct_base_iface_t *iface = ucs_derived_of(tl_iface, uct_base_iface_t);

    return iface->internal_ops->iface_estimate_perf(tl_iface, perf_attr);
}

ucs_status_t uct_iface_get_device_address(uct_iface_h iface, uct_device_addr_t *addr)
{
    return iface->ops.iface_get_device_address(iface, addr);
}

ucs_status_t uct_iface_get_address(uct_iface_h iface, uct_iface_addr_t *addr)
{
    return iface->ops.iface_get_address(iface, addr);
}

int uct_iface_is_reachable(const uct_iface_h iface, const uct_device_addr_t *dev_addr,
                           const uct_iface_addr_t *iface_addr)
{
    return iface->ops.iface_is_reachable(iface, dev_addr, iface_addr);
}

ucs_status_t uct_ep_check(const uct_ep_h ep, unsigned flags,
                          uct_completion_t *comp)
{
    return ep->iface->ops.ep_check(ep, flags, comp);
}

ucs_status_t uct_iface_event_fd_get(uct_iface_h iface, int *fd_p)
{
    return iface->ops.iface_event_fd_get(iface, fd_p);
}

ucs_status_t uct_iface_event_arm(uct_iface_h iface, unsigned events)
{
    return iface->ops.iface_event_arm(iface, events);
}

void uct_iface_close(uct_iface_h iface)
{
    ucs_vfs_obj_remove(iface);
    iface->ops.iface_close(iface);
}

void uct_base_iface_progress_enable(uct_iface_h tl_iface, unsigned flags)
{
    uct_base_iface_t *iface = ucs_derived_of(tl_iface, uct_base_iface_t);
    uct_base_iface_progress_enable_cb(iface,
                                      (ucs_callback_t)iface->super.ops.iface_progress,
                                      flags);
}

void uct_base_iface_progress_enable_cb(uct_base_iface_t *iface,
                                       ucs_callback_t cb, unsigned flags)
{
    uct_priv_worker_t *worker = iface->worker;
    unsigned thread_safe;

    UCS_ASYNC_BLOCK(worker->async);

    thread_safe = flags & UCT_PROGRESS_THREAD_SAFE;
    flags      &= ~UCT_PROGRESS_THREAD_SAFE;

    /* Add callback only if previous flags are 0 and new flags != 0 */
    if ((!iface->progress_flags && flags) &&
        (iface->prog.id == UCS_CALLBACKQ_ID_NULL)) {
        if (thread_safe) {
            iface->prog.id = ucs_callbackq_add_safe(&worker->super.progress_q,
                                                    cb, iface,
                                                    UCS_CALLBACKQ_FLAG_FAST);
        } else {
            iface->prog.id = ucs_callbackq_add(&worker->super.progress_q, cb,
                                               iface, UCS_CALLBACKQ_FLAG_FAST);
        }
    }
    iface->progress_flags |= flags;

    UCS_ASYNC_UNBLOCK(worker->async);
}

void uct_base_iface_progress_disable(uct_iface_h tl_iface, unsigned flags)
{
    uct_base_iface_t *iface = ucs_derived_of(tl_iface, uct_base_iface_t);
    uct_priv_worker_t *worker = iface->worker;
    unsigned thread_safe;

    UCS_ASYNC_BLOCK(worker->async);

    thread_safe = flags & UCT_PROGRESS_THREAD_SAFE;
    flags      &= ~UCT_PROGRESS_THREAD_SAFE;

    /* Remove callback only if previous flags != 0, and removing the given
     * flags makes it become 0.
     */
    if ((iface->progress_flags && !(iface->progress_flags & ~flags)) &&
        (iface->prog.id != UCS_CALLBACKQ_ID_NULL)) {
        if (thread_safe) {
            ucs_callbackq_remove_safe(&worker->super.progress_q, iface->prog.id);
        } else {
            ucs_callbackq_remove(&worker->super.progress_q, iface->prog.id);
        }
        iface->prog.id = UCS_CALLBACKQ_ID_NULL;
    }
    iface->progress_flags &= ~flags;

    UCS_ASYNC_UNBLOCK(worker->async);
}

ucs_status_t uct_base_iface_flush(uct_iface_h tl_iface, unsigned flags,
                                  uct_completion_t *comp)
{
    UCT_TL_IFACE_STAT_FLUSH(ucs_derived_of(tl_iface, uct_base_iface_t));
    return UCS_OK;
}

ucs_status_t uct_base_iface_fence(uct_iface_h tl_iface, unsigned flags)
{
    UCT_TL_IFACE_STAT_FENCE(ucs_derived_of(tl_iface, uct_base_iface_t));
    return UCS_OK;
}

ucs_status_t uct_base_ep_flush(uct_ep_h tl_ep, unsigned flags,
                               uct_completion_t *comp)
{
    UCT_TL_EP_STAT_FLUSH(ucs_derived_of(tl_ep, uct_base_ep_t));
    return UCS_OK;
}

ucs_status_t uct_base_ep_fence(uct_ep_h tl_ep, unsigned flags)
{
    UCT_TL_EP_STAT_FENCE(ucs_derived_of(tl_ep, uct_base_ep_t));
    return UCS_OK;
}

ucs_status_t uct_iface_handle_ep_err(uct_iface_h iface, uct_ep_h ep,
                                     ucs_status_t status)
{
    uct_base_iface_t *base_iface = ucs_derived_of(iface, uct_base_iface_t);

    if (base_iface->err_handler) {
        return base_iface->err_handler(base_iface->err_handler_arg, ep, status);
    }

    ucs_assert(status != UCS_ERR_CANCELED);
    ucs_debug("error %s was not handled for ep %p", ucs_status_string(status), ep);
    return status;
}

void uct_base_iface_query(uct_base_iface_t *iface, uct_iface_attr_t *iface_attr)
{
    memset(iface_attr, 0, sizeof(*iface_attr));

    iface_attr->max_num_eps   = iface->config.max_num_eps;
    iface_attr->dev_num_paths = 1;
}

ucs_status_t
uct_iface_param_am_alignment(const uct_iface_params_t *params, size_t elem_size,
                             size_t base_offset, size_t payload_offset,
                             size_t *align, size_t *align_offset)
{
    if (!(params->field_mask & UCT_IFACE_PARAM_FIELD_AM_ALIGNMENT)) {
        if (params->field_mask & UCT_IFACE_PARAM_FIELD_AM_ALIGN_OFFSET) {
            ucs_error("alignment offset has no effect without alignment");
            return UCS_ERR_INVALID_PARAM;
        }

        *align        = UCS_SYS_CACHE_LINE_SIZE;
        *align_offset = base_offset;

        return UCS_OK;
    }

    *align        = params->am_alignment;
    *align_offset = UCT_IFACE_PARAM_VALUE(params, am_align_offset,
                                          AM_ALIGN_OFFSET, 0ul);

    if (*align_offset >= elem_size) {
        ucs_diag("invalid AM alignment offset %zu, must be less than %zu",
                 *align_offset, elem_size);

        *align_offset = 0ul;
    }

    *align_offset += payload_offset;

    return UCS_OK;
}

ucs_status_t uct_single_device_resource(uct_md_h md, const char *dev_name,
                                        uct_device_type_t dev_type,
                                        ucs_sys_device_t sys_device,
                                        uct_tl_device_resource_t **tl_devices_p,
                                        unsigned *num_tl_devices_p)
{
    uct_tl_device_resource_t *device;

    device = ucs_calloc(1, sizeof(*device), "device resource");
    if (NULL == device) {
        ucs_error("failed to allocate device resource");
        return UCS_ERR_NO_MEMORY;
    }

    ucs_snprintf_zero(device->name, sizeof(device->name), "%s", dev_name);
    device->type       = dev_type;
    device->sys_device = sys_device;

    *num_tl_devices_p = 1;
    *tl_devices_p     = device;
    return UCS_OK;
}

ucs_status_t
uct_base_iface_estimate_perf(uct_iface_h iface, uct_perf_attr_t *perf_attr)
{
    ucs_status_t status;
    uct_iface_attr_t iface_attr;

    status = uct_iface_query(iface, &iface_attr);
    if (status != UCS_OK) {
        return status;
    }

    /* By default, the performance is assumed to be the same for all operations */
    if (perf_attr->field_mask & UCT_PERF_ATTR_FIELD_BANDWIDTH) {
        perf_attr->bandwidth = iface_attr.bandwidth;
    }

    if (perf_attr->field_mask & UCT_PERF_ATTR_FIELD_OVERHEAD) {
        perf_attr->overhead_short = iface_attr.overhead_short;
        perf_attr->overhead_bcopy = iface_attr.overhead_bcopy;
    }

    return UCS_OK;
}

uct_iface_internal_ops_t uct_base_iface_internal_ops = {
    .iface_estimate_perf = uct_base_iface_estimate_perf,
    .iface_vfs_refresh   = (uct_iface_vfs_refresh_func_t)ucs_empty_function,
};

UCS_CLASS_INIT_FUNC(uct_iface_t, uct_iface_ops_t *ops)
{
    ucs_assert_always(ops->ep_flush                 != NULL);
    ucs_assert_always(ops->ep_fence                 != NULL);
    ucs_assert_always(ops->ep_destroy               != NULL);
    ucs_assert_always(ops->iface_flush              != NULL);
    ucs_assert_always(ops->iface_fence              != NULL);
    ucs_assert_always(ops->iface_progress_enable    != NULL);
    ucs_assert_always(ops->iface_progress_disable   != NULL);
    ucs_assert_always(ops->iface_progress           != NULL);
    ucs_assert_always(ops->iface_close              != NULL);
    ucs_assert_always(ops->iface_query              != NULL);
    ucs_assert_always(ops->iface_get_device_address != NULL);
    ucs_assert_always(ops->iface_is_reachable       != NULL);

    self->ops = *ops;
    return UCS_OK;
}

UCS_CLASS_CLEANUP_FUNC(uct_iface_t)
{
}

UCS_CLASS_DEFINE(uct_iface_t, void);

#if ENABLE_MT

#define UCT_THREAD_SAFE_EP_VOID_FUNC(func_name, ep, ...) \
    uct_base_ep_t *base_ep       = ucs_derived_of(ep, uct_base_ep_t); \
    uct_base_iface_t *base_iface = ucs_derived_of(ep->iface, uct_base_iface_t); \
    \
    ucs_recursive_spin_lock(&base_ep->lock); \
    \
    base_iface->locked_ops.ep_##func_name (ep, ##__VA_ARGS__); \
    \
    ucs_recursive_spin_unlock(&base_ep->lock);

#define UCT_THREAD_SAFE_EP_TYPE_FUNC(func_name, ret_type, ep, ...) \
    uct_base_ep_t *base_ep       = ucs_derived_of(ep, uct_base_ep_t); \
    uct_base_iface_t *base_iface = ucs_derived_of(ep->iface, uct_base_iface_t); \
    \
    ucs_recursive_spin_lock(&base_ep->lock); \
    \
    ret_type ret = base_iface->locked_ops.ep_##func_name (ep, ##__VA_ARGS__); \
    \
    ucs_recursive_spin_unlock(&base_ep->lock); \
    \
    return ret;

#define UCT_THREAD_SAFE_EP_FUNC(func_name, ep, ...) \
        UCT_THREAD_SAFE_EP_TYPE_FUNC(func_name, ucs_status_t, ep, ##__VA_ARGS__)

static ucs_status_t uct_thread_safe_put_short(uct_ep_h ep,
                                              const void *buffer,
                                              unsigned length,
                                              uint64_t remote_addr,
                                              uct_rkey_t rkey)
{
    UCT_THREAD_SAFE_EP_FUNC(put_short, ep, buffer, length, remote_addr, rkey)
}

static ssize_t      uct_thread_safe_ep_put_bcopy(uct_ep_h ep,
                                                 uct_pack_callback_t pack_cb,
                                                 void *arg,
                                                 uint64_t remote_addr,
                                                 uct_rkey_t rkey)
{
    UCT_THREAD_SAFE_EP_TYPE_FUNC(put_bcopy, ssize_t, ep, pack_cb, arg, remote_addr, rkey)
}

static ucs_status_t uct_thread_safe_ep_put_zcopy(uct_ep_h ep,
                                                 const uct_iov_t *iov,
                                                 size_t iovcnt,
                                                 uint64_t remote_addr,
                                                 uct_rkey_t rkey,
                                                 uct_completion_t *comp)
{
    UCT_THREAD_SAFE_EP_FUNC(put_zcopy, ep, iov, iovcnt, remote_addr, rkey, comp)
}

static ucs_status_t uct_thread_safe_ep_get_short(uct_ep_h ep,
                                                 void *buffer,
                                                 unsigned length,
                                                 uint64_t remote_addr,
                                                 uct_rkey_t rkey)
{
    UCT_THREAD_SAFE_EP_FUNC(get_short, ep, buffer, length, remote_addr, rkey)
}

static ucs_status_t uct_thread_safe_ep_get_bcopy(uct_ep_h ep,
                                                 uct_unpack_callback_t unpack_cb,
                                                 void *arg,
                                                 size_t length,
                                                 uint64_t remote_addr,
                                                 uct_rkey_t rkey,
                                                 uct_completion_t *comp)
{
    UCT_THREAD_SAFE_EP_FUNC(get_bcopy, ep, unpack_cb, arg, length, remote_addr, rkey, comp)
}

static ucs_status_t uct_thread_safe_ep_get_zcopy(uct_ep_h ep,
                                                 const uct_iov_t *iov,
                                                 size_t iovcnt,
                                                 uint64_t remote_addr,
                                                 uct_rkey_t rkey,
                                                 uct_completion_t *comp)
{
    UCT_THREAD_SAFE_EP_FUNC(get_zcopy, ep, iov, iovcnt, remote_addr, rkey, comp)
}

static ucs_status_t uct_thread_safe_ep_am_short(uct_ep_h ep,
                                                uint8_t id,
                                                uint64_t header,
                                                const void *payload,
                                                unsigned length)
{
    UCT_THREAD_SAFE_EP_FUNC(am_short, ep, id, header, payload, length)
}

static ucs_status_t uct_thread_safe_ep_am_short_iov(uct_ep_h ep, uint8_t id,
                                                    const uct_iov_t *iov,
                                                    size_t iovcnt)
{
    UCT_THREAD_SAFE_EP_FUNC(am_short_iov, ep, id, iov, iovcnt)
}

static ssize_t      uct_thread_safe_ep_am_bcopy(uct_ep_h ep,
                                                uint8_t id,
                                                uct_pack_callback_t pack_cb,
                                                void *arg,
                                                unsigned flags)
{
    UCT_THREAD_SAFE_EP_TYPE_FUNC(am_bcopy, ssize_t, ep, id, pack_cb, arg, flags)
}

static ucs_status_t uct_thread_safe_ep_am_zcopy(uct_ep_h ep,
                                                uint8_t id,
                                                const void *header,
                                                unsigned header_length,
                                                const uct_iov_t *iov,
                                                size_t iovcnt,
                                                unsigned flags,
                                                uct_completion_t *comp)
{
    UCT_THREAD_SAFE_EP_FUNC(am_zcopy, ep, id, header, header_length, iov, iovcnt, flags, comp)
}

static ucs_status_t uct_thread_safe_ep_atomic_cswap64(uct_ep_h ep,
                                                      uint64_t compare,
                                                      uint64_t swap,
                                                      uint64_t remote_addr,
                                                      uct_rkey_t rkey,
                                                      uint64_t *result,
                                                      uct_completion_t *comp)
{
    UCT_THREAD_SAFE_EP_FUNC(atomic_cswap64, ep, compare, swap, remote_addr, rkey, result, comp)
}

static ucs_status_t uct_thread_safe_ep_atomic_cswap32(uct_ep_h ep,
                                                      uint32_t compare,
                                                      uint32_t swap,
                                                      uint64_t remote_addr,
                                                      uct_rkey_t rkey,
                                                      uint32_t *result,
                                                      uct_completion_t *comp)
{
    UCT_THREAD_SAFE_EP_FUNC(atomic_cswap32, ep, compare, swap, remote_addr, rkey, result, comp)
}

static ucs_status_t uct_thread_safe_ep_atomic32_post(uct_ep_h ep,
                                                     unsigned opcode,
                                                     uint32_t value,
                                                     uint64_t remote_addr,
                                                     uct_rkey_t rkey)
{
    UCT_THREAD_SAFE_EP_FUNC(atomic32_post, ep, opcode, value, remote_addr, rkey)
}

static ucs_status_t uct_thread_safe_ep_atomic64_post(uct_ep_h ep,
                                                     unsigned opcode,
                                                     uint64_t value,
                                                     uint64_t remote_addr,
                                                     uct_rkey_t rkey)
{
    UCT_THREAD_SAFE_EP_FUNC(atomic64_post, ep, opcode, value, remote_addr, rkey)
}

static ucs_status_t uct_thread_safe_ep_atomic32_fetch(uct_ep_h ep,
                                                      unsigned opcode,
                                                      uint32_t value,
                                                      uint32_t *result,
                                                      uint64_t remote_addr,
                                                      uct_rkey_t rkey,
                                                      uct_completion_t *comp)
{
    UCT_THREAD_SAFE_EP_FUNC(atomic32_fetch, ep, opcode, value, result, remote_addr, rkey, comp)
}

static ucs_status_t uct_thread_safe_ep_atomic64_fetch(uct_ep_h ep,
                                                      unsigned opcode,
                                                      uint64_t value,
                                                      uint64_t *result,
                                                      uint64_t remote_addr,
                                                      uct_rkey_t rkey,
                                                      uct_completion_t *comp)
{
    UCT_THREAD_SAFE_EP_FUNC(atomic64_fetch, ep, opcode, value, result, remote_addr, rkey, comp)
}

static ucs_status_t uct_thread_safe_ep_tag_eager_short(uct_ep_h ep,
                                                       uct_tag_t tag,
                                                       const void *data,
                                                       size_t length)
{
    UCT_THREAD_SAFE_EP_FUNC(tag_eager_short, ep, tag, data, length)
}

static ssize_t      uct_thread_safe_ep_tag_eager_bcopy(uct_ep_h ep,
                                                       uct_tag_t tag,
                                                       uint64_t imm,
                                                       uct_pack_callback_t pack_cb,
                                                       void *arg,
                                                       unsigned flags)
{
    UCT_THREAD_SAFE_EP_TYPE_FUNC(tag_eager_bcopy, ssize_t, ep, tag, imm, pack_cb, arg, flags)
}

static ucs_status_t uct_thread_safe_ep_tag_eager_zcopy(uct_ep_h ep,
                                                       uct_tag_t tag,
                                                       uint64_t imm,
                                                       const uct_iov_t *iov,
                                                       size_t iovcnt,
                                                       unsigned flags,
                                                       uct_completion_t *comp)
{
    UCT_THREAD_SAFE_EP_FUNC(tag_eager_zcopy, ep, tag, imm, iov, iovcnt, flags, comp)
}

static ucs_status_ptr_t uct_thread_safe_ep_tag_rndv_zcopy(uct_ep_h ep,
                                                          uct_tag_t tag,
                                                          const void *header,
                                                          unsigned header_length,
                                                          const uct_iov_t *iov,
                                                          size_t iovcnt,
                                                          unsigned flags,
                                                          uct_completion_t *comp)
{
    UCT_THREAD_SAFE_EP_TYPE_FUNC(tag_rndv_zcopy, ucs_status_ptr_t, ep, tag, header, header_length, iov, iovcnt, flags, comp)
}

static ucs_status_t uct_thread_safe_ep_tag_rndv_cancel(uct_ep_h ep, void *op)
{
    UCT_THREAD_SAFE_EP_FUNC(tag_rndv_cancel, ep, op)
}

static ucs_status_t uct_thread_safe_ep_tag_rndv_request(uct_ep_h ep,
                                                        uct_tag_t tag,
                                                        const void* header,
                                                        unsigned header_length,
                                                        unsigned flags)
{
    UCT_THREAD_SAFE_EP_FUNC(tag_rndv_request, ep, tag, header, header_length, flags)
}

#define UCT_THREAD_SAFE_IFACE_VOID_FUNC(func_name, iface, ...) \
    uct_base_iface_t *base_iface = ucs_derived_of(iface, uct_base_iface_t); \
    \
    ucs_recursive_spin_lock(&base_iface->lock); \
    \
    base_iface->locked_ops.iface_##func_name (iface, ##__VA_ARGS__); \
    \
    ucs_recursive_spin_unlock(&base_iface->lock);

#define UCT_THREAD_SAFE_IFACE_TYPE_FUNC(func_name, ret_type, iface, ...) \
    uct_base_iface_t *base_iface = ucs_derived_of(iface, uct_base_iface_t); \
    \
    ucs_recursive_spin_lock(&base_iface->lock); \
    \
    ret_type ret = base_iface->locked_ops.iface_##func_name (iface, ##__VA_ARGS__); \
    \
    ucs_recursive_spin_unlock(&base_iface->lock); \
    \
    return ret;

#define UCT_THREAD_SAFE_IFACE_FUNC(func_name, iface, ...) \
        UCT_THREAD_SAFE_IFACE_TYPE_FUNC(func_name, ucs_status_t, iface, ##__VA_ARGS__)

static ucs_status_t uct_thread_safe_iface_tag_recv_zcopy(uct_iface_h iface,
                                                         uct_tag_t tag,
                                                         uct_tag_t tag_mask,
                                                         const uct_iov_t *iov,
                                                         size_t iovcnt,
                                                         uct_tag_context_t *ctx)
{
    UCT_THREAD_SAFE_IFACE_FUNC(tag_recv_zcopy, iface, tag, tag_mask, iov, iovcnt, ctx)
}

static ucs_status_t uct_thread_safe_iface_tag_recv_cancel(uct_iface_h iface,
                                                          uct_tag_context_t *ctx,
                                                          int force)
{
    UCT_THREAD_SAFE_IFACE_FUNC(tag_recv_cancel, iface, ctx, force)
}

static ucs_status_t uct_thread_safe_ep_pending_add(uct_ep_h ep,
                                                   uct_pending_req_t *n,
                                                   unsigned flags)
{
    UCT_THREAD_SAFE_EP_FUNC(pending_add, ep, n, flags)
}

static void         uct_thread_safe_ep_pending_purge(uct_ep_h ep,
                                                     uct_pending_purge_callback_t cb,
                                                     void *arg)
{
    UCT_THREAD_SAFE_EP_VOID_FUNC(pending_purge, ep, cb, arg)
}

static ucs_status_t uct_thread_safe_ep_flush(uct_ep_h ep,
                                             unsigned flags,
                                             uct_completion_t *comp)
{
    UCT_THREAD_SAFE_EP_FUNC(flush, ep, flags, comp)
}

static ucs_status_t uct_thread_safe_ep_fence(uct_ep_h ep, unsigned flags)
{
    UCT_THREAD_SAFE_EP_FUNC(fence, ep, flags)
}

static ucs_status_t uct_thread_safe_ep_check(uct_ep_h ep,
                                             unsigned flags,
                                             uct_completion_t *comp)
{
    UCT_THREAD_SAFE_EP_FUNC(check, ep, flags, comp)
}

static ucs_status_t uct_thread_safe_ep_create(const uct_ep_params_t *params,
                                              uct_ep_h *ep_p)
{
    uct_base_ep_t *base_ep;
    uct_base_iface_t *base_iface;

    ucs_assert_always(params->field_mask & UCT_EP_PARAM_FIELD_IFACE);

    base_iface = ucs_derived_of(params->iface, uct_base_iface_t);

    ucs_recursive_spin_lock(&base_iface->lock);

    ucs_status_t ret = base_iface->locked_ops.ep_create(params,
                                                        (uct_ep_h*)&base_ep);

    if (ret == UCS_OK) {
        ret = ucs_recursive_spinlock_init(&base_ep->lock, 0);
        if (ret != UCS_OK) {
            base_iface->locked_ops.ep_destroy(&base_ep->super);
        } else {
            *ep_p = &base_ep->super;
        }
    }

    ucs_recursive_spin_unlock(&base_iface->lock);

    return ret;
}

static ucs_status_t uct_thread_safe_ep_disconnect(uct_ep_h ep, unsigned flags)
{
    UCT_THREAD_SAFE_EP_FUNC(disconnect, ep, flags)
}

static ucs_status_t uct_thread_safe_cm_ep_conn_notify(uct_ep_h ep)
{
    uct_base_ep_t *base_ep       = ucs_derived_of(ep, uct_base_ep_t);
    uct_base_iface_t *base_iface = ucs_derived_of(ep->iface, uct_base_iface_t);

    ucs_recursive_spin_unlock(&base_ep->lock);

    ucs_status_t ret = base_iface->locked_ops.cm_ep_conn_notify(ep);

    ucs_recursive_spin_unlock(&base_ep->lock);

    return ret;
}

static void         uct_thread_safe_ep_destroy(uct_ep_h ep)
{
    uct_base_iface_t *base_iface = ucs_derived_of(ep->iface, uct_base_iface_t);

    base_iface->locked_ops.ep_destroy(ep);
}

static ucs_status_t uct_thread_safe_ep_get_address(uct_ep_h ep,
                                                   uct_ep_addr_t *addr)
{
    UCT_THREAD_SAFE_EP_FUNC(get_address, ep, addr)
}

static ucs_status_t uct_thread_safe_ep_connect_to_ep(uct_ep_h ep,
                                                     const uct_device_addr_t *dev_addr,
                                                     const uct_ep_addr_t *ep_addr)
{
    UCT_THREAD_SAFE_EP_FUNC(connect_to_ep, ep, dev_addr, ep_addr)
}

static ucs_status_t uct_thread_safe_iface_accept(uct_iface_h iface,
                                                 uct_conn_request_h conn_request)
{
    UCT_THREAD_SAFE_IFACE_FUNC(accept, iface, conn_request)
}

static ucs_status_t uct_thread_safe_iface_reject(uct_iface_h iface,
                                                 uct_conn_request_h conn_request)
{
    UCT_THREAD_SAFE_IFACE_FUNC(reject, iface, conn_request)
}

static ucs_status_t uct_thread_safe_iface_flush(uct_iface_h iface,
                                                unsigned flags,
                                                uct_completion_t *comp)
{
    UCT_THREAD_SAFE_IFACE_FUNC(flush, iface, flags, comp)
}

static ucs_status_t uct_thread_safe_iface_fence(uct_iface_h iface, unsigned flags)
{
    UCT_THREAD_SAFE_IFACE_FUNC(fence, iface, flags)
}

static void         uct_thread_safe_iface_progress_enable(uct_iface_h iface,
                                                          unsigned flags)
{
    UCT_THREAD_SAFE_IFACE_VOID_FUNC(progress_enable, iface, flags)
}

static void         uct_thread_safe_iface_progress_disable(uct_iface_h iface,
                                                           unsigned flags)
{
    UCT_THREAD_SAFE_IFACE_VOID_FUNC(progress_disable, iface, flags)
}

static unsigned     uct_thread_safe_iface_progress(uct_iface_h iface)
{
    uct_base_iface_t *base_iface = ucs_derived_of(iface, uct_base_iface_t);

    /* Special case: locking happens inside the progress function */
    return base_iface->locked_ops.iface_progress(iface);
}

static ucs_status_t uct_thread_safe_iface_event_fd_get(uct_iface_h iface,
                                                       int *fd_p)
{
    UCT_THREAD_SAFE_IFACE_FUNC(event_fd_get, iface, fd_p)
}

static ucs_status_t uct_thread_safe_iface_event_arm(uct_iface_h iface,
                                                    unsigned events)
{
    UCT_THREAD_SAFE_IFACE_FUNC(event_arm, iface, events)
}

static void         uct_thread_safe_iface_close(uct_iface_h iface)
{
    UCT_THREAD_SAFE_IFACE_VOID_FUNC(close, iface)
}

static ucs_status_t uct_thread_safe_iface_query(uct_iface_h iface,
                                                uct_iface_attr_t *iface_attr)
{
    UCT_THREAD_SAFE_IFACE_FUNC(query, iface, iface_attr)
}

static ucs_status_t uct_thread_safe_iface_get_device_address(uct_iface_h iface,
                                                             uct_device_addr_t *addr)
{
    UCT_THREAD_SAFE_IFACE_FUNC(get_device_address, iface, addr)
}

static ucs_status_t uct_thread_safe_iface_get_address(uct_iface_h iface,
                                                      uct_iface_addr_t *addr)
{
    UCT_THREAD_SAFE_IFACE_FUNC(get_address, iface, addr)
}

static int          uct_thread_safe_iface_is_reachable(const uct_iface_h iface,
                                                       const uct_device_addr_t *dev_addr,
                                                       const uct_iface_addr_t *iface_addr)
{
    UCT_THREAD_SAFE_IFACE_FUNC(is_reachable, iface, dev_addr, iface_addr)
}

static int          uct_thread_safe_iface_release_shared_desc(uct_iface_h iface,
                                                              uct_recv_desc_t *self,
                                                              void *desc)
{
    UCT_THREAD_SAFE_IFACE_FUNC(release_shared_desc, iface, self, desc)
}

static uct_iface_ops_t thread_safe_ops = {
    .ep_put_short              = uct_thread_safe_put_short,
    .ep_put_bcopy              = uct_thread_safe_ep_put_bcopy,
    .ep_put_zcopy              = uct_thread_safe_ep_put_zcopy,
    .ep_get_short              = uct_thread_safe_ep_get_short,
    .ep_get_bcopy              = uct_thread_safe_ep_get_bcopy,
    .ep_get_zcopy              = uct_thread_safe_ep_get_zcopy,
    .ep_am_short               = uct_thread_safe_ep_am_short,
    .ep_am_short_iov           = uct_thread_safe_ep_am_short_iov,
    .ep_am_bcopy               = uct_thread_safe_ep_am_bcopy,
    .ep_am_zcopy               = uct_thread_safe_ep_am_zcopy,
    .ep_atomic_cswap64         = uct_thread_safe_ep_atomic_cswap64,
    .ep_atomic_cswap32         = uct_thread_safe_ep_atomic_cswap32,
    .ep_atomic32_post          = uct_thread_safe_ep_atomic32_post,
    .ep_atomic64_post          = uct_thread_safe_ep_atomic64_post,
    .ep_atomic32_fetch         = uct_thread_safe_ep_atomic32_fetch,
    .ep_atomic64_fetch         = uct_thread_safe_ep_atomic64_fetch,
    .ep_tag_eager_short        = uct_thread_safe_ep_tag_eager_short,
    .ep_tag_eager_bcopy        = uct_thread_safe_ep_tag_eager_bcopy,
    .ep_tag_eager_zcopy        = uct_thread_safe_ep_tag_eager_zcopy,
    .ep_tag_rndv_zcopy         = uct_thread_safe_ep_tag_rndv_zcopy,
    .ep_tag_rndv_cancel        = uct_thread_safe_ep_tag_rndv_cancel,
    .ep_tag_rndv_request       = uct_thread_safe_ep_tag_rndv_request,
    .iface_tag_recv_zcopy      = uct_thread_safe_iface_tag_recv_zcopy,
    .iface_tag_recv_cancel     = uct_thread_safe_iface_tag_recv_cancel,
    .ep_pending_add            = uct_thread_safe_ep_pending_add,
    .ep_pending_purge          = uct_thread_safe_ep_pending_purge,
    .ep_flush                  = uct_thread_safe_ep_flush,
    .ep_fence                  = uct_thread_safe_ep_fence,
    .ep_check                  = uct_thread_safe_ep_check,
    .ep_create                 = uct_thread_safe_ep_create,
    .ep_disconnect             = uct_thread_safe_ep_disconnect,
    .cm_ep_conn_notify         = uct_thread_safe_cm_ep_conn_notify,
    .ep_destroy                = uct_thread_safe_ep_destroy,
    .ep_get_address            = uct_thread_safe_ep_get_address,
    .ep_connect_to_ep          = uct_thread_safe_ep_connect_to_ep,
    .iface_accept              = uct_thread_safe_iface_accept,
    .iface_reject              = uct_thread_safe_iface_reject,
    .iface_flush               = uct_thread_safe_iface_flush,
    .iface_fence               = uct_thread_safe_iface_fence,
    .iface_progress_enable     = uct_thread_safe_iface_progress_enable,
    .iface_progress_disable    = uct_thread_safe_iface_progress_disable,
    .iface_progress            = uct_thread_safe_iface_progress,
    .iface_event_fd_get        = uct_thread_safe_iface_event_fd_get,
    .iface_event_arm           = uct_thread_safe_iface_event_arm,
    .iface_close               = uct_thread_safe_iface_close,
    .iface_query               = uct_thread_safe_iface_query,
    .iface_get_device_address  = uct_thread_safe_iface_get_device_address,
    .iface_get_address         = uct_thread_safe_iface_get_address,
    .iface_is_reachable        = uct_thread_safe_iface_is_reachable,
    .iface_release_shared_desc = uct_thread_safe_iface_release_shared_desc
};

#endif /* ENABLE_MT */

UCS_CLASS_INIT_FUNC(uct_base_iface_t, uct_iface_ops_t *ops,
                    uct_iface_internal_ops_t *internal_ops, uct_md_h md,
                    uct_worker_h worker, const uct_iface_params_t *params,
                    const uct_iface_config_t *config
                    UCS_STATS_ARG(ucs_stats_node_t *stats_parent)
                    UCS_STATS_ARG(const char *iface_name))
{
    uint64_t alloc_methods_bitmap;
    uct_alloc_method_t method;

    unsigned i;
    uint8_t id;

    if ((params->field_mask & UCT_IFACE_PARAM_FIELD_OPEN_MODE) &&
        (params->open_mode  & UCT_IFACE_OPEN_MODE_THREAD_SAFE)) {
#if ENABLE_MT
        UCS_CLASS_CALL_SUPER_INIT(uct_iface_t, &thread_safe_ops);

        self->locked_ops = *ops;
        self->is_locking = 1;

        ucs_status_t status = ucs_recursive_spinlock_init(&self->lock, 0);
        if (status != UCS_OK) {
            return status;
        }
#else
        ucs_error("UCT Thread safety requested, but is disabled in this build");
#endif /* ENABLE_MT */
    } else {
        UCS_CLASS_CALL_SUPER_INIT(uct_iface_t, ops);

#if ENABLE_MT
        self->is_locking = 0;
#endif
    }

    UCT_CB_FLAGS_CHECK((params->field_mask &
                        UCT_IFACE_PARAM_FIELD_ERR_HANDLER_FLAGS) ?
                       params->err_handler_flags : 0);

    self->md                = md;
    self->internal_ops      = internal_ops;
    self->worker            = ucs_derived_of(worker, uct_priv_worker_t);
    self->am_tracer         = NULL;
    self->am_tracer_arg     = NULL;
    self->err_handler       = UCT_IFACE_PARAM_VALUE(params, err_handler, ERR_HANDLER,
                                                    NULL);
    self->err_handler_flags = UCT_IFACE_PARAM_VALUE(params, err_handler_flags,
                                                    ERR_HANDLER_FLAGS, 0);
    self->err_handler_arg   = UCT_IFACE_PARAM_VALUE(params, err_handler_arg,
                                                    ERR_HANDLER_ARG, NULL);
    self->progress_flags    = 0;
    uct_worker_progress_init(&self->prog);

    for (id = 0; id < UCT_AM_ID_MAX; ++id) {
        uct_iface_set_stub_am_handler(self, id);
    }

    /* Copy allocation methods configuration. In the process, remove duplicates. */
    UCS_STATIC_ASSERT(sizeof(alloc_methods_bitmap) * 8 >= UCT_ALLOC_METHOD_LAST);
    self->config.num_alloc_methods = 0;
    alloc_methods_bitmap           = 0;
    for (i = 0; i < config->alloc_methods.count; ++i) {
        method = config->alloc_methods.methods[i];
        if (alloc_methods_bitmap & UCS_BIT(method)) {
            continue;
        }

        ucs_assert(self->config.num_alloc_methods < UCT_ALLOC_METHOD_LAST);
        self->config.alloc_methods[self->config.num_alloc_methods++] = method;
        alloc_methods_bitmap |= UCS_BIT(method);
    }

    self->config.failure_level = (ucs_log_level_t)config->failure;
    self->config.max_num_eps   = config->max_num_eps;

    return UCS_STATS_NODE_ALLOC(&self->stats, &uct_iface_stats_class,
                                stats_parent, "-%s-%p", iface_name, self);
}

static UCS_CLASS_CLEANUP_FUNC(uct_base_iface_t)
{
    UCS_STATS_NODE_FREE(self->stats);
}

UCS_CLASS_DEFINE(uct_base_iface_t, uct_iface_t);


ucs_status_t uct_iface_accept(uct_iface_h iface,
                              uct_conn_request_h conn_request)
{
    return iface->ops.iface_accept(iface, conn_request);
}


ucs_status_t uct_iface_reject(uct_iface_h iface,
                              uct_conn_request_h conn_request)
{
    return iface->ops.iface_reject(iface, conn_request);
}


ucs_status_t uct_ep_create(const uct_ep_params_t *params, uct_ep_h *ep_p)
{
    if (params->field_mask & UCT_EP_PARAM_FIELD_IFACE) {
        return params->iface->ops.ep_create(params, ep_p);
    } else if (params->field_mask & UCT_EP_PARAM_FIELD_CM) {
        return params->cm->ops->ep_create(params, ep_p);
    }

    return UCS_ERR_INVALID_PARAM;
}

ucs_status_t uct_ep_connect(uct_ep_h ep, const uct_ep_connect_params_t *params)
{
    return ep->iface->ops.ep_connect(ep, params);
}

ucs_status_t uct_ep_disconnect(uct_ep_h ep, unsigned flags)
{
    return ep->iface->ops.ep_disconnect(ep, flags);
}

void uct_ep_destroy(uct_ep_h ep)
{
    ep->iface->ops.ep_destroy(ep);
}

ucs_status_t uct_ep_get_address(uct_ep_h ep, uct_ep_addr_t *addr)
{
    return ep->iface->ops.ep_get_address(ep, addr);
}

ucs_status_t uct_ep_connect_to_ep(uct_ep_h ep, const uct_device_addr_t *dev_addr,
                                  const uct_ep_addr_t *ep_addr)
{
    return ep->iface->ops.ep_connect_to_ep(ep, dev_addr, ep_addr);
}

ucs_status_t uct_cm_client_ep_conn_notify(uct_ep_h ep)
{
    return ep->iface->ops.cm_ep_conn_notify(ep);
}

void uct_ep_set_iface(uct_ep_h ep, uct_iface_t *iface)
{
    ep->iface = iface;
}

UCS_CLASS_INIT_FUNC(uct_ep_t, uct_iface_t *iface)
{
    uct_ep_set_iface(self, iface);
    return UCS_OK;
}

UCS_CLASS_CLEANUP_FUNC(uct_ep_t)
{
}

UCS_CLASS_DEFINE(uct_ep_t, void);

UCS_CLASS_INIT_FUNC(uct_base_ep_t, uct_base_iface_t *iface)
{
    UCS_CLASS_CALL_SUPER_INIT(uct_ep_t, &iface->super);

    return UCS_STATS_NODE_ALLOC(&self->stats, &uct_ep_stats_class, iface->stats,
                                "-%p", self);
}

static UCS_CLASS_CLEANUP_FUNC(uct_base_ep_t)
{
    UCS_STATS_NODE_FREE(self->stats);
}

UCS_CLASS_DEFINE(uct_base_ep_t, uct_ep_t);


UCS_CONFIG_DEFINE_ARRAY(alloc_methods, sizeof(uct_alloc_method_t),
                        UCS_CONFIG_TYPE_ENUM(uct_alloc_method_names));

ucs_config_field_t uct_iface_config_table[] = {
  {"MAX_SHORT", "",
   "The configuration parameter replaced by: "
   "UCX_<IB transport>_TX_MIN_INLINE for IB, UCX_MM_FIFO_SIZE for MM",
   UCS_CONFIG_DEPRECATED_FIELD_OFFSET, UCS_CONFIG_TYPE_DEPRECATED},

  {"MAX_BCOPY", "",
   "The configuration parameter replaced by: "
   "UCX_<transport>_SEG_SIZE where <transport> is one of: IB, MM, SELF, TCP",
   UCS_CONFIG_DEPRECATED_FIELD_OFFSET, UCS_CONFIG_TYPE_DEPRECATED},

  {"ALLOC", "huge,thp,md,mmap,heap",
   "Priority of methods to allocate intermediate buffers for communication",
   ucs_offsetof(uct_iface_config_t, alloc_methods), UCS_CONFIG_TYPE_ARRAY(alloc_methods)},

  {"FAILURE", "diag",
   "Level of network failure reporting",
   ucs_offsetof(uct_iface_config_t, failure), UCS_CONFIG_TYPE_ENUM(ucs_log_level_names)},

  {"MAX_NUM_EPS", "inf",
   "Maximum number of endpoints that the transport interface is able to create",
   ucs_offsetof(uct_iface_config_t, max_num_eps), UCS_CONFIG_TYPE_ULUNITS},

  {NULL}
};

ucs_status_t uct_base_ep_stats_reset(uct_base_ep_t *ep, uct_base_iface_t *iface)
{
    ucs_status_t status;

    UCS_STATS_NODE_FREE(ep->stats);

    status = UCS_STATS_NODE_ALLOC(&ep->stats, &uct_ep_stats_class, iface->stats,
                                  "-%p", ep);
#ifdef ENABLE_STATS
    if (status != UCS_OK) {
        /* set the stats to NULL so that the UCS_STATS_NODE_FREE call on the
         * base_ep's cleanup flow won't fail */
        ep->stats = NULL;
    }
#endif

    return status;
}

ucs_status_t uct_base_ep_am_short_iov(uct_ep_h ep, uint8_t id, const uct_iov_t *iov,
                                      size_t iovcnt)
{
    uint64_t header = 0;
    size_t length;
    void *buffer;
    ucs_iov_iter_t iov_iter;
    ucs_status_t status;

    length = uct_iov_total_length(iov, iovcnt);

    /* Copy first sizeof(header) bytes of iov to header. If the total length of
     * iov is less than sizeof(header), the remainder of the header is filled
     * with zeros. */
    ucs_iov_iter_init(&iov_iter);
    uct_iov_to_buffer(iov, iovcnt, &iov_iter, &header, sizeof(header));

    /* If the total size of iov is greater than sizeof(header), then allocate
       buffer and copy the remainder of iov to the buffer. */
    if (length > sizeof(header)) {
        length -= sizeof(header);

        if (length > UCS_ALLOCA_MAX_SIZE) {
            buffer = ucs_malloc(length, "uct_base_ep_am_short_iov buffer");
        } else {
            buffer = ucs_alloca(length);
        }

        uct_iov_to_buffer(iov, iovcnt, &iov_iter, buffer, SIZE_MAX);
    } else {
        buffer = NULL;
        length = 0;
    }

    status = uct_ep_am_short(ep, id, header, buffer, length);

    if (length > UCS_ALLOCA_MAX_SIZE) {
        ucs_free(buffer);
    }

    return status;
}

int uct_ep_get_process_proc_dir(char *buffer, size_t max_len, pid_t pid)
{
    ucs_assert((buffer != NULL) || (max_len == 0));
    /* cppcheck-suppress nullPointer */
    /* cppcheck-suppress ctunullpointer */
    return snprintf(buffer, max_len, "/proc/%d", (int)pid);
}

ucs_status_t uct_ep_keepalive_create(pid_t pid, uct_keepalive_info_t **ka_p)
{
    uct_keepalive_info_t *ka;
    ucs_time_t start_time;
    ucs_status_t status;
    int proc_len;

    proc_len = uct_ep_get_process_proc_dir(NULL, 0, pid);
    if (proc_len <= 0) {
        ucs_error("failed to get length to hold path to a process directory");
        status = UCS_ERR_NO_MEMORY;
        goto err;
    }

    ka = ucs_malloc(sizeof(*ka) + proc_len + 1, "keepalive");
    if (ka == NULL) {
        ucs_error("failed to allocate keepalive info");
        status = UCS_ERR_NO_MEMORY;
        goto err;
    }

    uct_ep_get_process_proc_dir(ka->proc, proc_len + 1, pid);

    status = ucs_sys_get_file_time(ka->proc, UCS_SYS_FILE_TIME_CTIME,
                                   &start_time);
    if (status != UCS_OK) {
        ucs_error("failed to get process start time");
        goto err_free_ka;
    }

    ka->start_time = start_time;
    *ka_p          = ka;

    return UCS_OK;

err_free_ka:
    ucs_free(ka);
err:
    return status;
}

ucs_status_t
uct_ep_keepalive_check(uct_ep_h tl_ep, uct_keepalive_info_t **ka, pid_t pid,
                       unsigned flags, uct_completion_t *comp)
{
    ucs_status_t status;
    ucs_time_t create_time;

    UCT_EP_KEEPALIVE_CHECK_PARAM(flags, comp);

    if (ucs_unlikely(*ka == NULL)) {
        status = uct_ep_keepalive_create(pid, ka);
        if (status != UCS_OK) {
            return uct_iface_handle_ep_err(tl_ep->iface, tl_ep, status);
        }
    } else {
        status = ucs_sys_get_file_time((*ka)->proc, UCS_SYS_FILE_TIME_CTIME,
                                       &create_time);
        if (ucs_unlikely((status != UCS_OK) ||
                         ((*ka)->start_time != create_time))) {
            return uct_iface_handle_ep_err(tl_ep->iface, tl_ep,
                                           UCS_ERR_ENDPOINT_TIMEOUT);
        }
    }

    return UCS_OK;
}

void uct_iface_get_local_address(uct_iface_local_addr_ns_t *addr_ns,
                                 ucs_sys_namespace_type_t sys_ns_type)
{
    addr_ns->super.id = ucs_iface_get_system_id() &
                        ~UCT_IFACE_LOCAL_ADDR_FLAG_NS;

    if (!ucs_sys_ns_is_default(sys_ns_type)) {
        addr_ns->super.id |= UCT_IFACE_LOCAL_ADDR_FLAG_NS;
        addr_ns->sys_ns    = ucs_sys_get_ns(sys_ns_type);
    }
}

int uct_iface_local_is_reachable(uct_iface_local_addr_ns_t *addr_ns,
                                 ucs_sys_namespace_type_t sys_ns_type)
{
    uct_iface_local_addr_ns_t my_addr = {};

    uct_iface_get_local_address(&my_addr, sys_ns_type);

    /* Do not merge these evaluations into single 'if' due to Clang compilation
     * warning */
    /* Check if both processes are on same host and both of them are in root (or
     * non-root) pid namespace */
    if (addr_ns->super.id != my_addr.super.id) {
        return 0;
    }

    if (!(addr_ns->super.id & UCT_IFACE_LOCAL_ADDR_FLAG_NS)) {
        return 1; /* Both processes are in root namespace */
    }

    /* We are in non-root PID namespace - return 1 if ID of namespaces are the
     * same */
    return addr_ns->sys_ns == my_addr.sys_ns;
}
