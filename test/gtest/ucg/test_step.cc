/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2019-2020.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#include "test_op.h"

using namespace std;

class ucg_step_test : public ucg_op_test {
public:
    ucg_step_test() {
        num_procs = 4;
        //ucs_global_opts.log_level = UCS_LOG_LEVEL_DEBUG;
    }

    ~ucg_step_test() = default;

    ucg_builtin_comp_slot_t *create_slot(ucg_builtin_op_step_t *step);

    uct_iface_h create_iface();
};

ucg_builtin_comp_slot_t *ucg_step_test::create_slot(ucg_builtin_op_step_t *step) {
    ucg_builtin_comp_slot_t *slot = new ucg_builtin_comp_slot_t;
    slot->cb = NULL;
    slot->step_idx = 0;
    slot->coll_id = 0;
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

    ucg_plan_t *plan = new ucg_plan_t;
    plan->planner = &ucg_builtin_component;
    req->op->super.plan = plan;

    return slot;
}

static ucs_status_t ep_am_short_mock(uct_ep_h ep, uint8_t id, uint64_t header, const void *payload, unsigned length) {
    // do nothing
    return UCS_OK;
}

static ssize_t ep_am_bcopy_mock(uct_ep_h ep, uint8_t id, uct_pack_callback_t pack_cb, void *arg, unsigned flags) {
    void *dest = new ucg_builtin_header_t + (sizeof(int) * 10);
    return pack_cb(dest, arg);
}

static ucs_status_t ep_am_zcopy_mock(uct_ep_h ep, uint8_t id, const void *header,
                                     unsigned header_length, const uct_iov_t *iov,
                                     size_t iovcnt, unsigned flags,
                                     uct_completion_t *comp) {
    return UCS_INPROGRESS;
}

uct_iface_h ucg_step_test::create_iface() {
    uct_iface_h iface = new uct_iface;
    iface->ops.ep_am_short = ep_am_short_mock;
    iface->ops.ep_am_bcopy = ep_am_bcopy_mock;
    iface->ops.ep_am_zcopy = ep_am_zcopy_mock;

    return iface;
}

/**
 * Test: ucg_builtin_step_create
 */

TEST_F(ucg_step_test, test_step_create_method) {
    ucg_builtin_plan_method_type method[] = {UCG_PLAN_METHOD_SEND_TERMINAL, UCG_PLAN_METHOD_RECV_TERMINAL,
                                             UCG_PLAN_METHOD_BCAST_WAYPOINT, UCG_PLAN_METHOD_GATHER_WAYPOINT,
                                             UCG_PLAN_METHOD_SCATTER_TERMINAL, UCG_PLAN_METHOD_SCATTER_WAYPOINT,
                                             UCG_PLAN_METHOD_REDUCE_TERMINAL, UCG_PLAN_METHOD_REDUCE_WAYPOINT,
                                             UCG_PLAN_METHOD_REDUCE_RECURSIVE, UCG_PLAN_METHOD_ALLGATHER_BRUCK,
                                             UCG_PLAN_METHOD_REDUCE_SCATTER_RING, UCG_PLAN_METHOD_ALLGATHER_RING,
                                             UCG_PLAN_METHOD_ALLGATHER_RECURSIVE, UCG_PLAN_METHOD_INC,
                                             UCG_PLAN_METHOD_EXCHANGE};
    int len = sizeof(method) / sizeof(method[0]);
    for (int i = 0; i < len; i++) {
        ucg_builtin_plan_phase_t *phase = create_phase(method[i]);
        unsigned extra_flags = 0;
        unsigned base_am_id = 0;
        ucg_group_id_t group_id = 0;
        ucg_collective_params_t *params = create_allreduce_params();
        int8_t *current_data_buffer = NULL;
        ucg_builtin_op_step_t *step = new ucg_builtin_op_step_t();
        ucg_op_t *ucg_op = new ucg_op_t();
        ucp_datatype_t dtype = ucp_dt_make_contig(sizeof(int));

        ucs_status_t ret = ucg_builtin_step_create((ucg_builtin_op_t *)ucg_op, phase, dtype, dtype, extra_flags,
                                                   base_am_id, group_id, params, &current_data_buffer, step);

        if (method[i] == UCG_PLAN_METHOD_NEIGHBOR) {
            ASSERT_EQ(UCS_ERR_INVALID_PARAM, ret);
        } else {
            ASSERT_EQ(UCS_OK, ret);
        }
    }
}

