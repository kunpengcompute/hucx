/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2019-2020.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */


#include <ucg/builtin/plan/builtin_plan.h>
#include "ucg_test.h"
#include "ucg_plan_test.h"

ucg_plan_test::ucg_plan_test()
{
    m_builtin_ctx = NULL;
    m_planc = NULL;
    m_group_params = NULL;
    m_coll_params = NULL;
    m_group = NULL;
    m_all_rank_infos.clear();
}

ucg_plan_test::ucg_plan_test(size_t node_cnt, size_t ppn, unsigned myrank)
{
    m_planc = NULL;
    m_all_rank_infos.clear();
    m_resource_factory->create_balanced_rank_info(m_all_rank_infos, node_cnt, ppn);
    m_group_params = m_resource_factory->create_group_params(m_all_rank_infos[myrank], m_all_rank_infos);
    m_group = m_resource_factory->create_group(m_group_params, m_ucg_worker);
    m_coll_params = m_resource_factory->create_collective_params(UCG_GROUP_COLLECTIVE_MODIFIER_SINGLE_SOURCE,
                                                                 0, NULL, 1, NULL, 4, NULL, NULL);
    m_coll_type.modifiers = UCG_GROUP_COLLECTIVE_MODIFIER_AGGREGATE;
    m_coll_type.root = 0;
    ucg_plan_select(m_group, NULL, m_coll_params, &m_planc);
    m_builtin_ctx = (ucg_builtin_group_ctx_t *)UCG_GROUP_TO_COMPONENT_CTX(ucg_builtin_component, m_group);
}

ucg_plan_test::~ucg_plan_test()
{
    if (m_coll_params != NULL) {
        delete m_coll_params;
        m_coll_params = NULL;
    }

    ucg_group_destroy(m_group);

    if (m_group_params != NULL) {
        delete m_group_params;
        m_group_params = NULL;
    }

    m_all_rank_infos.clear();
}

extern "C" {
    STATIC_GTEST void ucg_builtin_set_algo(coll_type_t ctype, int algo_id, ucg_builtin_algo_t *algo);
    STATIC_GTEST void ucg_builtin_print(ucg_plan_t *plan, const ucg_collective_params_t *coll_params);
    STATIC_GTEST void ucg_builtin_set_phase_thresholds(ucg_builtin_group_ctx_t *ctx,
        ucg_builtin_plan_phase_t *phase);
}

TEST(ucg_plan_test, ucg_plan_1_test) {
    ucg_plan_test example(4, 8, 0);

    ucg_plan_t *plan = NULL;
    ucs_status_t ret = ucg_builtin_component.create(&ucg_builtin_component, example.m_ucg_worker,
                                                    example.m_group, 23, 0, NULL, example.m_group_params);
    EXPECT_EQ(UCS_OK, ret);
    ret = ucg_builtin_component.plan(example.m_group, 2, example.m_coll_params, &plan);
    EXPECT_EQ(UCS_OK, ret);

}

TEST(ucg_plan_test, ucg_plan_2_test) {
    ucg_plan_test example(4, 8, 0);

    ucg_plan_t *plan = NULL;
    ucs_status_t ret = ucg_builtin_component.create(&ucg_builtin_component, example.m_ucg_worker,
                                                    example.m_group, 23, 0, NULL, example.m_group_params);
    EXPECT_EQ(UCS_OK, ret);
    ret = ucg_builtin_component.plan(example.m_group, 2, example.m_coll_params, &plan);
    EXPECT_EQ(UCS_OK, ret);
}

