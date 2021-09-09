/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2019-2020.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#include <common/test.h>
#include <ucg/builtin/plan/builtin_plan.h>
#include <ucg/builtin/ops/builtin_ops.h>
#include <ucg/api/ucg_plan_component.h>
#include "ucg_test.h"
#include "ucg_plan_test.h"

extern "C" {
STATIC_GTEST unsigned ucg_builtin_keep_lowest_1_bit(unsigned num);
STATIC_GTEST unsigned ucg_builtin_keep_highest_1_bit(unsigned num);
STATIC_GTEST unsigned ucg_builtin_get_1bit_cnt(unsigned num);
STATIC_GTEST unsigned ucg_builtin_get_low_all(unsigned num, unsigned cnt);
STATIC_GTEST void ucg_builtin_get_binaryblocks_previous_group(unsigned my_index,
                                                              unsigned member_cnt,
                                                              unsigned *previous_group_process_cnt,
                                                              unsigned *previous_group_begin_index);
STATIC_GTEST void ucg_builtin_get_binaryblocks_current_group(unsigned my_index,
                                                             unsigned member_cnt,
                                                             unsigned *current_group_process_cnt,
                                                             unsigned *current_group_begin_index);
STATIC_GTEST void ucg_builtin_get_binaryblocks_next_group(unsigned my_index,
                                                          unsigned member_cnt,
                                                          unsigned *next_group_process_cnt,
                                                          unsigned *next_group_begin_index);
STATIC_GTEST void ucg_builtin_get_binaryblocks_ahead_group_cnt(unsigned member_cnt,
                                                               unsigned current_group_begin_index,
                                                               unsigned *ahead_group_cnt);
STATIC_GTEST void ucg_builtin_get_binaryblocks_behind_group_cnt(unsigned member_cnt,
                                                                unsigned next_group_begin_index,
                                                                unsigned *behind_group_cnt);
STATIC_GTEST void ucg_builtin_get_extra_reduction_peer_index(unsigned my_index,
                                                             unsigned member_cnt,
                                                             unsigned *peer_index);
STATIC_GTEST ucs_status_t ucg_builtin_get_recv_block_index(unsigned ep_cnt, unsigned ep_idx, unsigned* ret);
STATIC_GTEST void ucg_builtin_block_buffer(unsigned buffer_cnt,
                                           unsigned block_cnt,
                                           unsigned * const block_buffer);
STATIC_GTEST unsigned ucg_builtin_calc_disp(unsigned *block_num, unsigned start, unsigned cnt);
STATIC_GTEST ucs_status_t ucg_builtin_divide_block_buffers(unsigned block_cnt,
                                                           unsigned total_group_process_cnt,
                                                           unsigned total_group_cnt,
                                                           unsigned **block_buffers);
STATIC_GTEST void ucg_builtin_free_block_buffers(unsigned total_group_cnt, unsigned **block_buffers);
STATIC_GTEST ucs_status_t ucg_builtin_reduce_scatter_phase_cb(ucg_builtin_plan_phase_t *phase,
                                                              const ucg_collective_params_t *coll_params);
STATIC_GTEST ucs_status_t ucg_builtin_intra_reduce_scatter(ucg_builtin_index_group_t *index_group,
                                                           ucg_builtin_plan_phase_t **phase,
                                                           ucg_builtin_plan_t *binary_block,
                                                           ucg_builtin_group_ctx_t *ctx,
                                                           ucg_step_idx_t *step_idx);
STATIC_GTEST ucs_status_t ucg_builtin_extra_reduce_receive_cb(ucg_builtin_plan_phase_t *phase,
                                                              const ucg_collective_params_t *coll_params);
STATIC_GTEST ucs_status_t ucg_builtin_extra_reduce_send_cb(ucg_builtin_plan_phase_t *phase,
                                                           const ucg_collective_params_t *coll_params);
STATIC_GTEST ucs_status_t ucg_builtin_intra_extra_reduction(ucg_builtin_index_group_t *index_group,
                                                            ucg_builtin_plan_phase_t **phase,
                                                            ucg_builtin_plan_t *binary_block,
                                                            ucg_builtin_group_ctx_t *ctx,
                                                            ucg_step_idx_t *step_idx);
STATIC_GTEST ucs_status_t ucg_builtin_intra_node_allreduce_cb(ucg_builtin_plan_phase_t *phase,
                                                              const ucg_collective_params_t *coll_params);
STATIC_GTEST ucs_status_t ucg_builtin_extra_receive_bcast_cb(ucg_builtin_plan_phase_t *phase,
                                                             const ucg_collective_params_t *coll_params);
STATIC_GTEST ucs_status_t ucg_builtin_extra_send_bcast_cb(ucg_builtin_plan_phase_t *phase,
                                                          const ucg_collective_params_t *coll_params);
STATIC_GTEST ucs_status_t ucg_builtin_intra_bcast(ucg_builtin_index_group_t *index_group,
                                                  ucg_builtin_plan_phase_t **phase,
                                                  ucg_builtin_plan_t *binary_block,
                                                  ucg_builtin_group_ctx_t *ctx,
                                                  ucg_step_idx_t *step_idx);
STATIC_GTEST ucs_status_t ucg_builtin_extra_allgather_cb(ucg_builtin_plan_phase_t *phase,
                                                         const ucg_collective_params_t *coll_params);
STATIC_GTEST ucs_status_t ucg_builtin_intra_allgather(ucg_builtin_index_group_t *index_group,
                                                      ucg_builtin_plan_phase_t **phase,
                                                      ucg_builtin_plan_t *binary_block,
                                                      ucg_builtin_group_ctx_t *ctx,
                                                      ucg_step_idx_t *step_idx);
STATIC_GTEST ucs_status_t ucg_builtin_binary_block_build(ucg_builtin_plan_t *binary_block,
                                                         ucg_builtin_group_ctx_t *ctx,
                                                         const ucg_builtin_config_t *config,
                                                         ucg_builtin_topo_aware_params_t *params,
                                                         const ucg_group_member_index_t member_cnt);
STATIC_GTEST ucs_status_t ucg_builtin_topo_aware_binary_block_build(ucg_builtin_plan_t *binary_block,
                                                                    ucg_builtin_group_ctx_t *ctx,
                                                                    const ucg_builtin_config_t *config,
                                                                    ucg_builtin_topo_aware_params_t *params,
                                                                    const ucg_group_member_index_t member_cnt);
STATIC_GTEST ucs_status_t ucg_builtin_non_topo_tree_connect_fanout(ucg_builtin_plan_t *tree,
                                                             const ucg_builtin_binomial_tree_params_t *params,
                                                             ucg_group_member_index_t *up,
                                                             unsigned up_cnt,
                                                             ucg_group_member_index_t *down,
                                                             unsigned down_cnt,
                                                             unsigned ppx,
                                                             enum ucg_builtin_plan_method_type fanout_method,
                                                             uct_ep_h **eps,
                                                             ucg_builtin_topology_info_params_t *topo_params);
enum ucg_builtin_plan_method_type method;
}