TEST_F(ucg_step_test, test_step_create_short) {
    ucg_builtin_plan_phase_t *phase = create_phase(UCG_PLAN_METHOD_ALLGATHER_BRUCK);
    size_t one_count = phase->send_thresh.max_short_one / (sizeof(int) * (1 << phase->step_index));
    size_t fragmented_count = phase->send_thresh.max_short_max / (sizeof(int) * (1 << phase->step_index));

    size_t len[] = {one_count, fragmented_count};
    for (int i = 0; i < 2; i++) {
        size_t count = len[i];
        int *send_buf = new int[count];
        int *recv_buf = new int[count];
        for (size_t i = 0; i < count; i++) {
            send_buf[i] = 1;
            recv_buf[i] = -1;
        }
        ucg_ompi_op *op = new ucg_ompi_op();

        ucg_collective_params_t *params = m_resource_factory->create_collective_params((ucg_collective_modifiers)
                                                                                               (UCG_GROUP_COLLECTIVE_MODIFIER_AGGREGATE |
                                                                                                UCG_GROUP_COLLECTIVE_MODIFIER_BROADCAST),
                                                                                       0, send_buf, count, recv_buf,
                                                                                       sizeof(int), NULL, op);
        unsigned extra_flags = 0;
        unsigned base_am_id = 0;
        ucg_group_id_t group_id = 0;
        int8_t *current_data_buffer = NULL;
        ucg_builtin_op_step_t *step = new ucg_builtin_op_step_t();
        ucg_op_t *ucg_op = new ucg_op_t();
        ucp_datatype_t dtype = ucp_dt_make_contig(sizeof(int));

        ucs_status_t ret = ucg_builtin_step_create((ucg_builtin_op_t *)ucg_op, phase, dtype, dtype, extra_flags,
                                                   base_am_id, group_id, params, &current_data_buffer, step);
        ASSERT_EQ(UCS_OK, ret);
        ASSERT_EQ(UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_SHORT, step->flags & UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_SHORT);
        if (i) {
            ASSERT_EQ(UCG_BUILTIN_OP_STEP_FLAG_FRAGMENTED, step->flags & UCG_BUILTIN_OP_STEP_FLAG_FRAGMENTED);
        }
    }
}

TEST_F(ucg_step_test, test_step_create_bcopy) {
    ucg_builtin_plan_phase_t *phase = create_phase(UCG_PLAN_METHOD_ALLGATHER_BRUCK);
    size_t one_count = phase->send_thresh.max_bcopy_one / (sizeof(int) * (1 << phase->step_index));
    size_t fragmented_count = phase->send_thresh.max_bcopy_max / (sizeof(int) * (1 << phase->step_index));

    size_t len[] = {one_count, fragmented_count};
    for (int i = 0; i < 2; i++) {
        size_t count = len[i];
        int *send_buf = new int[count];
        int *recv_buf = new int[count];
        for (size_t i = 0; i < count; i++) {
            send_buf[i] = 1;
            recv_buf[i] = -1;
        }
        ucg_ompi_op *op = new ucg_ompi_op();

        ucg_collective_params_t *params = m_resource_factory->create_collective_params((ucg_collective_modifiers)
                                                                                               (UCG_GROUP_COLLECTIVE_MODIFIER_AGGREGATE |
                                                                                                UCG_GROUP_COLLECTIVE_MODIFIER_BROADCAST),
                                                                                       0, send_buf, count, recv_buf,
                                                                                       sizeof(int), NULL, op);
        unsigned extra_flags = 0;
        unsigned base_am_id = 0;
        ucg_group_id_t group_id = 0;
        int8_t *current_data_buffer = NULL;
        ucg_builtin_op_step_t *step = new ucg_builtin_op_step_t();
        ucg_op_t *ucg_op = new ucg_op_t();
        ucp_datatype_t dtype = ucp_dt_make_contig(sizeof(int));

        ucs_status_t ret = ucg_builtin_step_create((ucg_builtin_op_t *)ucg_op, phase, dtype, dtype, extra_flags,
                                                   base_am_id, group_id, params, &current_data_buffer, step);
        ASSERT_EQ(UCS_OK, ret);
        ASSERT_EQ(UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_BCOPY, step->flags & UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_BCOPY);
        if (i) {
            ASSERT_EQ(UCG_BUILTIN_OP_STEP_FLAG_FRAGMENTED, step->flags & UCG_BUILTIN_OP_STEP_FLAG_FRAGMENTED);
        }
    }
}

