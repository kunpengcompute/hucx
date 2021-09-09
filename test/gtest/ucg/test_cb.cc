/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2019-2020.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#include "test_op.h"
#include "ucg/builtin/ops/builtin_cb.inl"

using namespace std;

STATIC_GTEST ucs_status_t ucg_builtin_plummer_check_data_size(size_t dtype_size, int count);
STATIC_GTEST ucs_status_t ucg_builtin_plummer_check_overflow(int left, int right);


class ucg_cb_test : public ucg_op_test {
public:
    ucg_cb_test() {
        num_procs = 2;
    }

    ~ ucg_cb_test() = default;

public:
    ucg_builtin_request_t* create_request(ucg_builtin_op_step_t *step);
};

ucg_builtin_request_t* ucg_cb_test::create_request(ucg_builtin_op_step_t *step) {
    ucg_builtin_comp_slot_t *slot = new ucg_builtin_comp_slot_t;
    slot->cb = NULL;
    slot->step_idx = 1;
    ucs_list_head_init(&slot->msg_head);

    ucg_builtin_request_t *req = &slot->req;
    req->step = step;
    ucg_request_t *comp_req = new ucg_request_t;
    comp_req->flags = 0;
    comp_req->status = UCS_OK;
    req->comp_req = comp_req;
    ucg_builtin_op_t *op = new ucg_builtin_op_t;
    op->send_dt = NULL;
    op->recv_dt = NULL;
    op->final_cb = NULL;
    req->op = op;

    return req;
}

static void reduce_mock(void *mpi_op, char *src_buffer, char *dst_buffer, unsigned dcount, void *mpi_datatype)
{
    // do nothing
}

static void* coll_ucx_generic_datatype_start_pack(void *context, const void *buffer,
                                                  size_t count)
{
    memcpy(context, buffer, count);
    return NULL;
}

static void* coll_ucx_generic_datatype_start_unpack(void *context, void *buffer,
                                                    size_t count)
{
    memcpy(context, buffer, count);
    return NULL;
}

static void coll_ucx_generic_datatype_finish(void *state)
{
    // do nothing
}

static ucs_status_t coll_ucx_generic_datatype_unpack(void *state, size_t offset,
                                                     const void *src, size_t length)
{
    memcpy((int *)state + offset, src, length);
    return UCS_OK;
}

TEST_F(ucg_cb_test, test_op_no_optimization) {
    ucg_builtin_op_t *op = new ucg_builtin_op_t;
    ucs_status_t ret = ucg_builtin_no_optimization(op);

    ASSERT_EQ(UCS_OK, ret);
}

TEST_F(ucg_cb_test, test_op_cb_init) {
    ucg_group_h group = create_group();
    ucg_collective_params_t *params = create_allreduce_params();
    ucg_plan_t *plan = create_plan(1, params, group);
    ucp_dt_generic_t *dt_gen = new ucp_dt_generic_t;
    ucg_builtin_op_t *op = new ucg_builtin_op_t();

    ucs_status_t ret = ucg_builtin_op_create(plan, params, (ucg_op_t **)&op);
    ASSERT_EQ(UCS_OK, ret);

    op->super.params = *params;
    op->super.plan = plan;
    ((ucg_builtin_op_t *)op)->send_dt = dt_gen;
    ((ucg_builtin_op_t *)op)->recv_dt = dt_gen;
    ucg_builtin_op_step_t *step = &((ucg_builtin_op_t *)op)->steps[0];
    step->non_contig.pack_state = (void *)step->recv_buffer;
    step->non_contig.unpack_state = (void *)step->recv_buffer;
    int *send_buf = (int *)params->send.buf;
    int *recv_buf = (int *)params->recv.buf;
    ucg_builtin_request_t *req = new ucg_builtin_request_t;
    req->op = (ucg_builtin_op_t *)op;
    req->step = step;
    dt_gen->context = (void *)step->recv_buffer;
    dt_gen->ops.start_pack = coll_ucx_generic_datatype_start_pack;
    dt_gen->ops.start_unpack = coll_ucx_generic_datatype_start_unpack;

    ucg_builtin_init_reduce((ucg_builtin_op_t *)op);
    ASSERT_EQ(send_buf[0], recv_buf[0]);

    ucg_builtin_init_rabenseifner((ucg_builtin_op_t *)op);

    ucg_builtin_init_inc((ucg_builtin_op_t *)op);

    ucg_builtin_init_pairwise((ucg_builtin_op_t *)op);

    recv_buf[0] = -1;
    ucg_builtin_init_allgather_recursive((ucg_builtin_op_t *)op);
    ASSERT_EQ(send_buf[0], recv_buf[0]);

    recv_buf[0] = -1;
    plan->group_id = 0;
    ucg_builtin_init_gather((ucg_builtin_op_t *)op);
    ASSERT_EQ(send_buf[0], recv_buf[0]);

    recv_buf[0] = -1;
    step->buf_len_unit = step->buffer_length;
    ucg_builtin_init_allgather((ucg_builtin_op_t *)op);
    ASSERT_EQ(send_buf[0], recv_buf[0]);

    recv_buf[0] = -1;
    //proc_count = 2
    plan->my_index = 1;
    step->buf_len_unit = step->buffer_length / 2;
    ucg_builtin_init_alltoall((ucg_builtin_op_t *)op);
    ASSERT_EQ(send_buf[1], recv_buf[0]);

    recv_buf[0] = -1;
    step->remote_offset = 0;
    ucg_builtin_init_ring((ucg_builtin_op_t *)op);
    ASSERT_EQ(send_buf[0], recv_buf[0]);

    // do nothing
    ucg_builtin_init_dummy((ucg_builtin_op_t *)op);

    ucg_builtin_init_dt_state(step, UCG_BUILTIN_OP_DT_SEND, dt_gen, params);
    ASSERT_EQ(send_buf[0], recv_buf[0]);

    ucg_builtin_init_dt_state(step, UCG_BUILTIN_OP_DT_RECV, dt_gen, params);
    ASSERT_EQ(send_buf[0], recv_buf[0]);

    ucg_builtin_init_pack((ucg_builtin_op_t *)op);
    ASSERT_EQ(send_buf[0], recv_buf[0]);

    ucg_builtin_init_unpack((ucg_builtin_op_t *)op);
    ASSERT_EQ(send_buf[0], recv_buf[0]);

    ucg_builtin_init_pack_and_unpack((ucg_builtin_op_t *)op);
    ASSERT_EQ(send_buf[0], recv_buf[0]);

    ucg_builtin_init_reduce_and_pack((ucg_builtin_op_t *)op);
    ASSERT_EQ(send_buf[0], recv_buf[0]);

    ucg_builtin_init_reduce_and_unpack((ucg_builtin_op_t *)op);
    ASSERT_EQ(send_buf[0], recv_buf[0]);

    params->send.buf = MPI_IN_PLACE;
    ucg_builtin_init_reduce((ucg_builtin_op_t *)op);
    ASSERT_EQ(recv_buf[0], recv_buf[0]);

    delete req;
    delete dt_gen;
}

