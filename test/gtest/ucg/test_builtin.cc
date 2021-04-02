
/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2019-2020.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

extern "C" {
#include <ucg/builtin/plan/builtin_plan.h>
}

#include "ucg_test.h"

class test_ucg_builtin_4_8_0 : public test_ucg_group_base {
public:
    test_ucg_builtin_4_8_0() : test_ucg_group_base(4, 8, 0) {}
    virtual ~test_ucg_builtin_4_8_0() {}

protected:
    ucg_plan_component_t *m_builtin_comp;
    ucg_group_ctx_h       m_builtin_gctx;
};

typedef struct {
    size_t node_cnt;
    size_t ppn;
    unsigned myrank;
} ucg_plan_test_data_t;

typedef struct {
    ucg_plan_test_data_t data;
    int algo_id;
} ucg_plan_test_data_algo_t;

//test_ucg_plan::test_ucg_plan(size_t node_cnt, size_t ppn, unsigned myrank)
//{
//    m_planc = NULL;
//    m_all_rank_infos.clear();
//    m_resource_factory->create_balanced_rank_info(m_all_rank_infos, node_cnt, ppn);
//    m_group_params = m_resource_factory->create_group_params(m_all_rank_infos[myrank], m_all_rank_infos);
//    m_group = m_resource_factory->create_group(m_group_params, m_ucg_worker);
//    m_coll_params = m_resource_factory->create_collective_params(UCG_GROUP_COLLECTIVE_MODIFIER_SINGLE_SOURCE,
//                                                                 0, NULL, 1, NULL, 4, NULL, NULL);
//    m_coll_type.modifiers = UCG_GROUP_COLLECTIVE_MODIFIER_AGGREGATE;
//    m_coll_type.root = 0;
//    ucg_plan_select(m_group, NULL, m_coll_params, &m_planc);
//    m_builtin_ctx = (ucg_builtin_group_ctx_t *)UCG_GROUP_TO_COMPONENT_CTX(ucg_builtin_component, m_group);
//}


UCS_TEST_P(test_ucg_builtin_4_8_0, ucg_builtin_small_message)
{
    ucg_plan_t *plan;
    ucg_collective_params_t coll_params;

    coll_params.send.count = UCG_GROUP_MED_MSG_SIZE - 1024;

    EXPECT_EQ(UCS_OK, m_builtin_comp->plan(m_builtin_gctx, &coll_params, &plan));
}

UCS_TEST_P(test_ucg_builtin_4_8_0, ucg_builtin_large_message)
{
    ucg_plan_t *plan;
    ucg_collective_params_t coll_params;

    coll_params.send.count = UCG_GROUP_MED_MSG_SIZE + 1024;

    EXPECT_EQ(UCS_OK, m_builtin_comp->plan(m_builtin_gctx, &coll_params, &plan));
}

UCS_TEST_P(test_ucg_builtin_4_8_0, algorithm_selection)
{
    ucs_status_t ret;
    unsigned idx;
    for (idx = 0; idx < UCG_ALGORITHM_ALLREDUCE_LAST; idx++) {
        ret = ucg_builtin_allreduce_algo_switch((enum ucg_builtin_allreduce_algorithm) idx, &ucg_algo);
        ASSERT_EQ(UCS_OK, ret);
    }

    for (idx = 0; idx < UCG_ALGORITHM_BARRIER_LAST; idx++) {
        ret = ucg_builtin_barrier_algo_switch((enum ucg_builtin_barrier_algorithm) idx, &ucg_algo);
        ASSERT_EQ(UCS_OK, ret);
    }

    for (idx = 0; idx < UCG_ALGORITHM_BCAST_LAST; idx++) {
        ret = ucg_builtin_bcast_algo_switch((enum ucg_builtin_bcast_algorithm) idx, &ucg_algo);
        ASSERT_EQ(UCS_OK, ret);
    }

}

UCS_TEST_P(test_ucg_builtin_4_8_0, topo_level)
{
    ucs_status_t ret;
    ucg_algo.topo_level = UCG_GROUP_HIERARCHY_LEVEL_NODE;
    enum ucg_group_member_distance domain_distance = UCG_GROUP_MEMBER_DISTANCE_NONE;
    ret = choose_distance_from_topo_aware_level(&domain_distance);
    ASSERT_EQ(UCS_OK, ret);
    ucg_algo.topo_level = UCG_GROUP_HIERARCHY_LEVEL_SOCKET;
    ret = choose_distance_from_topo_aware_level(&domain_distance);
    ASSERT_EQ(UCS_OK, ret);
    ucg_algo.topo_level = UCG_GROUP_HIERARCHY_LEVEL_L3CACHE;
    ret = choose_distance_from_topo_aware_level(&domain_distance);
    ASSERT_EQ(UCS_OK, ret);
}

