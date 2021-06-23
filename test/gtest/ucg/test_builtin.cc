
/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2019-2020.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#include "ucg_test.h"

extern "C" {
#include <ucg/builtin/plan/builtin_plan.h>
#include <ucg/builtin/ops/builtin_comp_step.inl>
}

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
    //group_params.member_count = 4;
    m_group_params.distance_type = UCG_GROUP_DISTANCE_TYPE_PLACEMENT;
    m_group_params.placement[0] = new uint16_t[4] {UCG_GROUP_MEMBER_DISTANCE_NONE, UCG_GROUP_MEMBER_DISTANCE_HOST, UCG_GROUP_MEMBER_DISTANCE_UNKNOWN, UCG_GROUP_MEMBER_DISTANCE_UNKNOWN};
    m_group_params.placement[1] = new uint16_t[4] {UCG_GROUP_MEMBER_DISTANCE_HOST, UCG_GROUP_MEMBER_DISTANCE_NONE, UCG_GROUP_MEMBER_DISTANCE_UNKNOWN, UCG_GROUP_MEMBER_DISTANCE_UNKNOWN};
    m_group_params.placement[2] = new uint16_t[4] {UCG_GROUP_MEMBER_DISTANCE_UNKNOWN, UCG_GROUP_MEMBER_DISTANCE_UNKNOWN, UCG_GROUP_MEMBER_DISTANCE_NONE, UCG_GROUP_MEMBER_DISTANCE_HOST};
    m_group_params.placement[3] = new uint16_t[4] {UCG_GROUP_MEMBER_DISTANCE_UNKNOWN, UCG_GROUP_MEMBER_DISTANCE_UNKNOWN, UCG_GROUP_MEMBER_DISTANCE_HOST, UCG_GROUP_MEMBER_DISTANCE_NONE};
    // TODO: apply these params to the job...

    unsigned discount = 0;
    ucs_status_t status = ucg_builtin_check_continuous_number(&m_group_params, UCG_GROUP_MEMBER_DISTANCE_HOST, &discount);
    ASSERT_EQ(UCS_OK, status);
    ASSERT_EQ(0u, discount);

    m_group_params.placement[0] = new uint16_t[4] {UCG_GROUP_MEMBER_DISTANCE_NONE, UCG_GROUP_MEMBER_DISTANCE_HOST, UCG_GROUP_MEMBER_DISTANCE_HOST, UCG_GROUP_MEMBER_DISTANCE_SOCKET};
    m_group_params.placement[1] = new uint16_t[4] {UCG_GROUP_MEMBER_DISTANCE_HOST, UCG_GROUP_MEMBER_DISTANCE_NONE, UCG_GROUP_MEMBER_DISTANCE_SOCKET, UCG_GROUP_MEMBER_DISTANCE_HOST};
    m_group_params.placement[2] = new uint16_t[4] {UCG_GROUP_MEMBER_DISTANCE_HOST, UCG_GROUP_MEMBER_DISTANCE_SOCKET, UCG_GROUP_MEMBER_DISTANCE_NONE, UCG_GROUP_MEMBER_DISTANCE_HOST};
    m_group_params.placement[3] = new uint16_t[4] {UCG_GROUP_MEMBER_DISTANCE_SOCKET, UCG_GROUP_MEMBER_DISTANCE_HOST, UCG_GROUP_MEMBER_DISTANCE_HOST, UCG_GROUP_MEMBER_DISTANCE_NONE};
    // TODO: apply these params to the job...

    discount = 0;
    status = ucg_builtin_check_continuous_number(&m_group_params, UCG_GROUP_MEMBER_DISTANCE_SOCKET, &discount);
    ASSERT_EQ(UCS_OK, status);
    ASSERT_EQ(1u, discount);
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

#define RANKS (10)
#define DT_COUNT (5)
#define VECTOR_SLOTS (3)
#define ALIGNED_STRIDE (8)
#define UNALIGNED_STRIDE (9)

class test_ucg_builtin_cb : public ucg_test {
public:
    enum test_datatype {
        DATATYPE_INT = 0,

