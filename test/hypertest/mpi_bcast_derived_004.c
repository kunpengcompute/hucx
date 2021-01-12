/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2021. All rights reserved.
 * Description: Bcast收、发数据类型不一致测试，覆盖发连续收不连续 & 发不连续收连续2个场景
 * Author: shizhibao
 * Create: 2021-01-09
 */

#include "mpi_test_common.h"

static int g_count[] = {1};

typedef struct {
    int intData[11];
    int dummy;
    int doubleData[10];
} MultiType92;

typedef struct {
    int intData[11];
    char dummy[5];
    int doubleData[11];
} MultiType93;

typedef struct {
    int intData[1023];
    int dummy[7];
    int doubleData[1022];
} MultiType8188;

typedef struct {
    int intData[1023];
    int dummy[8];
    int doubleData[1023];
} MultiType8192;

typedef struct {
    int intData[1024];
    int dummy[9];
    int doubleData[1023];
} MultiType8196;

typedef struct {
    int intData[14];
    int dummy[9];
    int doubleData[14];
} MultiType120;

typedef struct {
    int intData[15];
    int dummy[9];
    int doubleData[14];
} MultiType124;

typedef struct {
    int intData[255];
    int dummy[9];
    int doubleData[254];
} MultiType2044;

typedef struct {
    int intData[255];
    int dummy[9];
    int doubleData[255];
} MultiType2048;

typedef struct {
    int intData[14];
    int dummy[9];
    int doubleData[13];
} MultiType116;

typedef struct {
    int intData[126];
    int dummy[9];
    int doubleData[125];
} MultiType1012;

typedef struct {
    int intData[126];
    int dummy[9];
    int doubleData[126];
} MultiType1016;

typedef struct {
    int intData[127];
    int dummy[9];
    int doubleData[126];
} MultiType1020;

typedef struct {
    int intData[3];
    int dummy[9];
    int doubleData[3];
} MultiType32;

typedef struct {
    int intData[3];
    int dummy[9];
    int doubleData[2];
} MultiType28;

