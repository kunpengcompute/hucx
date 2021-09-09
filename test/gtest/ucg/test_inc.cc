#include "ucg_test.h"
using namespace std;

extern "C" {
STATIC_GTEST uint8_t inc_get_random_number(uint16_t group_id);
STATIC_GTEST ucs_status_t check_get_ring_header(uint32_t *tor1_ips, uint32_t *ring_header,
                                                unsigned global_node_count, unsigned ppx);
STATIC_GTEST void check_ctl_err(ucg_group_h group, char *inc_buffer, inc_fail_cause_t *fail_cause, unsigned src,
    unsigned dst);
STATIC_GTEST void inc_set_notify(inc_query_t *inc_notify, inc_query_t *inc_query, unsigned number_node,
    uint32_t spine_select);
STATIC_GTEST void inc_query_assign(ucg_group_h group, char *inc_buffer, int inc_total_size);
STATIC_GTEST void inc_hton_simple(uint8_t *buf, int count, int data_type_len);
STATIC_GTEST char inc_hton_composite(uint8_t *buf, int count, int data_type_len, enum inc_datatype index);
STATIC_GTEST ucs_status_t inc_switch_feature_query(ucg_group_h group, unsigned proc_count, unsigned ppx,
    unsigned my_index);

extern STATIC_GTEST enum ucg_group_hierarchy_level g_inc_topo_level;

STATIC_GTEST ucs_status_t inc_ucp_send(ucg_group_h group, unsigned src_index, unsigned dst_index, const void *send_buff,
    unsigned length, uint32_t tag)
{
    return UCS_OK;
}
STATIC_GTEST ucs_status_t inc_ucp_recv(ucg_group_h group, unsigned src_index, void *recv_buff, unsigned length,
    uint32_t tag)
{
    return UCS_OK;
}

}

int g_op = 0;
int g_dt = 0;

int get_operate_param(void *mpi_op, void *mpi_dt, int *op, int *dt)
{
    *op = g_op;
    *dt = g_dt;
    return 1;
}

class ucg_inc_test : public ucg_test {
public:
    ucg_inc_test();
    ~ucg_inc_test();
    void init_inc_allreduce_coll_type(ucg_collective_type_t &type) const;
    void init_inc_barrier_coll_type(ucg_collective_type_t &type) const;
    void init_inc_bcast_coll_type(ucg_collective_type_t &type) const;
    void init_inc_reduce_coll_type(ucg_collective_type_t &type) const;
    void init_inc_builtin_request(ucg_builtin_request_t &req) const;
    void init_inc_builtin_config(ucg_builtin_config_t &config, int enable = 0) const;
    void set_g_inc_config(ucg_builtin_config_t &config) const;
    void init_inc_worker(ucg_worker_h &worker) const;
    void set_tls_name(ucg_worker_h &worker, const char *name) const;
    void delete_inc_worker(ucg_worker_h &worke) const;
protected:
    vector<ucg_rank_info> m_all_rank_infos;
    ucg_group_params_t *m_group_params;
    ucg_group_h m_group;
    ucg_collective_params_t *m_coll_params;
    unsigned inc_header_size;
};

ucg_inc_test::ucg_inc_test()
{
    m_group_params = NULL;
    m_group = NULL;
    m_coll_params = NULL;
    m_resource_factory->create_balanced_rank_info(m_all_rank_infos, 2, 2);
    m_group_params = m_resource_factory->create_group_params(m_all_rank_infos[0], m_all_rank_infos);
    m_group = m_resource_factory->create_group(m_group_params, m_ucg_worker);
    m_coll_params = m_resource_factory->create_collective_params(
                UCG_GROUP_COLLECTIVE_MODIFIER_SINGLE_SOURCE, 0, NULL, 1,
                NULL, 4, NULL, NULL);
    inc_header_size = sizeof(inc_header_t);
}

ucg_inc_test::~ucg_inc_test()
{
    if (m_group_params != NULL) {
        free(m_group_params->node_index);
        m_group_params->node_index = NULL;
        delete m_group_params;
        m_group_params = NULL;
    }
    ucg_group_destroy(m_group);
    if (m_coll_params != NULL) {
        delete m_coll_params;
        m_coll_params = NULL;
    }
}

