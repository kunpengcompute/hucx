/**
 * Copyright (C) Mellanox Technologies Ltd. 2001-2020.  ALL RIGHTS RESERVED.
 *
 * See file LICENSE for terms.
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "pagg.h"

#include <ucs/sys/compiler_def.h>
#include <ucs/profile/profile.h>
#include <ucp/core/ucp_context.h>
#include <ucp/core/ucp_worker.h>

ucs_status_t UCS_F_ALWAYS_INLINE
ucp_pagg_get_worker_dest_id(ucp_worker_h worker, uint16_t *my_dest_id)
{
    *my_dest_id = (uint16_t)worker->uuid;
    /* Note: this assumes (but doesn't require) the user set the worker's UUID
     *       to a value that fits in 16 bits. When used within MPI - this UUID
     *       could be the rank number of the process creating this worker.
     */

    return UCS_OK;
}

UCS_PROFILE_FUNC(ucs_status_t, ucp_pagg_handler, (arg, data, length, tl_flags),
                 void *arg, void *data, size_t length, unsigned tl_flags)
{
    ucp_worker_h worker      = arg;
    ucp_pagg_hdr_t *pagg_hdr = data;
    uint16_t my_dest_id, hdr_dest_id;
    uint8_t next_size;

    ucs_status_t status;

    status = ucp_pagg_get_worker_dest_id(worker, &my_dest_id);
    if (ucs_unlikely(status != UCS_OK)) {
        return status;
    }

    while ((UCS_PTR_BYTE_DIFF(pagg_hdr, data) < length) &&
           (ucs_likely(status == UCS_OK))) {
        hdr_dest_id = pagg_hdr->dest_coll_id;
        if ((hdr_dest_id == UCT_PAGG_ANY_DEST) ||
            (hdr_dest_id == my_dest_id)) {
            /* Detect the size of the next part of the payload */
            next_size = pagg_hdr->next_size;

            /* handle this next part of the payload */
            status = ucp_am_handlers[pagg_hdr->am_handler_id].cb(arg,
                                                                 pagg_hdr + 1,
                                                                 next_size,
                                                                 tl_flags);
            // TODO: make sure it's ok to pass FLAG_DESC to two different AM CBs

            /* proceed to the next header in this packet */
            pagg_hdr = UCS_PTR_BYTE_OFFSET(pagg_hdr,
                                           next_size + sizeof(pagg_hdr));
        }
    }

    ucs_assert((UCS_PTR_BYTE_DIFF(pagg_hdr, data) == length) ||
               (status != UCS_OK));

    return status;
}

UCP_DEFINE_AM(UCP_FEATURE_PAGG | UCP_FEATURE_AM, UCP_AM_ID_PAGG,
              ucp_pagg_handler, NULL, 0);

UCP_DEFINE_AM_PROXY(UCP_AM_ID_PAGG);
