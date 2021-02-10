/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2021. All rights reserved.
 * Description: Allreduce复杂内存布局的自定义数据类型测试，覆盖lb > 0、
 *              lb < 0、多个gap等场景，要检测gap区域操作前后不变
 * Author: shizhibao
 * Create: 2021-01-09
 */

#include "mpi_test_common.h"

static int g_count[] = {0, 1, 100};

/* true_lb > 0 */
struct complex1 {
    int dummy0;
    int real;
    int dummy1;
    int imag;
    int dummy2;
};

void sum_complex1(void *inP, void *inoutP, int *len, MPI_Datatype *dptr)
{
    struct complex1 *in = (struct complex1 *)inP;
    struct complex1 *io = (struct complex1 *)inoutP;
    int i;
    for (i = 0; i < *len; i++) {
        io->real += in->real;
        io->imag += in->imag;
        in++; io++;
    }
}

void sum_complex2(void *inP, void *inoutP, int *len, MPI_Datatype *dptr)
{
    struct complex1 *in = (struct complex1 *)inP;
    struct complex1 *io = (struct complex1 *)inoutP;
    int i;
    for (i = 0; i < *len; i++) {
        io->dummy0 += in->dummy0;
        io->real += in->real;
        io->dummy1 += in->dummy1;
        io->imag += in->imag;
        io->dummy2 += in->dummy2;
        in++; io++;
    }
}

/* for struct complex1 */
#define mpi_dt_and_op_create1()                         \
    MPI_Op op_sum_complex1;                             \
    MPI_Op_create(sum_complex1, 1, &op_sum_complex1);   \
    MPI_Datatype dt_complex1, dt_tmp;                   \
    int array_of_blocklen[2] = {1, 1};                  \
    MPI_Aint array_of_disp[2] = {4, 12};                \
    MPI_Datatype array_of_type[2] = {MPI_INT, MPI_INT}; \
    MPI_Type_create_struct(2, array_of_blocklen, array_of_disp, array_of_type, &dt_tmp); \
    MPI_Type_create_resized(dt_tmp, 0, 20, &dt_complex1); \
    MPI_Type_commit(&dt_complex1)

#define mpi_dt_and_op_change1()                         \
    MPI_Op_create(sum_complex2, 1, &op_sum_complex1);   \
    MPI_Type_contiguous(5, MPI_INT, &dt_complex1);      \
    MPI_Type_commit(&dt_complex1)

#define mpi_dt_and_op_change2()                         \
    MPI_Op_create(sum_complex1, 1, &op_sum_complex1);   \
    MPI_Type_create_resized(dt_tmp, 0, 20, &dt_complex1); \
    MPI_Type_commit(&dt_complex1)

#define mpi_dt_and_op_free1()                           \
    MPI_Op_free(&op_sum_complex1);                      \
    MPI_Type_free(&dt_complex1)

#define check1()                                                    \
    if (rc) {                                                       \
        free(in); free(out); free(sol); free(org);                  \
        MPI_Abort(MPI_COMM_WORLD, rc);                              \
    } else {                                                        \
        for (i = 0; i < count; i++) {                               \
            if (in[i].dummy0 != org[i].dummy0 ||                    \
                in[i].real != org[i].real ||                        \
                in[i].dummy1 != org[i].dummy1 ||                    \
                in[i].imag != org[i].imag ||                        \
                in[i].dummy2 != org[i].dummy2 ||                    \
                out[i].real != sol[i].real ||                       \
                out[i].imag != sol[i].imag ||                       \
                out[i].dummy0 != 3 ||                               \
                out[i].dummy1 != 4 ||                               \
                out[i].dummy2 != 5) {                               \
                free(in); free(out); free(sol); free(org);          \
                MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);            \
            }                                                       \
        }                                                           \
    }

#define check2()                                                    \
    if (rc) {                                                       \
        free(in); free(out); free(sol); free(org);                  \
        MPI_Abort(MPI_COMM_WORLD, rc);                              \
    } else {                                                        \
        for (i = 0; i < count; i++) {                               \
            if (in[i].dummy0 != org[i].dummy0 ||                    \
                in[i].real != org[i].real ||                        \
                in[i].dummy1 != org[i].dummy1 ||                    \
                in[i].imag != org[i].imag ||                        \
                in[i].dummy2 != org[i].dummy2 ||                    \
                out[i].real != sol[i].real ||                       \
                out[i].imag != sol[i].imag ||                       \
                out[i].dummy0 != sol[i].dummy0 ||                   \
                out[i].dummy1 != sol[i].dummy1 ||                   \
                out[i].dummy2 != sol[i].dummy2) {                   \
                free(in); free(out); free(sol); free(org);          \
                MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);            \
            }                                                       \
        }                                                           \
    }

static void test1(int rank, int size, int count)
{
    int i, rc, sum1, sum2;

    sum1 = (size - 1) * size / 2;
    sum2 = -sum1;

    DECL_MALLOC_IN_OUT_SOL(struct complex1);

    mpi_dt_and_op_create1();
    SET_INDEX_STRUCT_SUM(in, rank, real);
    SET_INDEX_STRUCT_SUM(in, -rank, imag);
    SET_INDEX_STRUCT_SUM(in, 0, dummy0);
    SET_INDEX_STRUCT_SUM(in, 1, dummy1);
    SET_INDEX_STRUCT_SUM(in, 2, dummy2);
    SET_INDEX_STRUCT_SUM(org, rank, real);
    SET_INDEX_STRUCT_SUM(org, -rank, imag);
    SET_INDEX_STRUCT_SUM(org, 0, dummy0);
    SET_INDEX_STRUCT_SUM(org, 1, dummy1);
    SET_INDEX_STRUCT_SUM(org, 2, dummy2);
    SET_INDEX_STRUCT_CONST(out, 3, dummy0);
    SET_INDEX_STRUCT_CONST(out, 4, dummy1);
    SET_INDEX_STRUCT_CONST(out, 5, dummy2);
    SET_INDEX_STRUCT_SUM_SIZE(sol, sum1, real);
    SET_INDEX_STRUCT_SUM_SIZE(sol, sum2, imag);
    rc = MPI_Allreduce(in, out, count, dt_complex1, op_sum_complex1, MPI_COMM_WORLD);
    mpi_dt_and_op_free1();
    check1();

    free(in);
    free(out);
    free(sol);
    free(org);
}