TEST_F(ucg_cb_test, test_op_alltoallv_cb_init) {
    ucg_group_h group = create_group();
    ucg_collective_params_t *allreduce_params = create_allreduce_params();
    ucg_plan_t *plan = create_plan(1, allreduce_params, group);
    plan->my_index = 0;

    ucg_builtin_plan_phase_t *phase = create_phase(UCG_PLAN_METHOD_SEND_TERMINAL);
    unsigned extra_flags = 0;
    unsigned base_am_id = 0;
    ucg_group_id_t group_id = 0;
    ucg_collective_params_t *params = create_alltoallv_params();
    int8_t *current_data_buffer = NULL;
    ucg_builtin_op_step_t *step = new ucg_builtin_op_step_t();
    ucg_builtin_op_t *op = new ucg_builtin_op_t();
    ucp_datatype_t dtype = ucp_dt_make_contig(sizeof(int));

    ucs_status_t ret = ucg_builtin_step_create(op, phase, dtype, dtype, extra_flags, base_am_id,
                                               group_id, params, &current_data_buffer, step);
    ASSERT_EQ(UCS_OK, ret);

    op->super.params            = *params;
    op->super.plan              = plan;
    step->phase                 = phase;

    ucg_builtin_init_throttled_scatter(op);
    ucg_builtin_init_plummer(op);
}

TEST_F(ucg_cb_test, test_op_cb_final) {
    ucg_group_h group = create_group();
    ucg_collective_params_t *params = create_allreduce_params();
    ucg_plan_t *plan = create_plan(1, params, group);

    ucg_op_t *op = new ucg_op_t();

    ucs_status_t ret = ucg_builtin_op_create(plan, params, &op);
    ASSERT_EQ(UCS_OK, ret);

    op->params = *params;
    op->plan = plan;
    plan->my_index = 1;
    ucg_builtin_op_step_t *step = &((ucg_builtin_op_t *)op)->steps[0];
    step->buf_len_unit = step->buffer_length;

    int count = 4;
    int *recv_buf = new int[count];
    for (int i = 0; i < count; i++) {
        recv_buf[i] = i;
    }
    step->recv_buffer = (int8_t *)recv_buf;
    step->non_contig.pack_state = (void *)step->recv_buffer;
    step->non_contig.unpack_state = (void *)step->recv_buffer;

    ucg_builtin_request_t *req = new ucg_builtin_request_t;
    req->op = (ucg_builtin_op_t *)op;
    req->step = step;
    ucp_dt_generic_t *dt_gen = new ucp_dt_generic_t;
    ((ucg_builtin_op_t *)op)->send_dt = dt_gen;
    ((ucg_builtin_op_t *)op)->recv_dt = dt_gen;
    dt_gen->ops.finish = coll_ucx_generic_datatype_finish;

    ucg_builtin_final_allgather(req);

    ucg_builtin_final_throttled_scatter(req);

    ucg_builtin_final_plummer(req);

    int half = count / 2;
    for (int i = 0; i < count; i++) {
        if (i < half) {
            ASSERT_EQ(i + half, recv_buf[i]);
        } else {
            ASSERT_EQ(i - half, recv_buf[i]);
        }
    }

    ucg_builtin_final_alltoall(req);
    for (int i = 0; i < count; i++) {
        ASSERT_EQ(i, recv_buf[i]);
    }

    ucg_builtin_finalize_dt_state(step, UCG_BUILTIN_OP_DT_SEND, dt_gen);
    for (int i = 0; i < count; i++) {
        ASSERT_EQ(i, recv_buf[i]);
    }

    ucg_builtin_finalize_dt_state(step, UCG_BUILTIN_OP_DT_RECV, dt_gen);
    for (int i = 0; i < count; i++) {
        ASSERT_EQ(i, recv_buf[i]);
    }

    ucg_builtin_finalize_pack(req);
    for (int i = 0; i < count; i++) {
        ASSERT_EQ(i, recv_buf[i]);
    }

    ucg_builtin_finalize_unpack(req);
    for (int i = 0; i < count; i++) {
        ASSERT_EQ(i, recv_buf[i]);
    }

    ucg_builtin_finalize_pack_and_unpack(req);
    for (int i = 0; i < count; i++) {
        ASSERT_EQ(i, recv_buf[i]);
    }

    delete req;
    delete dt_gen;
}

TEST_F(ucg_cb_test, test_send_cb) {
    ucg_builtin_plan_phase_t *phase = create_phase(UCG_PLAN_METHOD_SEND_TERMINAL);
    unsigned extra_flags = 0;
    unsigned base_am_id = 0;
    ucg_group_id_t group_id = 0;
    ucg_collective_params_t *params = create_allreduce_params();
    int8_t *current_data_buffer = NULL;
    ucg_builtin_op_step_t *step = new ucg_builtin_op_step_t();
    ucg_op_t *op = new ucg_op_t();
    ucp_datatype_t dtype = ucp_dt_make_contig(sizeof(int));

    ucs_status_t ret = ucg_builtin_step_create((ucg_builtin_op_t *)op, phase, dtype, dtype, extra_flags, base_am_id,
                                               group_id, params, &current_data_buffer, step);
    ASSERT_EQ(UCS_OK, ret);

    step->buf_len_unit = sizeof(int);
    step->am_header.step_idx = 0;
    step->displs_rule = UCG_BUILTIN_OP_STEP_DISPLS_RULE_BRUCK_ALLTOALL;

    ucg_builtin_request_t *req = new ucg_builtin_request_t;
    req->op = (ucg_builtin_op_t *)op;
    req->step = step;

    int *send_buf = (int *)step->send_buffer;
    int *recv_buf = (int *)step->recv_buffer;

    ucg_builtin_send_alltoall(req);
    ASSERT_EQ(recv_buf[1], send_buf[0]);

    ucg_builtin_send_inc(req);
}

