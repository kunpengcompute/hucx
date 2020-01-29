/**
 * Copyright (C) Mellanox Technologies Ltd. 2001-2016.  ALL RIGHTS RESERVED.
 *
 * See file LICENSE for terms.
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "ucx_info.h"

#include <ucp/api/ucp.h>
#include <ucs/time/time.h>
#include <ucs/sys/string.h>
#include <ucs/debug/assert.h>
#include <sys/resource.h>
#include <dirent.h>
#include <string.h>
#include <errno.h>

#if ENABLE_UCG
#include <ucg/base/ucg_context.h>
#endif


typedef struct {
    ucs_time_t       time;
    long             memory;
    int              num_fds;
} resource_usage_t;


static int get_num_fds()
{
    static const char *fds_dir = "/proc/self/fd";
    struct dirent *entry;
    int num_fds;
    DIR *dir;

    dir = opendir(fds_dir);
    if (dir == NULL) {
        return -1;
    }

    num_fds = 0;
    for (;;) {
        errno = 0;
        entry = readdir(dir);
        if (entry == NULL) {
            closedir(dir);
            if (errno == 0) {
                return num_fds;
            } else {
                return -1;
            }
        }

        if (strncmp(entry->d_name, ".", 1)) {
            ++num_fds;
        }
    }
}

static void get_resource_usage(resource_usage_t *usage)
{
    struct rusage rusage;
    int ret;

    usage->time = ucs_get_time();

    ret = getrusage(RUSAGE_SELF, &rusage);
    if (ret == 0) {
        usage->memory = rusage.ru_maxrss * 1024;
    } else {
        usage->memory = -1;
    }

    usage->num_fds = get_num_fds();
}

static void print_resource_usage(const resource_usage_t *usage_before,
                                 const char *title)
{
    resource_usage_t usage_after;

    get_resource_usage(&usage_after);

    if ((usage_after.memory != -1) && (usage_before->memory != -1) &&
        (usage_after.num_fds != -1) && (usage_before->num_fds != -1))
    {
        printf("# memory: %.2fMB, file descriptors: %d\n",
               (usage_after.memory - usage_before->memory) / (1024.0 * 1024.0),
               (usage_after.num_fds - usage_before->num_fds));
    }
    printf("# create time: %.3f ms\n",
           ucs_time_to_msec(usage_after.time - usage_before->time));
    printf("#\n");
}

static void listener_accept_callback(ucp_ep_h ep, void *arg)
{
    *(ucp_ep_h*)arg = ep;
}

static void
set_saddr(const char *addr_str, uint16_t port, struct sockaddr_in *saddr)
{
    memset(saddr, 0, sizeof(*saddr));
    saddr->sin_family      = AF_INET;
    saddr->sin_addr.s_addr = inet_addr(addr_str);
    saddr->sin_port        = htons(port);
}

static ucs_status_t
wait_completion(ucp_worker_h worker, ucp_worker_h peer_worker,
                ucs_status_ptr_t status_ptr)
{
    ucs_status_t status;

    if (status_ptr == NULL) {
        status = UCS_OK;
    } else if (UCS_PTR_IS_PTR(status_ptr)) {
        do {
            ucp_worker_progress(worker);
            ucp_worker_progress(peer_worker);
            status = ucp_request_test(status_ptr, NULL);
        } while (status == UCS_INPROGRESS);
        ucp_request_release(status_ptr);
    } else {
        status = UCS_PTR_STATUS(status_ptr);
    }

    return status;
}

static void
ep_close(ucp_worker_h worker, ucp_worker_h peer_worker, ucp_ep_h ep,
         ucp_ep_close_flags_t flags, const char *ep_type)
{
    ucp_request_param_t request_param;
    ucs_status_ptr_t status_ptr;

    request_param.op_attr_mask = UCP_OP_ATTR_FIELD_FLAGS;
    request_param.flags        = flags;

    status_ptr = ucp_ep_close_nbx(ep, &request_param);
    wait_completion(worker, peer_worker, status_ptr);
}

static ucs_status_t
create_listener(ucp_worker_h worker, ucp_listener_h *listener_p,
                uint16_t *listen_port_p, void *accept_cb_arg)
{
    ucp_listener_h listener;
    struct sockaddr_in listen_saddr;
    ucp_listener_params_t listen_params;
    ucp_listener_attr_t listen_attr;
    ucs_status_t status;

    set_saddr("0.0.0.0", 0, &listen_saddr);

    listen_params.field_mask         = UCP_LISTENER_PARAM_FIELD_SOCK_ADDR |
                                       UCP_LISTENER_PARAM_FIELD_ACCEPT_HANDLER;
    listen_params.sockaddr.addr      = (const struct sockaddr*)&listen_saddr;
    listen_params.sockaddr.addrlen   = sizeof(listen_saddr);
    listen_params.accept_handler.cb  = listener_accept_callback;
    listen_params.accept_handler.arg = accept_cb_arg;

    status = ucp_listener_create(worker, &listen_params, &listener);
    if (status != UCS_OK) {
        printf("<Failed to create UCP listener>\n");
        goto out;
    }

    listen_attr.field_mask = UCP_LISTENER_ATTR_FIELD_SOCKADDR;

    status = ucp_listener_query(listener, &listen_attr);
    if (status != UCS_OK) {
        printf("<Failed to query UCP listener>\n");
        goto out_destroy_listener;
    }

    status = ucs_sockaddr_get_port((struct sockaddr*)&listen_attr.sockaddr,
                                   listen_port_p);
    if (status != UCS_OK) {
        printf("<Failed to get port>\n");
        goto out_destroy_listener;
    }

    *listener_p = listener;
out:
    return status;

out_destroy_listener:
    ucp_listener_destroy(listener);
    goto out;
}

static ucs_status_t
print_ucp_ep_info(ucp_worker_h worker, ucp_worker_h peer_worker,
                  const ucp_ep_params_t *base_ep_params, const char *ip_addr,
                  process_placement_t proc_placement)
{
    ucp_listener_h listener        = NULL;
    ucp_ep_h server_ep             = NULL;
    ucp_ep_params_t ep_params      = *base_ep_params;
    ucp_worker_attr_t worker_attrs = {};
    ucs_status_t status;
    ucs_status_ptr_t status_ptr;
    struct sockaddr_in connect_saddr;
    uint16_t listen_port;
    ucp_ep_h ep;
    char ep_name[64];
    ucp_request_param_t request_param;

    if (ip_addr != NULL) {
        status = create_listener(peer_worker, &listener, &listen_port, &server_ep);
        if (status != UCS_OK) {
            return status;
        }

        ucs_strncpy_zero(ep_name, "client", sizeof(ep_name));

        set_saddr(ip_addr, listen_port, &connect_saddr);

        ep_params.field_mask      |= UCP_EP_PARAM_FIELD_FLAGS |
                                     UCP_EP_PARAM_FIELD_SOCK_ADDR;
        ep_params.flags            = UCP_EP_PARAMS_FLAGS_CLIENT_SERVER;
        ep_params.sockaddr.addr    = (struct sockaddr*)&connect_saddr;
        ep_params.sockaddr.addrlen = sizeof(connect_saddr);
    } else {
        worker_attrs.field_mask    = UCP_WORKER_ATTR_FIELD_ADDRESS |
                                     UCP_WORKER_ATTR_FIELD_ADDRESS_FLAGS;
        worker_attrs.address_flags =
                (proc_placement == PROCESS_PLACEMENT_INTER) ?
                UCP_WORKER_ADDRESS_FLAG_NET_ONLY : 0;

        status = ucp_worker_query(peer_worker, &worker_attrs);
        if (status != UCS_OK) {
            printf("<Failed to get UCP worker address>\n");
            return status;
        }

        ucs_strncpy_zero(ep_name, "connected to UCP worker", sizeof(ep_name));

        ep_params.field_mask |= UCP_EP_PARAM_FIELD_REMOTE_ADDRESS;
        ep_params.address     = worker_attrs.address;
    }

    status = ucp_ep_create(worker, &ep_params, &ep);
    if (status != UCS_OK) {
        printf("<Failed to create UCP endpoint>\n");
        goto out;
    }

    request_param.op_attr_mask = 0;
    /* do EP flush to make sure that fully completed to a peer and final
     * configuration is applied */
    status_ptr = ucp_ep_flush_nbx(ep, &request_param);
    status     = wait_completion(worker, peer_worker, status_ptr);
    if (status != UCS_OK) {
        printf("<Failed to flush UCP endpoint>\n");
        goto out_close_eps;
    }

    ucp_ep_print_info(ep, stdout);

