/**
* Copyright (C) Huawei Technologies Co., Ltd. 2026. ALL RIGHTS RESERVED.
*
* See file LICENSE for terms.
*/

#ifndef UCT_UB_IFACE_H
#define UCT_UB_IFACE_H

#include "ub_def.h"
#include "ub_md.h"

#include <uct/api/uct.h>
#include <uct/base/uct_iface.h>
#include <uct/base/uct_iov.inl>
#include <ucs/datastruct/ptr_array.h>
#include <ucs/debug/debug.h>

#include <urma_api.h>


/* Forward declarations */
typedef struct uct_ub_iface_config  uct_ub_iface_config_t;
typedef struct uct_ub_iface         uct_ub_iface_t;
typedef struct uct_ub_iface_send_op uct_ub_iface_send_op_t;

extern ucs_config_field_t uct_ub_iface_config_table[];
typedef void (*uct_ub_send_handler_t)(uct_ub_iface_send_op_t *op, const void *resp);

#define UCT_UB_MAX_ATOMIC_SIZE sizeof(uint64_t)
#define UCT_UB_MAX_IOV 8UL
#define UCT_UB_IFACE_RECV_MP_MIN_GROW_SIZE 1024
#define UCT_UB_IFACE_MAX_EPS (0xffff)
#define UCT_UB_EP_SEQ_TABLE_ORDER (12)
#define UCT_UB_IFACE_MAX_QP_ORDER (24)
#define UCT_UB_EP_SEQ_TABLE_MEMB_ORDER (UCT_UB_IFACE_MAX_QP_ORDER - UCT_UB_EP_SEQ_TABLE_ORDER)
#define UCT_UB_EP_ID_OFFSET (32)
#define UCT_UB_EP_SEQ_OFFSET (24)
#define UCT_UB_EP_TX_QUE_SN_MASK (0xffffffff)
#define UCT_UB_EP_SEQ_TABLE_SIZE UCS_BIT(UCT_UB_EP_SEQ_TABLE_ORDER)
#define UCT_IBP_MAX_RECV_WRS 32
#define UCT_UB_MAX_MESSAGE_SIZE (2UL << 30) /* Maximal IB message size */
#define UCT_UB_MAX_PROCESS_NUM 608
#define COUNT_UP 1

#define UCT_UB_ATOMIC_FETCH_FLAGS    (UCS_BIT(UCT_ATOMIC_OP_ADD)     | \
                                      UCS_BIT(UCT_ATOMIC_OP_AND)     | \
                                      UCS_BIT(UCT_ATOMIC_OP_OR)      | \
                                      UCS_BIT(UCT_ATOMIC_OP_XOR)     | \
                                      UCS_BIT(UCT_ATOMIC_OP_SWAP)    | \
                                      UCS_BIT(UCT_ATOMIC_OP_CSWAP))

#define UCT_UB_ATOMIC_POST_FLAGS     (UCS_BIT(UCT_ATOMIC_OP_ADD)     | \
                                      UCS_BIT(UCT_ATOMIC_OP_AND)     | \
                                      UCS_BIT(UCT_ATOMIC_OP_OR)      | \
                                      UCS_BIT(UCT_ATOMIC_OP_XOR))

#define UCT_UB_TYPICAL_RETRY_CNT    7        /* typical value of retry cnt for jfs cfg */
#define UCT_UB_TYPICAL_RNR_RETRY    7        /* typical value of rnr retry for jfs cfg */

#define UCT_UB_POLL_JFC_TIMEOUT            1
#define UCT_UB_POLL_JFC_SUCCESS            1
#define UCT_UB_POLL_JFC_ERR               -1
#define MAX_RETRY_TIME 5
#define RETRY_INTERVAL 100

#define UCT_UB_KEEPALIVE_JFS_NUM             (1)
#define UCT_UB_IFACE_MAX_JFS_NUM             (256)
#define UCT_UB_IFACE_DATA_JFS_NUM            (UCT_UB_IFACE_MAX_JFS_NUM - UCT_UB_CTRL_JFS_NUM)
#define UCT_UB_IFACE_FC_JFS_NUM              (1)
#define UCT_UB_CTRL_JFS_NUM                  (UCT_UB_IFACE_FC_JFS_NUM + UCT_UB_KEEPALIVE_JFS_NUM)
#define UCT_UB_KEEPALIVE_JFS_IDX(_njfs)      ((_njfs) - UCT_UB_CTRL_JFS_NUM)
#define UCT_UB_FC_JFS_IDX(_njfs)             ((_njfs) - UCT_UB_CTRL_JFS_NUM + UCT_UB_IFACE_FC_JFS_NUM)

