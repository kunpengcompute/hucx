/**
* Copyright (C) Huawei Technologies Co., Ltd. 2026. ALL RIGHTS RESERVED.
*
* See file LICENSE for terms.
*/

#ifndef UCT_UB_EP_H
#define UCT_UB_EP_H

#include <uct/api/uct.h>
#include <ucs/datastruct/arbiter.h>
#include <ucs/sys/compiler_def.h>
#include <uct/base/uct_iface.h>
#include <uct/urma/base/urma_iface.h>

#define MAX_TASK_COUNT 608
#define UCT_UB_MAX_HDR_SIZE 128

void uct_ub_ep_flush_op_completion_handler(uct_ub_iface_send_op_t *op, const void *resp);
void uct_ub_ep_get_bcopy_handler_no_completion(uct_ub_iface_send_op_t *op, const void *resp);
void uct_ub_ep_get_bcopy_handler(uct_ub_iface_send_op_t *op, const void *resp);
void uct_ub_ep_get_zcopy_completion_handler(uct_ub_iface_send_op_t *op, const void *resp);
UCS_CLASS_DECLARE(uct_ub_ep_t, uct_ub_iface_t *);
UCS_CLASS_DECLARE_DELETE_FUNC(uct_ub_ep_t, uct_ep_t);

/*
 * Auxillary AM ID bits used by FC protocol.
 */
typedef enum {
    /* Keepalive Request scheduled: indicates that keepalive request
     * is scheduled in pending queue and no more keepalive actions
     * are needed */
    UCT_UB_EP_FLAG_KEEPALIVE_PENDING   = UCS_BIT(0),

    /* EP keepalive request flag */
    UCT_UB_EP_FLAG_KEEPALIVE_POSTED    = UCS_BIT(1),

    /* Flush cancel was executed on EP */
    UCT_UB_EP_FLAG_FLUSH_CANCEL        = UCS_BIT(2),

    /* Error handler already called or flush(CANCEL) disabled it */
    UCT_UB_EP_FLAG_ERR_HANDLER_INVOKED = UCS_BIT(3),

    UCT_UB_EP_FLAG_CONNECTION_ERR      = UCS_BIT(4),

    UCT_UB_EP_FLAG_OOR                 = UCS_BIT(5),

    /* Hard Credit Request: indicates that wnd is close to be exhausted.
     * The peer must send separate AM with credit grant as soon as it
     * receives AM  with this bit set. */
    UCT_UB_EP_FLAG_FC_HARD_REQ         = UCS_BIT(6),

    /* Special FC AM with Credit Grant: Just an empty message indicating
     * credit grant. Can't be bundled with any other FC flag. */
    UCT_UB_EP_FLAG_FC_PURM_GRANT       = UCS_BIT(7),
} UB_EP_FLAG_E;

typedef struct uct_ub_fc {
    /* Not more than fc_wnd active messages can be sent w/o acknowledgment */
    int16_t             fc_wnd;
    /* used only for FC protocol at this point (3 higher bits) */
    UCS_STATS_NODE_DECLARE(stats)
} uct_ub_fc_t;

struct uct_ub_ep {
    uct_base_ep_t                   super;
    uint8_t                         flags;  // see UB_EP_FLAG_E
    uint8_t                         fc_flags;
    uct_ub_fence_info_t             fi;
    uint32_t                        ep_id;
    uint32_t                        dep_id;
    uct_ub_tx_queue_t               txqp;
    uct_ub_tx_que_status_t          status;
    uct_ub_fc_t                     fc;
};

typedef struct uct_rx_urma_tp_ep_addr_params {
    uint32_t              jetty_id;
    uint32_t              ep_id;
    uint64_t              seg_va;
    uint64_t              seg_len;
    uint32_t              seg_flag;
    uint32_t              seg_token_id;
    uint32_t              psn;
    uint64_t              tp_id;
    urma_jetty_id_t       remote_jetty_id;
    urma_eid_t            eid;
    urma_transport_mode_t trans_mode;
} uct_rx_urma_tp_ep_addr_params_t;