void ucg_inc_test::init_inc_allreduce_coll_type(ucg_collective_type_t &type) const
{
    type.modifiers = (ucg_collective_modifiers) (UCG_GROUP_COLLECTIVE_MODIFIER_AGGREGATE |
                                                 UCG_GROUP_COLLECTIVE_MODIFIER_BROADCAST);
    type.root = 0;
}

void ucg_inc_test::init_inc_barrier_coll_type(ucg_collective_type_t &type) const
{
    type.modifiers = (ucg_collective_modifiers) (UCG_GROUP_COLLECTIVE_MODIFIER_AGGREGATE |
                                                 UCG_GROUP_COLLECTIVE_MODIFIER_BROADCAST |
                                                 UCG_GROUP_COLLECTIVE_MODIFIER_BARRIER);
    type.root = 0;
}

void ucg_inc_test::init_inc_bcast_coll_type(ucg_collective_type_t &type) const
{
    type.modifiers = (ucg_collective_modifiers) (UCG_GROUP_COLLECTIVE_MODIFIER_BROADCAST |
                                                 UCG_GROUP_COLLECTIVE_MODIFIER_SINGLE_SOURCE);
    type.root = 0;
}

void ucg_inc_test::init_inc_reduce_coll_type(ucg_collective_type_t &type) const
{
    type.modifiers = (ucg_collective_modifiers) (UCG_GROUP_COLLECTIVE_MODIFIER_AGGREGATE |
                                                 UCG_GROUP_COLLECTIVE_MODIFIER_SINGLE_DESTINATION);
    type.root = 0;
}

void ucg_inc_test::init_inc_builtin_request(ucg_builtin_request_t &req) const
{
    req.op = (ucg_builtin_op_t *)malloc(sizeof(ucg_builtin_op_t));
    req.step = new ucg_builtin_op_step_t();
    req.step->recv_buffer = NULL;
    req.step->fragment_length = 1;
    req.op->super.params.send.dt_len = 1;
    req.op->super.params.send.count = 1;
    req.step->buffer_length_recv = 0;
    req.op->super.params.type.modifiers = UCG_GROUP_COLLECTIVE_MODIFIER_SINGLE_SOURCE;
}

void ucg_inc_test::init_inc_builtin_config(ucg_builtin_config_t &config, int enable) const
{
    config.inc.enable = enable;
    config.barrier_algorithm = UCG_ALGORITHM_BARRIER_AUTO_DECISION;
    config.allreduce_algorithm = UCG_ALGORITHM_BARRIER_AUTO_DECISION;
    config.bcast_algorithm = UCG_ALGORITHM_BARRIER_AUTO_DECISION;
}

void ucg_inc_test::set_g_inc_config(ucg_builtin_config_t &config) const
{
    ucg_group_h new_group = new ucg_group();
    init_inc_worker(new_group->worker);
    new_group->params.member_count = 0;
    config.inc.socket_count = 2;
    inc_create(new_group, &config, NULL);
    delete new_group;
}

void ucg_inc_test::init_inc_worker(ucg_worker_h &worker) const
{
    worker = new ucp_worker();
    worker->context = new ucp_context();
    ucp_context_h context = worker->context;
    context->num_tls = 1;
    context->tl_rscs = new ucp_tl_resource_desc_t();
    ucp_tl_resource_desc_t *tl_rscs = context->tl_rscs;
    context->tl_bitmap = UCS_MASK(context->num_tls);
    tl_rscs->tl_rsc.tl_name[0] = 'u';
    tl_rscs->tl_rsc.tl_name[1] = 'd';
    tl_rscs->tl_rsc.tl_name[2] = '\0';
}

void ucg_inc_test::set_tls_name(ucg_worker_h &worker, const char *name) const
{
    int size = (strlen(name) < 10) ? strlen(name) : 10;
    for (int i = 0; i < size; ++i) {
        worker->context->tl_rscs->tl_rsc.tl_name[i] = name[i];
    }
    worker->context->tl_rscs->tl_rsc.tl_name[9] = '\0';
}

void ucg_inc_test::delete_inc_worker(ucg_worker_h &worker) const
{
    delete worker->context->tl_rscs;
    delete worker->context;
    delete worker;
    worker = NULL;
}