#define Test_SendUn(len1, len2, index)\
{\
    void *sendBuf = NULL;\
    int *recvBuf = NULL;\
    int i, k;\
    MPI_Datatype type = NULL;\
    MPI_Aint begin;\
    MPI_Aint displacements[2];\
    MPI_Datatype dataType[] = {MPI_INT, MPI_INT};\
    int blocklength[2] = {len1, len2};\
    MultiType##index object;\
    int lenmul = sizeof(MultiType##index)*count;\
    sendBuf = (MultiType##index *)malloc(lenmul);\
    recvBuf = (int *)malloc(lenmul);\
    if (NULL == sendBuf || NULL == recvBuf ) {\
        MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);\
    }\
    for (i = 0; i < count; i++) {\
        for(k = 0; k < len1; k++) {\
            ((MultiType##index*)sendBuf)[i].intData[k] = 4;\
        }\
        for(k = 0; k < len2; k++) {\
            ((MultiType##index*)sendBuf)[i].doubleData[k] = 4;\
        }\
    }\
    MPI_Get_address(&object, &begin);\
    MPI_Get_address(&object.intData, &displacements[0]);\
    MPI_Get_address(&object.doubleData, &displacements[1]);\
    for (i = 0; i < 2; i++) {\
        displacements[i] -= begin;\
    }\
    MPI_Type_create_struct(2, &blocklength[0], &displacements[0], &dataType[0], &type);\
    MPI_Type_commit(&type);\
    if (rank == root) {\
        MPI_Bcast(sendBuf, count, type, root, MPI_COMM_WORLD);\
    }\
    else {\
        MPI_Bcast(recvBuf, (len1 + len2) * count, MPI_INT, root, MPI_COMM_WORLD);\
    }\
    if (rank != root) {\
        for (i = 0; i < (len1 + len2) * count; i++) {\
            if (recvBuf[i] != 4) {\
                printf("bcast error, recvbuf[%d]=%d\n", i, recvBuf[i]);\
                free(sendBuf);\
                free(recvBuf);\
                MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);\
            }\
        }\
    }\
    MPI_Type_free(&type);\
    free(sendBuf);\
    free(recvBuf);\
}

#define Test_RecvUn(len1, len2, index )\
{\
    void *recvbuf = NULL;\
    int *sendbuf = NULL;\
    int i = 0, k = 0;\
    MPI_Datatype type = NULL;\
    MPI_Aint begin;\
    MPI_Aint displacements[2];\
    MPI_Datatype dataType[2] = {MPI_INT, MPI_INT};\
    int blocklength[2] = {len1, len2};\
    MultiType##index object;\
    MPI_Get_address(&object, &begin);\
    MPI_Get_address(&object.intData, &displacements[0]);\
    MPI_Get_address(&object.doubleData, &displacements[1]);\
    recvbuf = (MultiType##index *)malloc(sizeof(MultiType##index) * count);\
    int lenmul = sizeof(MultiType##index) * count;\
    memset(recvbuf, 0, lenmul);\
    sendbuf = (int *)malloc(sizeof(MultiType##index) * count);\
    for (i = 0; i < 2; i++) {\
        displacements[i] -= begin;\
    }\
    MPI_Type_create_struct(2, &blocklength[0], &displacements[0], &dataType[0], &type);\
    MPI_Type_commit(&type);\
    if (NULL == recvbuf || NULL == sendbuf) {\
        MPI_Abort(MPI_COMM_WORLD,EXIT_FAILURE);\
    }\
    for (i = 0; i < (len1 + len2) * count; i++) {\
        sendbuf[i] = 4;\
    }\
    if (rank == root) {\
        MPI_Bcast(sendbuf, (len1 + len2) * count, MPI_INT, root, MPI_COMM_WORLD);\
    }\
    else {\
        MPI_Bcast(recvbuf, count, type, root, MPI_COMM_WORLD);\
    }\
    if (rank != root) {\
        for (k = 0; k < count; k++) {\
            for (i = 0; i < len1; i++) { \
                if (((MultiType##index *)recvbuf)[k].intData[i] != 4) {\
                    printf("bcast error, intData[%d]=%d\n", i, ((MultiType##index *)recvbuf)[k].intData[i]);\
                    free(sendbuf);\
                    free(recvbuf);\
                    MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);\
                }\
            }\
            for (i = 0; i < len2; i++) { \
                if(((MultiType##index *)recvbuf)[k].doubleData[i]!= 4) {\
                    printf("bcast error, doubleData[%d]=%d\n", i, ((MultiType##index *)recvbuf)[k].doubleData[i]);\
                    free(sendbuf);\
                    free(recvbuf);\
                    MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);\
                }\
            }\
        }\
    }\
    MPI_Type_free(&type);\
    free(sendbuf);\
    free(recvbuf);\
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

        /* sender non-contiguous, receiver contiguous */
        Test_SendUn(11,10,92);

        Test_SendUn(11,11,93);

        Test_SendUn(1023,1022,8188);

        Test_SendUn(1023,1023,8192);

        Test_SendUn(1024,1023,8196);

        Test_SendUn(14,14,120);

        Test_SendUn(15,14,124);

        Test_SendUn(255,254,2044);

        Test_SendUn(255,258,2048);

        Test_SendUn(14,13,116);

        Test_SendUn(126,125,1012);

        Test_SendUn(126,126,1016);

        Test_SendUn(127,126,1020);

        Test_SendUn(3,3,32);

        Test_SendUn(3,2,28);

        /* sender contiguous, receiver non-contiguous */
        Test_RecvUn(11,10,92);

        Test_RecvUn(11,10,93);

        Test_RecvUn(1023,1022,8188);

        Test_RecvUn(1023,1023,8192);

        Test_RecvUn(1024,1023,8196);

        Test_RecvUn(14,14,120);

        Test_RecvUn(15,14,124);

        Test_RecvUn(255,254,2044);

        Test_RecvUn(255,258,2048);

        Test_RecvUn(14,13,116);

        Test_RecvUn(126,125,1012);

        Test_RecvUn(126,126,1016);

        Test_RecvUn(127,126,1020);

        Test_RecvUn(3,3,32);

        Test_RecvUn(3,2,28);
    }

    WAIT_ALL_SUCCESS();

    MPI_Finalize();
    return 0;
}