TEST_F(ucg_cb_test, test_alltoallv_plummer_send_cb) {
    ucg_builtin_plan_phase_t *phase = create_phase(UCG_PLAN_METHOD_SEND_TERMINAL);
    unsigned extra_flags = 0;
    unsigned base_am_id = 0;
    ucg_group_id_t group_id = 0;
    ucg_collective_params_t *params = create_alltoallv_params();
    int8_t *current_data_buffer = NULL;
    ucg_builtin_op_step_t *step = new ucg_builtin_op_step_t();
    ucg_builtin_op_t *op = new ucg_builtin_op_t();
    ucp_datatype_t dtype = ucp_dt_make_contig(sizeof(int));

    ucs_status_t ret = ucg_builtin_step_create(op, phase, dtype, dtype, extra_flags, base_am_id,
                                               group_id, params, &current_data_buffer, step);
    ASSERT_EQ(UCS_OK, ret);

    ucg_builtin_request_t *req = new ucg_builtin_request_t;
    req->op = op;
    req->op->super.params = *params;

    /* init phase */
    phase->ex_attr.is_node_leader   = 1;
    phase->ex_attr.ppn              = 2;
    phase->ex_attr.member_cnt       = 2;
    phase->ex_attr.recv_start_block = 1;
    phase->ep_cnt                   = 2;
    phase->send_ep_cnt              = 0;
    phase->recv_ep_cnt              = 2;
    step->phase                     = phase;
    req->step                       = step;

    step->buf_len_unit              = sizeof(int) * phase->ex_attr.member_cnt;
    step->send_buffer               = (int8_t *)params->send.counts;
    ucg_builtin_plummer_gather_send_counts_cb(req);
    ASSERT_EQ(1, ((int *)step->recv_buffer)[2]);
    ASSERT_EQ(1, ((int *)step->recv_buffer)[3]);
    memcpy(step->recv_buffer, step->send_buffer, step->buf_len_unit);

    step->send_buffer               = (int8_t *)params->recv.counts;
    ucg_builtin_plummer_gather_recv_counts_cb(req);
    ASSERT_EQ(1, ((int *)step->recv_buffer)[2]);
    ASSERT_EQ(1, ((int *)step->recv_buffer)[3]);
    memcpy(step->recv_buffer, step->send_buffer, step->buf_len_unit);

    /* init recv collective parameters */
    step->recv_coll_params = ucg_builtin_allocate_coll_params(phase->ep_cnt);
    ucg_builtin_plummer_gather_send_buffers_cb(req);
    ASSERT_EQ(0, ((int *)step->recv_coll_params->init_buf)[0]);
    ASSERT_EQ(1, ((int *)step->recv_coll_params->init_buf)[1]);

    req->op->temp_exchange_buffer1 = req->op->temp_exchange_buffer;
    step->send_coll_params = ucg_builtin_allocate_coll_params(phase->ep_cnt);
    ucg_builtin_plummer_scatter_recv_buffers_cb(req);
    ASSERT_EQ(0, ((int *)step->send_coll_params->init_buf)[0]);
    ASSERT_EQ(1, ((int *)step->send_coll_params->init_buf)[1]);

    phase->ex_attr.member_cnt   = 2;
    phase->ex_attr.ppn          = 1;

    memset(step->send_coll_params->counts, 0, phase->ex_attr.member_cnt * sizeof(int));
    memset(step->send_coll_params->displs, 0, phase->ex_attr.member_cnt * sizeof(int));
    memset(step->recv_coll_params->counts, 0, phase->ex_attr.member_cnt * sizeof(int));
    memset(step->recv_coll_params->displs, 0, phase->ex_attr.member_cnt * sizeof(int));

    ucg_builtin_plummer_inter_alltoallv_cb(req);
}

TEST_F(ucg_cb_test, test_alltoallv_ladd_send_cb) {
    ucg_builtin_plan_phase_t *phase = create_phase(UCG_PLAN_METHOD_SEND_TERMINAL);
    unsigned extra_flags = 0;
    unsigned base_am_id = 0;
    ucg_group_id_t group_id = 0;
    ucg_collective_params_t *params = create_alltoallv_params();
    int8_t *current_data_buffer = NULL;
    ucg_builtin_op_step_t *step = new ucg_builtin_op_step_t();
    ucg_builtin_op_t *op = new ucg_builtin_op_t();
    ucp_datatype_t dtype = ucp_dt_make_contig(sizeof(int));

    ucs_status_t ret = ucg_builtin_step_create(op, phase, dtype, dtype, extra_flags, base_am_id,
                                               group_id, params, &current_data_buffer, step);
    ASSERT_EQ(UCS_OK, ret);

    ucg_builtin_request_t *req = new ucg_builtin_request_t;
    req->op = op;
    req->op->super.params = *params;

    step->phase                     = phase;
    req->step                       = step;
    step->send_coll_params          = (ucg_builtin_coll_params_t *)ucs_malloc(sizeof(ucg_builtin_coll_params_t), "coll params");
    step->recv_coll_params          = (ucg_builtin_coll_params_t *)ucs_malloc(sizeof(ucg_builtin_coll_params_t), "coll params");

    phase->ex_attr.start_block      = 0;
    phase->ex_attr.num_blocks       = 1;
    phase->ex_attr.member_cnt       = 2;
    step->buf_len_unit              = sizeof(int) * phase->ex_attr.member_cnt;

    ucg_builtin_throttled_scatter_alltoallv_cb(req);
}

TEST_F(ucg_cb_test, test_recv_cb_recv_one) {
    ucg_builtin_plan_phase_t *phase = create_phase(UCG_PLAN_METHOD_RECV_TERMINAL);
    unsigned extra_flags = UCG_BUILTIN_OP_STEP_FLAG_LAST_STEP;
    unsigned base_am_id = 0;
    ucg_group_id_t group_id = 0;
    ucg_collective_params_t *params = create_allreduce_params();
    int8_t *current_data_buffer = NULL;
    ucg_builtin_op_step_t *step = new ucg_builtin_op_step_t();
    ucg_op_t *op = new ucg_op_t();
    ucp_datatype_t dtype = ucp_dt_make_contig(sizeof(int));
    ucp_dt_generic_t *dt_gen = new ucp_dt_generic_t;

    ucs_status_t ret = ucg_builtin_step_create((ucg_builtin_op_t *)op, phase, dtype, dtype, extra_flags, base_am_id,
                                               group_id, params, &current_data_buffer, step);
    ASSERT_EQ(UCS_OK, ret);

    ucg_builtin_request_t *req = create_request(step);
    req->step->non_contig.unpack_state = (void *)step->recv_buffer;
    req->op->recv_dt = dt_gen;
    req->op->recv_dt->ops.unpack = coll_ucx_generic_datatype_unpack;

    int *recv_buf = (int *)step->recv_buffer;
    uint64_t offset = 0;
    int count = 2;
    int *data = new int[count];
    for (int i = 0; i < count; i++) {
        data[i] = i;
    }
    size_t length = sizeof(int) * count;

    int ret_int = ucg_builtin_comp_recv_one_cb(req, offset, (void *)data, length);
    ASSERT_EQ(1, ret_int);
    for (int i = 0; i < count; i++) {
        ASSERT_EQ(i, recv_buf[i]);
        recv_buf[i] = -1;
    }

    req->comp_req->flags = 0;
    ret_int = ucg_builtin_comp_recv_noncontig_one_cb(req, offset, (void *)data, length);
    ASSERT_EQ(1, ret_int);
    for (int i = 0; i < count; i++) {
        ASSERT_EQ(i, recv_buf[i]);
        recv_buf[i] = -1;
    }

    req->comp_req->flags = 0;
    ret_int = ucg_builtin_comp_recv_one_then_send_cb(req, offset, (void *)data, length);
    ASSERT_EQ(1, ret_int);
    for (int i = 0; i < count; i++) {
        ASSERT_EQ(i, recv_buf[i]);
        recv_buf[i] = -1;
    }

    req->comp_req->flags = 0;
    ret_int = ucg_builtin_comp_recv_noncontig_one_then_send_cb(req, offset, (void *)data, length);
    ASSERT_EQ(1, ret_int);
    for (int i = 0; i < count; i++) {
        ASSERT_EQ(i, recv_buf[i]);
    }

    delete[] data;
    delete dt_gen;
}