TEST_F(ucg_inc_test, test_inc_create)
{
    ucg_groups_t *ctx = UCG_WORKER_TO_GROUPS_CTX(m_ucg_worker);
    ucg_builtin_config_t *config = (ucg_builtin_config_t *)ctx->planners[0].plan_component->plan_config;
    ucg_group_h new_group = new ucg_group();

    new_group->params = *m_group_params;

    init_inc_worker(new_group->worker);

    //test inc_get_my_index
    ucs_status_t status = inc_create(new_group, config, m_group_params);
    ASSERT_EQ(UCS_ERR_INVALID_PARAM, status);

    //test invalid tls_status;
    set_tls_name(new_group->worker, "mm");
    status = inc_create(new_group, config, m_group_params);
    ASSERT_EQ(UCS_ERR_INVALID_PARAM, status);

    //test ud
    set_tls_name(new_group->worker, "ud_verbs");
    new_group->params.member_count = 4;

    //test inc_get_proc_pernode inc_check_proc
    status = inc_create(new_group, config, m_group_params);
    ASSERT_EQ(UCS_ERR_INVALID_PARAM, status);

    config->allreduce_algorithm = UCG_ALGORITHM_ALLREDUCE_SOCKET_AWARE_INC;
    status = inc_create(new_group, config, m_group_params);
    ASSERT_EQ(UCS_ERR_INVALID_PARAM, status);

    g_inc_topo_level = UCG_GROUP_HIERARCHY_LEVEL_NODE;
    config->allreduce_algorithm = UCG_ALGORITHM_ALLREDUCE_NODE_AWARE_INC;
    new_group->params.member_index = 1;

    status = inc_create(new_group, config, m_group_params);
    ASSERT_EQ(UCS_OK, status);

    delete_inc_worker(new_group->worker);
    delete new_group;
}

TEST_F(ucg_inc_test, test_inc_destroy)
{
    ucs_status_t status = inc_destroy(m_group, 0);
    ASSERT_EQ(UCS_OK, status);
}

TEST_F(ucg_inc_test, test_inc_check_set_allreduce_packet_para)
{
    g_inc_topo_level = UCG_GROUP_HIERARCHY_LEVEL_NODE;
    ucg_builtin_config_t config;
    config.inc.max_data_size = 256;
    set_g_inc_config(config);

    m_group->params.inc_param.max_data_size = 256;
    m_group->params.inc_param.coll_operation_type = 0x01;
    m_group->params.inc_param.data_type = 0x47ff;
    m_group->params.inc_param.data_operation_type = 0x0fff;
    m_group->params.inc_param.ppn = 2;

    ucs_status_t status = inc_check_set_packet_para(m_group, m_group_params);
    ASSERT_EQ(UCS_OK, status);

    ucg_collective_params_t param;
    param.type.modifiers = UCG_GROUP_COLLECTIVE_MODIFIER_SINGLE_SOURCE;
    status = inc_check_set_packet_para(m_group, m_group_params);
    ASSERT_EQ(UCS_OK, status);

    init_inc_allreduce_coll_type(param.type);
    m_group->params.get_operate_param_f = get_operate_param;

    /* test allreduce para check in INC
     * unset unsupported parameters in sequence.
     */
    config.allreduce_algorithm = UCG_ALGORITHM_ALLREDUCE_SOCKET_AWARE_INC;
    set_g_inc_config(config);
    status = inc_check_set_packet_para(m_group, &param);
    ASSERT_EQ(UCS_ERR_INVALID_PARAM, status);

    config.allreduce_algorithm = UCG_ALGORITHM_ALLREDUCE_NODE_AWARE_INC;
    set_g_inc_config(config);
    m_group->params.inc_param.coll_operation_type = 0x00;
    status = inc_check_set_packet_para(m_group, &param);
    ASSERT_EQ(UCS_ERR_INVALID_PARAM, status);

    m_group->params.inc_param.coll_operation_type = 0x01;
    status = inc_check_set_packet_para(m_group, &param);
    ASSERT_EQ(UCS_ERR_INVALID_PARAM, status);

    g_op = 1;
    status = inc_check_set_packet_para(m_group, &param);
    ASSERT_EQ(UCS_ERR_INVALID_PARAM, status);

    g_dt = 1;
    status = inc_check_set_packet_para(m_group, &param);
    ASSERT_EQ(UCS_ERR_INVALID_PARAM, status);

    param.send.dt_len = 1;
    param.send.count = 1;
    status = inc_check_set_packet_para(m_group, &param);
    ASSERT_EQ(UCS_OK, status);
}