#define UCT_RM_IFACE_CTRL_MPOOL_GROW_SIZE    (128)
#define UCT_UB_MAX_ORDER_GROUP_NUM           (8)
#define UCT_UB_TX_QUE_SINGLE_WORD_BITS       (64)
#define UCT_UB_TX_QUE_SINGLE_WORD_OFFSET     (6)
#define UCT_UB_TX_QUE_SINGLE_WORD_MASK       (63)

typedef enum {
    UCT_UB_FIND_DATA_EP,
    UCT_UB_FIND_KA_AND_DATA_EP,
    UCT_UB_EP_NOT_EXIST
} uct_ub_iface_find_ep_t;

enum {
#ifdef NVALGRIND
    UCT_UB_IFACE_SEND_OP_FLAG_IOV   = 0,
#else
    UCT_UB_IFACE_SEND_OP_FLAG_IOV   = UCS_BIT(12), /* save iovec to make mem defined */
#endif
#if UCS_ENABLE_ASSERT
    UCT_UB_IFACE_SEND_OP_FLAG_ZCOPY = UCS_BIT(13),
    UCT_UB_IFACE_SEND_OP_FLAG_IFACE = UCS_BIT(14),
    UCT_UB_IFACE_SEND_OP_FLAG_INUSE = UCS_BIT(15)
#else
    UCT_UB_IFACE_SEND_OP_FLAG_ZCOPY = 0,
    UCT_UB_IFACE_SEND_OP_FLAG_IFACE = 0,
    UCT_UB_IFACE_SEND_OP_FLAG_INUSE
#endif
};

typedef enum {
    UCT_UB_DIR_RX,
    UCT_UB_DIR_TX,
    UCT_UB_DIR_NUM
} uct_ub_dir_t;

/**
 * UB port/path MTU.
 */
typedef enum uct_ub_mtu {
    UCT_UB_MTU_DEFAULT = 0,
    UCT_UB_MTU_256,
    UCT_UB_MTU_512,
    UCT_UB_MTU_1024,
    UCT_UB_MTU_2048,
    UCT_UB_MTU_4096,
    UCT_UB_MTU_8192,
} uct_ub_mtu_t;

enum {
    /* Indicats that TX jfc depth in uct_ub_iface_init_attr_t is specified per UB path.
     * Therefore IB interface constructor would need to multiply TX CQ len by the number of UB paths.
     * the paths means the bond ports */
    UCT_UB_TX_OPS_PER_PATH        = UCS_BIT(2)
};

typedef struct uct_jfs_ep_array {
    ucs_ptr_array_t        ep_arr;
    unsigned               err_cnt;
} uct_jfs_ep_array_t;

typedef struct uct_ub_jfs {
    urma_jfs_t             *jfs;
    ucs_arbiter_group_t    arb_group;
    int16_t                available;
    uct_jfs_ep_array_t     jfs_ep_arr;
} uct_ub_jfs_t;

typedef struct uct_ub_ctrl_req {
    uint16_t               req_cnt;
    uint16_t               req_err_flag;
} uct_ub_ctrl_req_t;

typedef struct uct_ub_iface_addr {
    uint32_t jfr_id;
    urma_transport_mode_t trans_mode;
    /* keepalive write va addr */
    uint64_t seg_va;
    uint64_t seg_len;
    uint32_t seg_flag;
    uint32_t seg_token_id ;
} uct_ub_iface_addr_t;

