/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2021-2021. All rights reserved.
 * Description: Unit test for algorithm decision module
 * Author: shizhibao
 * Create: 2021-07-16
 */

#include <ucg/builtin/plan/builtin_algo_decision.h>

#include "ucg_test.h"

struct check_flag {
    unsigned int non_contig_datatype    : 1;
    unsigned int non_commutative        : 1;
    unsigned int nap_unsupport          : 1;
    unsigned int raben_unsupport        : 1;
    unsigned int naware_raben_unsupport : 1;
    unsigned int saware_raben_unsupport : 1;
    unsigned int bind_to_none           : 1;
    unsigned int ppn_unbalance          : 1;
    unsigned int nrank_uncontinue       : 1;
    unsigned int pps_unbalance          : 1;
    unsigned int srank_uncontinue       : 1;
    unsigned int large_datatype         : 1;
    unsigned int phase_segment          : 1;
    unsigned int inc_unsupport          : 1;
    unsigned int mpi_in_place           : 1;
    unsigned int plummer_unsupport      : 1;

    check_flag()
        : non_contig_datatype(0),
          non_commutative(0),
          nap_unsupport(0),
          raben_unsupport(0),
          naware_raben_unsupport(0),
          saware_raben_unsupport(0),
          bind_to_none(0),
          ppn_unbalance(0),
          nrank_uncontinue(0),
          pps_unbalance(0),
          srank_uncontinue(0),
          large_datatype(0),
          phase_segment(0),
          inc_unsupport(0),
          mpi_in_place(0),
          plummer_unsupport(0)
    {
    }
};

class ucg_algo_decision_test : public ucg_test {
public:
    int algo_select(coll_type_t coll_type, int size, int ppn, int node);

    int algo_check(coll_type_t coll_type, int algo, const check_flag &flag);

    int algo_decision(coll_type_t coll_type, int size, int ppn, int node);

    int algo_decision(coll_type_t coll_type, const check_flag &flag);

private:
    check_flag m_flag;

    void set_coll_type_field(ucg_collective_params_t &coll_params, coll_type_t coll_type);

    void set_group_params(ucg_group_params_t &group_params, int ppn, int node);

    void set_group_params(ucg_group_params_t &group_params);

    void set_coll_params(ucg_collective_params_t &coll_params, coll_type_t coll_type, int size);

    void set_coll_params(ucg_collective_params_t &coll_params, coll_type_t coll_type);
};

static int op_commute(void *op)
{
    return 1;
}

static int op_non_commute(void *op)
{
    return 0;
}

static int dt_contig(void *mpi_dt, ucp_datatype_t *ucp_dt)
{
    *ucp_dt = UCP_DATATYPE_CONTIG;
    return 0;
}

static int dt_non_contig(void *mpi_dt, ucp_datatype_t *ucp_dt)
{
    *ucp_dt = UCP_DATATYPE_GENERIC;
    return 0;
}

void ucg_algo_decision_test::set_coll_type_field(ucg_collective_params_t &coll_params,
                                                 coll_type_t coll_type)
{
    switch (coll_type) {
        case COLL_TYPE_BARRIER:
            coll_params.type = create_barrier_coll_type();
            coll_params.coll_type = COLL_TYPE_BARRIER;
            break;

        case COLL_TYPE_BCAST:
            coll_params.type = create_bcast_coll_type();
            coll_params.coll_type = COLL_TYPE_BCAST;
            break;

        case COLL_TYPE_ALLREDUCE:
            coll_params.type = create_allreduce_coll_type();
            coll_params.coll_type = COLL_TYPE_ALLREDUCE;
            break;

        case COLL_TYPE_ALLTOALLV:
            coll_params.type = create_alltoallv_coll_type();
            coll_params.coll_type = COLL_TYPE_ALLTOALLV;
            break;

        default:
            break;
    }
}

void ucg_algo_decision_test::set_group_params(ucg_group_params_t &group_params,
                                              int ppn, int node)
{
    const int socket_nums = 2;

    group_params.member_count = ppn * node;
    group_params.member_index = 0;
    group_params.topo_args.ppn_unbalance = 0;
    group_params.topo_args.pps_unbalance = 0;
    group_params.topo_args.nrank_uncontinue = 0;
    group_params.topo_args.srank_uncontinue = 0;
    group_params.topo_args.bind_to_none = 0;
    group_params.topo_args.ppn_local = ppn;
    group_params.topo_args.pps_local = ppn / socket_nums;
    group_params.topo_args.ppn_max = ppn;
    group_params.topo_args.node_nums = node;
    group_params.op_is_commute_f = op_commute;
    group_params.mpi_dt_convert = dt_contig;
    group_params.inc_param.switch_info_got = m_flag.inc_unsupport ? 0 : 1;
    group_params.inc_param.feature_used = m_flag.inc_unsupport ? 0 : 1;
}