TEST_F(ucg_inc_test, test_inc_check_set_bcast_barrier_packet_para)
{
    g_inc_topo_level = UCG_GROUP_HIERARCHY_LEVEL_NODE;
    ucg_builtin_config_t config;
    config.inc.max_data_size = 256;
    set_g_inc_config(config);

    m_group->params.inc_param.max_data_size = 256;
    m_group->params.inc_param.coll_operation_type = 0x01;
    m_group->params.inc_param.data_type = 0x47ff;
    m_group->params.inc_param.data_operation_type = 0x0fff;
    m_group->params.inc_param.ppn = 2;
    m_group->params.get_operate_param_f = get_operate_param;

    // test bcast para check in INC
    ucg_collective_params_t param;
    config.bcast_algorithm = UCG_ALGORITHM_BCAST_NODE_AWARE_BMTREE;
    param.type.root = 1;
    g_dt = 0;
    set_g_inc_config(config);
    init_inc_bcast_coll_type(param.type);
    ucs_status_t status = inc_check_set_packet_para(m_group, &param);
    ASSERT_EQ(UCS_ERR_INVALID_PARAM, status);

    param.send.dt_len = 1;
    param.send.count = 1;
    config.bcast_algorithm = UCG_ALGORITHM_BCAST_NODE_AWARE_INC;
    status = inc_check_set_packet_para(m_group, &param);
    ASSERT_EQ(UCS_ERR_INVALID_PARAM, status);

    param.type.root = 0;
    status = inc_check_set_packet_para(m_group, &param);
    ASSERT_EQ(UCS_ERR_INVALID_PARAM, status);

    param.send.dt_len = 0;
    param.send.count = 0;
    status = inc_check_set_packet_para(m_group, &param);
    ASSERT_EQ(UCS_ERR_INVALID_PARAM, status);

    g_dt = 1;
    param.send.dt_len = 1;
    param.send.count = 1;
    status = inc_check_set_packet_para(m_group, &param);
    ASSERT_EQ(UCS_OK, status);

    // test barrier para check in INC
    config.barrier_algorithm = UCG_ALGORITHM_BARRIER_SOCKET_AWARE_INC;
    set_g_inc_config(config);
    init_inc_barrier_coll_type(param.type);
    status = inc_check_set_packet_para(m_group, &param);
    ASSERT_EQ(UCS_ERR_INVALID_PARAM, status);

    config.barrier_algorithm = UCG_ALGORITHM_BARRIER_NODE_AWARE_INC;
    set_g_inc_config(config);
    status = inc_check_set_packet_para(m_group, &param);
    ASSERT_EQ(UCS_OK, status);
}

TEST_F(ucg_inc_test, test_inc_send_cb)
{
    ucg_groups_t *ctx = UCG_WORKER_TO_GROUPS_CTX(m_ucg_worker);
    ucg_builtin_config_t *config = (ucg_builtin_config_t *)ctx->planners[0].plan_component->plan_config;
    config->inc.max_data_size = 256;
    set_g_inc_config(*config);

    ucg_builtin_request_t req;
    init_inc_builtin_request(req);

    inc_send_cb(&req);
    ASSERT_EQ(UCS_ERR_INVALID_PARAM, req.inc_req_status);

    req.op->super.params.type.modifiers =
        (ucg_collective_modifiers)(UCG_GROUP_COLLECTIVE_MODIFIER_AGGREGATE | UCG_GROUP_COLLECTIVE_MODIFIER_BROADCAST |
                                   UCG_GROUP_COLLECTIVE_MODIFIER_BARRIER);
    inc_send_cb(&req);
    ASSERT_EQ(UCS_OK, req.inc_req_status);

    req.op->super.params.type.modifiers =
        (ucg_collective_modifiers)(UCG_GROUP_COLLECTIVE_MODIFIER_AGGREGATE | UCG_GROUP_COLLECTIVE_MODIFIER_BROADCAST);
    req.op->super.params.recv.buf = NULL;
    inc_send_cb(&req);
    ASSERT_EQ(UCS_ERR_INVALID_PARAM, req.inc_req_status);

    int8_t *buf = new int8_t[1];
    req.op->super.params.recv.buf = buf;
    inc_send_cb(&req);
    ASSERT_EQ(UCS_OK, req.inc_req_status);

    delete[] buf;
    free(req.op);
    delete req.step;
}