TEST_F(ucg_step_test, test_step_create_zcopy) {
    ucg_builtin_plan_phase_t *phase = create_phase(UCG_PLAN_METHOD_ALLGATHER_BRUCK);
    uct_md_h md = create_md();
    phase->md = md;
    phase->single_ep = new uct_ep;

    size_t one_count = (phase->send_thresh.max_bcopy_max / (sizeof(int) * (1 << phase->step_index))) + 1;
    size_t fragmented_count = phase->send_thresh.max_zcopy_one / (sizeof(int) * (1 << phase->step_index));

    size_t len[] = {one_count, fragmented_count};
    for (int i = 0; i < 2; i++) {
        size_t count = len[i];
        int *send_buf = new int[count];
        int *recv_buf = new int[count];
        for (size_t i = 0; i < count; i++) {
            send_buf[i] = 1;
            recv_buf[i] = -1;
        }
        ucg_ompi_op *op = new ucg_ompi_op();

        ucg_collective_params_t *params = m_resource_factory->create_collective_params((ucg_collective_modifiers)
                                                                                               (UCG_GROUP_COLLECTIVE_MODIFIER_AGGREGATE |
                                                                                                UCG_GROUP_COLLECTIVE_MODIFIER_BROADCAST),
                                                                                       0, send_buf, count, recv_buf,
                                                                                       sizeof(int), NULL, op);
        unsigned extra_flags = 0;
        unsigned base_am_id = 0;
        ucg_group_id_t group_id = 0;
        int8_t *current_data_buffer = NULL;
        ucg_builtin_op_step_t *step = new ucg_builtin_op_step_t();
        ucg_op_t *ucg_op = new ucg_op_t();
        ucp_datatype_t dtype = ucp_dt_make_contig(sizeof(int));

        ucs_status_t ret = ucg_builtin_step_create((ucg_builtin_op_t *)ucg_op, phase, dtype, dtype, extra_flags,
                                                   base_am_id, group_id, params, &current_data_buffer, step);
        ASSERT_EQ(UCS_OK, ret);
        ASSERT_EQ(UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_ZCOPY, step->flags & UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_ZCOPY);
        if (i) {
            ASSERT_EQ(UCG_BUILTIN_OP_STEP_FLAG_FRAGMENTED, step->flags & UCG_BUILTIN_OP_STEP_FLAG_FRAGMENTED);
        }
    }
}

/**
 * Test: ucg_builtin_step_execute
 */