void ucg_algo_decision_test::set_group_params(ucg_group_params_t &group_params)
{
    const int np = 1024;
    const int ppn = 128;
    const int pps = 64;

    group_params.member_count = np;
    group_params.member_index = 0;
    group_params.topo_args.ppn_unbalance = m_flag.ppn_unbalance;
    group_params.topo_args.pps_unbalance = m_flag.pps_unbalance;
    group_params.topo_args.nrank_uncontinue = m_flag.nrank_uncontinue;
    group_params.topo_args.srank_uncontinue = m_flag.srank_uncontinue;
    group_params.topo_args.bind_to_none = m_flag.bind_to_none;
    group_params.topo_args.ppn_local = ppn;
    group_params.topo_args.pps_local = pps;
    group_params.topo_args.ppn_max = ppn;
    group_params.topo_args.node_nums = np / ppn;
    group_params.op_is_commute_f = m_flag.non_commutative ? op_non_commute : op_commute;
    group_params.mpi_dt_convert = m_flag.non_contig_datatype ? dt_non_contig : dt_contig;
    group_params.inc_param.switch_info_got = m_flag.inc_unsupport ? 0 : 1;
    group_params.inc_param.feature_used = m_flag.inc_unsupport ? 0 : 1;
}

static void set_mpi_in_place(ucg_collective_params_t &coll_params, unsigned mpi_in_place)
{
    if (mpi_in_place) {
        coll_params.send.buf = ((void*)0x1);
    } else {
        coll_params.send.buf = ((void*)0x0);
    }
}

static void set_plummer_unsupport(ucg_group_params_t &group_params,
                                  ucg_collective_params_t &coll_params,
                                  unsigned plummer_unsupport)
{
    const int count = group_params.member_count;
    coll_params.send.displs = new int[count];
    coll_params.recv.displs = new int[count];
    coll_params.send.counts = new int[count];
    coll_params.recv.counts = new int[count];

    if (plummer_unsupport) {
        for (int i = 0; i < count; i++) {
            if (i == 0) {
                coll_params.send.displs[0] = -1;
                coll_params.recv.displs[0] = -1;
                coll_params.send.counts[0] = 1;
                coll_params.recv.counts[0] = 1;
            } else {
                coll_params.send.counts[i] = 1;
                coll_params.recv.counts[i] = 1;
                coll_params.send.displs[i] = coll_params.send.displs[i - 1] + coll_params.send.counts[i - 1];
                coll_params.recv.displs[i] = coll_params.recv.displs[i - 1] + coll_params.recv.counts[i - 1];
            }
        }
    } else {
        for (int i = 0; i < count; i++) {
            if (i == 0) {
                coll_params.send.displs[0] = 0;
                coll_params.recv.displs[0] = 0;
                coll_params.send.counts[0] = 1;
                coll_params.recv.counts[0] = 1;
            } else {
                coll_params.send.counts[i] = 1;
                coll_params.recv.counts[i] = 1;
                coll_params.send.displs[i] = coll_params.send.displs[i - 1] + coll_params.send.counts[i - 1];
                coll_params.recv.displs[i] = coll_params.recv.displs[i - 1] + coll_params.recv.counts[i - 1];
            }
        }
    }
}

static void plummer_unsupport_free(ucg_collective_params_t &coll_params)
{
    delete[] coll_params.send.displs;
    delete[] coll_params.recv.displs;
    delete[] coll_params.send.counts;
    delete[] coll_params.recv.counts;
}

void ucg_algo_decision_test::set_coll_params(ucg_collective_params_t &coll_params,
                                             coll_type_t coll_type, int size)
{
    set_coll_type_field(coll_params, coll_type);
    coll_params.send.dt_len = 1;
    coll_params.send.count = size;
    coll_params.recv.dt_len = 1;
    coll_params.recv.count = size;
}

void ucg_algo_decision_test::set_coll_params(ucg_collective_params_t &coll_params,
                                             coll_type_t coll_type)
{
    const int dt_normal = 4;
    const int dt_large = 33;
    const int dt_segment = 160;
    const int count = 1;

    set_coll_type_field(coll_params, coll_type);
    coll_params.send.dt_len = dt_normal;
    coll_params.send.count = count;
    coll_params.recv.dt_len = dt_normal;
    coll_params.recv.count = count;

    if (m_flag.large_datatype) {
        coll_params.send.dt_len = dt_large;
        coll_params.recv.dt_len = dt_large;
    }

    if (m_flag.phase_segment) {
        coll_params.send.dt_len = dt_segment;
        coll_params.recv.dt_len = dt_segment;
    }
}