/*
 * The recv desc structure is shown below:
 *
 * (1) the headroom is smaller than the transport data
 *
 * <rx_headroom_offset>
 *                   |
 *                   |
 * uct_recv_desc_t   |
 *               |   |
 *               |   am_callback
 *               |   |
 * +------+------+---+-----------+---------+
 * | tseg |  ??? | D | Head Room | Payload |
 * +------+------+---+--+--------+---------+
 * | tseg |         TL hdr       | Payload |
 * +------+-------------+--------+---------+
 *                      |
 *                      urma_recv buffer addr
 *
 * (2) the headroom is larger than transport data
 *
 *            am_callback
 *            |
 * +------+---+------------------+---------+
 * | tseg | D |     Head Room    | Payload |
 * +------+---+-----+---+--------+---------+
 * | tseg |           ? | TL hdr | Payload |
 * +------+---------+---+--------+---------+
 *                      |
 *                      urma_recv buffer addr
 *        <dsc>
 *            <--- rx_headroom -->
 * <------- rx_payload_offset --->
 * <--- rx_hdr_offset -->
 *
 */

 struct uct_ub_iface_send_op {
    union {
        ucs_queue_elem_t                  queue;      /* used when enqueued a txqp */
        uct_ub_iface_send_op_t            *next;      /* used when on free list */
    };
    uct_ub_send_handler_t                 handler;
    uint32_t                              sn;
    uint16_t                              flags;
    unsigned                              length;
    union {
        void                              *buffer;
        void                              *unpack_arg;
        uct_ub_iface_t                    *iface;
    };
    uct_completion_t                      *user_comp;
};

typedef struct uct_ub_iface_send_desc {
    uct_ub_iface_send_op_t               super;
    uct_unpack_callback_t                unpack_cb;
    urma_target_seg_t                    *tseg;
} uct_ub_iface_send_desc_t;

typedef struct uct_ub_iface_recv_desc {
    urma_target_seg_t *src_tseg;
} UCS_S_PACKED uct_ub_iface_recv_desc_t;

typedef struct uct_ub_hdr {
    uint8_t   am_id;
    uint16_t  ep_id;
} UCS_S_PACKED uct_ub_hdr_t;

typedef struct uct_ub_fence_info {
    uint16_t                    fence_beat; /* 16bit is enough because if it wraps around,
                                             * it means the older ops are already completed
                                             * because QP size is less than 64k */
} uct_ub_fence_info_t;

typedef struct uct_ub_iface_init_attr {
    unsigned use_min_seg;
    unsigned rx_priv_len;
    unsigned rx_hdr_len;                /* Length of transport network header */
    unsigned jfc_depth[UCT_UB_DIR_NUM];
    int      flags;                     /* (see enum above:like UCT_UB_TX_OPS_PER_PATH) */
} uct_ub_iface_init_attr_t;

struct uct_ub_iface_config {
    uct_iface_config_t               super;
    size_t                           seg_size;
    size_t                           max_am_hdr;
    int                              fence_mode;
    uint32_t                         path_mtu;
    uint32_t                         oor_cnt;
    uint32_t                         oos_cnt;
    uint32_t                         tx_psn;
    uint32_t                         rx_psn;
    size_t                           max_inline;
    struct {
        unsigned                     depth;            /* Tx Que depth */
        unsigned                     max_poll;         /* How many crs can be picked when polling tx jfc */
        int                          poll_always;
        size_t                       max_get_zcopy;
        unsigned                     tx_max_wr;
        unsigned                     jfc_moderation;
        size_t                       max_get_bytes;
        uct_iface_mpool_config_t     mp;
    } tx;
    struct {
        unsigned                     max_poll;         /* How many crs can be picked when polling tx jfc */
        unsigned                     max_batch;        /* How many buffers can be batched to one urma recv */
        unsigned                     depth;
        uct_iface_mpool_config_t     mp;
    } rx;
    struct {
        int                          enable;
        double                       hard_thresh;
        unsigned                     wnd_size;
    } fc;
    /* Whether to check RoCEv2 reachability by IP address and local subnet */
    int                              local_subnet;
    /* Length of subnet prefix for reachability check */
    unsigned long                    subnet_pfx_len;
};

typedef ucs_status_t (*uct_ub_iface_create_jfc_func_t)(uct_ub_iface_t *iface, uct_ub_dir_t dir,
                                                       const uct_ub_iface_init_attr_t *init_attr);

typedef ucs_status_t (*uct_ub_iface_arm_jfc_func_t)(uct_ub_iface_t *iface, uct_ub_dir_t dir,
                                                    int solicited_only);