out_close_eps:
    ep_close(worker, peer_worker, ep, 0, ep_name);

    if (server_ep != NULL) {
        ucs_assert(ip_addr != NULL); /* server EP is created only for sockaddr
                                      * connection flow */
        ep_close(worker, peer_worker, server_ep, UCP_EP_CLOSE_FLAG_FORCE,
                 "server");
    }

out:
    if (listener != NULL) {
        ucp_listener_destroy(listener);
    }

    if (worker_attrs.address == NULL) {
        ucp_worker_release_address(peer_worker, worker_attrs.address);
    }

    return status;
}

static int datatype_test_converter(void *datatype, ucp_datatype_t *ucp_datatype)
{
    *ucp_datatype = ucp_dt_make_contig((uintptr_t)datatype);
    return 0;
}

static int datatype_test_dt_is_int(void *datatype, int *is_signed)
{
    *is_signed = 0;
    return 0;
}

/* Check if the data-type is a floating-point (of any length) */
static int datatype_test_dt_is_fp(void *datatype)
{
    return 0;
}

/* Check if the reduction operation is a summation (e.g. MPI_SUM) */
static int datatype_test_op_is_sum(void *reduce_op)
{
    return 1;
}

/* Check if the reduction operation is indexed (e.g. MPI_MINLOC) */
static int datatype_test_op_is_loc(void *reduce_op)
{
    return 0;
}

