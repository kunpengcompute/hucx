/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2019-2020.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#include "ucg_test.h"

using namespace std;

extern "C" {
STATIC_GTEST int ucg_builtin_check_plummer_unsupport(const ucg_group_params_t *group_params,
                                                     const ucg_collective_params_t *coll_params);

STATIC_GTEST coll_type_t ucg_builtin_get_coll_type(const ucg_collective_type_t *coll_type);
}

class ucg_collective_test : public ucg_test {
public:
    ucg_collective_test();

    virtual ~ucg_collective_test();

protected:
    vector<ucg_rank_info> m_all_rank_infos;
    ucg_group_params_t *m_group_params;
    ucg_group_params_t *m_group2_params;
    ucg_collective_params_t *m_coll_params;
    ucg_group_h m_group;
    ucg_group_h m_group2;
};

ucg_collective_test::ucg_collective_test()
{
    m_all_rank_infos.clear();
    m_resource_factory->create_balanced_rank_info(m_all_rank_infos, 2, 2);
    m_group_params = m_resource_factory->create_group_params(m_all_rank_infos[0], m_all_rank_infos);
    m_group2_params = m_resource_factory->create_group_params(m_all_rank_infos[1], m_all_rank_infos);
    m_group = m_resource_factory->create_group(m_group_params, m_ucg_worker);
    m_group2 = m_resource_factory->create_group(m_group2_params, m_ucg_worker);
    m_resource_factory->ctype = COLL_TYPE_BCAST;
    m_coll_params = m_resource_factory->create_collective_params(
                                            UCG_GROUP_COLLECTIVE_MODIFIER_SINGLE_SOURCE,
                                            0, NULL, 1, NULL, 4, NULL, NULL);
}

ucg_collective_test::~ucg_collective_test()
{
    if (m_coll_params != NULL) {
        delete m_coll_params;
        m_coll_params = NULL;
    }

    ucg_group_destroy(m_group2);
    ucg_group_destroy(m_group);

    if (m_group2_params != NULL) {
        delete m_group2_params;
        m_group2_params = NULL;
    }

    if (m_group_params != NULL) {
        delete m_group_params;
        m_group_params = NULL;
    }

    m_all_rank_infos.clear();
}


TEST_F(ucg_collective_test, test_collective_create) {
    ucg_coll_h coll = NULL;

    ucs_status_t ret = ucg_collective_create(m_group, m_coll_params, &coll);

    ASSERT_EQ(UCS_OK, ret);
}

TEST_F(ucg_collective_test, test_collective_check_input) {
    ucs_status_t ret = ucg_collective_check_input(m_group, m_coll_params, NULL);
    ASSERT_EQ(UCS_ERR_INVALID_PARAM, ret);

    ucg_coll_h coll = NULL;
    ret = ucg_collective_create(m_group, m_coll_params, &coll);
    ASSERT_EQ(UCS_OK, ret);

    m_coll_params->type.modifiers = UCG_GROUP_COLLECTIVE_MODIFIER_SINGLE_SOURCE;
    ret = ucg_collective_check_input(m_group, m_coll_params, &coll);
    ASSERT_EQ(UCS_OK, ret);

    m_coll_params->send.count = -1;
    ret = ucg_collective_check_input(m_group, m_coll_params, &coll);
    ASSERT_EQ(UCS_ERR_INVALID_PARAM, ret);

    std::vector<ucg_rank_info> rank_infos;
    m_resource_factory->create_balanced_rank_info(rank_infos, 1, 2);
    ucg_group_params_t *group_params = m_resource_factory->create_group_params(rank_infos[0], m_all_rank_infos);
    ucg_group_h group = m_resource_factory->create_group(group_params, m_ucg_worker);

    ucg_collective_params_t *params = create_alltoallv_params();
    ret = ucg_collective_check_input(group, params, &coll);
    ASSERT_EQ(UCS_OK, ret);

    params->recv.counts[0] = -1;
    ret = ucg_collective_check_input(group, params, &coll);
    ASSERT_EQ(UCS_ERR_INVALID_PARAM, ret);

    params->send.counts[0] = -1;
    ret = ucg_collective_check_input(group, params, &coll);
    ASSERT_EQ(UCS_ERR_INVALID_PARAM, ret);

    delete group_params;
    delete params;
}

TEST_F(ucg_collective_test, test_collective_compare_params) {
    ucg_coll_h coll = NULL;
    ucs_status_t ret = ucg_collective_create(m_group, m_coll_params, &coll);
    ASSERT_EQ(UCS_OK, ret);

    ucg_collective_params_t *params1 = create_alltoallv_params();
    ret = ucg_collective_create(m_group, params1, &coll);

    ucg_collective_params_t *params2 = create_alltoallv_params();
    ret = ucg_collective_create(m_group, params2, &coll);

    delete params1;
    delete params2;
}