typedef struct uct_ub_am_short_hdr {
    uct_ub_hdr_t            ub_hdr;     /* Active message ID */
    uint64_t                am_hdr;
} UCS_S_PACKED uct_ub_am_short_hdr_t;


typedef void (*uct_ub_iface_handle_failure_func_t)(uct_ub_iface_t *iface, urma_cr_t *cr);
typedef ucs_status_t (*uct_ub_iface_fc_handler_func_t)(uct_ub_iface_t *iface, void *hdr,
                                                       urma_cr_t *cr, unsigned flags);
typedef void (*uct_ub_iface_destroy_jfc_func_t)(uct_ub_iface_t *iface, uint8_t direction);

typedef struct uct_ub_iface_ops {
    uct_iface_internal_ops_t                   super;
    uct_ub_iface_create_jfc_func_t             create_jfc;
    uct_ub_iface_arm_jfc_func_t                arm_jfc;
    uct_ub_iface_handle_failure_func_t         handle_failure;
    uct_ub_iface_fc_handler_func_t             fc_handler;
    uct_ub_iface_destroy_jfc_func_t            destroy_jfc;
} uct_ub_iface_ops_t;

typedef struct uct_ub_iface_ka_desc {
    urma_target_seg_t *tseg;
} UCS_S_PACKED uct_ub_iface_ka_desc_t;

struct uct_ub_iface {
    uct_base_iface_t             super;
    urma_eid_t                   eid;
    uct_ub_device_eid_info_t     eid_info;
    uint8_t                      addr_prefix_bits;
    uint32_t                     uasid;
    uint8_t                      addr_size;
    uint8_t                      port_num;
    uct_recv_desc_t              release_desc;
    urma_jfs_wr_t                send_wr;
    ucs_ptr_array_t              eps;
    urma_jfc_t                   *jfc[UCT_UB_DIR_NUM];
    urma_jfce_t                  *jfce;
    uct_ub_am_short_hdr_t        am_inl_hdr;
    urma_sge_t                   inl_sge[UCT_UB_MAX_IOV];
    uint32_t                     inline_num_sge;
    uct_ub_iface_ka_desc_t       *ka_local_seg;

    struct {
        size_t                 seg_size;
        unsigned               tx_max_poll;
        unsigned               rx_max_poll;
        unsigned               rx_max_batch;
        unsigned               rx_queue_depth;
        unsigned               tx_queue_depth;
        urma_mtu_t             mtu;
        int                    tx_poll_always;
        size_t                 max_inline;
        unsigned               rx_payload_offset;          /* offset from desc to payload. */
        unsigned               rx_hdr_offset;              /* offset from desc to transport header. */
        unsigned               rx_headroom_offset;         /* offset from desc to user headroom */
        unsigned               short_desc_size;
        size_t                 max_get_zcopy;
        size_t                 max_send_sge;
        unsigned               tx_max_wr;
        unsigned               tx_jfc_moderation;
        int                    fence_mode;
        uint8_t                fc_enabled;
        uint16_t               fc_wnd_size;
        uint16_t               fc_hard_thresh;
        unsigned               zcopy_ops_count;
        uint32_t               oor_cnt;
        uint32_t               oos_cnt;
        uint32_t               tx_psn;
        uint32_t               rx_psn;
        uct_ub_send_handler_t  atomic32_handler;
        uct_ub_send_handler_t  atomic64_handler;
    } config;
    struct{
        ssize_t                        reads_available;
        ssize_t                        reads_completed;
        uct_ub_fence_info_t            fi;
        ssize_t                        jfc_available;
        uct_ub_iface_send_op_t        *zcopy_ops_buffer;
        uct_ub_iface_send_op_t        *zcopy_free_ops;
        ucs_mpool_t                    mp;
        ucs_mpool_t                    short_desc_mp;
        ucs_mpool_t                    send_op_mp;
    } tx;
    struct {
        ucs_mpool_t             mp;
        urma_jfr_t              *jfr;
        unsigned                jfr_que_available;
    } rx;
    uct_ub_iface_ops_t          *ops;
    UCS_STATS_NODE_DECLARE(stats)
};