TEST_F(ucg_step_test, test_msg_process) {
    ucg_builtin_plan_phase_t *phase = create_phase(UCG_PLAN_METHOD_RECV_TERMINAL);
    unsigned extra_flags = UCG_BUILTIN_OP_STEP_FLAG_LAST_STEP;
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

    ucg_builtin_comp_slot_t *slot = create_slot(step);

    ret = ucg_builtin_msg_process(slot, &slot->req);
    ASSERT_EQ(UCS_INPROGRESS, ret);

    int count = 2;
    int *data = new int[count];
    for (int i = 0; i < count; i++) {
        data[i] = i;
    }
    size_t length = sizeof(int) * count;
    int *recv = (int *) step->recv_buffer;
    slot->mp = &m_ucg_worker->am_mp;

    ucg_builtin_comp_desc_t *desc = (ucg_builtin_comp_desc_t *) ucs_mpool_get_inline(slot->mp);
    memcpy(&desc->data[0], (void *) data, length);
    desc->super.flags = 0;
    desc->super.length = length;
    desc->release = ucs_mpool_put_inline;
    ucs_list_add_tail(&slot->msg_head, &desc->super.tag_list[0]);

    ret = ucg_builtin_msg_process(slot, &slot->req);
    ASSERT_EQ(UCS_OK, ret);
    for (int i = 0; i < count; i++) {
        ASSERT_EQ(i, recv[i]);
        recv[i] = -2;
    }

    slot->req.comp_req->flags = 0;
    step->phase->is_swap = 1;
    desc = (ucg_builtin_comp_desc_t *) ucs_mpool_get_inline(slot->mp);
    memcpy(&desc->data[0], (void *) data, length);
    desc->super.flags = 0;
    desc->super.length = length;
    desc->release = ucs_mpool_put_inline;
    ucs_list_add_tail(&slot->msg_head, &desc->super.tag_list[0]);
    ret = ucg_builtin_msg_process(slot, &slot->req);
    ASSERT_EQ(UCS_OK, ret);
    for (int i = 0; i < count; i++) {
        ASSERT_EQ(-2, recv[i]);
    }
}

TEST_F(ucg_step_test, test_step_execute_short) {
    ucg_builtin_plan_phase_t *phase = create_phase(UCG_PLAN_METHOD_SEND_TERMINAL);
    phase->step_index = 0;
    unsigned extra_flags = UCG_BUILTIN_OP_STEP_FLAG_LAST_STEP;
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

    ucg_builtin_comp_slot_t *slot = create_slot(step);

    uct_iface_h iface = create_iface();
    uct_ep ep;
    ep.iface = iface;
    phase->single_ep = &ep;

    ret = ucg_builtin_step_execute(&slot->req, NULL);
    ASSERT_EQ(UCS_OK, ret);

    slot->req.comp_req->flags = 0;
    step->flags |= UCG_BUILTIN_OP_STEP_FLAG_FRAGMENTED;
    step->fragment_length = sizeof(int);
    ret = ucg_builtin_step_execute(&slot->req, NULL);
    delete iface;
    ASSERT_EQ(UCS_OK, ret);
}

TEST_F(ucg_step_test, test_step_execute_bcopy) {
    ucg_builtin_plan_phase_t *phase = create_phase(UCG_PLAN_METHOD_SEND_TERMINAL);
    phase->step_index = 0;
    unsigned extra_flags = UCG_BUILTIN_OP_STEP_FLAG_LAST_STEP;
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

    ucg_builtin_comp_slot_t *slot = create_slot(step);

    uct_iface_h iface = create_iface();
    step->flags |= UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_BCOPY;
    step->flags &= ~UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_SHORT;

    uct_ep ep;
    ep.iface = iface;
    phase->single_ep = &ep;

    ret = ucg_builtin_step_execute(&slot->req, NULL);
    ASSERT_EQ(UCS_OK, ret);

    slot->req.comp_req->flags = 0;
    step->flags |= UCG_BUILTIN_OP_STEP_FLAG_FRAGMENTED;
    step->fragment_length = sizeof(int);
    ret = ucg_builtin_step_execute(&slot->req, NULL);
    delete iface;
    ASSERT_EQ(UCS_OK, ret);
    m_ucg_worker = NULL;
    m_ucg_context = NULL;
}