int ucg_algo_decision_test::algo_select(coll_type_t coll_type, int size, int ppn, int node)
{
    ucg_group_params_t group_params;
    ucg_collective_params_t coll_params;

    set_group_params(group_params, ppn, node);
    set_coll_params(coll_params, coll_type, size);

    return ucg_builtin_algo_auto_select(&group_params, &coll_params);
}

int ucg_algo_decision_test::algo_check(coll_type_t coll_type, int algo, const check_flag &flag)
{
    ucg_group_params_t group_params;
    ucg_collective_params_t coll_params;

    m_flag = flag;

    set_group_params(group_params);
    set_coll_params(coll_params, coll_type);

    if (coll_type == COLL_TYPE_ALLTOALLV) {
        set_mpi_in_place(coll_params, m_flag.mpi_in_place);
        set_plummer_unsupport(group_params, coll_params, m_flag.plummer_unsupport);
    }

    int rc = ucg_builtin_algo_check_fallback(&group_params, &coll_params, algo);

    if (coll_type == COLL_TYPE_ALLTOALLV) {
        plummer_unsupport_free(coll_params);
    }

    return rc;
}

int ucg_algo_decision_test::algo_decision(coll_type_t coll_type, int size, int ppn, int node)
{
    ucg_group_params_t group_params;
    ucg_collective_params_t coll_params;

    set_group_params(group_params, ppn, node);
    set_coll_params(coll_params, coll_type, size);

    return ucg_builtin_algo_decision(&group_params, &coll_params);
}

int ucg_algo_decision_test::algo_decision(coll_type_t coll_type, const check_flag &flag)
{
    ucg_group_params_t group_params;
    ucg_collective_params_t coll_params;

    m_flag = flag;

    set_group_params(group_params);
    set_coll_params(coll_params, coll_type);

    return ucg_builtin_algo_decision(&group_params, &coll_params);
}

TEST_F(ucg_algo_decision_test, test_select_barrier) {
    int algo;

    algo = algo_select(COLL_TYPE_BARRIER, 0, 1, 1);
    EXPECT_EQ(10, algo);

    algo = algo_select(COLL_TYPE_BARRIER, 0, 128, 1);
    EXPECT_EQ(10, algo);

    algo = algo_select(COLL_TYPE_BARRIER, 0, 1, 48);
    EXPECT_EQ(4, algo);

    algo = algo_select(COLL_TYPE_BARRIER, 0, 128, 48);
    EXPECT_EQ(5, algo);

    algo = algo_select(COLL_TYPE_BARRIER, 0, 128, 8);
    EXPECT_EQ(10, algo);

    algo = algo_select(COLL_TYPE_BARRIER, 0, 32, 48);
    EXPECT_EQ(6, algo);

    algo = algo_select(COLL_TYPE_BARRIER, 0, 8, 8);
    EXPECT_EQ(10, algo);

    algo = algo_select(COLL_TYPE_BARRIER, 0, 256, 96);
    EXPECT_EQ(5, algo);
}

TEST_F(ucg_algo_decision_test, test_select_bcast) {
    int algo;

    algo = algo_select(COLL_TYPE_BCAST, 0, 1, 1);
    EXPECT_EQ(3, algo);

    algo = algo_select(COLL_TYPE_BCAST, 0, 1, 48);
    EXPECT_EQ(3, algo);

    algo = algo_select(COLL_TYPE_BCAST, 8192, 128, 1);
    EXPECT_EQ(4, algo);

    algo = algo_select(COLL_TYPE_BCAST, 8192, 128, 48);
    EXPECT_EQ(4, algo);

    algo = algo_select(COLL_TYPE_BCAST, 16384, 256, 96);
    EXPECT_EQ(4, algo);

    algo = algo_select(COLL_TYPE_BCAST, 4096, 32, 32);
    EXPECT_EQ(4, algo);

    algo = algo_select(COLL_TYPE_BCAST, 16, 64, 32);
    EXPECT_EQ(4, algo);

    algo = algo_select(COLL_TYPE_BCAST, 130, 10, 7);
    EXPECT_EQ(4, algo);
}

TEST_F(ucg_algo_decision_test, test_select_allreduce) {
    int algo;

    algo = algo_select(COLL_TYPE_ALLREDUCE, 0, 1, 1);
    EXPECT_EQ(11, algo);

    algo = algo_select(COLL_TYPE_ALLREDUCE, 0, 1, 48);
    EXPECT_EQ(7, algo);

    algo = algo_select(COLL_TYPE_ALLREDUCE, 8192, 128, 1);
    EXPECT_EQ(12, algo);

    algo = algo_select(COLL_TYPE_ALLREDUCE, 8192, 128, 48);
    EXPECT_EQ(6, algo);

    algo = algo_select(COLL_TYPE_ALLREDUCE, 16384, 256, 96);
    EXPECT_EQ(6, algo);

    algo = algo_select(COLL_TYPE_ALLREDUCE, 256, 128, 8);
    EXPECT_EQ(11, algo);

    algo = algo_select(COLL_TYPE_ALLREDUCE, 1025, 65, 5);
    EXPECT_EQ(11, algo);

    algo = algo_select(COLL_TYPE_ALLREDUCE, 513, 33, 33);
    EXPECT_EQ(7, algo);
}