static void test2(int rank, int size)
{
    int i, rc, sum1, sum2;
    int count = 3;

    sum1 = (size - 1) * size / 2;
    sum2 = -sum1;

    DECL_MALLOC_IN_OUT_SOL(struct complex1);

    mpi_dt_and_op_create1();
    SET_INDEX_STRUCT_SUM(in, rank, real);
    SET_INDEX_STRUCT_SUM(in, -rank, imag);
    SET_INDEX_STRUCT_SUM(in, 0, dummy0);
    SET_INDEX_STRUCT_SUM(in, 1, dummy1);
    SET_INDEX_STRUCT_SUM(in, 2, dummy2);
    SET_INDEX_STRUCT_SUM(org, rank, real);
    SET_INDEX_STRUCT_SUM(org, -rank, imag);
    SET_INDEX_STRUCT_SUM(org, 0, dummy0);
    SET_INDEX_STRUCT_SUM(org, 1, dummy1);
    SET_INDEX_STRUCT_SUM(org, 2, dummy2);
    SET_INDEX_STRUCT_CONST(out, 3, dummy0);
    SET_INDEX_STRUCT_CONST(out, 4, dummy1);
    SET_INDEX_STRUCT_CONST(out, 5, dummy2);
    SET_INDEX_STRUCT_SUM_SIZE(sol, sum1, real);
    SET_INDEX_STRUCT_SUM_SIZE(sol, sum2, imag);
    rc = MPI_Allreduce(in, out, count, dt_complex1, op_sum_complex1, MPI_COMM_WORLD);
    mpi_dt_and_op_free1();
    check1();

    mpi_dt_and_op_change1();
    SET_INDEX_STRUCT_SUM(in, rank, dummy0);
    SET_INDEX_STRUCT_SUM(in, -rank, real);
    SET_INDEX_STRUCT_SUM(in, rank, dummy1);
    SET_INDEX_STRUCT_SUM(in, -rank, imag);
    SET_INDEX_STRUCT_SUM(in, rank, dummy2);
    SET_INDEX_STRUCT_SUM(org, rank, dummy0);
    SET_INDEX_STRUCT_SUM(org, -rank, real);
    SET_INDEX_STRUCT_SUM(org, rank, dummy1);
    SET_INDEX_STRUCT_SUM(org, -rank, imag);
    SET_INDEX_STRUCT_SUM(org, rank, dummy2);
    SET_INDEX_STRUCT_SUM_SIZE(sol, sum1, dummy0);
    SET_INDEX_STRUCT_SUM_SIZE(sol, sum2, real);
    SET_INDEX_STRUCT_SUM_SIZE(sol, sum1, dummy1);
    SET_INDEX_STRUCT_SUM_SIZE(sol, sum2, imag);
    SET_INDEX_STRUCT_SUM_SIZE(sol, sum1, dummy2);
    rc = MPI_Allreduce(in, out, count, dt_complex1, op_sum_complex1, MPI_COMM_WORLD);
    mpi_dt_and_op_free1();
    check2();

    mpi_dt_and_op_change2();
    SET_INDEX_STRUCT_SUM(in, -rank, real);
    SET_INDEX_STRUCT_SUM(in, rank, imag);
    SET_INDEX_STRUCT_SUM(in, 6, dummy0);
    SET_INDEX_STRUCT_SUM(in, 7, dummy1);
    SET_INDEX_STRUCT_SUM(in, 8, dummy2);
    SET_INDEX_STRUCT_SUM(org, -rank, real);
    SET_INDEX_STRUCT_SUM(org, rank, imag);
    SET_INDEX_STRUCT_SUM(org, 6, dummy0);
    SET_INDEX_STRUCT_SUM(org, 7, dummy1);
    SET_INDEX_STRUCT_SUM(org, 8, dummy2);
    SET_INDEX_STRUCT_CONST(out, 3, dummy0);
    SET_INDEX_STRUCT_CONST(out, 4, dummy1);
    SET_INDEX_STRUCT_CONST(out, 5, dummy2);
    SET_INDEX_STRUCT_SUM_SIZE(sol, sum2, real);
    SET_INDEX_STRUCT_SUM_SIZE(sol, sum1, imag);
    rc = MPI_Allreduce(in, out, count, dt_complex1, op_sum_complex1, MPI_COMM_WORLD);
    mpi_dt_and_op_free1();
    check1();

    free(in);
    free(out);
    free(sol);
    free(org);
}

int main(int argc, char *argv[])
{
    int rank, size;
    int i, count;

    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    /* test small, medium, large and super large length */
    for (i = 0; i < ARRAY_SIZE(g_count); i++) {
        count = g_count[i];
        test1(rank, size, count);
    }

    /* mixture test of contig and non-contig datatype */
    test2(rank, size);

    WAIT_ALL_SUCCESS();

    MPI_Finalize();
    return 0;
}

