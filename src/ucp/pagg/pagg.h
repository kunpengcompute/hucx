/**
 * Copyright (C) Huawei Technologies Co., Ltd. 2021.  ALL RIGHTS RESERVED.
 *
 * See file LICENSE for terms.
 */

#ifndef UCP_PAGG_H_
#define UCP_PAGG_H_

#include <ucp/core/ucp_types.h>

#define UCT_PAGG_ANY_DEST (-1)

/*
 * Packet aggregation header
 */
typedef struct {
    uint16_t dest_coll_id;  /* destination identifier */
    uint8_t  next_size;     /* offset to the next header (in bytes) */
    uint8_t  am_handler_id; /* Active Message ID from @ref ucp_am_id_t */
} UCS_S_PACKED ucp_pagg_hdr_t;

#endif