TEST_F(ucg_algo_decision_test, test_select_alltoallv) {
    int algo;

    algo = algo_select(COLL_TYPE_ALLTOALLV, 0, 1, 1);
    EXPECT_EQ(2, algo);
}

TEST_F(ucg_algo_decision_test, test_check_alltoallv) {
    int algo_fb;
    check_flag flag;

    algo_fb = algo_check(COLL_TYPE_ALLTOALLV, 1, flag);
    EXPECT_EQ(1, algo_fb);

    algo_fb = algo_check(COLL_TYPE_ALLTOALLV, 2, flag);
    EXPECT_EQ(2, algo_fb);

    flag.ppn_unbalance = 1;
    algo_fb = algo_check(COLL_TYPE_ALLTOALLV, 2, flag);
    EXPECT_EQ(1, algo_fb);

    flag.ppn_unbalance = 0;
    flag.nrank_uncontinue = 1;
    algo_fb = algo_check(COLL_TYPE_ALLTOALLV, 2, flag);
    EXPECT_EQ(1, algo_fb);

    flag.nrank_uncontinue = 0;
    flag.mpi_in_place = 1;
    algo_fb = algo_check(COLL_TYPE_ALLTOALLV, 2, flag);
    EXPECT_EQ(1, algo_fb);

    flag.mpi_in_place = 0;
    flag.plummer_unsupport = 1;
    algo_fb = algo_check(COLL_TYPE_ALLTOALLV, 2, flag);
    EXPECT_EQ(1, algo_fb);
    flag.plummer_unsupport = 0;
}

TEST_F(ucg_algo_decision_test, test_check_barrier) {
    int algo, algo_fb;
    check_flag flag;
    int checkresult[] = {0, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2};

    flag.pps_unbalance = 1;
    flag.nrank_uncontinue = 1;

    for (algo = 1; algo <= 10; algo++) {
        algo_fb = algo_check(COLL_TYPE_BARRIER, algo, flag);
        EXPECT_EQ(checkresult[algo], algo_fb);
    }
}

TEST_F(ucg_algo_decision_test, test_check_bcast) {
    int algo, algo_fb;
    check_flag flag;

    flag.nrank_uncontinue = 1;

    for (algo = 1; algo <= 5; algo++) {
        algo_fb = algo_check(COLL_TYPE_BCAST, algo, flag);
        if (1 == algo) {
            EXPECT_EQ(1, algo_fb);
        } else {
            EXPECT_EQ(2, algo_fb);
        }
    }
}

TEST_F(ucg_algo_decision_test, test_check_allreduce) {
    int algo, algo_fb;
    check_flag flag;
    int checkresult[] = {0, 1, 2, 2, 4, 5, 5, 7, 7, 9, 2, 2, 2, 2, 2};

    flag.bind_to_none = 1;

    for (algo = 1; algo <= 14; algo++) {
        algo_fb = algo_check(COLL_TYPE_ALLREDUCE, algo, flag);
        EXPECT_EQ(checkresult[algo], algo_fb);
    }
}

TEST_F(ucg_algo_decision_test, test_decision_barrier) {
    int algo;

    algo = algo_decision(COLL_TYPE_BARRIER, 0, 64, 4);
    EXPECT_EQ(10, algo);
}

TEST_F(ucg_algo_decision_test, test_decision_bcast) {
    check_flag flag;
    int algo;

    algo = algo_decision(COLL_TYPE_BCAST, flag);
    EXPECT_EQ(3, algo);

    flag.ppn_unbalance = 1;
    algo = algo_decision(COLL_TYPE_BCAST, flag);
    EXPECT_EQ(2, algo);
}

TEST_F(ucg_algo_decision_test, test_decision_allreduce) {
    check_flag flag;
    int algo;

    algo = algo_decision(COLL_TYPE_ALLREDUCE, flag);
    EXPECT_EQ(11, algo);

    flag.large_datatype = 1;
    algo = algo_decision(COLL_TYPE_ALLREDUCE, flag);
    EXPECT_EQ(1, algo);

    flag.large_datatype = 0;
    flag.bind_to_none = 1;
    algo = algo_decision(COLL_TYPE_ALLREDUCE, flag);
    EXPECT_EQ(2, algo);
}
