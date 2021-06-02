/**
* Copyright (C) Mellanox Technologies Ltd. 2001-2018.  ALL RIGHTS RESERVED.
*
* See file LICENSE for terms.
*/

#ifndef UCT_IB_MLX5_HW_H_
#define UCT_IB_MLX5_HW_H_

#include <stdint.h>

struct mlx5dv_qp {
    volatile uint32_t   *dbrec;
    struct {
        void            *buf;
        uint32_t        wqe_cnt;
        uint32_t        stride;
    } sq;
    struct {
        void            *buf;
        uint32_t        wqe_cnt;
        uint32_t        stride;
    } rq;
    struct {
        void            *reg;
        uint32_t        size;
    } bf;
    uint64_t            comp_mask;
};

struct mlx5dv_cq {
    void                *buf;
    volatile uint32_t   *dbrec;
    uint32_t            cqe_cnt;
    uint32_t            cqe_size;
    void                *cq_uar;
/* DV backport will behave as DV with fixed CQ UAR */
#undef HAVE_STRUCT_MLX5DV_CQ_CQ_UAR
#define HAVE_STRUCT_MLX5DV_CQ_CQ_UAR 1
    uint32_t            cqn;
    uint64_t            comp_mask;
};

struct mlx5dv_srq {
    void                *buf;
    volatile uint32_t   *dbrec;
    uint32_t            stride;
    uint32_t            head;
    uint32_t            tail;
    uint64_t            comp_mask;
};

struct mlx5dv_obj {
    struct {
        struct ibv_qp           *in;
        struct mlx5dv_qp        *out;
    } qp;
    struct {
        struct ibv_cq           *in;
        struct mlx5dv_cq        *out;
    } cq;
    struct {
        struct ibv_srq          *in;
        struct mlx5dv_srq       *out;
    } srq;
    struct {
        struct ibv_exp_wq       *in;
        struct mlx5dv_rwq       *out;
    } rwq;
};

enum mlx5dv_obj_type {
    MLX5DV_OBJ_QP   = 1 << 0,
    MLX5DV_OBJ_CQ   = 1 << 1,
    MLX5DV_OBJ_SRQ  = 1 << 2,
    MLX5DV_OBJ_RWQ  = 1 << 3,
};

#ifdef HAVE_DM
#include <ucs/type/status.h>
#include <uct/ib/base/ib_md.h>

#ifdef HAVE_IBV_EXP_DM
typedef union uct_ib_mlx5_dm uct_ib_mlx5_dm_t;
ucs_status_t
uct_ib_mlx5_exp_md_dm_create(uct_ib_md_t *md, size_t length, uct_ib_mlx5_dm_t *dm);
void uct_ib_mlx5_exp_md_dm_destroy(uct_ib_mlx5_dm_t *dm);
#endif
#endif

#endif
