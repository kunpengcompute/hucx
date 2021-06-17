/**
* Copyright (C) Mellanox Technologies Ltd. 2001-2016.  ALL RIGHTS RESERVED.
* Copyright (C) Advanced Micro Devices, Inc. 2018. ALL RIGHTS RESERVED.
* Copyright (C) Huawei Technologies Co., Ltd. 2021.  ALL RIGHTS RESERVED.
*
* See file LICENSE for terms.
*/

#ifndef HAVE_CONFIG_H
#  define HAVE_CONFIG_H /* Force using config.h, so test would fail if header
                           actually tries to use it */
#endif

/*
 * UCG hello world broadcast example utility
 * -----------------------------------------------
 *
 * Root side:
 *
 *    ./ucg_hello_world
 *
 * Non-root side:
 *
 *    ./ucg_hello_world -n <root host name>
 *
 *
 * Authors:
 *
 *    Shuki Zanyovka <shuki.zanyovka@huawei.com>
 *    Alex Margolin <alex.margolin@huawei.com>
 */

#include "hello_world_util.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <ucg/api/ucg_minimal.h>

static uint16_t server_port     = 13337;
static long test_string_length  = 16;
static unsigned num_connections = 1;

static void print_usage()
{
    fprintf(stderr, "Usage: ucg_hello_world [parameters]\n");
    fprintf(stderr, "UCG hello world boradcast example utility\n");
    fprintf(stderr, "\nParameters are:\n");
    fprintf(stderr, "  -g <number> Set how many connections should the server "
                    "wait for (before broadcasting)\n");
    fprintf(stderr, "  -n <name>   Set node name or IP address "
                    "of the server (required for non-roots and should be "
                    "ignored for the root server)\n");
    print_common_help();
    fprintf(stderr, "\n");
}

ucs_status_t parse_cmd(int argc, char * const argv[], char **server_name)
{
    int c = 0, idx = 0;

    while ((c = getopt(argc, argv, "g:n:p:s:m:h")) != -1) {
        switch (c) {
        case 'g':
            num_connections = atoi(optarg);
            break;
        case 'n':
            *server_name = optarg;
            break;
        case 'p':
            server_port = atoi(optarg);
            if (server_port <= 0) {
                fprintf(stderr, "Wrong server port number %d\n", server_port);
                return UCS_ERR_UNSUPPORTED;
            }
            break;
        case 's':
            test_string_length = atol(optarg);
            if (test_string_length < 0) {
                fprintf(stderr, "Wrong string size %ld\n", test_string_length);
                return UCS_ERR_UNSUPPORTED;
            }
            break;
        case 'm':
            test_mem_type = parse_mem_type(optarg);
            if (test_mem_type == UCS_MEMORY_TYPE_LAST) {
                return UCS_ERR_UNSUPPORTED;
            }
            break;
        case 'h':
        default:
            print_usage();
            return UCS_ERR_UNSUPPORTED;
        }
    }
    fprintf(stderr, "INFO: UCG_HELLO_WORLD server = %s port = %d, pid = %d\n",
             *server_name, server_port, getpid());

    for (idx = optind; idx < argc; idx++) {
        fprintf(stderr, "WARNING: Non-option argument %s\n", argv[idx]);
    }
    return UCS_OK;
}

int main(int argc, char **argv)
{
    // TODO: make this function work...
    int ret         = -1;
    char *root_name = NULL;
    ucs_status_t status;

    /* Parse the command line */
    status = parse_cmd(argc, argv, &root_name);
    CHKERR_JUMP(status != UCS_OK, "parse_cmd\n", err);

    ucg_minimal_ctx_t ctx;
    struct sockaddr_in sock_addr   = {
            .sin_family            = AF_INET,
            .sin_port              = htons(server_port),
            .sin_addr              = {
                    .s_addr        = root_name ? inet_addr(root_name) : INADDR_ANY
            }
    };
    ucs_sock_addr_t server_address = {
            .addr                  = (struct sockaddr *)&sock_addr,
            .addrlen               = sizeof(struct sockaddr)
    };

    void *test_string = mem_type_malloc(test_string_length);
    CHKERR_JUMP(test_string == NULL, "allocate memory\n", err);

    status = ucg_minimal_init(&ctx, &server_address, num_connections, root_name ? UCG_MINIMAL_FLAG_SERVER : 0);
    CHKERR_JUMP(status != UCS_OK, "ucg_minimal_init\n", err_cleanup);

    status = ucg_minimal_broadcast(&ctx, test_string, sizeof(test_string_length));
    CHKERR_JUMP(status != UCS_OK, "ucg_minimal_broadcast\n", err_finalize);

    ret = 0;

err_finalize:
    ucg_minimal_finalize(&ctx);

err_cleanup:
    mem_type_free(test_string);

err:
    return ret;
}