UCS_TEST_P(test_ucg_builtin_4_8_0, check_continus_number)
{
    ucg_params_t job_params;

    //group_params.member_count = 4;
    job_params.job_info.info_type = UCG_TOPO_INFO_PLACEMENT_TABLE;
    job_params.job_info.placement[0] = new uint16_t[4] {UCG_GROUP_MEMBER_DISTANCE_NONE, UCG_GROUP_MEMBER_DISTANCE_HOST, UCG_GROUP_MEMBER_DISTANCE_UNKNOWN, UCG_GROUP_MEMBER_DISTANCE_UNKNOWN};
    job_params.job_info.placement[1] = new uint16_t[4] {UCG_GROUP_MEMBER_DISTANCE_HOST, UCG_GROUP_MEMBER_DISTANCE_NONE, UCG_GROUP_MEMBER_DISTANCE_UNKNOWN, UCG_GROUP_MEMBER_DISTANCE_UNKNOWN};
    job_params.job_info.placement[2] = new uint16_t[4] {UCG_GROUP_MEMBER_DISTANCE_UNKNOWN, UCG_GROUP_MEMBER_DISTANCE_UNKNOWN, UCG_GROUP_MEMBER_DISTANCE_NONE, UCG_GROUP_MEMBER_DISTANCE_HOST};
    job_params.job_info.placement[3] = new uint16_t[4] {UCG_GROUP_MEMBER_DISTANCE_UNKNOWN, UCG_GROUP_MEMBER_DISTANCE_UNKNOWN, UCG_GROUP_MEMBER_DISTANCE_HOST, UCG_GROUP_MEMBER_DISTANCE_NONE};
    // TODO: apply these params to the job...

    unsigned discount = 0;
    ucs_status_t status = ucg_builtin_check_continuous_number(&m_group_params, UCG_GROUP_MEMBER_DISTANCE_HOST, &discount);
    ASSERT_EQ(UCS_OK, status);
    ASSERT_EQ(0u, discount);

    job_params.job_info.placement[0] = new uint16_t[4] {UCG_GROUP_MEMBER_DISTANCE_NONE, UCG_GROUP_MEMBER_DISTANCE_HOST, UCG_GROUP_MEMBER_DISTANCE_HOST, UCG_GROUP_MEMBER_DISTANCE_SOCKET};
    job_params.job_info.placement[1] = new uint16_t[4] {UCG_GROUP_MEMBER_DISTANCE_HOST, UCG_GROUP_MEMBER_DISTANCE_NONE, UCG_GROUP_MEMBER_DISTANCE_SOCKET, UCG_GROUP_MEMBER_DISTANCE_HOST};
    job_params.job_info.placement[2] = new uint16_t[4] {UCG_GROUP_MEMBER_DISTANCE_HOST, UCG_GROUP_MEMBER_DISTANCE_SOCKET, UCG_GROUP_MEMBER_DISTANCE_NONE, UCG_GROUP_MEMBER_DISTANCE_HOST};
    job_params.job_info.placement[3] = new uint16_t[4] {UCG_GROUP_MEMBER_DISTANCE_SOCKET, UCG_GROUP_MEMBER_DISTANCE_HOST, UCG_GROUP_MEMBER_DISTANCE_HOST, UCG_GROUP_MEMBER_DISTANCE_NONE};
    // TODO: apply these params to the job...

    discount = 0;
    status = ucg_builtin_check_continuous_number(&m_group_params, UCG_GROUP_MEMBER_DISTANCE_SOCKET, &discount);
    ASSERT_EQ(UCS_OK, status);
    ASSERT_EQ(1u, discount);

    ASSERT_EQ(job_params.job_info.info_type, UCG_TOPO_INFO_PLACEMENT_TABLE); // TODO: remove
}

