/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2019-2020.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#include <vector>
#include <common/test.h>
#include <ucg/builtin/plan/builtin_plan.h>
#include "ucg_test.h"

using namespace std;

TEST_F(ucg_test, test_topo_info) {
    ucg_rank_info my_rank_info = {.rank = 0, .nodex_idx = 0, .socket_idx = 0};
    ucg_rank_info other_rank_info = {.rank = 1, .nodex_idx = 0, .socket_idx = 0};
    vector<ucg_rank_info> all_rank_infos;
    all_rank_infos.push_back(my_rank_info);
    all_rank_infos.push_back(other_rank_info);

    ucg_group_params_t *group_params = m_resource_factory->create_group_params(my_rank_info, all_rank_infos);

    ucg_builtin_topology_info_params_t *topo_params = (ucg_builtin_topology_info_params_t *) malloc(
            sizeof(ucg_builtin_topology_info_params_t));
    ucg_group_member_index_t root = 0;
    ucs_status_t ret = ucg_builtin_topology_info_create(topo_params, group_params, root);
    ASSERT_EQ(UCS_OK, ret);
    free(group_params);
    free(topo_params);
}

TEST_F(ucg_test, test_check_nap) {
    ucg_rank_info my_rank_info = {.rank = 0, .nodex_idx = 0, .socket_idx = 0};
    ucg_rank_info other_rank_info = {.rank = 1, .nodex_idx = 0, .socket_idx = 0};
    vector<ucg_rank_info> all_rank_infos;
    all_rank_infos.push_back(my_rank_info);
    all_rank_infos.push_back(other_rank_info);

    ucg_group_params_t *group_params = m_resource_factory->create_group_params(my_rank_info, all_rank_infos);
    ucs_status_t ret = ucg_builtin_check_nap(group_params);
    ASSERT_EQ(UCS_OK, ret);
    free(group_params);
}