TEST_F(ucg_step_test, test_step_execute_zcopy) {
    ucg_builtin_plan_phase_t *phase = create_phase(UCG_PLAN_METHOD_SEND_TERMINAL);
    phase->step_index = 0;
    unsigned extra_flags = UCG_BUILTIN_OP_STEP_FLAG_LAST_STEP;
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

    ucg_builtin_comp_slot_t *slot = create_slot(step);

    uct_iface_h iface = create_iface();
    ucg_builtin_zcomp_t *zcomp = new ucg_builtin_zcomp_t + sizeof(ucg_builtin_zcomp_t) * 2;
    step->zcopy.zcomp = zcomp;
    step->flags |= UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_ZCOPY;
    step->flags &= ~UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_SHORT;

    uct_ep ep;
    ep.iface = iface;
    phase->single_ep = &ep;

    slot->req.pending = 0;
    ret = ucg_builtin_step_execute(&slot->req, NULL);
    ASSERT_EQ(UCS_OK, ret);

    slot->req.comp_req->flags = 0;
    slot->req.pending = 1;
    step->flags |= UCG_BUILTIN_OP_STEP_FLAG_FRAGMENTED;
    step->fragment_length = sizeof(int);
    ret = ucg_builtin_step_execute(&slot->req, NULL);
    ASSERT_EQ(UCS_INPROGRESS, ret);

    slot->mp = &m_ucg_worker->am_mp;
    ucg_builtin_comp_desc_t *desc = (ucg_builtin_comp_desc_t *) ucs_mpool_get_inline(slot->mp);
    ucs_list_add_tail(&slot->msg_head, &desc->super.tag_list[0]);
    desc->release = ucs_mpool_put_inline;
    step->zcopy.num_store = 0;

    ret = ucg_builtin_step_execute(&slot->req, NULL);
    delete iface;
    ASSERT_EQ(UCS_OK, ret);
    ASSERT_EQ((unsigned)0, step->zcopy.num_store);
    m_ucg_worker = NULL;
    m_ucg_context = NULL;
}

TEST_F(ucg_step_test, test_step_pack_rank) {
    ucg_builtin_plan_phase_t *phase = create_phase(UCG_PLAN_METHOD_SEND_TERMINAL);
    phase->step_index = 0;
    unsigned extra_flags = UCG_BUILTIN_OP_STEP_FLAG_LAST_STEP;
    unsigned base_am_id = 0;
    ucg_group_id_t group_id = 0;
    ucg_collective_params_t *params = create_allreduce_params();
    int8_t *current_data_buffer = NULL;
    ucg_builtin_op_step_t *step = new ucg_builtin_op_step_t();
    ucg_builtin_op_t *ucg_op = new ucg_builtin_op_t();
    ucp_datatype_t dtype = ucp_dt_make_contig(sizeof(int));

    ucs_status_t ret = ucg_builtin_step_create(ucg_op, phase, dtype, dtype, extra_flags,
                                               base_am_id, group_id, params, &current_data_buffer, step);
    ASSERT_EQ(UCS_OK, ret);

    step->variable_length.pack_rank_buffer  = NULL;
    step->variable_length.pack_rank_func    = NULL;
    step->variable_length.unpack_rank_func  = NULL;
    step->phase->send_ep_cnt                = 1;
    step->phase->ex_attr.packed_rank        = 1;

    size_t buffer_length = 4;
    ret = ucg_builtin_step_alloc_pack_rank_buffer(step, buffer_length);
    ASSERT_EQ(UCS_OK, ret);

    size_t new_buffer_len;
    int send_buffer[1] = {1};
    step->variable_length.pack_rank_func(step, send_buffer, buffer_length, &new_buffer_len);

    ucg_builtin_step_free_pack_rank_buffer(step);
}

