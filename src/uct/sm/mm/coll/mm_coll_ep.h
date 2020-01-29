/**
* Copyright (C) UT-Battelle, LLC. 2015. ALL RIGHTS RESERVED.
* Copyright (C) Mellanox Technologies Ltd. 2001-2019.  ALL RIGHTS RESERVED.
* Copyright (C) ARM Ltd. 2016.  ALL RIGHTS RESERVED.
* See file LICENSE for terms.
*/

#ifndef UCT_MM_COLL_EP_H
#define UCT_MM_COLL_EP_H

#include "mm_coll_iface.h"

#include <uct/sm/mm/base/mm_ep.h>

struct uct_mm_coll_ep {
    uct_mm_ep_t                 super;
    uct_mm_coll_fifo_element_t *tx_elem;    /* next TX element pointer */
    uint64_t                    tx_index;   /* next TX element index */
    uint8_t                     tx_cnt;     /* shortcut to iface->sm_proc_cnt */
    uint8_t                     coll_id;    /* ID of the connected remote peer */
    uint8_t                     my_offset;  /* where to write in "batch mode" */
    uint8_t                     fifo_shift; /* shortcut to iface->fifo_shift */
    uint16_t                    elem_size;  /* fifo_elem_size used for RX */
    unsigned                    fifo_mask;  /* shortcut to iface->fifo_mask */
    unsigned                    seg_size;   /* bcopy segment size */
    unsigned                    ref_count;  /* counts the uses of this slot */
};

struct uct_mm_bcast_ep {
    uct_mm_coll_ep_t super;
    uct_mm_fifo_check_t recv_check;
};

typedef struct uct_mm_incast_ep {
    uct_mm_coll_ep_t super;
} uct_mm_incast_ep_t;

UCS_CLASS_DECLARE_NEW_FUNC(uct_mm_incast_ep_t, uct_mm_coll_ep_t, const uct_ep_params_t*);
UCS_CLASS_DECLARE_NEW_FUNC(uct_mm_bcast_ep_t,  uct_mm_coll_ep_t, const uct_ep_params_t*);

UCS_CLASS_DECLARE_DELETE_FUNC(uct_mm_incast_ep_t, uct_ep_t);
UCS_CLASS_DECLARE_DELETE_FUNC(uct_mm_bcast_ep_t,  uct_ep_t);


ucs_status_t uct_mm_bcast_ep_am_short(uct_ep_h ep, uint8_t id, uint64_t header,
                                      const void *payload, unsigned length);
ucs_status_t uct_mm_incast_ep_am_short(uct_ep_h ep, uint8_t id, uint64_t header,
                                       const void *payload, unsigned length);

ssize_t uct_mm_bcast_ep_am_bcopy(uct_ep_h ep, uint8_t id,
                                 uct_pack_callback_t pack_cb,
                                 void *arg, unsigned flags);
ssize_t uct_mm_incast_ep_am_bcopy(uct_ep_h ep, uint8_t id,
                                  uct_pack_callback_t pack_cb,
                                  void *arg, unsigned flags);

ucs_status_t uct_mm_bcast_ep_am_zcopy(uct_ep_h ep, uint8_t id, const void *header,
                                      unsigned header_length, const uct_iov_t *iov,
                                      size_t iovcnt, unsigned flags,
                                      uct_completion_t *comp);
ucs_status_t uct_mm_incast_ep_am_zcopy(uct_ep_h ep, uint8_t id, const void *header,
                                       unsigned header_length, const uct_iov_t *iov,
                                       size_t iovcnt, unsigned flags,
                                       uct_completion_t *comp);

ucs_status_t uct_mm_bcast_ep_create(const uct_ep_params_t *params, uct_ep_h *ep_p);
ucs_status_t uct_mm_incast_ep_create(const uct_ep_params_t *params, uct_ep_h *ep_p);
void uct_mm_coll_ep_release_desc(uct_mm_coll_ep_t *ep, void *desc);
void uct_mm_coll_ep_destroy(uct_ep_h ep);

#endif