/* Check if the reduction operation is commutative (predefined MPI ops are) */
static int datatype_test_op_is_com(void *reduce_op)
{
    return 1;
}

ucs_status_t
print_ucp_info(int print_opts, ucs_config_print_flags_t print_flags,
               uint64_t ctx_features, const ucp_ep_params_t *base_ep_params,
               size_t estimated_num_eps, size_t estimated_num_ppn,
               unsigned dev_type_bitmap, process_placement_t proc_placement,
               const char *mem_size, const char *ip_addr
#if ENABLE_UCG
               ,const char *planner_name,
               ucg_group_member_index_t root_index,
               ucg_group_member_index_t my_index,
               const char *collective_type_name,
               size_t dtype_count,
               ucg_group_member_index_t peer_count[UCG_GROUP_MEMBER_DISTANCE_LAST]
#endif
               )
{
    ucp_config_t *ucp_config;
    ucg_config_t *ucg_config;
    ucs_status_t status;
    ucp_context_h context;
    ucp_worker_h worker;
    ucp_params_t ucp_params;
    ucg_params_t ucg_params;
    ucp_worker_params_t worker_params;
    resource_usage_t usage;


#if ENABLE_UCG
    status     = ucg_config_read(NULL, NULL, &ucg_config);
    ucp_config = (ucp_config_t*)ucg_config;
#else
    status = ucp_config_read(NULL, NULL, &ucp_config);
#endif
    if (status != UCS_OK) {
        goto out;
    }

    ucp_params.field_mask        = UCP_PARAM_FIELD_FEATURES |
                                   UCP_PARAM_FIELD_ESTIMATED_NUM_EPS |
                                   UCP_PARAM_FIELD_ESTIMATED_NUM_PPN;
    ucp_params.features          = ctx_features;
    ucp_params.estimated_num_eps = estimated_num_eps;
    ucp_params.estimated_num_ppn = estimated_num_ppn;

    get_resource_usage(&usage);

    if (!(dev_type_bitmap & UCS_BIT(UCT_DEVICE_TYPE_SELF))) {
        ucp_config_modify(ucp_config, "SELF_DEVICES", "");
    }
    if (!(dev_type_bitmap & UCS_BIT(UCT_DEVICE_TYPE_SHM))) {
        ucp_config_modify(ucp_config, "SHM_DEVICES", "");
    }
    if (!(dev_type_bitmap & UCS_BIT(UCT_DEVICE_TYPE_NET))) {
        ucp_config_modify(ucp_config, "NET_DEVICES", "");
    }

#if ENABLE_UCG
    ucg_params.super             = &ucp_params;
    ucg_params.field_mask        = UCG_PARAM_FIELD_ADDRESS_CB  |
                                   UCG_PARAM_FIELD_DATATYPE_CB |
                                   UCG_PARAM_FIELD_REDUCE_OP_CB;
    ucg_params.address.lookup_f  = dummy_resolve_address;
    ucg_params.address.release_f = dummy_release_address;

    /* Not a real callbacks, but good enough for these tests */
    ucg_params.datatype.convert             = datatype_test_converter;
    ucg_params.datatype.is_integer_f        = datatype_test_dt_is_int;
    ucg_params.datatype.is_floating_point_f = datatype_test_dt_is_fp;
    ucg_params.reduce_op.is_sum_f           = datatype_test_op_is_sum;
    ucg_params.reduce_op.is_loc_expected_f  = datatype_test_op_is_loc;
    ucg_params.reduce_op.is_commutative_f   = datatype_test_op_is_com;

    ucg_context_h ucg_context;
    if (ctx_features & UCP_FEATURE_GROUPS) {
        status  = ucg_init(&ucg_params, ucg_config, &ucg_context);
        context = &ucg_context->ucp_ctx;
        /* It isn't ideal, but the idea is NOT to mix UCG and UCP APIs anyway */
    } else
#endif
    status = ucp_init(&ucp_params, ucp_config, &context);
    if (status != UCS_OK) {
        printf("<Failed to create context>\n");
        goto out_release_config;
    }

    if ((print_opts & PRINT_MEM_MAP) && (mem_size != NULL)) {
        ucp_mem_print_info(mem_size, context, stdout);
    }

    if (print_opts & PRINT_UCP_CONTEXT) {
        ucp_context_print_info(context, stdout);
        print_resource_usage(&usage, "UCP context");
    }

    if (!(print_opts & (PRINT_UCP_WORKER|PRINT_UCP_EP|PRINT_UCG|PRINT_UCG_TOPO))) {
        goto out_cleanup_context;
    }

    worker_params.field_mask  = UCP_WORKER_PARAM_FIELD_THREAD_MODE;
    worker_params.thread_mode = UCS_THREAD_MODE_MULTI;

    get_resource_usage(&usage);

    status = ucp_worker_create(context, &worker_params, &worker);
    if (status != UCS_OK) {
        printf("<Failed to create UCP worker>\n");
        goto out_cleanup_context;
    }

    if (print_opts & PRINT_UCP_WORKER) {
        ucp_worker_print_info(worker, stdout);
        print_resource_usage(&usage, "UCP worker");
    }

#if ENABLE_UCG
    if (print_opts & PRINT_UCG) {
        /* create a group with the generated parameters */
        enum ucg_group_member_distance distance = UCG_GROUP_MEMBER_DISTANCE_SELF;
        ucg_group_params_t group_params = {
                .field_mask        = UCG_GROUP_PARAM_FIELD_MEMBER_COUNT |
                                     UCG_GROUP_PARAM_FIELD_MEMBER_INDEX |
                                     UCG_GROUP_PARAM_FIELD_CB_CONTEXT   |
                                     UCG_GROUP_PARAM_FIELD_DISTANCES,
                .distance = &distance,
                .member_count = 1,
                .cb_context = NULL
        };

        ucg_group_h group;
        status = ucg_group_create(worker, &group_params, &group);
        if (status != UCS_OK) {
            printf("<Failed to create UCG group>\n");
            goto out_destroy_worker;
        }

        print_resource_usage(&usage, "UCG group");
        ucg_group_destroy(group);
    }

    if (print_opts & PRINT_UCG_TOPO) {
        ucg_group_member_index_t dist_len;
        enum ucg_group_member_distance* dist;
        if (UCS_OK == gen_ucg_topology(my_index, peer_count, &dist, &dist_len)) {
            print_ucg_topology(planner_name, worker, root_index, my_index,
                    collective_type_name, dtype_count, dist, dist_len, 1);
        }
    }
#endif

    if (print_opts & PRINT_UCP_EP) {
        if (proc_placement != PROCESS_PLACEMENT_SELF) {
            status = ucp_worker_create(context, &worker_params, &peer_worker);
            if (status != UCS_OK) {
                printf("<Failed to create peer UCP worker>\n");
                goto out_worker_destroy;
            }
        } else {
            peer_worker = worker;
        }

        status = print_ucp_ep_info(worker, peer_worker, base_ep_params, ip_addr,
                                   proc_placement);
        if (peer_worker != worker) {
            ucp_worker_destroy(peer_worker);
        }
    }

out_worker_destroy:
    ucp_worker_destroy(worker);

out_cleanup_context:
#if ENABLE_UCG
    if (ctx_features & UCP_FEATURE_GROUPS) {
        ucg_cleanup(ucg_context);
    } else
#endif
    ucp_cleanup(context);

out_release_config:
#if ENABLE_UCG
    if (ctx_features & UCP_FEATURE_GROUPS) {
        ucg_config_release(ucg_config);
    } else
#endif
    ucp_config_release(ucp_config);
out:
    return status;
}