TEST_F(ucg_cb_test, test_recv_cb_recv_var_one) {
    ucg_builtin_plan_phase_t *phase = create_phase(UCG_PLAN_METHOD_RECV_TERMINAL);
    unsigned extra_flags = UCG_BUILTIN_OP_STEP_FLAG_LAST_STEP;
    unsigned base_am_id = 0;
    ucg_group_id_t group_id = 0;
    ucg_collective_params_t *params = create_allreduce_params();
    int8_t *current_data_buffer = NULL;
    ucg_builtin_op_step_t *step = new ucg_builtin_op_step_t();
    ucg_op_t *op = new ucg_op_t();
    ucp_datatype_t dtype = ucp_dt_make_contig(sizeof(int));
    ucp_dt_generic_t *dt_gen = new ucp_dt_generic_t;

    ucs_status_t ret = ucg_builtin_step_create((ucg_builtin_op_t *)op, phase, dtype, dtype, extra_flags, base_am_id, group_id,
                                               params, &current_data_buffer, step);
    ASSERT_EQ(UCS_OK, ret);

    ucg_builtin_request_t *req = create_request(step);
    req->step->non_contig.unpack_state = (void *)step->recv_buffer;
    req->op->recv_dt = dt_gen;
    req->op->recv_dt->ops.unpack = coll_ucx_generic_datatype_unpack;
    int8_t *buf = new int8_t[1]();
    int *displs = new int[1]();
    ucg_builtin_coll_params_t coll_params = {
        .init_buf = buf,
        .displs = displs
    };
    req->step->recv_coll_params = &coll_params;
    req->step->recv_buffer = coll_params.init_buf;

    int *recv_buf = (int *)step->recv_buffer;
    uint64_t offset = 0;
    typedef struct rank_data_s {
        ucg_group_member_index_t rank;
        int8_t                   data;
    } rank_data_t;
    rank_data_t rank_data = {0, 1};

    req->comp_req->flags = 0;
    int ret_int = ucg_builtin_comp_recv_var_one_cb(req, offset, &rank_data, sizeof(rank_data));
    ASSERT_EQ(1, ret_int);
    int8_t actual = *((int8_t *)recv_buf);
    ASSERT_EQ(1, (int)actual);

    delete[] buf;
    delete[] displs;
    delete dt_gen;
}

TEST_F(ucg_cb_test, test_recv_cb_recv_many) {
    ucg_builtin_plan_phase_t *phase = create_phase(UCG_PLAN_METHOD_RECV_TERMINAL);
    unsigned extra_flags = UCG_BUILTIN_OP_STEP_FLAG_LAST_STEP;
    unsigned base_am_id = 0;
    ucg_group_id_t group_id = 0;
    ucg_collective_params_t *params = create_allreduce_params();
    int8_t *current_data_buffer = NULL;
    ucg_builtin_op_step_t *step = new ucg_builtin_op_step_t();
    ucg_op_t *op = new ucg_op_t();
    ucp_datatype_t dtype = ucp_dt_make_contig(sizeof(int));
    ucp_dt_generic_t *dt_gen = new ucp_dt_generic_t;

    ucs_status_t ret = ucg_builtin_step_create((ucg_builtin_op_t *)op, phase, dtype, dtype, extra_flags, base_am_id,
                                               group_id, params, &current_data_buffer, step);
    ASSERT_EQ(UCS_OK, ret);

    ucg_builtin_request_t *req = create_request(step);
    req->step->non_contig.unpack_state = (void *)step->recv_buffer;
    req->op->recv_dt = dt_gen;
    req->op->recv_dt->ops.unpack = coll_ucx_generic_datatype_unpack;

    uint64_t offset = 0;
    int count = 2;
    int *data = new int[count];
    for (int i = 0; i < count; i++) {
        data[i] = i;
    }
    size_t length = sizeof(int) * count;

    req->pending = 2;
    int ret_int = ucg_builtin_comp_recv_many_cb(req, offset, (void *)data, length);
    ASSERT_EQ(0, ret_int);
    ret_int = ucg_builtin_comp_recv_many_cb(req, offset, (void *)data, length);
    ASSERT_EQ(1, ret_int);

    req->comp_req->flags = 0;
    req->pending = 2;
    ret_int = ucg_builtin_comp_recv_noncontig_many_cb(req, offset, (void *)data, length);
    ASSERT_EQ(0, ret_int);
    ret_int = ucg_builtin_comp_recv_noncontig_many_cb(req, offset, (void *)data, length);
    ASSERT_EQ(1, ret_int);

    req->comp_req->flags = 0;
    req->pending = 2;
    ret_int = ucg_builtin_comp_recv_many_then_send_cb(req, offset, (void *)data, length);
    ASSERT_EQ(0, ret_int);
    ret_int = ucg_builtin_comp_recv_many_then_send_cb(req, offset, (void *)data, length);
    ASSERT_EQ(1, ret_int);

    req->comp_req->flags = 0;
    req->pending = 2;
    ret_int = ucg_builtin_comp_recv_noncontig_many_then_send_cb(req, offset, (void *)data, length);
    ASSERT_EQ(0, ret_int);
    ret_int = ucg_builtin_comp_recv_noncontig_many_then_send_cb(req, offset, (void *)data, length);
    ASSERT_EQ(1, ret_int);

    req->comp_req->flags = 0;
    uint8_t frag_pending = 1;
    step->fragment_pending = &frag_pending;
    step->fragment_length = 1;
    step->iter_offset = UCG_BUILTIN_OFFSET_PIPELINE_PENDING;
    ret_int = ucg_builtin_comp_recv_many_then_send_pipe_cb(req, offset, (void *)data, length);
    ASSERT_EQ(1, ret_int);
    ASSERT_EQ(UCG_BUILTIN_FRAG_PENDING, step->fragment_pending[offset / step->fragment_length]);

    frag_pending = 1;
    step->fragment_pending = &frag_pending;
    step->iter_offset = UCG_BUILTIN_OFFSET_PIPELINE_READY;
    ret_int = ucg_builtin_comp_recv_many_then_send_pipe_cb(req, offset, (void *)data, length);
    ASSERT_EQ(1, ret_int);

    req->comp_req->flags = 0;
    frag_pending = 1;
    step->fragment_pending = &frag_pending;
    step->fragment_length = 1;
    step->iter_offset = UCG_BUILTIN_OFFSET_PIPELINE_PENDING;
    ret_int = ucg_builtin_comp_recv_noncontig_many_then_send_pipe_cb(req, offset, (void *)data, length);
    ASSERT_EQ(1, ret_int);
    ASSERT_EQ(UCG_BUILTIN_FRAG_PENDING, step->fragment_pending[offset / step->fragment_length]);

    frag_pending = 1;
    step->fragment_pending = &frag_pending;
    step->iter_offset = UCG_BUILTIN_OFFSET_PIPELINE_READY;
    ret_int = ucg_builtin_comp_recv_noncontig_many_then_send_pipe_cb(req, offset, (void *)data, length);
    ASSERT_EQ(1, ret_int);

    delete[] data;
    delete dt_gen;
}

