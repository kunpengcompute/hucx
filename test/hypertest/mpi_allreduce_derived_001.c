/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2021. All rights reserved.
 * Description: Allreduce连续自定义数据类型测试，单个数据大小覆盖0、
 *              am_short&bcopy_one临界值左右、超大几种场景
 * Author: shizhibao
 * Create: 2021-01-09
 */

#include "mpi_test_common.h"

static int g_count[] = {0, 1, 100};

#define sum_arr_int_test(type, mpi_type, mpi_op, n)                     \
    {                                                                   \
        int ranks_sum = size * (size - 1) / 2;                          \
        DECL_MALLOC_INOUT_SOL(type);                                    \
        SET_INDEX_ARRAY_SUM(io, rank, a, n);                            \
        SET_INDEX_ARRAY_SUM_SIZE(sol, ranks_sum, a, n);                 \
        ARRAY_ALLREDUCE_AND_FREE_INPLACE(mpi_type, mpi_op, io, sol, n); \
    }

#define declare_arr_int_struct_and_op(n)                                \
struct arr_int##n {                                                     \
    int a[n];                                                           \
};                                                                      \
void sum_int##n(void *inP, void *inoutP, int *len, MPI_Datatype *dptr)  \
{                                                                       \
    struct arr_int##n *in = (struct arr_int##n *)inP;                   \
    struct arr_int##n *io = (struct arr_int##n *)inoutP;                \
    int i, j;                                                           \
    for (i = 0; i < *len; i++) {                                        \
        for (j = 0; j < n; j++) {                                       \
            io->a[j] += in->a[j];                                       \
        }                                                               \
        in++; io++;                                                     \
    }                                                                   \
}

declare_arr_int_struct_and_op(0);
declare_arr_int_struct_and_op(10);
declare_arr_int_struct_and_op(45);
declare_arr_int_struct_and_op(300);
declare_arr_int_struct_and_op(2000);
declare_arr_int_struct_and_op(100000);

#define mpi_dt_and_op_create(n)                     \
    MPI_Op op_sum_int##n;                           \
    MPI_Op_create(sum_int##n, 1, &op_sum_int##n);   \
    MPI_Datatype dt_int##n;                         \
    MPI_Type_contiguous(n, MPI_INT, &dt_int##n);    \
    MPI_Type_commit(&dt_int##n)

#define mpi_dt_and_op_free(n)                       \
    MPI_Op_free(&op_sum_int##n);                    \
    MPI_Type_free(&dt_int##n)

int main(int argc, char *argv[])
{
    int rank, size;
    int i, count;

    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    mpi_dt_and_op_create(0);
    mpi_dt_and_op_create(10);
    mpi_dt_and_op_create(45);
    mpi_dt_and_op_create(300);
    mpi_dt_and_op_create(2000);
    mpi_dt_and_op_create(100000);

    /* test small, medium, large and super large length */
    for (i = 0; i < ARRAY_SIZE(g_count); i++) {
        count = g_count[i];
        sum_arr_int_test(struct arr_int0, dt_int0, op_sum_int0, 0);
        sum_arr_int_test(struct arr_int10, dt_int10, op_sum_int10, 10);
        sum_arr_int_test(struct arr_int45, dt_int45, op_sum_int45, 45);
        sum_arr_int_test(struct arr_int300, dt_int300, op_sum_int300, 300);
        sum_arr_int_test(struct arr_int2000, dt_int2000, op_sum_int2000, 2000);
        sum_arr_int_test(struct arr_int100000, dt_int100000, op_sum_int100000, 100000);
    }

    mpi_dt_and_op_free(0);
    mpi_dt_and_op_free(10);
    mpi_dt_and_op_free(45);
    mpi_dt_and_op_free(300);
    mpi_dt_and_op_free(2000);
    mpi_dt_and_op_free(100000);

    WAIT_ALL_SUCCESS();

    MPI_Finalize();
    return 0;
}