enum {
    UCT_UB_FC_STAT_NO_CRED,
    UCT_UB_FC_STAT_TX_GRANT,
    UCT_UB_FC_STAT_TX_PURE_GRANT,
    UCT_UB_FC_STAT_TX_SOFT_REQ,
    UCT_UB_FC_STAT_TX_HARD_REQ,
    UCT_UB_FC_STAT_RX_GRANT,
    UCT_UB_FC_STAT_RX_PURE_GRANT,
    UCT_UB_FC_STAT_RX_SOFT_REQ,
    UCT_UB_FC_STAT_RX_HARD_REQ,
    UCT_UB_FC_STAT_RX_GRANT_CHECK,
    UCT_UB_FC_STAT_FC_WND,
    UCT_UB_FC_STAT_LAST
};

#define UCT_UB_DECLARE_ATOMIC_LE_HANDLER(_bits) \
    void \
    uct_ub_common_atomic##_bits##_le_handler(uct_ub_iface_send_op_t *op, \
                                             const void *resp);
UCT_UB_DECLARE_ATOMIC_LE_HANDLER(32)
UCT_UB_DECLARE_ATOMIC_LE_HANDLER(64)

#define UCT_UB_ATOMIC_OPS (UCS_BIT(UCT_ATOMIC_OP_ADD) | \
                           UCS_BIT(UCT_ATOMIC_OP_AND) | \
                           UCS_BIT(UCT_ATOMIC_OP_OR)  | \
                           UCS_BIT(UCT_ATOMIC_OP_XOR) | \
                           UCS_BIT(UCT_ATOMIC_OP_SWAP))

#define UCT_UB_CHECK_ATOMIC_OPS(_op, _size, _flags)                             \
    if (ucs_unlikely(!(UCS_BIT(_op) & (_flags)))) {                             \
        ucs_assertv(0, "Incorrect opcode for atomic: %d", _op);                 \
        return UCS_ERR_UNSUPPORTED;                                             \
    } else {                                                                    \
        ucs_assert((_size == sizeof(uint64_t)) || (_size == sizeof(uint32_t))); \
    }

#define UCT_UB_FILL_SGE_BUF(_sge, _addr, _len, _tseg) \
    { \
        ((_sge)->addr) = _addr; \
        ((_sge)->len) = _len; \
        ((_sge)->tseg) = _tseg; \
    }

#define UCT_UB_FILL_WR_SG_BUF(_sg, _addr, _len, _tseg, _num, _sge_buff) \
    { \
        (_sg)->sge = _sge_buff; \
        UCT_UB_FILL_SGE_BUF((_sg)->sge, _addr, _len, _tseg); \
        (_sg)->num_sge = _num; \
    }

#define UCT_UB_FILL_FAA(_wr, _addr, _value, _len, \
                        _src, _src_tseg, _dst_tseg, _dst_buff, _src_buff) \
    { \
        _wr->faa.dst = _dst_buff; \
        _wr->faa.src = _src_buff; \
        UCT_UB_FILL_SGE_BUF(_wr->faa.dst, _addr, _len, _dst_tseg); \
        UCT_UB_FILL_SGE_BUF(_wr->faa.src, _src, _len, _src_tseg); \
        _wr->faa.operand = _value; \
    }

#define UCT_UB_FILL_SRC_SGL_BUF(_sgl, _sge, _num) \
    { \
        ((_sgl)->sge) = _sge; \
        ((_sgl)->num_sge) = _num; \
    }

#define UCT_UB_FILL_ATOMIC_WR(_wr, _src, _remote_addr, _compare, \
                              _swap_add, _length, _dst_tseg, _src_tseg, _dst_buff, _src_buff) \
    _wr->cas.dst = _dst_buff; \
    _wr->cas.src = _src_buff; \
    UCT_UB_FILL_SGE_BUF(_wr->cas.dst, _remote_addr, _length, _dst_tseg); \
    UCT_UB_FILL_SGE_BUF(_wr->cas.src, _src, _length, _src_tseg); \
    _wr->cas.cmp_data = _compare; \
    _wr->cas.swap_data = _swap_add; \


#define UCT_UB_CHECK_JFC_RET(_iface, _ret) \
{ \
    if (ucs_unlikely((_iface)->tx.jfc_available <= 0)) {\
        return _ret; \
    } \
}

#define UCT_UB_CHECK_NUM_READ_RET(_iface, _ret) \
    if (ucs_unlikely((_iface)->tx.reads_available <= 0)) { \
        UCS_STATS_UPDATE_COUNTER((_iface)->stats, UCT_UB_IFACE_STAT_NO_READS_AVAILABLE, 1); \
        return _ret; \
    }