TEST_F(ucg_step_test, ucg_builtin_dynamic_send_recv) {
    ucg_builtin_plan_phase_t *phase = create_phase(UCG_PLAN_METHOD_SEND_TERMINAL);
    phase->step_index = 0;
    unsigned extra_flags = UCG_BUILTIN_OP_STEP_FLAG_LAST_STEP;
    unsigned base_am_id = 0;
    ucg_group_id_t group_id = 0;
    ucg_collective_params_t *params = create_alltoallv_params();
    int8_t *current_data_buffer = NULL;
    ucg_builtin_op_step_t *step = new ucg_builtin_op_step_t();
    ucg_builtin_op_t *ucg_op = new ucg_builtin_op_t();
    ucp_datatype_t dtype = ucp_dt_make_contig(sizeof(int));

    phase->ep_cnt                   = 12;
    phase->send_ep_cnt              = 6;
    phase->recv_ep_cnt              = 6;
    phase->multi_eps                = (uct_ep_h *)ucs_malloc(sizeof(uct_ep_h) * phase->ep_cnt, "uct_eps");
    phase->ep_thresh                = (ucg_builtin_tl_threshold_t *)ucs_malloc(sizeof(ucg_builtin_tl_threshold_t) * phase->ep_cnt, "tl threshold");
    phase->ex_attr.is_variable_len  = 1;
    phase->ex_attr.start_block      = 0;
    phase->ex_attr.recv_start_block = 0;
    phase->ex_attr.member_cnt       = 6;
    phase->ex_attr.packed_rank      = 0;

    for (int i = 0; i < phase->ep_cnt; i++) {
        phase->multi_eps[i]         = (uct_ep_h)ucs_malloc(sizeof(uct_ep_h), "uct_ep");
        uct_iface_h iface           = create_iface();
        phase->multi_eps[i]->iface  = iface;
        phase->ep_thresh[i]         = phase->send_thresh;
    }

    phase->method = UCG_PLAN_METHOD_ALLTOALLV_LADD;
    ucs_status_t ret = ucg_builtin_step_create(ucg_op, phase, dtype, dtype, extra_flags,
                                               base_am_id, group_id, params, &current_data_buffer, step);
    ASSERT_EQ(UCS_OK, ret);

    ucg_builtin_comp_slot_t *slot = create_slot(step);

    ucg_builtin_coll_params_t *send_coll_params = ucg_builtin_allocate_coll_params(phase->send_ep_cnt);
    ucg_builtin_coll_params_t *recv_coll_params = ucg_builtin_allocate_coll_params(phase->recv_ep_cnt);

    send_coll_params->counts[0] = 8;
    send_coll_params->counts[1] = 9;
    send_coll_params->counts[2] = 8;
    send_coll_params->counts[3] = 16;
    send_coll_params->counts[4] = 33;
    send_coll_params->counts[5] = 8;

    for (int i = 0; i < phase->send_ep_cnt; i++) {
        recv_coll_params->counts[i] = send_coll_params->counts[i];
    }

    for (int i = 0; i < (phase->send_ep_cnt-1); i++) {
        send_coll_params->displs[i+1] = send_coll_params->displs[i] + send_coll_params->counts[i];
        recv_coll_params->displs[i+1] = recv_coll_params->displs[i] + recv_coll_params->counts[i];
    }

    int total_count = send_coll_params->counts[5] + send_coll_params->displs[5];
    send_coll_params->init_buf = (int8_t *)ucs_malloc(sizeof(int)*total_count, "send init buf");
    recv_coll_params->init_buf = (int8_t *)ucs_malloc(sizeof(int)*total_count, "recv init buf");

    step->flags             = 0;
    step->flags            |= UCG_BUILTIN_OP_STEP_FLAG_LAST_STEP;
    step->phase             = phase;
    step->send_coll_params  = send_coll_params;
    step->recv_coll_params  = recv_coll_params;
    step->send_buffer       = send_coll_params->init_buf;
    step->recv_buffer       = recv_coll_params->init_buf;
    step->am_header.header  = 0;
    step->iter_ep           = 0;
    step->send_cb           = NULL;
    step->resend_flag       = UCG_BUILTIN_OP_STEP_FIRST_SEND;

    ret = ucg_builtin_step_alloc_pack_rank_buffer(step, sizeof(int)*total_count);
    ASSERT_EQ(UCS_OK, ret);

    slot->req.op->super.params = *params;
    slot->req.pending = 0;

    ret = ucg_builtin_step_execute(&slot->req, NULL);
    ASSERT_EQ(UCS_INPROGRESS, ret);
}