typedef struct uct_ub_address_pack_params {
    uint8_t                 flags; /* Packing flags, UCT_UB_ADDRESS_PACK_FLAG_xx. */
    urma_eid_t              eid;
    uint32_t                uasid;
    uint8_t                 urma_mode;
    urma_mtu_t              path_mtu;
    uint16_t                oor_cnt;
    uint8_t                 mn;
    uint8_t                 cc_alg;
    uint8_t                 rx_psn;
} UCS_S_PACKED uct_ub_address_pack_params_t;

/**
 * urma fence type.
 */
typedef enum uct_ub_fence_mode {
    UCT_UB_FENCE_MODE_NONE,
    UCT_UB_FENCE_MODE_AUTO,
    UCT_UB_FENCE_MODE_LAST
} uct_ub_fence_mode_t;

typedef struct uct_ub_tx_que_status {
    uint32_t       pi;
    uint32_t       ci;
    unsigned       uncompltes;
    unsigned       que_bitmap_mask;
    int            que_bitmap_arr_num;
} uct_ub_tx_que_status_t;

typedef struct uct_ub_tx_queue {
    ucs_queue_head_t    outstanding;
    uint64_t            *que_bitmap;
    UCS_STATS_NODE_DECLARE(stats)
} uct_ub_tx_queue_t;

enum {
    UCT_UB_IFACE_STAT_RX_COMPLETION,
    UCT_UB_IFACE_STAT_TX_COMPLETION,
    UCT_UB_IFACE_STAT_RX_JFR_AVAILABLE,
    UCT_UB_IFACE_STAT_NO_READS_AVAILABLE,
    UCT_UB_IFACE_STAT_RX_DL_CR_ERR,
    UCT_UB_IFACE_STAT_TX_DL_CR_ERR,
    UCT_UB_IFACE_STAT_LAST
};

static inline uct_ub_md_t *uct_ub_iface_md(const uct_ub_iface_t *iface)
{
    return ucs_derived_of(iface->super.md, uct_ub_md_t);
}

static inline uct_ub_device_t *uct_ub_iface_device(const uct_ub_iface_t *iface)
{
    return &uct_ub_iface_md(iface)->dev;
}

void uct_ub_ep_am_zcopy_handler(uct_ub_iface_send_op_t *op, const void *resp);

static inline void uct_ub_zcopy_desc_set_comp(uct_ub_iface_send_desc_t *desc,
                                              uct_completion_t *comp, uint32_t *force_complete_enable)
{
    if (comp == NULL) {
        desc->super.handler = (uct_ub_send_handler_t)ucs_mpool_put;
        *force_complete_enable = 0;
    } else {
        desc->super.handler = uct_ub_ep_am_zcopy_handler;
        desc->super.user_comp = comp;
        *force_complete_enable = 1;
    }
}

static UCS_F_ALWAYS_INLINE void uct_ub_iface_update_reads(uct_ub_iface_t *iface)
{
    ucs_assert(iface->tx.reads_completed >= 0);

    iface->tx.reads_available += iface->tx.reads_completed;
    iface->tx.reads_completed = 0;
}

static UCS_F_ALWAYS_INLINE void uct_ub_iface_put_send_op(uct_ub_iface_send_op_t *op)
{
    uct_ub_iface_t *iface = op->iface;

    ucs_assert(op->flags == UCT_UB_IFACE_SEND_OP_FLAG_IFACE);
    op->next = iface->tx.zcopy_free_ops;
    iface->tx.zcopy_free_ops = op;
}

static UCS_F_ALWAYS_INLINE uct_ub_iface_send_op_t *uct_ub_iface_get_send_op(uct_ub_iface_t *iface)
{
    uct_ub_iface_send_op_t *op = NULL;

    op = iface->tx.zcopy_free_ops;
    iface->tx.zcopy_free_ops = (op == NULL) ? NULL : op->next;
    return op;
}

static inline uct_ub_ep_t *uct_ub_iface_lookup_ep(uct_ub_iface_t *iface, urma_cr_t *cr)
{
    uint32_t ep_value = cr->user_ctx >> UCT_UB_EP_ID_OFFSET;
    uint32_t ep_id = (ep_value & UCT_UB_IFACE_MAX_EPS);

    return (uct_ub_ep_t *)(iface->eps.start[ep_id]);
}

static inline void *uct_ub_iface_recv_desc_hdr(uct_ub_iface_t *iface, uct_ub_iface_recv_desc_t *desc)
{
    return (void *)((char *)desc + iface->config.rx_hdr_offset);
}