TEST(ucg_plan_test, test_ring) {
    ucs_status_t ret;
    ucg_plan_test *obj = NULL;
    ucg_builtin_plan_t *plan = NULL;
    ucg_plan_test_data_t data[] = {
        {4, 8, 0},
        {4, 7, 0},
        {1, 2, 0},
    };

    for (unsigned i = 0; i < sizeof(data) / sizeof(data[0]); i++) {
        obj = new ucg_plan_test(data[i].node_cnt, data[i].ppn, data[i].myrank);
        ret = ucg_builtin_ring_create(obj->m_builtin_ctx, UCG_PLAN_RING,
                                      (ucg_builtin_config_t *)obj->m_planc->plan_config,
                                      obj->m_group_params, &obj->m_coll_type, &plan);
        delete obj;

        ASSERT_EQ(UCS_OK, ret);
    }
}

TEST(ucg_plan_test, test_recursive) {
    ucs_status_t ret;
    ucg_plan_test *obj = NULL;
    ucg_builtin_plan_t *plan = NULL;
    ucg_plan_test_data_t data[] = {
        {2, 2, 0}, {2, 2, 1}, {2, 2, 2}, {2, 2, 3},
        {1, 3, 0}, {1, 3, 1}, {1, 3, 2},
    };

    for (unsigned i = 0; i < sizeof(data) / sizeof(data[0]); i++) {
        obj = new ucg_plan_test(data[i].node_cnt, data[i].ppn, data[i].myrank);
        ret = ucg_builtin_recursive_create(obj->m_builtin_ctx, UCG_PLAN_RECURSIVE,
                                           (ucg_builtin_config_t *)obj->m_planc->plan_config,
                                           obj->m_group_params, &obj->m_coll_type, &plan);
        delete obj;

        ASSERT_EQ(UCS_OK, ret);
    }
}

