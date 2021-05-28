/**
 * Copyright (C) Mellanox Technologies Ltd. 2001-2018.  ALL RIGHTS RESERVED.
 *
 * See file LICENSE for terms.
 */

#ifndef UCP_RMA_H_
#define UCP_RMA_H_

#include <ucp/core/ucp_types.h>
#include <ucp/core/ucp_rkey.h>
#include <ucp/proto/proto_am.h>
#include <uct/api/uct.h>
#include <ucs/datastruct/ptr_map.h>


/**
 * Defines functions for RMA protocol
 */
struct ucp_rma_proto {
    const char                 *name;
    uct_pending_callback_t     progress_put;
    uct_pending_callback_t     progress_get;
};


/**
 * Defines functions for AMO protocol
 */
struct ucp_amo_proto {
    const char                 *name;
    uct_pending_callback_t     progress_fetch;
    uct_pending_callback_t     progress_post;
};


/**
 * Atomic reply data
 */
typedef union {
    uint32_t           reply32; /* 32-bit reply */
    uint64_t           reply64; /* 64-bit reply */
} ucp_atomic_reply_t;


typedef struct {
    uint64_t                  address;
    uint64_t                  ep_id;
    ucs_memory_type_t         mem_type;
} UCS_S_PACKED ucp_put_hdr_t;


typedef struct {
    uint64_t                  ep_id;
} UCS_S_PACKED ucp_cmpl_hdr_t;


typedef struct {
    uint64_t                  address;
    uint64_t                  length;
    ucp_request_hdr_t         req;
    ucs_memory_type_t         mem_type;
} UCS_S_PACKED ucp_get_req_hdr_t;


typedef struct {
    uint64_t                  req_id;
} UCS_S_PACKED ucp_rma_rep_hdr_t;


typedef struct {
    uint64_t                  address;
    ucp_request_hdr_t         req; /* invalid req_id if no reply */
    uint8_t                   length;
    uint8_t                   opcode;
} UCS_S_PACKED ucp_atomic_req_hdr_t;


extern ucp_rma_proto_t ucp_rma_basic_proto;
extern ucp_rma_proto_t ucp_rma_sw_proto;
extern ucp_amo_proto_t ucp_amo_basic_proto;
extern ucp_amo_proto_t ucp_amo_sw_proto;


ucs_status_t ucp_rma_request_advance(ucp_request_t *req, ssize_t frag_length,
                                     ucs_status_t status,
                                     ucs_ptr_map_key_t req_id);

void ucp_append_amo_cb(void *request, ucs_status_t status, void *user_data);

void ucp_ep_flush_remote_completed(ucp_request_t *req);

void ucp_rma_sw_send_cmpl(ucp_ep_h ep);

/*
 * Check RMA protocol requirements
 */
#define UCP_RMA_PROTO_INIT_CHECK(_init_params, _op_id) \
    if (((_init_params)->select_param->op_id    != (_op_id)) || \
        ((_init_params)->select_param->dt_class != UCP_DATATYPE_CONTIG)) { \
        return UCS_ERR_UNSUPPORTED; \
    }


#define UCP_RMA_CHECK_BUFFER(_buffer, _action) \
    do { \
        if (ENABLE_PARAMS_CHECK && ucs_unlikely((_buffer) == NULL)) { \
            _action; \
        } \
    } while (0)


#define UCP_RMA_CHECK_ZERO_LENGTH(_length, _action) \
    do { \
        if ((_length) == 0) { \
            _action; \
        } \
    } while (0)


#define UCP_RMA_CHECK(_context, _buffer, _length) \
    do { \
        UCP_CONTEXT_CHECK_FEATURE_FLAGS(_context, UCP_FEATURE_RMA, \
                                        return UCS_ERR_INVALID_PARAM); \
        UCP_RMA_CHECK_ZERO_LENGTH(_length, return UCS_OK); \
        UCP_RMA_CHECK_BUFFER(_buffer, return UCS_ERR_INVALID_PARAM); \
    } while (0)


#define UCP_RMA_CHECK_PTR(_context, _buffer, _length) \
    do { \
        UCP_CONTEXT_CHECK_FEATURE_FLAGS(_context, UCP_FEATURE_RMA, \
                                        return UCS_STATUS_PTR(UCS_ERR_INVALID_PARAM)); \
        UCP_RMA_CHECK_ZERO_LENGTH(_length, return NULL); \
        UCP_RMA_CHECK_BUFFER(_buffer, \
                             return UCS_STATUS_PTR(UCS_ERR_INVALID_PARAM)); \
    } while (0)


#define UCP_RMA_CHECK_CONTIG1(_param) \
    if (ucs_unlikely(ENABLE_PARAMS_CHECK && \
                     ((_param)->op_attr_mask & UCP_OP_ATTR_FIELD_DATATYPE) && \
                     ((_param)->datatype != ucp_dt_make_contig(1)))) { \
        return UCS_STATUS_PTR(UCS_ERR_UNSUPPORTED); \
    }


#endif