        DATATYPE_CONTIG_VECTOR,
        DATATYPE_STRIDED_VECTOR_ALIGNED,
        DATATYPE_STRIDED_VECTOR_UNALIGNED,

        DATATYPE_CUSTOM_STRUCT,
        DATATYPE_CUSTOM_STRUCT_VECTOR_ALIGNED,
        DATATYPE_CUSTOM_STRUCT_VECTOR_UNALIGNED,

        DATATYPE_LAST
    };

    enum test_operator {
        OPERATOR_SUM = 0,
        OPERATOR_MAX,

        OPERATOR_LAST
    };

    struct custom {
        char first;
        int second;
        void *third;
        int fourth;
        void *fifth;
    };

    void iterate_by_datatype(int *buf, enum test_datatype datatype, int value, int is_set) {
        unsigned idx;

        switch (datatype) {
        case DATATYPE_INT:
            if (is_set) {
                *buf = value;
            } else {
                EXPECT_EQ(*buf, value);
            }
            break;

        case DATATYPE_CONTIG_VECTOR:
            for (idx = 0; idx < VECTOR_SLOTS; idx++) {
                iterate_by_datatype(buf + idx, DATATYPE_INT, value + idx, is_set);
            }
            break;

        case DATATYPE_STRIDED_VECTOR_ALIGNED:
            for (idx = 0; idx < VECTOR_SLOTS; idx++) {
                iterate_by_datatype(buf, DATATYPE_INT, value + idx, is_set);
                buf = (int*)UCS_PTR_BYTE_OFFSET(buf, ALIGNED_STRIDE);
            }
            break;

        case DATATYPE_STRIDED_VECTOR_UNALIGNED:
            for (idx = 0; idx < VECTOR_SLOTS; idx++) {
                iterate_by_datatype(buf, DATATYPE_INT, value + idx, is_set);
                buf = (int*)UCS_PTR_BYTE_OFFSET(buf, DATATYPE_STRIDED_VECTOR_UNALIGNED);
            }
            break;

        case DATATYPE_CUSTOM_STRUCT:
            iterate_by_datatype(&((struct custom*)buf)->second, DATATYPE_INT, value, is_set);
            iterate_by_datatype(&((struct custom*)buf)->fourth, DATATYPE_INT, value + 1, is_set);
            break;

        case DATATYPE_CUSTOM_STRUCT_VECTOR_ALIGNED:
            for (idx = 0; idx < VECTOR_SLOTS; idx++) {
                iterate_by_datatype(buf, DATATYPE_CUSTOM_STRUCT, value + idx, is_set);
                buf = (int*)UCS_PTR_BYTE_OFFSET(buf, sizeof(struct custom));
            }
            break;

        case DATATYPE_CUSTOM_STRUCT_VECTOR_UNALIGNED:
            for (idx = 0; idx < VECTOR_SLOTS; idx++) {
                iterate_by_datatype(buf, DATATYPE_CUSTOM_STRUCT, value + idx, is_set);
                buf = (int*)UCS_PTR_BYTE_OFFSET(buf, sizeof(struct custom) + 1);
            }
            break;

        case DATATYPE_LAST:
            break;
        }
    }

    int calc_value(enum test_operator op, unsigned ranks)
    {
        int idx, agg = 0;

        /* calculate the expected value */
        for (idx = 0; idx < ranks; idx++) {
            switch (op) {
            case OPERATOR_SUM:
                agg += idx + 1;
                break;
            case OPERATOR_MAX:
                agg = ucs_max(agg, idx + 1);
                break;
            case OPERATOR_LAST:
                break;
            }
        }

        return agg;
    }