/**
 * Fill urma_sge_t data structure by data provided in uct_iov_t
 * The function avoids copying IOVs with zero length
 *
 * @return Number of elements in sge[]
 */
static UCS_F_ALWAYS_INLINE
size_t uct_ub_sge_fill_iov(urma_sge_t *sge, const uct_iov_t *iov, size_t iovcnt)
{
    size_t iov_it, sge_it = 0;

    for (iov_it = 0; iov_it < iovcnt; ++iov_it) {
        sge[sge_it].len = uct_iov_get_length(&iov[iov_it]);
        if (sge[sge_it].len > 0) {
            sge[sge_it].addr = (uint64_t)(iov[iov_it].buffer);
        } else {
            continue; /* to avoid zero length elements in sge */
        }

        if (iov[iov_it].memh == UCT_MEM_HANDLE_NULL) {
            sge[sge_it].tseg = NULL;
        } else {
            sge[sge_it].tseg = uct_ub_memh_get_tseg(iov[iov_it].memh);
        }
        ++sge_it;
    }

    return sge_it;
}

static UCS_F_ALWAYS_INLINE void
uct_ub_iface_fill_inl_sge(uct_ub_iface_t *iface, const void *addr0,
                          unsigned len0, const void* addr1, unsigned len1)
{
    int idx = 0;

    iface->inl_sge[idx].addr = (uintptr_t)addr0;
    iface->inl_sge[idx].len = len0;
    iface->inl_sge[idx++].tseg = NULL;
    if (ucs_likely(len1 != 0)) {
        iface->inl_sge[idx].addr = (uintptr_t)addr1;
        iface->inl_sge[idx].len = len1;
        iface->inl_sge[idx++].tseg = NULL;
    }
    iface->inline_num_sge = idx;
}

static UCS_F_ALWAYS_INLINE uct_ub_send_handler_t
uct_ub_iface_atomic_handler(uct_ub_iface_t *iface, unsigned length)
{
    ucs_assert((length == sizeof(uint32_t)) || (length == sizeof(uint64_t)));
    switch (length) {
        case sizeof(uint32_t):
            return iface->config.atomic32_handler;
        case sizeof(uint64_t):
            return iface->config.atomic64_handler;
    }
    return NULL;
}

static UCS_F_ALWAYS_INLINE void uct_ub_iface_txqp_completion_op(uct_ub_iface_send_op_t *op, const void *resp)
{
    ucs_trace_poll("Complete sn %d handler %s", op->sn,
                   ucs_debug_get_symbol_name((void*)op->handler));
    ucs_assert(op->flags & UCT_UB_IFACE_SEND_OP_FLAG_INUSE);
    op->flags &= ~(UCT_UB_IFACE_SEND_OP_FLAG_INUSE |
                   UCT_UB_IFACE_SEND_OP_FLAG_ZCOPY);
    op->handler(op, resp);
}

static UCS_F_ALWAYS_INLINE void uct_ub_iface_txqp_completion_desc(uct_ub_tx_queue_t *tx_queue, uint32_t sn)
{
    uct_ub_iface_send_op_t *op = NULL;

    ucs_trace("Tx_queue complete ops up to sn %d", sn);
    ucs_queue_for_each_extract(op, &tx_queue->outstanding, queue,
                               UCS_CIRCULAR_COMPARE32(op->sn, <=, sn)) {
        uct_ub_iface_txqp_completion_op(op, ucs_derived_of(op, uct_ub_iface_send_desc_t) + COUNT_UP);
    }
}

static UCS_F_ALWAYS_INLINE
ucs_log_level_t uct_ub_iface_failure_log_level(uct_base_iface_t *iface,
                                                 ucs_status_t err_handler_status,
                                                 ucs_status_t status)
{
    if (err_handler_status != UCS_OK) {
        return UCS_LOG_LEVEL_FATAL;
    } else if ((status == UCS_ERR_ENDPOINT_TIMEOUT) ||
               (status == UCS_ERR_CONNECTION_RESET)) {
        return iface->config.failure_level;
    } else {
        return UCS_LOG_LEVEL_ERROR;
    }
}