TEST(ucg_plan_test, ucg_plan_3_test) {
    ucg_plan_test example(4, 8, 0);

    ucg_plan_t *plan = NULL;
    ucs_status_t ret = ucg_builtin_component.create(&ucg_builtin_component, example.m_ucg_worker,
                                                    example.m_group, 23, 0, NULL, example.m_group_params);
    EXPECT_EQ(UCS_OK, ret);
    ret = ucg_builtin_component.plan(example.m_group, 2, example.m_coll_params, &plan);
    EXPECT_EQ(UCS_OK, ret);
}
/*
TEST(ucg_plan_test, ucg_plan_4_test) {
    ucg_plan_test example(4, 8, 0);
    size_t msg_size = UCG_GROUP_MED_MSG_SIZE + 1024;

    ucg_plan_t *plan;
    ucs_status_t ret = ucg_builtin_component.create(&ucg_builtin_component, example.m_ucg_worker,
                                                    example.m_group, 23, 0, NULL, example.m_group_params);
    EXPECT_EQ(UCS_OK, ret);
    ret = ucg_builtin_component.plan(&ucg_builtin_component, &example.m_coll_type, msg_size,
                                     example.m_group, example.m_coll_params, &plan);
    EXPECT_EQ(UCS_OK, ret);
}
*/
TEST(ucg_plan_test, algorithm_selection) {
    unsigned idx;
    for (idx = 0; idx < UCG_ALGORITHM_ALLREDUCE_LAST; idx++) {
        ucg_builtin_allreduce_algo_switch((enum ucg_builtin_allreduce_algorithm) idx, &ucg_algo);
    }

    for (idx = 0; idx < UCG_ALGORITHM_BARRIER_LAST; idx++) {
        ucg_builtin_barrier_algo_switch((enum ucg_builtin_barrier_algorithm) idx, &ucg_algo);
    }

    for (idx = 0; idx < UCG_ALGORITHM_BCAST_LAST; idx++) {
        ucg_builtin_bcast_algo_switch((enum ucg_builtin_bcast_algorithm) idx, &ucg_algo);
    }

    for (idx = 0; idx < UCG_ALGORITHM_ALLTOALLV_LAST; idx++) {
        ucg_builtin_alltoallv_algo_switch((enum ucg_builtin_alltoallv_algorithm) idx, &ucg_algo);
    }
}

TEST(ucg_plan_test, topo_level) {
    ucg_algo.topo_level = UCG_GROUP_HIERARCHY_LEVEL_NODE;
    enum ucg_group_member_distance domain_distance = UCG_GROUP_MEMBER_DISTANCE_SELF;
    choose_distance_from_topo_aware_level(&domain_distance);
    ucg_algo.topo_level = UCG_GROUP_HIERARCHY_LEVEL_SOCKET;
    choose_distance_from_topo_aware_level(&domain_distance);
    ucg_algo.topo_level = UCG_GROUP_HIERARCHY_LEVEL_L3CACHE;
    choose_distance_from_topo_aware_level(&domain_distance);
}

TEST(ucg_plan_test, check_continus_number) {
    ucg_group_params_t group_params;

    group_params.member_count = 4;
    group_params.topo_args.nrank_uncontinue = 0;
    group_params.topo_args.srank_uncontinue = 1;

    unsigned discount = 0;
    ucs_status_t status = ucg_builtin_check_continuous_number(&group_params, UCG_GROUP_MEMBER_DISTANCE_HOST, &discount);
    ASSERT_EQ(UCS_OK, status);
    ASSERT_EQ(0u, discount);

    discount = 0;
    status = ucg_builtin_check_continuous_number(&group_params, UCG_GROUP_MEMBER_DISTANCE_SOCKET, &discount);
    ASSERT_EQ(UCS_OK, status);
    ASSERT_EQ(1u, discount);
}