TEST_F(ucg_inc_test, test_inc_comp_recv_one)
{
    ucg_builtin_request_t req;
    init_inc_builtin_request(req);
    req.comp_req = new ucg_request();
    void *data = NULL;
    int size = sizeof(int8_t);

    //test invalid modifiers;
    req.op->super.params.type = create_bcast_coll_type();
    ucs_status_t status = inc_comp_recv_one(&req, 0, data, 0);
    ASSERT_EQ(UCS_ERR_INVALID_PARAM, status);

    //test invalid recv buffer;
    init_inc_allreduce_coll_type(req.op->super.params.type);
    status = inc_comp_recv_one(&req, 0, data, 0);
    ASSERT_EQ(UCS_ERR_INVALID_PARAM, status);

    //test invalid data;
    req.step->recv_buffer = (int8_t*)malloc(size);
    status = inc_comp_recv_one(&req, 0, data, 0);
    ASSERT_EQ(UCS_ERR_INVALID_PARAM, status);

    data = malloc(inc_header_size + size);

    //test whether it can run correctly under normal parameters
    req.step->buffer_length_recv = 1;
    req.step->buffer_length = 1;
    inc_header_t *recv_header = (inc_header_t *)((uint8_t *)data);
    recv_header->data_flag = 0;
    status = inc_comp_recv_one(&req, 0, data, 0);
    ASSERT_EQ(UCS_ERR_INVALID_PARAM, status);

    recv_header->data_flag = 1;
    status = inc_comp_recv_one(&req, 0, data, 0);
    ASSERT_EQ(UCS_OK, status);

    init_inc_barrier_coll_type(req.op->super.params.type);
    status = inc_comp_recv_one(&req, 0, data, 0);
    ASSERT_EQ(UCS_OK, status);

    init_inc_reduce_coll_type(req.op->super.params.type);
    status = inc_comp_recv_one(&req, 0, data, 0);
    ASSERT_EQ(UCS_ERR_INVALID_PARAM, status);

    free(data);
    delete req.comp_req;
    free(req.step->recv_buffer);
    delete req.step;
    free(req.op);
}

TEST_F(ucg_inc_test, test_inc_comp_recv_many)
{
    ucg_builtin_request_t req;
    init_inc_builtin_request(req);
    void *data = NULL;
    int size = sizeof(int8_t);

    //test invalid modifiers;
    req.op->super.params.type = create_bcast_coll_type();
    ucs_status_t status = inc_comp_recv_many(&req, 0, data, 0);
    ASSERT_EQ(UCS_ERR_INVALID_PARAM, status);

    //test invalid recv buffer;
    init_inc_allreduce_coll_type(req.op->super.params.type);
    status = inc_comp_recv_many(&req, 0, data, 0);
    ASSERT_EQ(UCS_ERR_INVALID_PARAM, status);

    //test invalid data;
    req.step->recv_buffer = (int8_t*)malloc(size);
    status = inc_comp_recv_many(&req, 0, data, 0);
    ASSERT_EQ(UCS_ERR_INVALID_PARAM, status);

    data = malloc(inc_header_size + size);

    //test whether it can run correctly under normal parameters
    req.step->buffer_length_recv = 1;
    req.step->buffer_length = 1;
    status = inc_comp_recv_many(&req, 0, data, inc_header_size + size);
    ASSERT_EQ(UCS_OK, status);

    init_inc_barrier_coll_type(req.op->super.params.type);
    status = inc_comp_recv_many(&req, 0, data, 0);
    ASSERT_EQ(UCS_OK, status);

    init_inc_reduce_coll_type(req.op->super.params.type);
    status = inc_comp_recv_many(&req, 0, data, 0);
    ASSERT_EQ(UCS_ERR_INVALID_PARAM, status);

    free(data);
    free(req.step->recv_buffer);
    delete req.step;
    free(req.op);
}