TEST(ucg_plan_test, test_Rabenseifner)
{
    ucs_status_t ret;
    ucg_plan_test *obj = NULL;
    ucg_builtin_plan_t *plan = NULL;
    ucg_builtin_plan_phase_t *phase = new ucg_builtin_plan_phase_t();
    phase->raben_extend.index_group.total_group_cnt = 2;
    phase->raben_extend.index_group.ahead_group_cnt = 0;
    phase->raben_extend.index_group.recv_block_index = 1;
    phase->raben_extend.index_group.total_group_process_cnt = 1;
    phase->raben_extend.index_group.cur_group_process_cnt = 1;
    phase->raben_extend.index_group.next_group_process_cnt = 1;


    ucg_builtin_config_t *config = new ucg_builtin_config_t();
    ucg_plan_test_data_t data[] = {
        {2, 2, 0}, {2, 2, 1}, {2, 2, 2}, {2, 2, 3},
        {1, 3, 0}, {1, 3, 1}, {1, 3, 2},
        {4, 2, 0}, {4, 2, 1}, {4, 2, 2}, {4, 2, 3}, {4, 2, 4}, {4, 2, 5}, {4, 2, 6}, {4, 2, 7},
        {2, 4, 0}, {2, 4, 1}, {2, 4, 2}, {2, 4, 3}, {2, 4, 4}, {2, 4, 5}, {2, 4, 6}, {2, 4, 7},
    };
    config->recursive.factor = 2;
    unsigned previous_group_process_cnt = 1;
    unsigned previous_group_begin_index = 0;
    ASSERT_EQ(1, ucg_builtin_keep_lowest_1_bit(5));
    ASSERT_EQ(4, ucg_builtin_keep_highest_1_bit(5));
    ASSERT_EQ(2, ucg_builtin_get_1bit_cnt(5));
    ASSERT_EQ(2, ucg_builtin_get_low_all(0, 2));
    ucg_builtin_get_binaryblocks_previous_group(0, 2, &previous_group_process_cnt, &previous_group_begin_index);
    ucg_builtin_get_binaryblocks_previous_group(1, 2, &previous_group_process_cnt, &previous_group_begin_index);
    ucg_builtin_get_binaryblocks_current_group(0, 2, &previous_group_process_cnt, &previous_group_begin_index);
    ucg_builtin_get_binaryblocks_current_group(1, 2, &previous_group_process_cnt, &previous_group_begin_index);
    ucg_builtin_get_binaryblocks_next_group(0, 2, &previous_group_process_cnt, &previous_group_begin_index);
    ucg_builtin_get_binaryblocks_next_group(1, 2, &previous_group_process_cnt, &previous_group_begin_index);
    ucg_builtin_get_binaryblocks_ahead_group_cnt(2, 0, &previous_group_process_cnt);
    ucg_builtin_get_binaryblocks_ahead_group_cnt(2, 1, &previous_group_process_cnt);
    ucg_builtin_get_binaryblocks_behind_group_cnt(2, 0, &previous_group_process_cnt);
    ucg_builtin_get_binaryblocks_behind_group_cnt(2, 1, &previous_group_process_cnt);
    ucg_builtin_get_extra_reduction_peer_index(0, 2, &previous_group_process_cnt);
    ucg_builtin_get_extra_reduction_peer_index(1, 2, &previous_group_process_cnt);
    unsigned recv_block_index = 0;
    ASSERT_EQ(UCS_OK, ucg_builtin_get_recv_block_index(8, 3, &recv_block_index));
    ASSERT_EQ(6, recv_block_index);

    unsigned *block_buffer = (unsigned *)malloc(2 * sizeof(unsigned));
    ucg_builtin_block_buffer(2, 2, block_buffer);
    ucg_builtin_divide_block_buffers(2, 2, 2, &block_buffer);
    size_t alloc_size = sizeof(ucg_builtin_plan_t) + 128 * (sizeof(ucg_builtin_plan_phase_t) +
                        (128 * sizeof(uct_ep_h)));
    ucg_builtin_plan_t *binary_block = (ucg_builtin_plan_t*)malloc(alloc_size);
    binary_block->super.my_index = 1;
    binary_block->step_cnt = 1;

    ucg_builtin_index_group_t index_group = {
        .my_index                 = binary_block->super.my_index,
        .cur_group_begin_index    = 0,
        .cur_group_process_cnt    = 1,
        .next_group_begin_index   = 0,
        .next_group_process_cnt   = 0,
        .total_group_process_cnt  = 1,
        .ahead_group_cnt          = 0,
        .behind_group_cnt         = 0,
        .total_group_cnt          = 1,
        .local_group_index        = 0,
        .local_peer_ahead_group   = 0,
        .recv_block_index         = 1
    };
    ucg_step_idx_t step_idx       = binary_block->step_cnt;

    obj = new ucg_plan_test(data[0].node_cnt, data[0].ppn, data[0].myrank);
    ucg_builtin_reduce_scatter_phase_cb(phase, obj->m_coll_params);
    phase->raben_extend.index_group.total_group_cnt = 2;
    phase->raben_extend.index_group.total_group_process_cnt = 1;
    phase->raben_extend.step_index = 0;
    ucg_builtin_extra_reduce_send_cb(phase, obj->m_coll_params);
    ucg_builtin_extra_reduce_receive_cb(phase, obj->m_coll_params);
    ucg_builtin_extra_send_bcast_cb(phase, obj->m_coll_params);
    ucg_builtin_intra_node_allreduce_cb(phase, obj->m_coll_params);
    phase->raben_extend.index_group.cur_group_process_cnt = 4;
    phase->raben_extend.step_index = 1;
    ucg_builtin_extra_allgather_cb(phase, obj->m_coll_params);
    ucg_builtin_extra_receive_bcast_cb(phase, obj->m_coll_params);

    ucg_builtin_intra_allgather(&index_group, &phase, binary_block, obj->m_builtin_ctx, &step_idx);
    ucg_builtin_intra_bcast(&index_group, &phase, binary_block, obj->m_builtin_ctx, &step_idx);
    ucg_builtin_intra_reduce_scatter(&index_group, &phase, binary_block, obj->m_builtin_ctx, &step_idx);

    ucg_algo.topo = 0;
    for (unsigned i = 0; i < sizeof(data) / sizeof(data[0]); i++) {
        obj = new ucg_plan_test(data[i].node_cnt, data[i].ppn, data[i].myrank);
        ret = ucg_builtin_binary_block_create(obj->m_builtin_ctx, UCG_PLAN_BINARY_BLOCK,
                                              config, obj->m_group_params, &obj->m_coll_type, &plan);
        delete obj;
        ASSERT_EQ(UCS_OK, ret);
    }

    ucg_algo.topo = 1;
    for (unsigned topo_level = 0; topo_level < 2; ++topo_level) {
        ucg_algo.topo_level = (ucg_group_hierarchy_level)topo_level;
        obj = new ucg_plan_test(data[0].node_cnt, data[0].ppn, data[0].myrank);
        ret = ucg_builtin_binary_block_create(obj->m_builtin_ctx, UCG_PLAN_BINARY_BLOCK,
                                              config, obj->m_group_params, &obj->m_coll_type, &plan);
        delete obj;
        ASSERT_EQ(UCS_OK, ret);
    }

    delete config;
}