TEST(ucg_plan_test, choose_type) {

    enum ucg_collective_modifiers flags[] = \
            { UCG_GROUP_COLLECTIVE_MODIFIER_SINGLE_SOURCE, UCG_GROUP_COLLECTIVE_MODIFIER_SINGLE_DESTINATION, \
            UCG_GROUP_COLLECTIVE_MODIFIER_AGGREGATE, UCG_GROUP_COLLECTIVE_MODIFIER_AGGREGATE, UCG_GROUP_COLLECTIVE_MODIFIER_AGGREGATE, \
            UCG_GROUP_COLLECTIVE_MODIFIER_ALLGATHER, UCG_GROUP_COLLECTIVE_MODIFIER_ALLGATHER};

    enum ucg_builtin_plan_topology_type expect_result[] = {UCG_PLAN_TREE_FANOUT, UCG_PLAN_TREE_FANIN, \
            UCG_PLAN_RECURSIVE, UCG_PLAN_RING, UCG_PLAN_TREE_FANIN_FANOUT, \
            UCG_PLAN_BRUCK, UCG_PLAN_RECURSIVE};
    enum ucg_builtin_plan_topology_type  ret_type;
    /* TODO */
    unsigned case_num = 7;
    for (unsigned i = 0; i < case_num; i++) {

        switch (i)
        {
        case 2:
            ucg_algo.recursive = 1;
            ucg_algo.ring      = 0;
            ucg_algo.bruck     = 0;
            break;
        case 3:
            ucg_algo.recursive = 0;
            ucg_algo.ring      = 1;
            ucg_algo.bruck     = 0;
            break;
        case 5:
            ucg_algo.recursive = 0;
            ucg_algo.ring      = 0;
            ucg_algo.bruck     = 1;
            break;
        default:
            ucg_algo.recursive = 0;
            ucg_algo.ring      = 0;
            ucg_algo.bruck     = 0;
            break;
        }

        ret_type = ucg_builtin_choose_type(flags[i]);
        ASSERT_EQ(expect_result[i], ret_type);
    }
}

TEST(ucg_plan_test, set_algo) {
    coll_type_t ctype[] = {COLL_TYPE_BARRIER, COLL_TYPE_BCAST, COLL_TYPE_ALLREDUCE, COLL_TYPE_ALLTOALLV};
    for (int i = 0; i < sizeof(ctype) / sizeof(ctype[0]); i++) {
        ucg_builtin_set_algo(ctype[i], 0, &ucg_algo);
    }
}

TEST(ucg_plan_test, builtin_plan) {
    ucg_plan_t *plan = new ucg_plan_t;
    plan->planner = &ucg_builtin_component;
    ucg_collective_params_t *coll_params = NULL;
    ucg_builtin_print(plan, coll_params);
}

TEST(ucg_plan_test, set_phase_thresholds) {
    ucg_plan_test example(4, 8, 0);
    ucg_builtin_plan_phase_t plan_phase;

    uct_iface_attr_t *ep_attr = new uct_iface_attr_t();
    plan_phase.ep_attr = ep_attr;

    uct_md_attr_t *md_attr = new uct_md_attr_t();
    md_attr->cap.max_reg = 8128;
    plan_phase.md_attr = md_attr;

    plan_phase.send_thresh.max_short_one = 31;
    plan_phase.send_thresh.max_short_max = 63;
    plan_phase.send_thresh.max_bcopy_one = 127;
    plan_phase.send_thresh.max_bcopy_max = 255;
    plan_phase.send_thresh.max_zcopy_one = 1023;
    plan_phase.send_thresh.md_attr_cap_max_reg = 8127;

    plan_phase.recv_thresh.max_short_one = plan_phase.send_thresh.max_short_one;
    plan_phase.recv_thresh.max_short_max = plan_phase.send_thresh.max_short_max;
    plan_phase.recv_thresh.max_bcopy_one = plan_phase.send_thresh.max_bcopy_one;
    plan_phase.recv_thresh.max_bcopy_max = plan_phase.send_thresh.max_bcopy_max;
    plan_phase.recv_thresh.max_zcopy_one = plan_phase.send_thresh.max_zcopy_one;
    plan_phase.recv_thresh.md_attr_cap_max_reg = 8127;

    ucg_builtin_set_phase_thresholds(example.m_builtin_ctx, &plan_phase);

    delete ep_attr;
}