TEST_F(ucg_cb_test, test_recv_cb_recv_var_many) {
    ucg_builtin_plan_phase_t *phase = create_phase(UCG_PLAN_METHOD_RECV_TERMINAL);
    unsigned extra_flags = UCG_BUILTIN_OP_STEP_FLAG_LAST_STEP;
    unsigned base_am_id = 0;
    ucg_group_id_t group_id = 0;
    ucg_collective_params_t *params = create_allreduce_params();
    int8_t *current_data_buffer = NULL;
    ucg_builtin_op_step_t *step = new ucg_builtin_op_step_t();
    ucg_op_t *op = new ucg_op_t();
    ucp_datatype_t dtype = ucp_dt_make_contig(sizeof(int));
    ucp_dt_generic_t *dt_gen = new ucp_dt_generic_t;

    ucs_status_t ret = ucg_builtin_step_create((ucg_builtin_op_t *)op, phase, dtype, dtype, extra_flags, base_am_id, group_id,
                                               params, &current_data_buffer, step);
    ASSERT_EQ(UCS_OK, ret);

    ucg_builtin_request_t *req = create_request(step);
    req->step->non_contig.unpack_state = (void *)step->recv_buffer;
    req->op->recv_dt = dt_gen;
    req->op->recv_dt->ops.unpack = coll_ucx_generic_datatype_unpack;
    int8_t *buf = new int8_t[1]();
    int *displs = new int[1]();
    ucg_builtin_coll_params_t coll_params = {
        .init_buf = buf,
        .displs = displs
    };
    req->step->recv_coll_params = &coll_params;
    req->step->recv_buffer = coll_params.init_buf;

    uint64_t offset = 0;
    typedef struct rank_data_s {
        ucg_group_member_index_t rank;
        int8_t                   data;
    } rank_data_t;
    rank_data_t rank_data = {0, 1};

    req->pending = 2;
    int ret_int = ucg_builtin_comp_recv_var_many_cb(req, offset, &rank_data, sizeof(rank_data));
    ASSERT_EQ(0, ret_int);
    ret_int = ucg_builtin_comp_recv_var_many_cb(req, offset, &rank_data, sizeof(rank_data));
    ASSERT_EQ(1, ret_int);

    delete dt_gen;
}

TEST_F(ucg_cb_test, test_recv_cb_reduce_one) {
    ucg_builtin_plan_phase_t *phase = create_phase(UCG_PLAN_METHOD_RECV_TERMINAL);
    unsigned extra_flags = UCG_BUILTIN_OP_STEP_FLAG_LAST_STEP;
    unsigned base_am_id = 0;
    ucg_group_id_t group_id = 0;
    ucg_collective_params_t *params = create_allreduce_params();
    int8_t *current_data_buffer = NULL;
    ucg_builtin_op_step_t *step = new ucg_builtin_op_step_t();
    ucg_op_t *op = new ucg_op_t();
    ucp_datatype_t dtype = ucp_dt_make_contig(sizeof(int));

    ucs_status_t ret = ucg_builtin_step_create((ucg_builtin_op_t *)op, phase, dtype, dtype, extra_flags, base_am_id,
                                               group_id, params, &current_data_buffer, step);
    ASSERT_EQ(UCS_OK, ret);

    ucg_builtin_request_t *req = create_request(step);

    uint64_t offset = 0;
    int count = 2;
    int *data = new int[count];
    for (int i = 0; i < count; i++) {
        data[i] = i;
    }
    size_t length = sizeof(int) * count;

    params->recv.count = count;
    params->recv.dt_len = sizeof(int);
    req->op->super.params = *params;
    ucg_builtin_mpi_reduce_cb = reduce_mock;

    int ret_int = ucg_builtin_comp_reduce_one_cb(req, offset, (void *)data, length);
    ASSERT_EQ(1, ret_int);

    req->comp_req->flags = 0;
    ret_int = ucg_builtin_comp_reduce_one_then_send_cb(req, offset, (void *)data, length);
    ASSERT_EQ(1, ret_int);
}

TEST_F(ucg_cb_test, test_recv_cb_reduce_many) {
    ucg_builtin_plan_phase_t *phase = create_phase(UCG_PLAN_METHOD_RECV_TERMINAL);
    unsigned extra_flags = UCG_BUILTIN_OP_STEP_FLAG_LAST_STEP;
    unsigned base_am_id = 0;
    ucg_group_id_t group_id = 0;
    ucg_collective_params_t *params = create_allreduce_params();
    int8_t *current_data_buffer = NULL;
    ucg_builtin_op_step_t *step = new ucg_builtin_op_step_t();
    ucg_op_t *op = new ucg_op_t();
    ucp_datatype_t dtype = ucp_dt_make_contig(sizeof(int));

    ucs_status_t ret = ucg_builtin_step_create((ucg_builtin_op_t *)op, phase, dtype, dtype, extra_flags, base_am_id,
                                               group_id, params, &current_data_buffer, step);
    ASSERT_EQ(UCS_OK, ret);

    ucg_builtin_request_t *req = create_request(step);

    uint64_t offset = 0;
    int count = 2;
    int *data = new int[count];
    for (int i = 0; i < count; i++) {
        data[i] = i;
    }
    size_t length = sizeof(int) * count;

    params->recv.count = count;
    params->recv.dt_len = sizeof(int);
    req->op->super.params = *params;
    ucg_builtin_mpi_reduce_cb = reduce_mock;

    req->pending = 2;
    int ret_int = ucg_builtin_comp_reduce_many_cb(req, offset, (void *)data, length);
    ASSERT_EQ(0, ret_int);
    ret_int = ucg_builtin_comp_reduce_many_cb(req, offset, (void *)data, length);
    ASSERT_EQ(1, ret_int);

    req->comp_req->flags = 0;
    req->pending = 2;
    ret_int = ucg_builtin_comp_reduce_many_then_send_cb(req, offset, (void *)data, length);
    ASSERT_EQ(0, ret_int);
    ret_int = ucg_builtin_comp_reduce_many_then_send_cb(req, offset, (void *)data, length);
    ASSERT_EQ(1, ret_int);

    req->comp_req->flags = 0;
    req->pending = 2;
    uint8_t frag_pending = 1;
    step->fragment_pending = &frag_pending;
    step->fragment_length = 1;
    step->iter_offset = UCG_BUILTIN_OFFSET_PIPELINE_PENDING;
    ret_int = ucg_builtin_comp_reduce_many_then_send_pipe_cb(req, offset, (void *)data, length);
    ASSERT_EQ(1, ret_int);
}