TEST_F(ucg_inc_test, test_inc_enable)
{
    ucg_builtin_config_t config;
    init_inc_builtin_config(config);

    int res = inc_enable(&config);
    ASSERT_EQ(0, res);

    init_inc_builtin_config(config, 1);
    res = inc_enable(&config);
    ASSERT_EQ(0, res);

    config.barrier_algorithm = UCG_ALGORITHM_BARRIER_NODE_AWARE_INC;
    res = inc_enable(&config);
    ASSERT_EQ(1, res);

    init_inc_builtin_config(config, 1);
    config.barrier_algorithm = UCG_ALGORITHM_BARRIER_SOCKET_AWARE_INC;
    res = inc_enable(&config);
    ASSERT_EQ(1, res);

    init_inc_builtin_config(config, 1);
    config.allreduce_algorithm = UCG_ALGORITHM_ALLREDUCE_NODE_AWARE_INC;
    res = inc_enable(&config);
    ASSERT_EQ(1, res);

    init_inc_builtin_config(config, 1);
    config.allreduce_algorithm = UCG_ALGORITHM_ALLREDUCE_SOCKET_AWARE_INC;
    res = inc_enable(&config);
    ASSERT_EQ(1, res);

    init_inc_builtin_config(config, 1);
    config.bcast_algorithm = UCG_ALGORITHM_BCAST_NODE_AWARE_INC;
    res = inc_enable(&config);
    ASSERT_EQ(1, res);
}

TEST_F(ucg_inc_test, test_inc_available)
{
    ucg_group_h new_group = new ucg_group();
    new_group->params.inc_param.switch_info_got = 0;
    int res = inc_available(new_group);
    ASSERT_EQ(0, res);

    new_group->params.inc_param.switch_info_got = 1;
    res = inc_available(new_group);
    ASSERT_EQ(1, res);
    delete new_group;
}

TEST_F(ucg_inc_test, test_inc_used)
{
    ucg_group_h new_group = new ucg_group();
    new_group->params.inc_param.switch_info_got = 0;
    int res = inc_used(&new_group->params);
    ASSERT_EQ(0, res);
    new_group->params.inc_param.switch_info_got = 1;
    new_group->params.inc_param.feature_used = 0;
    res = inc_used(&new_group->params);
    ASSERT_EQ(0, res);

    new_group->params.inc_param.feature_used = 1;
    res = inc_used(&new_group->params);
    ASSERT_EQ(1, res);
    delete new_group;
}

TEST_F(ucg_inc_test, test_random_number)
{
    uint8_t num = inc_get_random_number(0);
    ASSERT_NE(num, 0);
    ASSERT_NE(num, 1);
    ASSERT_NE(num, 2);
}

TEST_F(ucg_inc_test, test_ring_header)
{
    uint32_t *tor1_ips = new uint32_t[4] {0, 0, 1, 1};
    uint32_t *ring_header = new uint32_t[4];

    ucs_status_t status = check_get_ring_header(tor1_ips, ring_header, 4, 1);
    ASSERT_EQ(UCS_OK, status);

    tor1_ips[0] = 1;
    status = check_get_ring_header(tor1_ips, ring_header, 4, 1);
    ASSERT_EQ(UCS_ERR_INVALID_PARAM, status);

    delete[] tor1_ips;
    delete[] ring_header;
}

TEST_F(ucg_inc_test, test_set_query_notify)
{
    unsigned g_inc_header_rc_size = 0;
    unsigned inc_total_size = sizeof(inc_header_t) + sizeof(inc_query_t) + g_inc_header_rc_size;
    char *inc_buffer = new char[inc_total_size];
    inc_query_t *inc_query = (inc_query_t *)(inc_buffer + inc_header_size + g_inc_header_rc_size);

    inc_set_notify(inc_query, inc_query, 2, 0);
    inc_query_assign(m_group, inc_buffer, inc_total_size);

    delete[] inc_buffer;
}

TEST_F(ucg_inc_test, test_inc_hton)
{
    uint8_t *buf = new uint8_t[24];

    inc_hton_simple(buf, 12, 2);
    inc_hton_simple(buf, 6, 4);
    inc_hton_simple(buf, 3, 8);

    char ret = inc_hton_composite(buf, 4, 6, INC_DATATYPE_NULL);
    ASSERT_EQ(ret, 0);
    ret = inc_hton_composite(buf, 4, 6, INC_DATATYPE_UNAVAILABLE);
    ASSERT_EQ(ret, 0);
    ret = inc_hton_composite(buf, 4, 6, INC_DATATYPE_SHORT_INT);
    ASSERT_EQ(ret, 1);
    ret = inc_hton_composite(buf, 3, 8, INC_DATATYPE_FLOAT_INT);
    ASSERT_EQ(ret, 1);
    ret = inc_hton_composite(buf, 2, 12, INC_DATATYPE_DOUBLE_INT);
    ASSERT_EQ(ret, 1);

    delete[] buf;
}