#define UCT_UB_CHECK_RES(_iface, _ep) \
    { \
        UCT_UB_CHECK_JFC_RET((_iface), UCS_ERR_NO_RESOURCE) \
        UCT_UB_CHECK_NUM_READ_RET((_iface), \
                                  UCS_ERR_NO_RESOURCE) \
    }

#define UCT_UB_READ_POSTED(_iface, _length) \
    { \
        ucs_assert((_iface)->super.tx.reads_available > 0); \
        (_iface)->super.tx.reads_available -= (_length); \
    }

static UCS_F_ALWAYS_INLINE void
uct_ub_outstanding_queue_add_send_op(uct_ub_tx_queue_t *tx_queue, uct_ub_iface_send_op_t *op)
{
    /* NOTE: We insert the descriptor with the sequence number after the post,
     * because when polling completions, we get the number of completions (rather
     * than completion zero-based index).
     */
    ucs_assert(op != NULL);
    ucs_assert(!(op->flags & UCT_UB_IFACE_SEND_OP_FLAG_INUSE));
    op->flags |= UCT_UB_IFACE_SEND_OP_FLAG_INUSE;
    ucs_queue_push(&tx_queue->outstanding, &op->queue);
}

static UCS_F_ALWAYS_INLINE void
uct_ub_outstanding_queue_add_send_op_sn(uct_ub_tx_queue_t *tx_queue,
                                        uct_ub_iface_send_op_t *op, uint32_t sn)
{
    ucs_trace_poll("Tx_queue add send op sn %d handler %s", sn,
                   ucs_debug_get_symbol_name((void*)op->handler));

    op->sn = sn;
    uct_ub_outstanding_queue_add_send_op(tx_queue, op);
}

static UCS_F_ALWAYS_INLINE void
uct_ub_ep_init_send_op(uct_ub_iface_send_op_t *op, unsigned flags, uct_completion_t *comp,
                       uct_ub_send_handler_t handler)
{
    op->flags = flags;
    op->user_comp = comp;
    op->handler = handler;
}

static UCS_F_ALWAYS_INLINE ucs_status_t
uct_ub_txqp_add_flush_comp(uct_ub_iface_t *iface, uct_base_ep_t *ep,
                           uct_ub_tx_queue_t *tx_queue, uct_completion_t *comp, uint16_t sn)
{
    uct_ub_iface_send_op_t *op = NULL;

    if (comp != NULL) {
        op = (uct_ub_iface_send_op_t*)ucs_mpool_get(&iface->tx.send_op_mp);
        if (ucs_unlikely(op == NULL)) {
            ucs_error("Failed to allocate flush completion");
            return UCS_ERR_NO_MEMORY;
        }
        uct_ub_ep_init_send_op(op, 0, comp, uct_ub_ep_flush_op_completion_handler);
        op->iface = iface;
        uct_ub_outstanding_queue_add_send_op_sn(tx_queue, op, sn);
    }
    UCT_TL_EP_STAT_FLUSH_WAIT(ep);

    return UCS_INPROGRESS;
}

static UCS_F_ALWAYS_INLINE void uct_ub_op_release_get_bcopy(uct_ub_iface_send_op_t *op)
{
    uct_ub_iface_send_desc_t *desc = ucs_derived_of(op, uct_ub_iface_send_desc_t);
    uct_ub_iface_t *iface = ucs_container_of(ucs_mpool_obj_owner(desc), uct_ub_iface_t, tx.mp);

    iface->tx.reads_completed += op->length;
}

static UCS_F_ALWAYS_INLINE void uct_ub_op_release_get_zcopy(uct_ub_iface_send_op_t *op)
{
    op->iface->tx.reads_completed += op->length;
    op->flags &= ~UCT_UB_IFACE_SEND_OP_FLAG_IOV;
}

static UCS_F_ALWAYS_INLINE void
uct_ub_tx_outstanding_queue_add_send_comp(uct_ub_iface_t *iface, uct_ub_tx_queue_t *tx_queue,
                                          uct_ub_send_handler_t handler, uct_completion_t *comp,
                                          uint32_t sn, uint16_t flags, size_t length)
{
    uct_ub_iface_send_op_t *op = NULL;

    if (comp == NULL) {
        return;
    }

    op = uct_ub_iface_get_send_op(iface);
    ucs_assert(op != NULL);

    op->handler = handler;
    op->user_comp = comp;
    op->flags |= flags;
    op->length = length;
    uct_ub_outstanding_queue_add_send_op_sn(tx_queue, op, sn);
}