TEST_F(ucg_cb_test, test_recv_cb_reduce_full) {
    ucg_builtin_plan_phase_t *phase = create_phase(UCG_PLAN_METHOD_RECV_TERMINAL);
    unsigned extra_flags = UCG_BUILTIN_OP_STEP_FLAG_LAST_STEP;
    unsigned base_am_id = 0;
    ucg_group_id_t group_id = 0;
    ucg_collective_params_t *params = create_allreduce_params();
    int8_t *current_data_buffer = NULL;
    ucg_builtin_op_step_t *step = new ucg_builtin_op_step_t();
    ucg_op_t *op = new ucg_op_t();
    ucp_datatype_t dtype = ucp_dt_make_contig(sizeof(int));

    ucs_status_t ret = ucg_builtin_step_create((ucg_builtin_op_t *)op, phase, dtype, dtype, extra_flags, base_am_id,
                                               group_id, params, &current_data_buffer, step);
    ASSERT_EQ(UCS_OK, ret);

    ucg_builtin_request_t *req = create_request(step);

    uint64_t offset = 0;
    int count = 2;
    int *data = new int[count];
    int *cache = new int[count];
    for (int i = 0; i < count; i++) {
        data[i] = i;
        cache[i] = -1;
    }
    size_t length = sizeof(int) * count;

    phase->recv_cache_buffer = (int8_t *)cache;
    ucg_builtin_mpi_reduce_cb = reduce_mock;

    req->pending = 1;
    int ret_int = ucg_builtin_comp_reduce_full_cb(req, offset, (void *)data, length);
    ASSERT_EQ(1, ret_int);
    for (int i = 0; i < count; i++) {
        ASSERT_EQ(i, cache[i]);
        cache[i] = -1;
    }

    req->comp_req->flags = 0;
    req->pending = 1;
    ret_int = ucg_builtin_comp_reduce_full_then_send_cb(req, offset, (void *)data, length);
    ASSERT_EQ(1, ret_int);
    for (int i = 0; i < count; i++) {
        ASSERT_EQ(i, cache[i]);
    }
}

TEST_F(ucg_cb_test, test_recv_cb_wait) {
    ucg_builtin_plan_phase_t *phase = create_phase(UCG_PLAN_METHOD_RECV_TERMINAL);
    unsigned extra_flags = UCG_BUILTIN_OP_STEP_FLAG_LAST_STEP;
    unsigned base_am_id = 0;
    ucg_group_id_t group_id = 0;
    ucg_collective_params_t *params = create_allreduce_params();
    int8_t *current_data_buffer = NULL;
    ucg_builtin_op_step_t *step = new ucg_builtin_op_step_t();
    ucg_op_t *op = new ucg_op_t();
    ucp_datatype_t dtype = ucp_dt_make_contig(sizeof(int));

    ucs_status_t ret = ucg_builtin_step_create((ucg_builtin_op_t *)op, phase, dtype, dtype, extra_flags, base_am_id,
                                               group_id, params, &current_data_buffer, step);
    ASSERT_EQ(UCS_OK, ret);

    ucg_builtin_request_t *req = create_request(step);

    uint64_t offset = 0;
    int count = 2;
    int *data = new int[count];
    size_t length = sizeof(int) * count;

    int ret_int = ucg_builtin_comp_wait_one_cb(req, offset, (void *)data, length);
    ASSERT_EQ(1, ret_int);

    req->comp_req->flags = 0;
    ret_int = ucg_builtin_comp_wait_one_then_send_cb(req, offset, (void *)data, length);
    ASSERT_EQ(1, ret_int);

    req->comp_req->flags = 0;
    req->pending = 2;
    ret_int = ucg_builtin_comp_wait_many_cb(req, offset, (void *)data, length);
    ASSERT_EQ(0, ret_int);

    ret_int = ucg_builtin_comp_wait_many_then_send_cb(req, offset, (void *)data, length);
    ASSERT_EQ(1, ret_int);
}

TEST_F(ucg_cb_test, test_recv_cb_last_barrier) {
    ucg_builtin_plan_phase_t *phase = create_phase(UCG_PLAN_METHOD_RECV_TERMINAL);
    unsigned extra_flags = UCG_BUILTIN_OP_STEP_FLAG_LAST_STEP;
    unsigned base_am_id = 0;
    ucg_group_id_t group_id = 0;
    ucg_collective_params_t *params = create_allreduce_params();
    int8_t *current_data_buffer = NULL;
    ucg_builtin_op_step_t *step = new ucg_builtin_op_step_t();
    ucg_op_t *op = new ucg_op_t();
    ucp_datatype_t dtype = ucp_dt_make_contig(sizeof(int));

    ucs_status_t ret = ucg_builtin_step_create((ucg_builtin_op_t *)op, phase, dtype, dtype, extra_flags, base_am_id,
                                               group_id, params, &current_data_buffer, step);
    ASSERT_EQ(UCS_OK, ret);

    ucg_builtin_request_t *req = create_request(step);

    ucg_group_h group = create_group();
    ucg_plan_t *plan = create_plan(1, params, group);
    req->op->super.plan = plan;

    uint64_t offset = 0;
    int count = 2;
    int *data = new int[count];
    size_t length = sizeof(int) * count;

    group->is_barrier_outstanding = 1;
    int ret_int = ucg_builtin_comp_last_barrier_step_one_cb(req, offset, (void *)data, length);
    ASSERT_EQ(1, ret_int);

    req->comp_req->flags = 0;
    group->is_barrier_outstanding = 1;
    req->pending = 2;
    ret_int = ucg_builtin_comp_last_barrier_step_many_cb(req, offset, (void *)data, length);
    ASSERT_EQ(0, ret_int);
    ret_int = ucg_builtin_comp_last_barrier_step_many_cb(req, offset, (void *)data, length);
    ASSERT_EQ(1, ret_int);
}