static void ucg_algo_set(int option)
{
    switch (option) {
    case 0:
        ucg_algo.topo = 1;
        ucg_algo.bmtree = 1;
        ucg_algo.recursive = 0;
        ucg_algo.kmtree_intra = 1;
        ucg_algo.kmtree = 1;
        return;

    case 1:
        ucg_algo.topo = 1;
        ucg_algo.bmtree = 1;
        ucg_algo.recursive = 0;
        ucg_algo.kmtree_intra = 0;
        ucg_algo.kmtree = 1;
        return;

    case 2:
        ucg_algo.topo = 1;
        ucg_algo.bmtree = 1;
        ucg_algo.recursive = 0;
        ucg_algo.kmtree_intra = 0;
        ucg_algo.kmtree = 0;
        ucg_algo.topo_level = UCG_GROUP_HIERARCHY_LEVEL_NODE;
        return;

    case 3:
        ucg_algo.topo = 1;
        ucg_algo.bmtree = 1;
        ucg_algo.recursive = 0;
        ucg_algo.kmtree_intra = 0;
        ucg_algo.kmtree = 0;
        return;

    case 4:
        ucg_algo.topo = 1;
        ucg_algo.bmtree = 1;
        ucg_algo.recursive = 0;
        ucg_algo.kmtree_intra = 0;
        ucg_algo.kmtree = 0;
        ucg_algo.topo_level = UCG_GROUP_HIERARCHY_LEVEL_SOCKET;
        return;

    case 5:
        ucg_algo.topo = 1;
        ucg_algo.bmtree = 1;
        ucg_algo.recursive = 0;
        ucg_algo.kmtree_intra = 1;
        ucg_algo.topo_level = UCG_GROUP_HIERARCHY_LEVEL_SOCKET;
        return;

    case 6:
        ucg_algo.topo = 1;
        ucg_algo.bmtree = 1;
        ucg_algo.recursive = 0;
        ucg_algo.kmtree_intra = 1;
        ucg_algo.kmtree = 1;
        ucg_algo.topo_level = UCG_GROUP_HIERARCHY_LEVEL_SOCKET;
        return;

    case 7:
        ucg_algo.topo = 1;
        return;

    case 8:
        ucg_algo.topo = 1;
        ucg_algo.bmtree = 1;
        ucg_algo.recursive = 0;
        ucg_algo.kmtree_intra = 1;
        return;

    case 9:
        ucg_algo.topo = 1;
        ucg_algo.bmtree = 1;
        ucg_algo.recursive = 0;
        ucg_algo.kmtree_intra = 1;
        ucg_algo.kmtree = 1;
        return;

    default:
        return;
    }
}