UCS_TEST_P(test_ucg_builtin_4_8_0, choose_type)
{

    enum ucg_collective_modifiers flags[] = {
            UCG_GROUP_COLLECTIVE_MODIFIER_SINGLE_SOURCE,
            UCG_GROUP_COLLECTIVE_MODIFIER_SINGLE_DESTINATION,
            UCG_GROUP_COLLECTIVE_MODIFIER_AGGREGATE,
            UCG_GROUP_COLLECTIVE_MODIFIER_AGGREGATE,
            UCG_GROUP_COLLECTIVE_MODIFIER_AGGREGATE
    };

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


class test_ucg_builtin_2_2_0 : public test_ucg_group_base {
public:
    test_ucg_builtin_2_2_0() : test_ucg_group_base(2, 2, 0) {}
    virtual ~test_ucg_builtin_2_2_0() {}
};

/* TODO: add verification to below functions */
/*
UCS_TEST_P(test_ucg_builtin_2_2_0, plan_decision_in_discontinuous_case)
{
    unsigned op_num = 3;
    enum ucg_collective_modifiers modifiers[op_num] = { (enum ucg_collective_modifiers ) (UCG_GROUP_COLLECTIVE_MODIFIER_BROADCAST | UCG_GROUP_COLLECTIVE_MODIFIER_SINGLE_SOURCE), \
        (enum ucg_collective_modifiers) (UCG_GROUP_COLLECTIVE_MODIFIER_AGGREGATE | UCG_GROUP_COLLECTIVE_MODIFIER_BROADCAST | UCG_GROUP_COLLECTIVE_MODIFIER_BARRIER), \
        (enum ucg_collective_modifiers) (UCG_GROUP_COLLECTIVE_MODIFIER_AGGREGATE | UCG_GROUP_COLLECTIVE_MODIFIER_BROADCAST) };
    unsigned size_num = 2;
    size_t msg_size[size_num] = {UCG_GROUP_MED_MSG_SIZE - 10, UCG_GROUP_MED_MSG_SIZE + 10};
    for (unsigned i = 0; i < op_num; i++) {
        for (unsigned j = 0; j < size_num; j++) {
            ucg_builtin_plan_decision_in_discontinuous_case(msg_size[j], m_group_params, modifiers[i], example.m_coll_params);
        }
    }
}
*/

UCS_TEST_P(test_ucg_builtin_2_2_0, plan_decision_fixed)
{
    ucg_collective_params_t coll_params;
    unsigned op_num = 3;
    enum ucg_collective_modifiers modifiers[op_num] = { (enum ucg_collective_modifiers ) (UCG_GROUP_COLLECTIVE_MODIFIER_BROADCAST | UCG_GROUP_COLLECTIVE_MODIFIER_SINGLE_SOURCE), \
        (enum ucg_collective_modifiers) (UCG_GROUP_COLLECTIVE_MODIFIER_AGGREGATE | UCG_GROUP_COLLECTIVE_MODIFIER_BROADCAST | UCG_GROUP_COLLECTIVE_MODIFIER_BARRIER), \
        (enum ucg_collective_modifiers) (UCG_GROUP_COLLECTIVE_MODIFIER_AGGREGATE | UCG_GROUP_COLLECTIVE_MODIFIER_BROADCAST) };
    unsigned size_num = 2;
    size_t msg_size[size_num] = {UCG_GROUP_MED_MSG_SIZE - 10, UCG_GROUP_MED_MSG_SIZE + 10};
    unsigned data_num = 2;
    unsigned large_data[data_num] = {100, 10000};
    coll_params.send.count = 200;
    enum ucg_builtin_bcast_algorithm bcast_algo_decision;
    enum ucg_builtin_allreduce_algorithm allreduce_algo_decision;
    enum ucg_builtin_barrier_algorithm barrier_algo_decision;
    for (unsigned i = 0; i < op_num; i++) {
        for (unsigned j = 0; j < size_num; j++) {
            for (unsigned k = 0; k < data_num; k++) {
                plan_decision_fixed(msg_size[j], &m_group_params, modifiers[i], &coll_params, large_data[k], 0, &bcast_algo_decision, &allreduce_algo_decision, &barrier_algo_decision);
            }
        }
    }
}

UCS_TEST_P(test_ucg_builtin_2_2_0, plan_chooose_ops)
{
    unsigned op_num = 3;
    enum ucg_collective_modifiers modifiers[op_num] = {
        (enum ucg_collective_modifiers) (UCG_GROUP_COLLECTIVE_MODIFIER_BROADCAST | UCG_GROUP_COLLECTIVE_MODIFIER_SINGLE_SOURCE),
        (enum ucg_collective_modifiers) (UCG_GROUP_COLLECTIVE_MODIFIER_AGGREGATE | UCG_GROUP_COLLECTIVE_MODIFIER_BROADCAST | UCG_GROUP_COLLECTIVE_MODIFIER_BARRIER),
        (enum ucg_collective_modifiers) (UCG_GROUP_COLLECTIVE_MODIFIER_AGGREGATE | UCG_GROUP_COLLECTIVE_MODIFIER_BROADCAST)
    };

    ucg_builtin_config_t config = {0}; // TODO

    for (unsigned i = 0; i < op_num; i++) {
            ucg_builtin_plan_choose_ops(&config, modifiers[i]);
    }
}

UCS_TEST_P(test_ucg_builtin_2_2_0, test_algorithm_decision)
{
    ucg_collective_type_t coll_type = {0}; // TODO
    ucg_builtin_config_t config = {0}; // TODO
    ucg_collective_params_t coll_params = {0}; // TODO
    ASSERT_EQ(UCS_OK, ucg_builtin_algorithm_decision(&coll_type, 1024, &m_group_params, &coll_params, &config));
}

UCG_INSTANTIATE_TEST_CASE(test_ucg_builtin_4_8_0)
UCG_INSTANTIATE_TEST_CASE(test_ucg_builtin_2_2_0)