TEST_F(ucg_inc_test, test_check_ctr_err)
{
    unsigned g_inc_header_rc_size = 0;
    unsigned inc_total_size = sizeof(inc_header_t) + sizeof(inc_query_t) + g_inc_header_rc_size;
    char *inc_buffer = new char[inc_total_size]();
    inc_query_t *inc_query = (inc_query_t *)(inc_buffer + inc_header_size + g_inc_header_rc_size);
    inc_fail_cause_t *fail_cause = new inc_fail_cause();

    check_ctl_err(m_group, inc_buffer, fail_cause, 0, 0);
    ASSERT_EQ(fail_cause->server_fail_cause, INC_ERR_HOP_NUM);

    inc_query->fail_cause = INC_TABLE_NO_RESOURCE;
    check_ctl_err(m_group, inc_buffer, fail_cause, 0, 0);
    ASSERT_EQ(fail_cause->switch_fail_cause, INC_TABLE_NO_RESOURCE);

    delete[] inc_buffer;
    delete fail_cause;
}

TEST_F(ucg_inc_test, test_ucg_builtin_add_inc)
{
    ucg_groups_t *ctx = UCG_WORKER_TO_GROUPS_CTX(m_ucg_worker);
    ucg_builtin_config_t *config = (ucg_builtin_config_t *)ctx->planners[0].plan_component->plan_config;
    config->inc.max_data_size = 256;
    config->inc.socket_count = 2;
    set_g_inc_config(*config);

    ucg_builtin_plan_t tree;
    ucg_builtin_plan_phase_t phase;
    phase.ucp_eps = new ucp_ep_h[2];
    ucg_builtin_binomial_tree_params_t param;
    ucg_group_params_t *group_params = new ucg_group_params();
    param.group_params = group_params;
    ucg_builtin_group_ctx_t *builtin_ctx =
        (ucg_builtin_group_ctx_t *)UCG_GROUP_TO_COMPONENT_CTX(ucg_builtin_component, m_group);
    param.ctx = builtin_ctx;
    uct_ep_h *eps = new uct_ep_h[2];
    unsigned phs_increase_cnt;
    unsigned step_increase_cnt;

    tree.super.my_index = 0;
    group_params->member_count = 4;
    group_params->inc_param.node_under_tor = 4;
    group_params->inc_param.header_under_tor = 0;
    ucs_status_t status;
    for (int i = 1; i <= 3; i++)
    {
        tree.super.my_index = i;
        status = ucg_builtin_add_inc(&tree, &phase, &param, &eps, &phs_increase_cnt, &step_increase_cnt, 1,
                            UCG_GROUP_HIERARCHY_LEVEL_NODE);
        ASSERT_EQ(status, UCS_OK);
        status = ucg_builtin_add_inc(&tree, &phase, &param, &eps, &phs_increase_cnt, &step_increase_cnt, 1,
                            UCG_GROUP_HIERARCHY_LEVEL_SOCKET);
        ASSERT_EQ(status, UCS_OK);
    }

    status = ucg_builtin_add_inc(NULL, NULL, NULL, NULL, NULL, NULL, 0, (ucg_group_hierarchy_level)0);
    ASSERT_EQ(UCS_ERR_NO_PROGRESS, status);

    delete[] phase.ucp_eps;
    delete[] eps;
}

TEST_F(ucg_inc_test, test_inc_query)
{
    ucs_status_t status = inc_switch_feature_query(m_group, 4, 1, 0);
    ASSERT_EQ(status, UCS_ERR_INVALID_PARAM);

    status = inc_switch_feature_query(m_group, 4, 1, 1);
    ASSERT_EQ(status, UCS_ERR_INVALID_PARAM);

    g_inc_topo_level = UCG_GROUP_HIERARCHY_LEVEL_SOCKET;
    status = inc_switch_feature_query(m_group, 4, 1, 0);
    ASSERT_EQ(status, UCS_ERR_INVALID_PARAM);

    status = inc_switch_feature_query(m_group, 4, 1, 1);
    ASSERT_EQ(status, UCS_ERR_INVALID_PARAM);
}