static uint32_t UCS_F_ALWAYS_INLINE uct_ub_get_complete_enable_flag(uint32_t force_complete_enable,
                                                                    uct_ub_ep_t *ep,
                                                                    uct_ub_iface_t *iface)
{
    ucs_trace("Get complete enable flag force_complete_enable:%u, uncompltes:%u, \
              tx_jfc_moderation:%u, jfc_available:%zd\n",
              force_complete_enable, ep->status.uncompltes, iface->config.tx_jfc_moderation,
              iface->tx.jfc_available);

    if (force_complete_enable == 1) {
        return URMA_COMPLETE_ENABLE;
    }

    if (ucs_unlikely(iface->tx.jfc_available < iface->config.tx_jfc_moderation)) {
        return URMA_COMPLETE_ENABLE;
    }

    if (ucs_unlikely(ep->status.uncompltes >= iface->config.tx_jfc_moderation)) {
        return URMA_COMPLETE_ENABLE;
    }

    return URMA_COMPLETE_DISABLE;
}

static void UCS_F_ALWAYS_INLINE uct_ub_init_base_wr(urma_jfs_wr_t *wr, uct_ub_iface_t *iface,
                                                    urma_opcode_t opcode, uct_ub_ep_t *ep,
                                                    uint32_t force_complete_enable, uint32_t inline_flag,
                                                    uint32_t solicited_flag)
{
    wr->flag.bs.complete_enable  = uct_ub_get_complete_enable_flag(force_complete_enable, ep, iface);
    wr->flag.bs.inline_flag      = inline_flag;
    wr->flag.bs.solicited_enable = solicited_flag;
    wr->opcode                   = opcode;
}

static UCS_F_ALWAYS_INLINE void uct_ub_ep_fm(uct_ub_iface_t *iface, uct_ub_ep_t* ep, urma_jfs_wr_t *wr)
{
    if (ucs_likely(ep->fi.fence_beat == iface->tx.fi.fence_beat)) {
        return;
    }
    ep->fi.fence_beat = iface->tx.fi.fence_beat;
    wr->flag.bs.fence = URMA_FENCE_ENABLE;
}

static UCS_F_ALWAYS_INLINE int
uct_ub_iface_has_tx_resources(uct_ub_iface_t *iface)
{
    return !ucs_mpool_is_empty(&iface->tx.mp) && (iface->tx.reads_available > 0);
}

static UCS_F_ALWAYS_INLINE
int uct_ub_fc_has_resources(uct_ub_iface_t *iface, uct_ub_fc_t *fc)
{
    /* When FC is disabled, fc_wnd may still become 0 because it's decremented
     * unconditionally (for performance reasons) */
    return (fc->fc_wnd > 0) || !iface->config.fc_enabled;
}

static UCS_F_ALWAYS_INLINE unsigned uct_ub_iface_get_tx_res_count(uct_ub_ep_t *ep,
                                                                  urma_cr_t *cr)
{
    uint32_t sn = cr->user_ctx & UCT_UB_EP_TX_QUE_SN_MASK;

    return sn - ep->status.ci;
}

static UCS_F_ALWAYS_INLINE void
uct_ub_am_hdr_fill(uct_ub_hdr_t *rch, uct_ub_ep_t *ep, uint8_t id)
{
    rch->am_id = id;
    rch->ep_id = ep->dep_id;
}

static inline void uct_ub_zcopy_desc_set_header(uct_ub_hdr_t *rch, uct_ub_ep_t *ep,
                                                uint8_t id, const void *header,
                                                unsigned header_length)
{
    rch->am_id = id;
    rch->ep_id = ep->dep_id;

    if (header_length > (UCT_UB_MAX_HDR_SIZE - sizeof(uct_ub_hdr_t))) {
        ucs_warn("The header_length: %u is too large, MAX: %ld",
        header_length, (UCT_UB_MAX_HDR_SIZE - sizeof(uct_ub_hdr_t)));
    }

    memcpy(rch + 1, header, header_length);
}