TEST_F(ucg_cb_test, test_zcopy_step_check_cb) {
    ucg_builtin_plan_phase_t *phase = create_phase(UCG_PLAN_METHOD_RECV_TERMINAL);
    unsigned extra_flags = UCG_BUILTIN_OP_STEP_FLAG_LAST_STEP;
    unsigned base_am_id = 0;
    ucg_group_id_t group_id = 0;
    ucg_collective_params_t *params = create_allreduce_params();
    int8_t *current_data_buffer = NULL;
    ucg_builtin_op_step_t *step = new ucg_builtin_op_step_t();
    ucg_op_t *op = new ucg_op_t();
    ucp_datatype_t dtype = ucp_dt_make_contig(sizeof(int));

    ucs_status_t ret = ucg_builtin_step_create((ucg_builtin_op_t *)op, phase, dtype, dtype, extra_flags, base_am_id,
                                               group_id, params, &current_data_buffer, step);
    ASSERT_EQ(UCS_OK, ret);

    ucg_builtin_request_t *req = create_request(step);
    ucg_builtin_zcomp_t *zcomp = new ucg_builtin_zcomp_t;
    zcomp->req = req;
    uct_completion_t *self = &zcomp->comp;

    req->pending = 1;
    step->zcopy.num_store = 0;
    ucg_builtin_step_am_zcopy_comp_step_check_cb(self, UCS_OK);

    req->pending = 2;
    step->zcopy.num_store = 1;
    ucg_plan_t *plan = new ucg_plan_t;
    plan->planner = &ucg_builtin_component;
    req->op->super.plan = plan;
    ucg_builtin_step_am_zcopy_comp_step_check_cb(self, UCS_OK);
}

TEST_F(ucg_cb_test, test_zcopy_prep) {
    ucg_builtin_plan_phase_t *phase = create_phase(UCG_PLAN_METHOD_RECV_TERMINAL);
    unsigned extra_flags = UCG_BUILTIN_OP_STEP_FLAG_LAST_STEP;
    unsigned base_am_id = 0;
    ucg_group_id_t group_id = 0;
    ucg_collective_params_t *params = create_allreduce_params();
    int8_t *current_data_buffer = NULL;
    ucg_builtin_op_step_t *step = new ucg_builtin_op_step_t();
    ucg_op_t *op = new ucg_op_t();
    ucp_datatype_t dtype = ucp_dt_make_contig(sizeof(int));

    ucs_status_t ret = ucg_builtin_step_create((ucg_builtin_op_t *)op, phase, dtype, dtype, extra_flags, base_am_id,
                                               group_id, params, &current_data_buffer, step);
    ASSERT_EQ(UCS_OK, ret);

    uct_md_h md = create_md();
    step->uct_md = md;

    ret = ucg_builtin_step_zcopy_prep(step);
    ASSERT_EQ(UCS_OK, ret);
}

TEST_F(ucg_cb_test, test_bcopy_to_zcopy) {
    ucg_builtin_plan_phase_t *phase = create_phase(UCG_PLAN_METHOD_RECV_TERMINAL);
    unsigned extra_flags = UCG_BUILTIN_OP_STEP_FLAG_LAST_STEP | UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_BCOPY;
    unsigned base_am_id = 0;
    ucg_group_id_t group_id = 0;
    ucg_collective_params_t *params = create_allreduce_params();
    int8_t *current_data_buffer = NULL;
    ucg_builtin_op_step_t *step = new ucg_builtin_op_step_t();
    ucg_op_t *ucg_op = new ucg_op_t();
    ucp_datatype_t dtype = ucp_dt_make_contig(sizeof(int));

    ucs_status_t ret = ucg_builtin_step_create((ucg_builtin_op_t *)ucg_op, phase, dtype, dtype, extra_flags,
                                               base_am_id, group_id, params, &current_data_buffer, step);
    ASSERT_EQ(UCS_OK, ret);

    uct_md_h md = create_md();
    step->uct_md = md;
    step->recv_cb = ucg_builtin_comp_reduce_one_cb;

    ucg_builtin_op_t *op = (ucg_builtin_op_t *)malloc(sizeof(ucg_builtin_op_t) + sizeof(ucg_builtin_op_step_t));
    op->steps[0] = *step;

    ret = ucg_builtin_optimize_bcopy_to_zcopy(op);
    ASSERT_EQ(UCS_OK, ret);
}

/**
 * Test: ucg_builtin_step_select_callbacks
 */
TEST_F(ucg_cb_test, test_step_callback_select_termonal) {
    ucg_builtin_plan_method_type method[] = {UCG_PLAN_METHOD_SEND_TERMINAL, UCG_PLAN_METHOD_RECV_TERMINAL,
                                             UCG_PLAN_METHOD_SCATTER_TERMINAL};

    int len = sizeof(method) / sizeof(method[0]);
    for (int i = 0; i < len; i++) {
        ucg_builtin_plan_phase_t *phase = create_phase(method[i]);
        ucg_builtin_comp_recv_cb_t *recv_cb = new ucg_builtin_comp_recv_cb_t();
        int nonzero_length = 0;
        int flags = 0;

        ucs_status_t ret = ucg_builtin_step_select_callbacks(phase, 1, recv_cb, nonzero_length, flags);
        ASSERT_EQ(UCS_OK, ret);

        flags |= UCG_BUILTIN_OP_STEP_FLAG_SINGLE_ENDPOINT;
        ret = ucg_builtin_step_select_callbacks(phase, 1, recv_cb, nonzero_length, flags);
        ASSERT_EQ(UCS_OK, ret);
    }
}

TEST_F(ucg_cb_test, test_step_callback_select_waypoint_fanout) {
    ucg_builtin_plan_method_type method[] = {UCG_PLAN_METHOD_BCAST_WAYPOINT, UCG_PLAN_METHOD_SCATTER_WAYPOINT,
                                             UCG_PLAN_METHOD_GATHER_WAYPOINT};

    int len = sizeof(method) / sizeof(method[0]);
    for (int i = 0; i < len; i++) {
        ucg_builtin_plan_phase_t *phase = create_phase(method[i]);
        ucg_builtin_comp_recv_cb_t *recv_cb = new ucg_builtin_comp_recv_cb_t();
        int nonzero_length = 0;
        int flags = 0;

        ucs_status_t ret = ucg_builtin_step_select_callbacks(phase, 1, recv_cb, nonzero_length, flags);
        ASSERT_EQ(UCS_OK, ret);

        nonzero_length = 1;
        ret = ucg_builtin_step_select_callbacks(phase, 1, recv_cb, nonzero_length, flags);
        ASSERT_EQ(UCS_OK, ret);

        flags |= UCG_BUILTIN_OP_STEP_FLAG_FRAGMENTED;
        ret = ucg_builtin_step_select_callbacks(phase, 1, recv_cb, nonzero_length, flags);
        ASSERT_EQ(UCS_OK, ret);
    }
}