#define UCT_UB_IFACE_GET_TX_DESC(_iface, _mp, _desc) \
    UCT_TL_IFACE_GET_TX_DESC(&(_iface)->super, _mp, _desc, \
                             return UCS_ERR_NO_RESOURCE);

#define UCT_UB_IFACE_GET_TX_PUT_BCOPY_DESC(_iface, _mp, _desc, _pack_cb, _arg, _length) \
    UCT_UB_IFACE_GET_TX_DESC(_iface, _mp, _desc) \
    (_desc)->super.handler = (uct_ub_send_handler_t)ucs_mpool_put; \
    _length = _pack_cb(_desc + 1, _arg); \
    UCT_SKIP_ZERO_LENGTH(_length, _desc);

#define UCT_UB_IFACE_GET_TX_GET_BCOPY_DESC(_iface, _mp, _desc, _unpack_cb, _comp, _arg, _length) \
    UCT_UB_IFACE_GET_TX_DESC(&(_iface)->super, _mp, _desc) \
    ucs_assert(_length <= (_iface)->super.config.seg_size); \
    _desc->super.handler     = (_comp == NULL) ? \
                                uct_ub_ep_get_bcopy_handler_no_completion : \
                                uct_ub_ep_get_bcopy_handler; \
    _desc->super.unpack_arg  = _arg; \
    _desc->super.user_comp   = _comp; \
    _desc->super.length      = _length; \
    _desc->unpack_cb         = _unpack_cb;

#define UCT_UB_IFACE_GET_TX_AM_BCOPY_DESC(_iface, _ep, _mp, _desc, _id, _pk_hdr_cb, \
                                          _hdr, _pack_cb, _arg, _length) ({ \
    _hdr *rch; \
    UCT_UB_IFACE_GET_TX_DESC(_iface, _mp, _desc) \
    (_desc)->super.handler = (uct_ub_send_handler_t)ucs_mpool_put; \
    rch = (_hdr *)(_desc + 1); \
    _pk_hdr_cb(rch, _ep, _id); \
    *(_length) = _pack_cb(rch + 1, _arg); \
})

#define UCT_UB_IFACE_GET_TX_ATOMIC_FETCH_DESC(_iface, _mp, _desc, _handler, _result, _comp) \
    UCT_CHECK_PARAM(_comp != NULL, "completion must be non-NULL"); \
    UCT_UB_IFACE_GET_TX_DESC(_iface, _mp, _desc) \
    _desc->super.handler   = _handler; \
    _desc->super.buffer    = _result; \
    _desc->super.user_comp = _comp;

#define UCT_UB_IFACE_GET_TX_ATOMIC_DESC(_iface, _mp, _desc) \
    UCT_UB_IFACE_GET_TX_DESC(_iface, _mp, _desc) \
    _desc->super.handler = (uct_ub_send_handler_t)ucs_mpool_put;

#define UCT_UB_CHECK_ZCOPY_DATA(_header_length, _length, _seg_size) \
    UCT_CHECK_LENGTH(_header_length + _length, 0, _seg_size, "am_zcopy payload"); \
    UCT_CHECK_LENGTH(_header_length + _length, 0, UCT_UB_MAX_MESSAGE_SIZE, "am_zcopy ib max message");

#define UCT_UB_CHECK_AM_ZCOPY(_id, _header_length, _length, _desc_size, _seg_size) \
    UCT_CHECK_AM_ID(_id); \
    UCT_UB_CHECK_ZCOPY_DATA(_header_length, _length, _seg_size) \
    UCT_CHECK_LENGTH(sizeof(uct_ub_hdr_t) + header_length, 0, \
                     _desc_size, "am_zcopy header");

#define UCT_UB_CHECK_AM_SHORT(_am_id, _length, _header_t, _max_inline) \
     UCT_CHECK_AM_ID(_am_id); \
     UCT_CHECK_LENGTH(sizeof(_header_t) + _length, 0, _max_inline, "am_short");

#define uct_ub_serialize_next_raw(_iter, _type, _offset) \
    ({ \
        _type *_result = (_type*)(*(_iter)); \
        *(_iter)       = UCS_PTR_BYTE_OFFSET(*(_iter), _offset); \
        _result; \
    })

#define uct_ub_serialize_next(_iter, _type) \
    uct_ub_serialize_next_raw(_iter, _type, sizeof(_type))

