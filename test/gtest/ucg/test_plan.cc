/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2019-2020.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#include "ucg_test.h"


using namespace std;

TEST_F(ucg_test, create_plan) {
    // vector<ucg_rank_info> all_rank_infos = m_resource_factory->create_balanced_rank_info(4,8);
    // ucg_group_h group = m_resource_factory->create_group(all_rank_infos[0],all_rank_infos,m_ucp_worker);
    // ucg_collective_params_t *params = create_allreduce_params();
    // ucg_collective_type_t coll_type = create_allreduce_coll_type();
    // size_t msg_size = 32;

    // ucg_plan_t *plan;
    // ucs_status_t ret = ucg_builtin_component.plan(&ucg_builtin_component, &coll_type, msg_size, group, params, &plan);

    // ASSERT_EQ(UCS_OK, ret);
    // ASSERT_TRUE(plan!=NULL);
}

TEST_F(ucg_test, create_plan_component) {
    vector<ucg_rank_info> all_rank_infos;

    m_resource_factory->create_balanced_rank_info(all_rank_infos, 4, 8);
    ucg_group_params_t *group_params = m_resource_factory->create_group_params(all_rank_infos[0],
                                                                               all_rank_infos,
                                                                               m_ucp_worker);
    ucg_group_h group = NULL;
    ucs_status_t ret = m_resource_factory->create_group(group_params, m_ucg_context, &group);
    ASSERT_EQ(UCS_OK, ret);
    ucg_group_destroy(group);
    delete group_params;
    all_rank_infos.clear();
}

TEST_F(ucg_test, destroy_group) {
    vector<ucg_rank_info> all_rank_infos;

    m_resource_factory->create_balanced_rank_info(all_rank_infos, 4, 8);
    ucg_group_params_t *group_params = m_resource_factory->create_group_params(all_rank_infos[0],
                                                                               all_rank_infos,
                                                                               m_ucp_worker);
    ucg_group_h group = NULL;
    ucs_status_t ret = m_resource_factory->create_group(group_params, m_ucg_context, &group);
    ASSERT_EQ(UCS_OK, ret);
    ASSERT_TRUE(true);

    ucg_group_destroy(group);
    delete group_params;
    all_rank_infos.clear();
}

TEST_F(ucg_test, progress_group) {
    vector<ucg_rank_info> all_rank_infos;

    m_resource_factory->create_balanced_rank_info(all_rank_infos, 4, 8);
    ucg_group_params_t *group_params = m_resource_factory->create_group_params(all_rank_infos[0],
                                                                               all_rank_infos,
                                                                               m_ucp_worker);
    ucg_group_h ucg_group = NULL;
    ucs_status_t ret = m_resource_factory->create_group(group_params, m_ucg_context, &ucg_group);
    ASSERT_EQ(UCS_OK, ret);
    ucg_planner_h planner = NULL;
    ucg_planner_ctx_h planner_ctx;
    ucg_collective_params_t * m_coll_params = m_resource_factory->create_collective_params(
                                              UCG_GROUP_COLLECTIVE_MODIFIER_SINGLE_SOURCE,
                                              0, NULL, 1, NULL, 4, NULL, NULL);
    ret = ucg_group_select_planner(ucg_group, NULL, m_coll_params, &planner, &planner_ctx);
    ASSERT_EQ(UCS_OK, ret);

    ucg_planner_h planner_p;

    ret = ucg_builtin_component.query(&planner_p);
    ASSERT_EQ(UCS_OK, ret);
    unsigned res = planner_p->progress(planner_ctx);
    //TODO how to judge progress result?
    cout << "ucg_builtin_component.progress return: " << res << endl;

    ucg_group_destroy(ucg_group);
    delete group_params;
    all_rank_infos.clear();
}

TEST_F(ucg_test, query_plan) {
    ucg_planner_h planner_p;

    ucs_status_t ret = ucg_builtin_component.query(&planner_p);
    ASSERT_EQ(UCS_OK, ret);
    ASSERT_STREQ("builtin", planner_p->name);
    free(planner_p);
}