static inline void
uct_ub_iface_fill_inl_am_sge_iov(uct_ub_iface_t *iface, uct_ub_ep_t *ep, uint8_t id,
                                 const uct_iov_t *iov, size_t iovcnt)
{
    uct_ub_hdr_t *hdr = &iface->am_inl_hdr.ub_hdr;

    hdr->am_id = id;
    hdr->ep_id = ep->dep_id;

    iface->inl_sge[0].addr   = (uintptr_t)hdr;
    iface->inl_sge[0].len    = sizeof(*hdr);
    iface->inl_sge[0].tseg   = NULL;
    iface->inline_num_sge    = uct_ub_sge_fill_iov(iface->inl_sge + 1, iov, iovcnt) + 1;
}

static UCS_F_ALWAYS_INLINE ucs_status_t uct_ub_ep_import_seg(uct_ub_md_t *ub_md, uct_ub_rkey_info_t *rkey_info)
{
    ucs_status_t status = UCS_OK;
    urma_import_seg_flag_t flag;

    flag.bs.cacheable = URMA_NON_CACHEABLE;
    flag.bs.access = URMA_ACCESS_READ |
                     URMA_ACCESS_WRITE |
                     URMA_ACCESS_ATOMIC;
    flag.bs.mapping = URMA_SEG_NOMAP;
    flag.bs.reserved = 0;

    pthread_rwlock_wrlock(&rkey_info->import_tseg_lock);
    rkey_info->import_tseg = urma_import_seg(ub_md->dev.urma_ctx, &rkey_info->remote_seg, &ub_md->dev.token, 0, flag);
    if (rkey_info->import_tseg == NULL) {
        ucs_error("Failed to import segment in uct_ub_ep_import_seg");
        status = UCS_ERR_IO_ERROR;
    }

    pthread_rwlock_unlock(&rkey_info->import_tseg_lock);

    return status;
}

static UCS_F_ALWAYS_INLINE ucs_status_t uct_ub_ep_connect_tseg(uct_ub_iface_t *iface, uct_rkey_t rkey_p,
                                                               urma_target_seg_t **target_seg)
{
    uct_ub_rkey_info_t *rkey_info = NULL;
    ucs_status_t status = UCS_OK;
    uct_ub_md_t *ub_md = NULL;

    rkey_info = (uct_ub_rkey_info_t *)rkey_p;

    ub_md = ucs_derived_of(iface->super.md, uct_ub_md_t);
    status = uct_ub_ep_import_seg(ub_md, rkey_info);
    if (status != UCS_OK) {
        goto out;
    }

    *target_seg = rkey_info->import_tseg;

out:
    return status;
}

static UCS_F_ALWAYS_INLINE ucs_status_t uct_ub_ep_get_tseg(uct_ub_iface_t *iface, uct_rkey_t rkey_p,
                                                           urma_target_seg_t **target_seg)
{
    uct_ub_rkey_info_t *rkey_info = (uct_ub_rkey_info_t *)rkey_p;
    ucs_status_t status = UCS_OK;

    if (ucs_unlikely(rkey_info == NULL)) {
        ucs_error("Parsing rkey error");
        return UCS_ERR_INVALID_PARAM;
    }

    if (ucs_unlikely(rkey_info->import_tseg == NULL)) {
        status = uct_ub_ep_connect_tseg(iface, rkey_p, target_seg);
        if (status != UCS_OK) {
            ucs_warn("Failed to uct_ub_ep_connect_tseg");
            return status;
        }
    } else {
        *target_seg = rkey_info->import_tseg;
    }

    return status;
}

#define UCT_UB_IFACE_GET_TX_AM_ZCOPY_DESC(_iface, _ep, _mp, _desc, \
                                          _id, _header, _header_length, _comp, _send_flags) \
    UCT_UB_IFACE_GET_TX_DESC(_iface, _mp, _desc); \
    uct_ub_zcopy_desc_set_comp(_desc, _comp, _send_flags); \
    uct_ub_zcopy_desc_set_header((uct_ub_hdr_t*)(_desc + 1), _ep, _id, _header, _header_length);

extern ucs_status_t uct_ub_iface_txqp_purge_process(uct_ub_iface_t *iface, uct_ub_tx_queue_t *txqp,
                                                    ucs_status_t status, uct_ub_iface_send_op_t *op, int warn);

#endif