TEST_F(ucg_cb_test, test_step_callback_select_reduce) {
    ucg_builtin_plan_method_type method[] = {UCG_PLAN_METHOD_REDUCE_TERMINAL, UCG_PLAN_METHOD_REDUCE_RECURSIVE,
                                             UCG_PLAN_METHOD_REDUCE_WAYPOINT};

    int len = sizeof(method) / sizeof(method[0]);
    for (int i = 0; i < len; i++) {
        ucg_builtin_plan_phase_t *phase = create_phase(method[i]);
        ucg_builtin_comp_recv_cb_t *recv_cb = new ucg_builtin_comp_recv_cb_t();

        for (int nonzero_length = 0; nonzero_length < 2; nonzero_length++) {
            int flags = 0;
            ucs_status_t ret = ucg_builtin_step_select_callbacks(phase, 1, recv_cb, nonzero_length, flags);
            ASSERT_EQ(UCS_OK, ret);

            flags |= UCG_BUILTIN_OP_STEP_FLAG_SINGLE_ENDPOINT;
            ret = ucg_builtin_step_select_callbacks(phase, 1, recv_cb, nonzero_length, flags);
            ASSERT_EQ(UCS_OK, ret);
        }
    }
}

TEST_F(ucg_cb_test, test_step_callback_select_nonzero) {
    ucg_builtin_plan_method_type method[] = {UCG_PLAN_METHOD_ALLGATHER_RECURSIVE,
                                             UCG_PLAN_METHOD_REDUCE_SCATTER_RING};

    int len = sizeof(method) / sizeof(method[0]);
    for (int i = 0; i < len; i++) {
        ucg_builtin_plan_phase_t *phase = create_phase(method[i]);
        ucg_builtin_comp_recv_cb_t *recv_cb = new ucg_builtin_comp_recv_cb_t();

        for (int nonzero_length = 0; nonzero_length < 2; nonzero_length++) {
            int flags = 0;
            ucs_status_t ret = ucg_builtin_step_select_callbacks(phase, 1, recv_cb, nonzero_length, flags);
            ASSERT_EQ(UCS_OK, ret);
        }
    }
}

TEST_F(ucg_cb_test, test_step_callback_select_barrier) {
    ucg_builtin_plan_phase_t *phase = create_phase(UCG_PLAN_METHOD_ALLGATHER_RING);
    ucg_builtin_comp_recv_cb_t *recv_cb = new ucg_builtin_comp_recv_cb_t();
    int nonzero_length = 0;
    int flags = UCG_BUILTIN_OP_STEP_FLAG_LAST_STEP;

    ucs_status_t ret = ucg_builtin_step_select_callbacks(phase, 1, recv_cb, nonzero_length, flags);
    ASSERT_EQ(UCS_OK, ret);

    flags |= UCG_BUILTIN_OP_STEP_FLAG_SINGLE_ENDPOINT;
    ret = ucg_builtin_step_select_callbacks(phase, 1, recv_cb, nonzero_length, flags);
    ASSERT_EQ(UCS_OK, ret);
}

/**
 * Test: ucg_builtin_op_select_callback
 */
TEST_F(ucg_cb_test, test_op_collback_select) {
    ucg_builtin_plan_method_type method[] = {UCG_PLAN_METHOD_SEND_TERMINAL, UCG_PLAN_METHOD_RECV_TERMINAL,
                                             UCG_PLAN_METHOD_BCAST_WAYPOINT, UCG_PLAN_METHOD_GATHER_WAYPOINT,
                                             UCG_PLAN_METHOD_SCATTER_TERMINAL, UCG_PLAN_METHOD_SCATTER_WAYPOINT,
                                             UCG_PLAN_METHOD_REDUCE_TERMINAL, UCG_PLAN_METHOD_REDUCE_WAYPOINT,
                                             UCG_PLAN_METHOD_REDUCE_RECURSIVE, UCG_PLAN_METHOD_NEIGHBOR,
                                             UCG_PLAN_METHOD_ALLGATHER_BRUCK, UCG_PLAN_METHOD_ALLGATHER_RECURSIVE,
                                             UCG_PLAN_METHOD_ALLTOALL_BRUCK, UCG_PLAN_METHOD_REDUCE_SCATTER_RING,
                                             UCG_PLAN_METHOD_ALLGATHER_RING, UCG_PLAN_METHOD_INC,
                                             UCG_PLAN_METHOD_EXCHANGE, UCG_PLAN_METHOD_ALLTOALLV_LADD};

    int len = sizeof(method) / sizeof(method[0]);
    for (int i = 0; i < len; i++) {
        ucg_builtin_plan_t *plan = create_method_plan(method[i]);
        ucg_builtin_op_init_cb_t *init_cb = new ucg_builtin_op_init_cb_t;
        ucg_builtin_op_final_cb_t *final_cb = new ucg_builtin_op_final_cb_t;
        ucp_datatype_t dtype = ucp_dt_make_contig(sizeof(int));

        ucs_status_t ret = ucg_builtin_op_select_callback(plan, dtype, dtype, init_cb, final_cb);

        ASSERT_EQ(UCS_OK, ret);
    }
}

/**
 * Test: ucg_builtin_op_consider_optimization
 */
TEST_F(ucg_cb_test, test_op_consider_optimization) {
    ucg_group_h group = create_group();
    ucg_collective_params_t *params = create_bcast_params();
    ucg_plan_t *plan = create_plan(1, params, group);

    ucg_op_t *op = new ucg_op_t();

    ucs_status_t ret = ucg_builtin_op_create(plan, params, &op);
    ASSERT_EQ(UCS_OK, ret);

    ret = ucg_builtin_op_consider_optimization((ucg_builtin_op_t*)op,
                                               (ucg_builtin_config_t*)plan->planner->plan_config);
    ASSERT_EQ(UCS_OK, ret);
}

TEST_F(ucg_cb_test, test_check_data_size) {
    ucs_status_t status;

    status = ucg_builtin_plummer_check_data_size(2147483647, 4);
    ASSERT_EQ(UCS_ERR_OUT_OF_RANGE, status);

    status = ucg_builtin_plummer_check_data_size(2, 4);
    ASSERT_EQ(UCS_OK, status);
}
TEST_F(ucg_cb_test, test_check_overflow) {
    ucs_status_t status;

    status = ucg_builtin_plummer_check_overflow(1, 2);
    ASSERT_EQ(UCS_OK, status);

    status = ucg_builtin_plummer_check_overflow(2, 1);
    ASSERT_EQ(UCS_ERR_OUT_OF_RANGE, status);
}
