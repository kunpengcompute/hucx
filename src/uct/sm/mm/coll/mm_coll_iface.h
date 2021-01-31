/**
 * Copyright (c) UT-Battelle, LLC. 2014-2015. ALL RIGHTS RESERVED.
 * Copyright (C) Mellanox Technologies Ltd. 2001-2015.  ALL RIGHTS RESERVED.
 * Copyright (C) ARM Ltd. 2016.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#ifndef UCT_MM_COLL_IFACE_H
#define UCT_MM_COLL_IFACE_H

#include <ucs/sys/compiler.h>
#include <ucs/datastruct/ptr_array.h>
#include <uct/sm/mm/base/mm_iface.h>

typedef struct uct_mm_coll_iface_addr {
    uint32_t            coll_id;
    uct_mm_iface_addr_t super;
} UCS_S_PACKED uct_mm_coll_iface_addr_t;

typedef struct uct_mm_coll_fifo_element {
    uct_mm_fifo_element_t super;
    ucs_spinlock_t        lock;
    volatile uint32_t     pending;

    UCS_CACHELINE_PADDING(uct_mm_fifo_element_t,
                          ucs_spinlock_t,
                          uint32_t,
                          uct_coll_dtype_mode_t,
                          uint64_t);

    uct_coll_dtype_mode_t op_mode;
    uint64_t              header;
} UCS_S_PACKED UCS_V_ALIGNED(UCS_SYS_CACHE_LINE_SIZE) uct_mm_coll_fifo_element_t;

#define UCT_MM_COLL_NEXT_FIFO_ELEM(_obj, _elem, _index, _init, _size) \
    (ucs_unlikely(((_index)++ & (_obj)->fifo_mask) == (_obj)->fifo_mask)) ? \
            (_init) : UCS_PTR_BYTE_OFFSET((_elem), (_size));

#define UCT_MM_COLL_IFACE_NEXT_RECV_FIFO_ELEM(_iface, _elem, _elem_index) \
    _elem = UCT_MM_COLL_NEXT_FIFO_ELEM(_iface, _elem, _elem_index, \
                                       (_iface)->recv_fifo_elems, \
                                       ((uct_mm_base_iface_t*)_iface)->config.fifo_elem_size)

#define UCT_MM_COLL_IFACE_GET_FIFO_ELEM(_iface, _index) \
   ucs_container_of(UCT_MM_IFACE_GET_FIFO_ELEM(&(_iface)->super.super, \
                    (_iface)->super.super.recv_fifo_elems, _index), \
                    uct_mm_coll_fifo_element_t, super)

#define UCT_MM_COLL_BCAST_EP_DUMMY(_name, _bcast_iface) \
    uct_mm_coll_ep_t _name = { \
        .proc_cnt  = (_bcast_iface)->super.sm_proc_cnt - 1, \
        .elem_size = (_bcast_iface)->super.super.config.fifo_elem_size, \
        .seg_size  = (_bcast_iface)->super.super.config.seg_size \
    }

typedef struct uct_mm_coll_ep uct_mm_coll_ep_t;
typedef struct uct_mm_bcast_ep uct_mm_bcast_ep_t;

typedef struct uct_mm_coll_iface {
    uct_mm_base_iface_t super;
    uint8_t             my_coll_id;  /**< my (unique) index in the group */
    uint8_t             sm_proc_cnt; /**< number of processes in the group */
    ucs_ptr_array_t     ep_ptrs;     /**< endpoints to other connections */
    uct_mm_coll_ep_t   *loopback_ep; /**< endpoint connected to this iface */
} uct_mm_coll_iface_t;

typedef struct uct_mm_bcast_iface_config {
    uct_mm_iface_config_t super;
} uct_mm_bcast_iface_config_t;

typedef struct uct_mm_incast_iface_config {
    uct_mm_iface_config_t super;
    unsigned                 batched_thresh; /* batched-centralized threshold */
} uct_mm_incast_iface_config_t;

typedef struct uct_mm_bcast_iface {
    uct_mm_coll_iface_t super;
} uct_mm_bcast_iface_t;

typedef struct uct_mm_incast_iface {
    uct_mm_coll_iface_t super;
} uct_mm_incast_iface_t;

UCS_CLASS_DECLARE(uct_mm_coll_iface_t, uct_iface_ops_t*, uct_md_h, uct_worker_h,
                  const uct_iface_params_t*, const uct_iface_config_t*);

UCS_CLASS_DECLARE(uct_mm_bcast_iface_t, uct_md_h, uct_worker_h,
                  const uct_iface_params_t*, const uct_iface_config_t*);

UCS_CLASS_DECLARE(uct_mm_incast_iface_t, uct_md_h, uct_worker_h,
                  const uct_iface_params_t*, const uct_iface_config_t*);

UCS_CLASS_DECLARE_NEW_FUNC(uct_mm_bcast_iface_t, uct_iface_t, uct_md_h,
                           uct_worker_h, const uct_iface_params_t*,
                           const uct_iface_config_t*);

UCS_CLASS_DECLARE_NEW_FUNC(uct_mm_incast_iface_t, uct_iface_t, uct_md_h,
                           uct_worker_h, const uct_iface_params_t*,
                           const uct_iface_config_t*);

ucs_status_t uct_mm_coll_iface_query(uct_iface_h tl_iface,
                                     uct_iface_attr_t *iface_attr);

ucs_status_t uct_mm_coll_iface_get_address(uct_iface_t *tl_iface,
                                           uct_iface_addr_t *addr);

int uct_mm_coll_iface_is_reachable(const uct_iface_h tl_iface,
                                   const uct_device_addr_t *dev_addr,
                                   const uct_iface_addr_t *tl_iface_addr);

int uct_mm_ep_process_recv_loopback(uct_mm_coll_iface_t *iface,
                                    uct_mm_coll_fifo_element_t *elem);

void uct_mm_coll_ep_centralized_reset_bcast_elem(uct_mm_coll_fifo_element_t* elem,
                                                 uct_mm_coll_ep_t *ep,
                                                 int is_short);

unsigned uct_mm_bcast_iface_progress(uct_iface_h tl_iface);

#endif