    void test_all_datatypes(ucg_builtin_op_step_t *step) {
        unsigned dt, rank;
        ucg_builtin_header_t header = {0};
        ucg_builtin_request_t req   = {0};

        /* calculate the length used by DATATYPE_CUSTOM_STRUCT_VECTOR_UNALIGNED */
        int cb_ret = 0;
        size_t max_length = (DT_COUNT * (sizeof(struct custom) + 1)) / sizeof(int);
        int agg           = calc_value(OPERATOR_SUM, RANKS);
        int *src          = new int[max_length];
        int *dst          = new int[max_length];
        req.step          = step;

        for (dt = 0; dt < (int)DATATYPE_LAST; dt++) {
            memset(dst, 0, max_length);
            iterate_by_datatype(src, (enum test_datatype)dt, agg, 1);

            for (rank = 0; rank < RANKS; rank++) {
                EXPECT_EQ(cb_ret, 0); /* operation still incomplete */
                cb_ret = ucg_builtin_step_recv_cb(&req, header, (uint8_t*)src, max_length, 0);
            }

            EXPECT_EQ(cb_ret, 1); /* operation is now complete! */

            iterate_by_datatype(dst, (enum test_datatype)dt, agg, 0);
        }
    }
};

UCS_TEST_F(test_ucg_builtin_cb, test_method_reduce)
{
    ucg_builtin_op_step_t step = {
        .flags            = (enum ucg_builtin_op_step_flags)0,
        .comp_flags       = (enum ucg_builtin_op_step_comp_flags)0,
        .comp_aggregation = UCG_BUILTIN_OP_STEP_COMP_AGGREGATE_REDUCE,
        .comp_criteria    = UCG_BUILTIN_OP_STEP_COMP_CRITERIA_MULTIPLE_MESSAGES
    };

    test_all_datatypes(&step);
}

UCS_TEST_F(test_ucg_builtin_cb, test_method_gather)
{
    ucg_builtin_op_step_t step = {
        .flags            = (enum ucg_builtin_op_step_flags)0,
        .comp_flags       = (enum ucg_builtin_op_step_comp_flags)0,
        .comp_aggregation = UCG_BUILTIN_OP_STEP_COMP_AGGREGATE_GATHER,
        .comp_criteria    = UCG_BUILTIN_OP_STEP_COMP_CRITERIA_MULTIPLE_MESSAGES
    };

    test_all_datatypes(&step);
}

#define PLAN_FOR_RANKS (10)
class test_ucg_builtin_planning : public ucg_test {
public:
    ucg_group_member_index_t m_rank, m_size;

    test_ucg_builtin_planning() : ucg_test(), m_size(PLAN_FOR_RANKS) {
        /* create context */
    }

    void set_my_rank(ucg_group_member_index_t rank) {
        m_rank = rank;
    }

    void create_op(void *planner, ucg_builtin_plan_t **plans, ucg_builtin_op_t **ops) {
        ucg_group_member_index_t idx;
        ucg_collective_params_t params = {0};

        *plans = (ucg_builtin_plan_t*)malloc(m_size * sizeof(ucg_builtin_plan_t*));
        *ops   = (ucg_builtin_op_t*)malloc(m_size * sizeof(ucg_builtin_op_t*));

        for (idx = 0; idx < m_size; idx++) {
            set_my_rank(idx);
            //ASSERT_UCS_OK(planner(m_ctx, &plans[idx]))
            ASSERT_UCS_OK(ucg_builtin_op_create(&(*plans)[idx].super, &params,
                                                (ucg_op_t**)(*ops + idx)));
        }
    }
};

UCS_TEST_F(test_ucg_builtin_planning, test_tree_planner)
{
    ucg_builtin_plan_t *plans;
    ucg_builtin_op_t *ops;

    create_op((void*)ucg_builtin_recursive_create, &plans, &ops);
}

UCS_TEST_F(test_ucg_builtin_planning, test_ring_planner)
{
    ucg_builtin_plan_t *plans;
    ucg_builtin_op_t *ops;

    create_op((void*)ucg_builtin_ring_create, &plans, &ops);
}

UCS_TEST_F(test_ucg_builtin_planning, test_binomial_tree_planner)
{
    ucg_builtin_plan_t *plans;
    ucg_builtin_op_t *ops;

    create_op((void*)ucg_builtin_binomial_tree_create, &plans, &ops);
}

UCS_TEST_F(test_ucg_builtin_planning, test_recursive_planner)
{
    ucg_builtin_plan_t *plans;
    ucg_builtin_op_t *ops;

    create_op((void*)ucg_builtin_recursive_create, &plans, &ops);
}