TEST(ucg_plan_test, test_binomial_tree) {
    unsigned i;
    ucs_status_t ret;
    ucg_plan_test *obj = NULL;
    ucg_builtin_plan_t *plan = NULL;
    ucg_plan_test_data_algo_t fanout[] = {
        {{4, 8, 0}, -1},
        {{1, 3, 0}, -1},
        {{1, 3, 1}, -1},
        {{1, 3, 2}, -1},
        {{4, 8, 0}, 0},
        {{4, 8, 0}, 1},
        {{2, 1, 0}, 2},
    };
    ucg_plan_test_data_algo_t fanin_out[] = {
        {{3, 8, 0}, 3},
        {{4, 8, 0}, 4},
        {{4, 8, 0}, 5},
        {{4, 8, 0}, 6},
        {{4, 7, 0}, 7},
        {{4, 8, 0}, 8},
        {{4, 8, 0}, 9},
    };

    for (i = 0; i < sizeof(fanout) / sizeof(fanout[0]); i++) {
        obj = new ucg_plan_test(fanout[i].data.node_cnt, fanout[i].data.ppn, fanout[i].data.myrank);
        ucg_algo_set(fanout[i].algo_id);
        ret = ucg_builtin_binomial_tree_create(obj->m_builtin_ctx, UCG_PLAN_TREE_FANOUT,
                                               (ucg_builtin_config_t *)obj->m_planc->plan_config,
                                               obj->m_group_params, &obj->m_coll_type, &plan);
        delete obj;

        ASSERT_EQ(UCS_OK, ret);
    }

    for (i = 0; i < sizeof(fanin_out) / sizeof(fanin_out[0]); i++) {
        obj = new ucg_plan_test(fanin_out[i].data.node_cnt, fanin_out[i].data.ppn, fanin_out[i].data.myrank);
        ucg_algo_set(fanin_out[i].algo_id);
        ret = ucg_builtin_binomial_tree_create(obj->m_builtin_ctx, UCG_PLAN_TREE_FANIN_FANOUT,
                                               (ucg_builtin_config_t *)obj->m_planc->plan_config,
                                               obj->m_group_params, &obj->m_coll_type, &plan);
        delete obj;

        ASSERT_EQ(UCS_OK, ret);
    }

    ucg_builtin_non_topo_tree_connect_fanout(NULL, NULL, NULL, 0, NULL, 0, 0, method, NULL, NULL);
}