TEST_F(ucg_collective_test, test_collective_check_plummer_unsupport) {
    ucg_coll_h coll = NULL;

    ucs_status_t ret = ucg_collective_create(m_group, m_coll_params, &coll);
    ASSERT_EQ(UCS_OK, ret);

    ucg_collective_params_t *params1 = create_alltoallv_params();
    ret = ucg_collective_create(m_group, params1, &coll);

    m_group_params->member_count = 2;
    int inc = ucg_builtin_check_plummer_unsupport(m_group_params, params1);
    ASSERT_EQ(0, inc);

    params1->send.displs[0] = -1;
    params1->recv.displs[0] = -1;
    inc = ucg_builtin_check_plummer_unsupport(m_group_params, params1);
    ASSERT_EQ(1, inc);

    params1->send.displs[0] = 1;
    params1->recv.displs[0] = 1;
    inc = ucg_builtin_check_plummer_unsupport(m_group_params, params1);
    ASSERT_EQ(1, inc);

    params1->send.displs[1] = 2;
    params1->recv.displs[1] = 2;
    inc = ucg_builtin_check_plummer_unsupport(m_group_params, params1);
    ASSERT_EQ(1, inc);

    delete params1;
}

TEST_F(ucg_collective_test, test_collective_start_nb) {
    ucg_coll_h coll = NULL;

    ucs_status_t retC = ucg_collective_create(m_group2, m_coll_params, &coll);
    EXPECT_EQ(UCS_OK, retC);

    ucs_status_ptr_t retP = ucg_collective_start_nb(coll);

    ASSERT_TRUE(retP != NULL);
}

TEST_F(ucg_collective_test, test_collective_start_nbr) {
    ucg_request_t *req = NULL;
    ucg_coll_h coll = NULL;

    ucs_status_t retC = ucg_collective_create(m_group2, m_coll_params, &coll);
    EXPECT_EQ(UCS_OK, retC);

    ucg_collective_start_nbr(coll, req);

    //ASSERT_EQ(UCS_OK, retS);
}

TEST_F(ucg_collective_test, test_collective_destroy) {
    ucg_coll_h coll = NULL;

    ucs_status_t ret = ucg_collective_create(m_group, m_coll_params, &coll);
    EXPECT_EQ(UCS_OK, ret);
    
    //TODO
    ASSERT_TRUE(true);
}

TEST_F(ucg_collective_test, test_get_coll_type) {
    ucg_collective_type_t type;
    type.root = 0;

    type.modifiers = (ucg_collective_modifiers)(UCG_GROUP_COLLECTIVE_MODIFIER_AGGREGATE |
        UCG_GROUP_COLLECTIVE_MODIFIER_BROADCAST | UCG_GROUP_COLLECTIVE_MODIFIER_BARRIER);
    EXPECT_EQ(COLL_TYPE_BARRIER, ucg_builtin_get_coll_type(&type));

    type.modifiers = (ucg_collective_modifiers)(UCG_GROUP_COLLECTIVE_MODIFIER_BROADCAST |
        UCG_GROUP_COLLECTIVE_MODIFIER_SINGLE_SOURCE);
    EXPECT_EQ(COLL_TYPE_BCAST, ucg_builtin_get_coll_type(&type));

    type.modifiers =
        (ucg_collective_modifiers)(UCG_GROUP_COLLECTIVE_MODIFIER_AGGREGATE | UCG_GROUP_COLLECTIVE_MODIFIER_BROADCAST);
    EXPECT_EQ(COLL_TYPE_ALLREDUCE, ucg_builtin_get_coll_type(&type));

    type.modifiers = (ucg_collective_modifiers)(UCG_GROUP_COLLECTIVE_MODIFIER_ALLTOALLV |
        UCG_GROUP_COLLECTIVE_MODIFIER_VARIABLE_LENGTH);
    EXPECT_EQ(COLL_TYPE_ALLTOALLV, ucg_builtin_get_coll_type(&type));

    type.modifiers = (ucg_collective_modifiers)(UCG_GROUP_COLLECTIVE_MODIFIER_BROADCAST |
        UCG_GROUP_COLLECTIVE_MODIFIER_VARIABLE_LENGTH);
    EXPECT_EQ(COLL_TYPE_NUMS, ucg_builtin_get_coll_type(&type));
}