UCS_CLASS_DECLARE(uct_ub_iface_t, uct_ub_iface_ops_t*, uct_iface_ops_t*,
                  uct_md_h, uct_worker_h, const uct_iface_params_t*, const uct_iface_config_t*,
                  const uct_ub_iface_init_attr_t*);

ucs_status_t uct_ub_iface_get_device_address(uct_iface_h tl_iface, uct_device_addr_t *dev_addr);
void uct_ub_address_unpack(const uct_ub_address_t *ub_addr, uct_ub_address_pack_params_t *params_p);

int uct_ub_iface_is_ubn(uct_ub_iface_t *iface);

int uct_ub_iface_is_reachable(const uct_iface_h tl_iface,
                              const uct_iface_is_reachable_params_t *params);

int uct_ub_base_iface_is_reachable(const uct_iface_h tl_iface, const uct_device_addr_t *dev_addr,
                                   const uct_iface_addr_t *iface_addr);

ucs_status_t uct_ub_base_iface_query(uct_ub_iface_t *iface, uct_iface_attr_t *iface_attr);
ucs_status_t uct_ub_iface_cap_query(uct_ub_iface_t *iface, uct_iface_attr_t *iface_attr);
ucs_status_t uct_ub_iface_fence(uct_iface_h tl_iface, unsigned flags);
void uct_ub_iface_fc_init(uct_ub_iface_t *iface, const uct_ub_iface_config_t *config);
void uct_ub_send_op_completion_handler(uct_ub_iface_send_op_t *op, const void *resp);
ucs_status_t uct_ub_iface_zcopy_ops_init(uct_ub_iface_t *iface);
void uct_ub_iface_zcopy_ops_cleanup(uct_ub_iface_t *iface);

void uct_ub_iface_release_desc(uct_recv_desc_t *self, void *desc);
ucs_status_t uct_ub_iface_mempool_init(uct_ub_iface_t *iface, const uct_ub_iface_config_t *config,
                                       const uct_iface_params_t *params);
ucs_status_t uct_ub_ep_basic_init(uct_ub_iface_t *iface, uct_ub_ep_t *ep);
extern ucs_status_t uct_ub_iface_add_ep(uct_ub_iface_t *iface, uct_ub_ep_t *ep);
void uct_ub_ep_tx_outstanding_queue_init(uct_ub_tx_queue_t *tx_queue);
void uct_ub_txcnt_init(uct_ub_tx_que_status_t *tx_que_status, unsigned que_bitmap_mask,
                       int que_bitmap_arr_num);
void uct_ub_err_delete_ep(uct_ub_iface_t *iface, uct_ub_ep_t *ep);
extern void uct_ub_iface_remove_ep(uct_ub_iface_t *iface, uct_ub_ep_t *ep);
ucs_status_t uct_ub_iface_check_validity(const uct_iface_params_t *params, const uct_worker_h worker);
void uct_ub_destroy_jfr(urma_jfr_t *jfr);
ucs_status_t uct_ub_create_jfc(uct_ub_iface_t *iface, uct_ub_dir_t dir,
                               const uct_ub_iface_init_attr_t *init_attr);
ucs_status_t uct_ub_iface_arm_jfc(uct_ub_iface_t *iface, uct_ub_dir_t dir,
                                  int solicited_only);
void uct_ub_iface_mpool_cleanup(uct_ub_iface_t *iface);
ucs_status_t uct_ub_iface_create_jfr(uct_ub_iface_t *iface, urma_jfr_cfg_t *jfr_cfg);
ucs_status_t uct_ub_iface_event_fd_get(uct_iface_h tl_iface, int *fd_p);
ucs_status_t uct_ub_iface_event_arm(uct_iface_h tl_iface, unsigned events);
void uct_ub_iface_init_recv_queue(uct_ub_iface_t *iface, unsigned init_count);
unsigned uct_ub_iface_poll_rx(uct_ub_iface_t *iface, uint32_t fc_mask);
ucs_status_t uct_ub_iface_completion_err_proc(uct_ub_iface_t *iface, urma_cr_t *cr, void *jetty,
                                              uct_ub_ep_t *ep, uct_ub_ep_t *fc_ep);
void uct_ub_destroy_jfc(urma_jfc_t *jfc);
#endif
