/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2021. All rights reserved.
 * Description: Bcast复杂内存布局的自定义数据类型测试，覆盖lb > 0、
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

/* for struct complex1 */
#define mpi_dt_create1()                                \
    MPI_Datatype dt_complex1, dt_tmp;                   \
    int array_of_blocklen[2] = {1, 1};                  \
    MPI_Aint array_of_disp[2] = {4, 12};                \
    MPI_Datatype array_of_type[2] = {MPI_INT, MPI_INT}; \
    MPI_Type_create_struct(2, array_of_blocklen, array_of_disp, array_of_type, &dt_tmp); \
    MPI_Type_create_resized(dt_tmp, 0, 20, &dt_complex1); \
    MPI_Type_commit(&dt_complex1)

#define mpi_dt_free1()                                  \
    MPI_Type_free(&dt_complex1)

#define check_and_free1()                                           \
    if (rc) {                                                       \
        free(io); free(sol);                                        \
        MPI_Abort(MPI_COMM_WORLD, rc);                              \
    } else {                                                        \
        for (i = 0; i < count; i++) {                               \
            if (io[i].real != sol[i].real ||                        \
                io[i].imag != sol[i].imag ||                        \
                io[i].dummy0 != 0 ||                                \
                io[i].dummy1 != 1 ||                                \
                io[i].dummy2 != 2) {                                \
                free(io); free(sol);                                \
                MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);            \
            }                                                       \
        }                                                           \
    }                                                               \
    free(io); free(sol);

static void test1(int rank, int size, int count, int root)
{
    int i, rc;

    DECL_MALLOC_INOUT_SOL(struct complex1);

    if (rank == root) {
        SET_INDEX_STRUCT_SUM(io, 66, real);
        SET_INDEX_STRUCT_SUM(io, -66, imag);
    }
    SET_INDEX_STRUCT_SUM(sol, 66, real);
    SET_INDEX_STRUCT_SUM(sol, -66, imag);
    SET_INDEX_STRUCT_CONST(io, 0, dummy0);
    SET_INDEX_STRUCT_CONST(io, 1, dummy1);
    SET_INDEX_STRUCT_CONST(io, 2, dummy2);

    mpi_dt_create1();
    rc = MPI_Bcast(io, count, dt_complex1, root, MPI_COMM_WORLD);
    mpi_dt_free1();
    check_and_free1();
}

int main(int argc, char *argv[])
{
    int rank, size;
    int i, count;
    int root = 0;

    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    /* test small, medium, large and super large length */
    for (i = 0; i < ARRAY_SIZE(g_count); i++) {
        count = g_count[i];
        test1(rank, size, count, root);
    }

    WAIT_ALL_SUCCESS();

    MPI_Finalize();
    return 0;
}

