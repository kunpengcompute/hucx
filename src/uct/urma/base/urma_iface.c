/**
* Copyright (C) Huawei Technologies Co., Ltd. 2026. ALL RIGHTS RESERVED.
*
* See file LICENSE for terms.
*/

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "urma_iface.h"
#include "urma_ep.h"

#include <uct/api/uct.h>
#include <uct/base/uct_md.h>
#include <uct/base/uct_iface.h>
#include <ucs/arch/bitops.h>
#include <ucs/arch/cpu.h>
#include <ucs/type/class.h>
#include <ucs/type/cpu_set.h>
#include <ucs/debug/log.h>
#include <ucs/sys/string.h>
#include <ucs/sys/sock.h>

#define UCT_COMPLETION_ERR_INFO_MAX_LEN 256
#define UB_UBN 1

static const char *uct_ub_fence_mode_values[] = {
    [UCT_UB_FENCE_MODE_NONE]   = "none",
    [UCT_UB_FENCE_MODE_AUTO]   = "auto",
    [UCT_UB_FENCE_MODE_LAST]   = NULL
};

const char *uct_ub_mtu_values[] = {
    [UCT_UB_MTU_DEFAULT]    = "default",
    [UCT_UB_MTU_256]        = "256",
    [UCT_UB_MTU_512]        = "512",
    [UCT_UB_MTU_1024]       = "1024",
    [UCT_UB_MTU_2048]       = "2048",
    [UCT_UB_MTU_4096]       = "4096",
    [UCT_UB_MTU_8192]       = "8192",
};

#ifdef ENABLE_STATS
static ucs_stats_class_t uct_ub_iface_stats_class = {
    .name = "ub_iface",
    .num_counters = UCT_UB_IFACE_STAT_LAST,
    .counter_names = {
        [UCT_UB_IFACE_STAT_RX_COMPLETION]      = "rx_completion",
        [UCT_UB_IFACE_STAT_TX_COMPLETION]      = "tx_completion",
        [UCT_UB_IFACE_STAT_RX_JFR_AVAILABLE]   = "rx_jfr_available",
        [UCT_UB_IFACE_STAT_NO_READS_AVAILABLE] = "no_reads_available",
        [UCT_UB_IFACE_STAT_RX_DL_CR_ERR]       = "rx_dl_cr_error",
        [UCT_UB_IFACE_STAT_TX_DL_CR_ERR]       = "no_reads_available"
    }
};
#endif /* ENABLE_STATS */

static urma_jfr_wr_t __thread g_wrs[UCT_IBP_MAX_RECV_WRS + 1] = {0};
static urma_sge_t __thread g_src_sge_buf[UCT_IBP_MAX_RECV_WRS] = {0};

ucs_config_field_t uct_ub_iface_config_table[] = {
    {"", "MAX_NUM_EPS=256", NULL,
     ucs_offsetof(uct_ub_iface_config_t, super), UCS_CONFIG_TYPE_TABLE(uct_iface_config_table)},

    {"SEG_SIZE", "8192",
     "Size of bounce buffers used for send and recv",
     ucs_offsetof(uct_ub_iface_config_t, seg_size), UCS_CONFIG_TYPE_MEMUNITS},

    {"MAX_INLINE_SIZE", "128",
     "Max inline size used to control the capability of short message communication",
     ucs_offsetof(uct_ub_iface_config_t, max_inline), UCS_CONFIG_TYPE_MEMUNITS},

    {"TX_MAX_POLL", "16",
     "Max number of receive completions to pick during TX poll",
     ucs_offsetof(uct_ub_iface_config_t, tx.max_poll), UCS_CONFIG_TYPE_UINT},

    {"RX_MAX_POLL", "16",
     "Max number of receive completions to pick during RX poll",
     ucs_offsetof(uct_ub_iface_config_t, rx.max_poll), UCS_CONFIG_TYPE_UINT},

    {"TX_DEPTH", "256",
     "Length of jfs queue depth, for IB device, the max value is 8191.",
     ucs_offsetof(uct_ub_iface_config_t, tx.depth), UCS_CONFIG_TYPE_UINT},

    {"RX_DEPTH", "4096",
     "Length of jfr queue depth, for IB device, the max value is 4096.",
     ucs_offsetof(uct_ub_iface_config_t, rx.depth), UCS_CONFIG_TYPE_UINT},

    {"RX_MAX_BATCH", "16",
     "How many buffers assigned to receive data in one batch.",
     ucs_offsetof(uct_ub_iface_config_t, rx.max_batch), UCS_CONFIG_TYPE_UINT},

    {"TX_POLL_ALWAYS", "n",
     "When enabled, TX completions are polled every time the progress function is invoked.\n"
     "Otherwise poll TX completions only if no RX completions found.",
     ucs_offsetof(uct_ub_iface_config_t, tx.poll_always), UCS_CONFIG_TYPE_BOOL},

    {"LOCAL_SUBNET", "n",
     "Use the local IP address and subnet mask of each network device to route ub packets.\n"
     "If set to 'y', only addresses within the interface's subnet will be assumed as reachable.\n"
     "If set to 'n', every remote ub address is assumed to be reachable from any port.",
     ucs_offsetof(uct_ub_iface_config_t, local_subnet), UCS_CONFIG_TYPE_BOOL},

    {"SUBNET_PREFIX_LEN", "auto",
     "Length, in bits, of the subnet prefix to be used for reachability check.\n"
     "when UCX_IB_ROCE_LOCAL_SUBNET is enabled.\n"
     " - auto  - Detect the subnet prefix length automatically from device address.\n"
     " - inf   - Allow connections only within the same machine and same device.\n"
     " - <num> - Specify a numeric bit-length value for the subnet prefix.",
     ucs_offsetof(uct_ub_iface_config_t, subnet_pfx_len), UCS_CONFIG_TYPE_ULUNITS},

    {"MAX_AM_HDR", "128",
     "Buffer size to reserve for active message headers. If set to 0, the transport will\n"
     "not support zero-copy active messages.",
     ucs_offsetof(uct_ub_iface_config_t, max_am_hdr), UCS_CONFIG_TYPE_MEMUNITS},

    {"MAX_GET_ZCOPY", "auto",
     "Maximal size of get operation with zcopy protocol.",
     ucs_offsetof(uct_ub_iface_config_t, tx.max_get_zcopy), UCS_CONFIG_TYPE_MEMUNITS},

    {"TX_MAX_WR", "-1",
     "Limits the number of outstanding posted work requests. The actual limit is\n"
     "a minimum between this value and the TX queue length. -1 means no limit.",
     ucs_offsetof(uct_ub_iface_config_t, tx.tx_max_wr), UCS_CONFIG_TYPE_UINT},

    {"TX_JFC_MODERATION", "64",
     "Maximum number of send WRs which can be posted without requesting a completion.",
     ucs_offsetof(uct_ub_iface_config_t, tx.jfc_moderation), UCS_CONFIG_TYPE_UINT},

    {"TX_MAX_GET_BYTES", "inf",
     "Maximal size of get operation with zcopy protocol",
     ucs_offsetof(uct_ub_iface_config_t, tx.max_get_bytes), UCS_CONFIG_TYPE_MEMUNITS},

    {"FENCE", "auto",
     "ub fence type when API fence requested:\n"
     "  none   - fence is a no-op\n"
     "  auto   - enable fence function, ordering of outstanding communications on the interface",
     ucs_offsetof(uct_ub_iface_config_t, fence_mode), UCS_CONFIG_TYPE_ENUM(uct_ub_fence_mode_values)},

    {"FC_ENABLE", "n",
     "Enable flow control protocol to prevent sender from overwhelming the receiver,\n",
     ucs_offsetof(uct_ub_iface_config_t, fc.enable), UCS_CONFIG_TYPE_BOOL},

    {"FC_HARD_THRESH", "0.25",
     "Threshold for sending hard request for FC credits to the peer. This value\n"
     "refers to the percentage of the FC_WND_SIZE value. (must be > 0 and < 1)",
     ucs_offsetof(uct_ub_iface_config_t, fc.hard_thresh), UCS_CONFIG_TYPE_DOUBLE},

    {"FC_WND_SIZE", "512",
     "The size of flow control window per endpoint. limits the number of AM\n"
     "which can be sent w/o acknowledgment.",
     ucs_offsetof(uct_ub_iface_config_t, fc.wnd_size), UCS_CONFIG_TYPE_UINT},

    {"PATH_MTU", "default",
     "Path MTU. \"default\" will select the best MTU for the device.",
     ucs_offsetof(uct_ub_iface_config_t, path_mtu), UCS_CONFIG_TYPE_ENUM(uct_ub_mtu_values)},

    {"OOR_CNT", "0",
     "OOR window size: by packet.",
     ucs_offsetof(uct_ub_iface_config_t, oor_cnt), UCS_CONFIG_TYPE_UINT},

    {"OOS_CNT", "0",
     "out of standing packet cnt.",
     ucs_offsetof(uct_ub_iface_config_t, oos_cnt), UCS_CONFIG_TYPE_UINT},

    {"TX_PSN", "1",
     "PSN of tx.\n",
     ucs_offsetof(uct_ub_iface_config_t, tx_psn), UCS_CONFIG_TYPE_UINT},

    {"RX_PSN", "1",
     "PSN of rx.\n",
     ucs_offsetof(uct_ub_iface_config_t, rx_psn), UCS_CONFIG_TYPE_UINT},

    UCT_IFACE_MPOOL_CONFIG_FIELDS("TX_", -1, 1024, 128m, 1.0, "send",
                                  ucs_offsetof(uct_ub_iface_config_t, tx.mp),
                                  "\nAttention: Setting this param with value != -1 is a dangerous thing\n"
                                  "will cause deadlock or performance degradation."),

    UCT_IFACE_MPOOL_CONFIG_FIELDS("RX_", -1, 0, 128m, 1.0, "receive",
                                  ucs_offsetof(uct_ub_iface_config_t, rx.mp), ""),

    {NULL}
};

ucs_status_t uct_ub_iface_check_validity(const uct_iface_params_t *params, const uct_worker_h worker)
{
    if (!(params->open_mode & UCT_IFACE_OPEN_MODE_DEVICE)) {
        ucs_error("UB open_mode is not UCT_IFACE_OPEN_MODE_DEVICE");
        return UCS_ERR_UNSUPPORTED;
    }

    if (ucs_derived_of(worker, uct_priv_worker_t)->thread_mode == UCS_THREAD_MODE_MULTI) {
        ucs_error("UB transports do not support multi-threaded worker");
        return UCS_ERR_INVALID_PARAM;
    }

    return UCS_OK;
}

void uct_ub_send_op_completion_handler(uct_ub_iface_send_op_t *op, const void *resp)
{
    if (op->user_comp != NULL) {
        uct_invoke_completion(op->user_comp, UCS_OK);
    }
    uct_ub_iface_put_send_op(op);
}

static void uct_ub_address_pack(const uct_ub_address_pack_params_t *params, uct_ub_address_t *ub_addr)
{
    void *ptr = ub_addr + 1;

    *uct_ub_serialize_next(&ptr, urma_eid_t) = params->eid;
    *uct_ub_serialize_next(&ptr, uint32_t)   = params->uasid;
    *uct_ub_serialize_next(&ptr, uint8_t)    = params->urma_mode;
    *uct_ub_serialize_next(&ptr, uint8_t)    = params->path_mtu;
    *uct_ub_serialize_next(&ptr, uint16_t)   = params->oor_cnt;
    *uct_ub_serialize_next(&ptr, uint8_t)    = params->mn;
    *uct_ub_serialize_next(&ptr, uint8_t)    = params->cc_alg;
    *uct_ub_serialize_next(&ptr, uint8_t)    = params->rx_psn;
    /* If there are parameters to be selectively packed, set this flags based on params->flags. */
    ub_addr->flags = 0;
}

static void uct_ub_iface_device_address_pack(uct_ub_iface_t *iface, uct_ub_address_t *ub_addr)
{
    uct_ub_md_t *md = uct_ub_iface_md(iface);
    uct_ub_address_pack_params_t params;

    params.flags = 0;
    params.eid = iface->eid;
    params.uasid = iface->uasid;
    params.urma_mode = md->dev.urma_ctx->dev->type;
    params.path_mtu = iface->config.mtu;
    params.oor_cnt = iface->config.oor_cnt;
    params.mn = md->dev.dev_attr.dev_cap.mn;
    params.cc_alg = md->dev.dev_attr.dev_cap.congestion_ctrl_alg;
    params.rx_psn = iface->config.rx_psn;
    uct_ub_address_pack(&params, ub_addr);
}

static inline void uct_ub_init_iface_eid_uasid(uct_ub_iface_t *iface, const uct_ub_device_t *dev)
{
    iface->eid = dev->urma_ctx->eid;
    iface->uasid = dev->urma_ctx->uasid;
}

ucs_status_t uct_ub_iface_get_device_address(uct_iface_h tl_iface, uct_device_addr_t *dev_addr)
{
    uct_ub_iface_t *iface = ucs_derived_of(tl_iface, uct_ub_iface_t);

    uct_ub_iface_device_address_pack(iface, (void*)dev_addr);

    return UCS_OK;
}

/* Description: Calculate packed address size based on flags. */
static size_t uct_ub_address_size(const uct_ub_address_pack_params_t *params)
{
    size_t size = sizeof(uct_ub_address_t);

    size += sizeof(urma_eid_t); // For eid
    size += sizeof(uint32_t); // For uasid
    size += sizeof(uint8_t); // For urma_mode
    size += sizeof(uint8_t); // For path_mtu
    size += sizeof(uint16_t); // For oor_cnt
    size += sizeof(uint8_t); // For mn
    size += sizeof(uint8_t); // For cc_alg
    size += sizeof(uint8_t); // For rx_psn
    return size;
}

static size_t uct_ub_iface_address_size(uct_ub_iface_t *iface)
{
    uct_ub_address_pack_params_t params;

    params.flags = 0;

    return uct_ub_address_size(&params);
}

void uct_ub_address_unpack(const uct_ub_address_t *ub_addr, uct_ub_address_pack_params_t *params_p)
{
    uct_ub_address_pack_params_t params = {0};
    const void *ptr = ub_addr + 1;

    params.eid = *uct_ub_serialize_next(&ptr, urma_eid_t);
    params.uasid = *uct_ub_serialize_next(&ptr, uint32_t);
    params.urma_mode = *uct_ub_serialize_next(&ptr, uint8_t);
    params.path_mtu = *uct_ub_serialize_next(&ptr, uint8_t);
    params.oor_cnt = *uct_ub_serialize_next(&ptr, uint16_t);
    params.mn = *uct_ub_serialize_next(&ptr, uint8_t);
    params.cc_alg = *uct_ub_serialize_next(&ptr, uint8_t);
    params.rx_psn = *uct_ub_serialize_next(&ptr, uint8_t);
    params.flags = ub_addr->flags;

    *params_p = params;
}

static int uct_ub_iface_hns_ub_is_reachable(const uct_ub_device_eid_info_t * local_eid_info,
                                          const uct_ub_address_pack_params_t *remote_ub_addr, unsigned prefix_bits)
{
    urma_transport_type_t remote_urma_mode = (urma_transport_type_t)remote_ub_addr->urma_mode;
    urma_transport_type_t local_urma_mode = local_eid_info->type;
    sa_family_t local_ub_addr_af = local_eid_info->addr_family;
    sa_family_t remote_ub_addr_af;
    uint8_t *remote_addr = NULL;
    uint8_t *local_addr = NULL;
    char remote_str[128];
    char local_str[128];
    ucs_status_t status;
    size_t addr_offset;
    size_t addr_size;
    int ret;

    /* check for zero-sized netmask */
    if (prefix_bits == 0) {
        return 1;
    }
    if (local_urma_mode != URMA_TRANSPORT_UB) {
        ucs_debug("Different transport type detected, local %d remote %d.", local_urma_mode, remote_urma_mode);
        return 0;
    }

    remote_ub_addr_af = AF_INET;
    if (local_ub_addr_af != remote_ub_addr_af) {
        ucs_debug("Different addr_family detected. local %s remote %s",
                  ucs_sockaddr_address_family_str(local_ub_addr_af),
                  ucs_sockaddr_address_family_str(remote_ub_addr_af));
        return 0;
    }
    status = ucs_sockaddr_inet_addr_size(local_ub_addr_af, &addr_size);
    if (status != UCS_OK) {
        ucs_error("Failed to detect ub address size");
        return 0;
    }
    /* sanity check on the subnet mask size (bits belonging to the prefix) */
    ucs_assert((prefix_bits / 8) <= addr_size);
    addr_offset = sizeof(urma_eid_t) - addr_size;
    local_addr = UCS_PTR_BYTE_OFFSET(&local_eid_info->eid, addr_offset);
    remote_addr = UCS_PTR_BYTE_OFFSET(&remote_ub_addr->flags + 1, addr_offset);

    /* check if the addresses have matching prefixes */
    ret = ucs_bitwise_is_equal(local_addr, remote_addr, prefix_bits);
    ucs_debug(ret ? "IP addresses match with a %u-bit prefix: local IP is %s,"
                    " remote IP is %s" :
                    "IP addresses do not match with a %u-bit prefix. local IP"
                    " is %s, remote IP is %s",
              prefix_bits,
              inet_ntop(local_ub_addr_af, local_addr, local_str, 128),
              inet_ntop(remote_ub_addr_af, remote_addr, remote_str, 128));
    return ret;
}

int uct_ub_iface_is_ubn(uct_ub_iface_t *iface)
{
    /*TODO distinguishing link type*/
    return UB_UBN;
}

int uct_ub_iface_is_reachable(const uct_iface_h tl_iface,
                              const uct_iface_is_reachable_params_t *params)
{
    const uct_ub_address_t *ub_addr = (const void*)params->device_addr;
    uct_ub_iface_t *iface = ucs_derived_of(tl_iface, uct_ub_iface_t);
    urma_transport_type_t local_urma_mode, remote_urma_mode;
    int is_local_eth = uct_ub_iface_is_ubn(iface);
    uct_ub_md_t *md = uct_ub_iface_md(iface);
    uct_ub_address_pack_params_t ub_params;

    uct_ub_address_unpack(ub_addr, &ub_params);

    local_urma_mode = md->dev.urma_ctx->dev->type;
    remote_urma_mode = (urma_transport_type_t)ub_params.urma_mode;

    if ((local_urma_mode == URMA_TRANSPORT_UB) && (remote_urma_mode == URMA_TRANSPORT_UB)) {
        /* UB mode always reachable */
        return 1;
    }
    if (is_local_eth) {
        return uct_ub_iface_hns_ub_is_reachable(&iface->eid_info, &ub_params, iface->addr_prefix_bits);
    }

    /* local and remote have different mode and therefore are unreachable */
    return 0;
}

int uct_ub_base_iface_is_reachable(const uct_iface_h tl_iface,
                                   const uct_device_addr_t *dev_addr,
                                   const uct_iface_addr_t *iface_addr)
{
    uct_iface_is_reachable_params_t params = {
        .field_mask  = UCT_IFACE_IS_REACHABLE_FIELD_DEVICE_ADDR |
                       UCT_IFACE_IS_REACHABLE_FIELD_IFACE_ADDR,
        .device_addr = dev_addr,
        .iface_addr  = iface_addr
    };

    return uct_iface_is_reachable_v2(tl_iface, &params);
}

static ucs_status_t uct_ub_iface_get_numa_latency(uct_ub_iface_t *iface, double *latency)
{
    uct_ub_device_t *dev = uct_ub_iface_device(iface);
    ucs_sys_cpuset_t temp_cpu_mask, process_affinity;
#if HAVE_NUMA
    int distance, min_cpu_distance;
    int cpu, num_cpus;
#endif
    int ret;

    ret = ucs_sys_getaffinity(&process_affinity);
    if (ret) {
        ucs_error("Failed to sched_getaffinity: %m");
        return UCS_ERR_INVALID_PARAM;
    }

#if HAVE_NUMA
    /* Try to estimate the extra device latency according to NUMA distance */
    if (dev->numa_node != -1) {
        min_cpu_distance = INT_MAX;
        num_cpus = ucs_min(CPU_SETSIZE, numa_num_configured_cpus());
        for (cpu = 0; cpu < num_cpus; ++cpu) {
            if (!CPU_ISSET(cpu, &process_affinity)) {
                continue;
            }
            distance = numa_distance(ucs_numa_node_of_cpu(cpu), dev->numa_node);
            if (distance >= UCS_NUMA_MIN_DISTANCE) {
                min_cpu_distance = ucs_min(min_cpu_distance, distance);
            }
        }

        if (min_cpu_distance != INT_MAX) {
            /* set the extra latency to (numa_distance - 10) * 20nsec */
            *latency = (min_cpu_distance - UCS_NUMA_MIN_DISTANCE) * 20e-9;
            return UCS_OK;
        }
    }
#endif

    /* Estimate the extra device latency according to its local CPUs mask */
    CPU_AND(&temp_cpu_mask, &dev->local_cpus, &process_affinity);
    if (CPU_EQUAL(&process_affinity, &temp_cpu_mask)) {
        *latency = 0;
    } else {
        *latency = 200e-9;
    }
    return UCS_OK;
}

static ucs_status_t uct_ub_get_info_from_active_speed(urma_speed_t active_speed, double *c, double *encoding,
                                                      double *signal_rate)
{
    switch (active_speed) {
        case URMA_SP_10M:
            *c = 5000e-9;
            *signal_rate = 10e6;
            *encoding = 8.0 / 10.0;
            break;
        case URMA_SP_100M:
            *c = 4500e-9;
            *signal_rate = 100e6;
            *encoding = 8.0 / 10.0;
            break;
        case URMA_SP_1G:
            *c = 4000e-9;
            *signal_rate = 1e9;
            *encoding = 8.0 / 10.0;
            break;
        case URMA_SP_2_5G:
            *c = 3200e-9;
            *signal_rate = 2.5e9;
            *encoding = 8.0 / 10.0;
            break;
        case URMA_SP_5G:
            *c = 2600e-9;
            *signal_rate = 5e9;
            *encoding = 8.0 / 10.0;
            break;
        case URMA_SP_10G:
            *c = 1300e-9;
            *signal_rate = 10e9;
            *encoding = 8.0 / 10.0;
            break;
        case URMA_SP_14G:
            *c = 1200e-9;
            *signal_rate = 14e9;
            *encoding = 64.0 / 66.0;
            break;
        case URMA_SP_25G:
            *c = 1200e-9;
            *signal_rate = 25e9;
            *encoding = 64.0 / 66.0;
            break;
        case URMA_SP_40G:
            *c = 1000e-9;
            *signal_rate = 40e9;
            *encoding = 64.0 / 66.0;
            break;
        case URMA_SP_50G:
            *c = 1000e-9;
            *signal_rate = 50e9;
            *encoding = 64.0 / 66.0;
            break;
        case URMA_SP_100G:
            *c = 600e-9;
            *signal_rate = 100e9;
            *encoding = 64.0 / 66.0;
            break;
        case URMA_SP_200G:
            *c = 400e-9;
            *signal_rate = 200e9;
            *encoding = 64.0 / 66.0;
            break;
        case URMA_SP_400G:
            *c = 300e-9;
            *signal_rate = 400e9;
            *encoding = 64.0 / 66.0;
            break;
        case URMA_SP_800G:
            *c = 200e-9;
            *signal_rate = 800e9;
            *encoding = 64.0 / 66.0;
            break;
        default:
            ucs_error("Invalid active_speed:%d", active_speed);
            return UCS_ERR_IO_ERROR;
    }

    return UCS_OK;
}

ucs_status_t uct_ub_iface_event_fd_get(uct_iface_h tl_iface, int *fd_p)
{
    uct_ub_iface_t *iface = ucs_derived_of(tl_iface, uct_ub_iface_t);

    *fd_p = iface->jfce->fd;
    return UCS_OK;
}

void uct_ub_iface_release_desc(uct_recv_desc_t *self, void *desc)
{
    uct_ub_iface_t *iface = ucs_container_of(self, uct_ub_iface_t, release_desc);
    void *ub_desc = NULL;

    ub_desc = UCS_PTR_BYTE_OFFSET(desc, -(ptrdiff_t)iface->config.rx_headroom_offset);
    ucs_mpool_put_inline(ub_desc);
}

ucs_status_t uct_ub_base_iface_query(uct_ub_iface_t *iface, uct_iface_attr_t *iface_attr)
{
    static const uint8_t ub_port_widths[] = {0, 1, 4, 0, 8, 0, 0, 0, 12, 0, 0, 0, 0, 0, 0, 0, 2};
    uct_ub_md_t *md = ucs_derived_of(iface->super.md, uct_ub_md_t);
    double encoding, signal_rate, wire_speed;
    const uct_ub_device_spec_t* spec = NULL;
    uct_ub_device_t *dev = &md->dev;
    urma_link_width_t active_width;
    urma_speed_t active_speed;
    size_t mtu, extra_pkt_len;
    urma_mtu_t active_mtu;
    ucs_status_t status;
    double numa_latency;
    uint8_t width;

    spec = uct_ub_device_spec(dev);

    uct_base_iface_query(&iface->super, iface_attr);

    active_width = dev->dev_attr.port_attr[iface->port_num - dev->first_port].active_width;
    active_speed = dev->dev_attr.port_attr[iface->port_num - dev->first_port].active_speed;
    active_mtu = dev->dev_attr.port_attr[iface->port_num - dev->first_port].active_mtu;

    if ((active_width >= ucs_static_array_size(ub_port_widths)) ||
        (ub_port_widths[active_width] == 0)) {
        ucs_warn("Invalid active width on %s:%d: %d, assuming 1x",
                 dev->dev_name, iface->port_num, active_width);
        width = 1;
    } else {
        width = ub_port_widths[active_width];
    }

    status = uct_ub_get_info_from_active_speed(active_speed, &(iface_attr->latency.c),
                                               &encoding, &signal_rate);
    if (status != UCS_OK) {
        return status;
    }

    status = uct_ub_iface_get_numa_latency(iface, &numa_latency);
    if (status != UCS_OK) {
        return status;
    }

    /* Wire speed calculation: Width * SignalRate * Encoding */
    wire_speed = (width * signal_rate * encoding) / 8.0;

    /* Calculate packet overhead  */
    mtu = ucs_min(uct_ub_mtu_value(active_mtu), iface->config.seg_size);
    if (mtu == 0) {
        return UCS_ERR_IO_ERROR;
    }

    /* Length of the packet header, which may vary according to the TL type. */
    extra_pkt_len = 0;

    iface_attr->latency.c += numa_latency;
    iface_attr->latency.m = 0;

    iface_attr->bandwidth.shared = ucs_min((wire_speed * mtu) / (mtu + extra_pkt_len), md->pci_bw);
    iface_attr->bandwidth.dedicated = 0;
    iface_attr->priority = (spec == NULL) ? 0 : spec->priority;

    iface_attr->device_addr_len = iface->addr_size;
    iface_attr->dev_num_paths = 1;

    return UCS_OK;
}

ucs_status_t uct_ub_iface_cap_query(uct_ub_iface_t *iface, uct_iface_attr_t *iface_attr)
{
    const size_t am_max_iov = iface->config.max_send_sge - 1;
    const size_t rma_max_iov = iface->config.max_send_sge;
    uct_ub_device_t *dev = uct_ub_iface_device(iface);
    size_t am_max_hdr = iface->config.short_desc_size;
    size_t max_inline = iface->config.max_inline;
    size_t am_min_hdr = sizeof(uct_ub_hdr_t);
    size_t mtu;

    iface_attr->max_conn_priv = 0;
    mtu = uct_ub_mtu_value(iface->config.mtu);
    if (mtu == 0) {
        return UCS_ERR_IO_ERROR;
    }
    iface_attr->cap.flags = UCT_IFACE_FLAG_AM_BCOPY           |
                            UCT_IFACE_FLAG_AM_ZCOPY           |
                            UCT_IFACE_FLAG_PUT_BCOPY          |
                            UCT_IFACE_FLAG_PUT_ZCOPY          |
                            UCT_IFACE_FLAG_PENDING            |
                            UCT_IFACE_FLAG_CB_SYNC            |
                            UCT_IFACE_FLAG_EP_CHECK;

    iface_attr->cap.event_flags = UCT_IFACE_FLAG_EVENT_SEND_COMP    |
                                  UCT_IFACE_FLAG_EVENT_RECV         |
                                  UCT_IFACE_FLAG_EVENT_FD;

    iface_attr->cap.put.opt_zcopy_align = UCS_SYS_PCI_MAX_PAYLOAD;
    iface_attr->cap.get.opt_zcopy_align = UCS_SYS_PCI_MAX_PAYLOAD;
    iface_attr->cap.am.opt_zcopy_align  = UCS_SYS_PCI_MAX_PAYLOAD;

    iface_attr->cap.put.align_mtu = mtu;
    iface_attr->cap.get.align_mtu = mtu;
    iface_attr->cap.am.align_mtu  = mtu;

    /* PUT */
    iface_attr->cap.put.max_short = max_inline;
    iface_attr->cap.put.max_bcopy = iface->config.seg_size;
    iface_attr->cap.put.min_zcopy = 0;
    iface_attr->cap.put.max_zcopy = dev->dev_attr.dev_cap.max_msg_size;
    iface_attr->cap.put.max_iov = rma_max_iov;

    /* GET */
    iface_attr->cap.get.max_bcopy = iface->config.seg_size;
    iface_attr->cap.get.min_zcopy = 0;
    iface_attr->cap.get.max_zcopy = iface->config.max_get_zcopy;
    iface_attr->cap.get.max_iov = rma_max_iov;

    /* AM */
    iface_attr->cap.am.max_short = (size_t)ucs_max((ssize_t)(max_inline - am_min_hdr), 0);
    iface_attr->cap.am.max_bcopy = iface->config.seg_size - am_min_hdr;
    iface_attr->cap.am.min_zcopy = 0;
    iface_attr->cap.am.max_zcopy = iface->config.seg_size - am_min_hdr;
    iface_attr->cap.am.max_hdr = am_max_hdr - am_min_hdr;
    iface_attr->cap.am.max_iov = am_max_iov;

    /* atomic */
    if (dev->urma_ctx->dev->type == URMA_TRANSPORT_UB) {
        iface_attr->cap.atomic64.op_flags |= UCS_BIT(UCT_ATOMIC_OP_ADD);
        iface_attr->cap.atomic64.fop_flags |= UCS_BIT(UCT_ATOMIC_OP_ADD)  |
                                              UCS_BIT(UCT_ATOMIC_OP_CSWAP);
    } else {
        iface_attr->cap.atomic64.fop_flags = UCT_UB_ATOMIC_FETCH_FLAGS;
        iface_attr->cap.atomic32.fop_flags = UCT_UB_ATOMIC_FETCH_FLAGS;

        iface_attr->cap.atomic64.op_flags = UCT_UB_ATOMIC_POST_FLAGS;
        iface_attr->cap.atomic32.op_flags = UCT_UB_ATOMIC_POST_FLAGS;
    }

    iface_attr->cap.flags |= UCT_IFACE_FLAG_ATOMIC_DEVICE;

    /* Error Handling */
    iface_attr->cap.flags |= UCT_IFACE_FLAG_ERRHANDLE_PEER_FAILURE;

    if (iface_attr->cap.am.max_short) {
        iface_attr->cap.flags |= UCT_IFACE_FLAG_AM_SHORT;
    }

    if (iface_attr->cap.put.max_short) {
        iface_attr->cap.flags |= UCT_IFACE_FLAG_PUT_SHORT;
    }

    return UCS_OK;
}

static UCS_F_ALWAYS_INLINE size_t uct_ub_iface_max_get_zcopy(const uct_ub_iface_config_t *config,
                                                             uct_ub_device_t *dev)
{
    uint64_t max_ub_msg_size = dev->dev_attr.dev_cap.max_msg_size;

    if (config->tx.max_get_zcopy == UCS_MEMUNITS_AUTO) {
        return max_ub_msg_size;
    }

    return ucs_min(config->tx.max_get_zcopy, max_ub_msg_size);
}

static inline void uct_ub_init_iface_reads_count(uct_ub_iface_t *iface,
                                                 const uct_ub_iface_config_t *config)
{
    if ((config->tx.max_get_bytes == UCS_MEMUNITS_INF) ||
        (config->tx.max_get_bytes == UCS_MEMUNITS_AUTO)) {
        iface->tx.reads_available = SSIZE_MAX;
    } else {
        iface->tx.reads_available = config->tx.max_get_bytes;
    }
    iface->tx.reads_completed = 0;
}

static UCS_F_ALWAYS_INLINE ucs_status_t uct_ub_fence_info_init(uct_ub_iface_t *iface, int fence_mode)
{
    if (fence_mode == UCT_UB_FENCE_MODE_AUTO) {
        iface->config.fence_mode = UCT_UB_FENCE_MODE_AUTO;
    } else if (fence_mode == UCT_UB_FENCE_MODE_NONE) {
        iface->config.fence_mode = UCT_UB_FENCE_MODE_NONE;
    } else {
        ucs_error("Incorrect fence value: %d", iface->config.fence_mode);
        return UCS_ERR_INVALID_PARAM;
    }
    iface->tx.fi.fence_beat = 0;
    return UCS_OK;
}

static inline ucs_status_t uct_ub_init_iface_attr(uct_ub_iface_t *iface, const uct_ub_iface_config_t *config,
                                                  const uct_iface_params_t *params,
                                                  const uct_ub_iface_init_attr_t *init_attr, uct_ub_iface_ops_t *ops)
{
    size_t rx_headroom = (params->field_mask &
                          UCT_IFACE_PARAM_FIELD_RX_HEADROOM) ?
                          params->rx_headroom : 0;
    uct_ub_device_t *dev = uct_ub_iface_device(iface);
    enum urma_mtu port_mtu;
    ucs_status_t status;
    uint8_t port_num;

    status = uct_ub_device_find_port(dev, params->mode.device.dev_name, &port_num);
    if (status != UCS_OK) {
        return status;
    }

    ucs_ptr_array_init(&iface->eps, "ub_eps");
    (void)memset(&(iface->send_wr), 0, sizeof(urma_jfs_wr_t));

    iface->ops = ops;
    iface->port_num = port_num;
    iface->config.zcopy_ops_count = init_attr->jfc_depth[UCT_UB_DIR_TX];
    iface->tx.jfc_available = init_attr->jfc_depth[UCT_UB_DIR_TX] - 1;
    iface->config.tx_max_poll = config->tx.max_poll;
    iface->config.rx_max_poll = config->rx.max_poll;
    iface->config.tx_queue_depth = ucs_min(config->tx.depth, dev->dev_attr.dev_cap.max_jfs_depth);
    iface->config.rx_queue_depth = ucs_min(config->rx.depth, dev->dev_attr.dev_cap.max_jfr_depth);
    iface->config.rx_max_batch = ucs_min(config->rx.max_batch,
                                         iface->config.rx_queue_depth / 4); /* max_batch should not exceed a quarter
                                                                               of  rx_queue_depth */
    /* when rx_queue_depth is less than 4,  assign 1 to rx_max_batch*/
    iface->config.rx_max_batch = ucs_max(iface->config.rx_max_batch, 1);

    port_mtu = dev->dev_attr.port_attr[iface->port_num - dev->first_port].active_mtu;

    /* MTU is set by user configuration */
    if (config->path_mtu != UCT_UB_MTU_DEFAULT) {
        /* cast from uct_ub_mtu_t to urma_mtu */
        iface->config.mtu = (enum urma_mtu)(config->path_mtu +
                                           (URMA_MTU_512 - UCT_UB_MTU_512));
    } else {
        iface->config.mtu = port_mtu;
    }

    if (init_attr->use_min_seg) {
        iface->config.seg_size = ucs_min(uct_ub_mtu_value(iface->config.mtu), config->seg_size);
    } else {
        iface->config.seg_size = config->seg_size;
    }
    if (iface->config.seg_size == 0) {
        ucs_error("ub iface segment size is 0");
        return UCS_ERR_IO_ERROR;
    }
    iface->config.oor_cnt = config->oor_cnt;
    iface->config.oos_cnt = config->oos_cnt;
    iface->config.tx_psn = config->tx_psn;
    iface->config.rx_psn = config->rx_psn;
    iface->config.tx_poll_always = config->tx.poll_always;
    iface->config.max_inline = ucs_min(config->max_inline, dev->dev_attr.dev_cap.max_jfs_inline_len);

    iface->config.rx_payload_offset = sizeof(uct_ub_iface_recv_desc_t) +
                                      ucs_max(sizeof(uct_recv_desc_t) + rx_headroom,
                                      init_attr->rx_priv_len + init_attr->rx_hdr_len);
    iface->config.rx_hdr_offset = iface->config.rx_payload_offset - init_attr->rx_hdr_len;
    iface->config.rx_headroom_offset = iface->config.rx_payload_offset - rx_headroom;
    iface->release_desc.cb = uct_ub_iface_release_desc;

    iface->config.short_desc_size = sizeof(uct_ub_hdr_t) + config->max_am_hdr;
    iface->config.short_desc_size = ucs_max(UCT_UB_MAX_ATOMIC_SIZE, iface->config.short_desc_size);

    iface->config.max_get_zcopy = uct_ub_iface_max_get_zcopy(config, dev);
    iface->config.max_send_sge = ucs_min(UCT_UB_MAX_IOV, dev->dev_attr.dev_cap.max_jfs_sge);
    iface->config.tx_max_wr = ucs_min(config->tx.tx_max_wr, iface->config.tx_queue_depth);
    iface->config.tx_jfc_moderation = ucs_min(config->tx.jfc_moderation,
                                              iface->config.tx_max_wr / 4); /* max number of wrs without completion
                                                                               should not exceed a quater of tx queue
                                                                               depth. */
    iface->addr_size = uct_ub_iface_address_size(iface);

    uct_ub_init_iface_reads_count(iface, config);

    status = uct_ub_fence_info_init(iface, config->fence_mode);
    if (status != UCS_OK) {
        return status;
    }

    return UCS_OK;
}

ucs_status_t uct_ub_iface_fence(uct_iface_h tl_iface, unsigned flags)
{
    uct_ub_iface_t *iface = ucs_derived_of(tl_iface, uct_ub_iface_t);

    if (iface->config.fence_mode != UCT_UB_FENCE_MODE_NONE) {
        iface->tx.fi.fence_beat++;
    }

    UCT_TL_IFACE_STAT_FENCE(&iface->super);
    return UCS_OK;
}

void uct_ub_iface_fc_init(uct_ub_iface_t *iface, const uct_ub_iface_config_t *config)
{
    iface->config.fc_enabled = config->fc.enable;
    if (iface->config.fc_enabled) {
        /* Assume that number of recv buffers is the same on all peers.
         * Then FC window size is the same for all endpoints as well.
         * TODO: Make wnd size to be a property of the particular interface. */
        iface->config.fc_wnd_size = ucs_min(config->fc.wnd_size, iface->config.rx_queue_depth);
        iface->config.fc_hard_thresh = ucs_max((int)(iface->config.fc_wnd_size * config->fc.hard_thresh), 1);
    } else {
        iface->config.fc_wnd_size = INT16_MAX;
        iface->config.fc_hard_thresh = 0;
    }
}

ucs_status_t uct_ub_iface_zcopy_ops_init(uct_ub_iface_t *iface)
{
    const unsigned count = iface->config.zcopy_ops_count;
    uct_ub_iface_send_op_t *op = NULL;

    iface->tx.zcopy_ops_buffer = (uct_ub_iface_send_op_t*)ucs_calloc(count, sizeof(*iface->tx.zcopy_ops_buffer), "ub_zcopy_ops");
    if (iface->tx.zcopy_ops_buffer == NULL) {
        return UCS_ERR_NO_MEMORY;
    }
    iface->tx.zcopy_free_ops = &iface->tx.zcopy_ops_buffer[0];

    for (op = iface->tx.zcopy_ops_buffer; op < iface->tx.zcopy_ops_buffer + count; op++) {
        op->handler = uct_ub_send_op_completion_handler;
        op->flags = UCT_UB_IFACE_SEND_OP_FLAG_IFACE;
        op->iface = iface;
        op->next = (op == (iface->tx.zcopy_ops_buffer + count - 1)) ? NULL : (op + 1);
    }

    return UCS_OK;
}

void uct_ub_iface_zcopy_ops_cleanup(uct_ub_iface_t *iface)
{
    const unsigned total_count = iface->config.zcopy_ops_count;
    uct_ub_iface_send_op_t *op = NULL;
    unsigned free_count;

    free_count = 0;
    for (op = iface->tx.zcopy_free_ops; op != NULL; op = op->next) {
        free_count++;
        ucs_assert(free_count <= total_count);
    }
    if (free_count != iface->config.zcopy_ops_count) {
        ucs_warn("Rm urma iface: %u/%d zcopy ops were not released.",
                 total_count - free_count, total_count);
    }
    ucs_free(iface->tx.zcopy_ops_buffer);
    iface->tx.zcopy_ops_buffer = NULL;
}

static void uct_ub_iface_send_desc_init(uct_iface_h tl_iface, void *obj, uct_mem_h memh)
{
    uct_ub_iface_send_desc_t *desc = (uct_ub_iface_send_desc_t *)obj;
    desc->tseg = uct_ub_memh_get_tseg(memh);
    desc->super.flags = 0;
}

static void uct_ub_iface_recv_desc_init(uct_iface_h tl_iface, void *obj, uct_mem_h memh)
{
    uct_ub_iface_recv_desc_t *desc = (uct_ub_iface_recv_desc_t *)obj;
    desc->src_tseg = uct_ub_memh_get_tseg(memh);
}

static ucs_status_t uct_ub_iface_recv_mpool_init(uct_ub_iface_t *iface, const uct_ub_iface_config_t *config,
                                                 const uct_iface_params_t *params,
                                                 const char *name, ucs_mpool_t *mp)
{
    size_t elem_size;
    unsigned grow;

    if (config->rx.depth < UCT_UB_IFACE_RECV_MP_MIN_GROW_SIZE) {
        grow = UCT_UB_IFACE_RECV_MP_MIN_GROW_SIZE;
    } else {
        grow = ucs_min((int)(1.1 * config->rx.depth + 0.5), /* We want to have some free (+10%)
                                                               elements to avoid mem pool expansion */
                        config->rx.mp.max_bufs);
    }

    elem_size = iface->config.rx_payload_offset + iface->config.seg_size;
    return uct_iface_mpool_init(&iface->super, mp, elem_size, iface->config.rx_hdr_offset, UCS_SYS_CACHE_LINE_SIZE,
                                &config->rx.mp, grow, uct_ub_iface_recv_desc_init, name);
}

static ucs_status_t uct_ub_iface_short_desc_mpool_init(uct_ub_iface_t *iface, const uct_ub_iface_config_t *config)
{
    size_t elem_size = sizeof(uct_ub_iface_send_desc_t) + iface->config.short_desc_size;
    size_t align_offset = sizeof(uct_ub_iface_send_desc_t);
    ucs_status_t status;

    status = uct_iface_mpool_init(&iface->super, &iface->tx.short_desc_mp,
                                  elem_size, align_offset, UCS_SYS_CACHE_LINE_SIZE,
                                  &config->tx.mp, iface->config.tx_queue_depth,
                                  uct_ub_iface_send_desc_init, "ub_short_desc");
    return status;
}

static ucs_mpool_ops_t uct_ub_send_op_mpool_ops = {
    .chunk_alloc   = ucs_mpool_chunk_malloc,
    .chunk_release = ucs_mpool_chunk_free,
    .obj_init      = NULL,
    .obj_cleanup   = NULL
};

static ucs_status_t uct_ub_iface_send_op_mp_init(uct_ub_iface_t *iface)
{
    ucs_mpool_params_t mp_params;
    ucs_status_t status;

    /* Create memory pool for flush completions. Can't just alloc a certain
     * size buffer, because number of simultaneous flushes is not limited by
     * CQ or QP resources. */
    ucs_mpool_params_reset(&mp_params);
    mp_params.elem_size = sizeof(uct_ub_iface_send_op_t);
    mp_params.elems_per_chunk = 512;
    mp_params.ops = &uct_ub_send_op_mpool_ops;
    mp_params.name = "ub-send-ops";
    status = ucs_mpool_init(&mp_params, &iface->tx.send_op_mp);

    return status;
}

ucs_status_t uct_ub_iface_mempool_init(uct_ub_iface_t *iface, const uct_ub_iface_config_t *config,
                                       const uct_iface_params_t *params)
{
    ucs_status_t status;

    status = uct_iface_mpool_init(&iface->super, &iface->tx.mp,
                                  sizeof(uct_ub_iface_send_desc_t) + iface->config.seg_size,
                                  sizeof(uct_ub_iface_send_desc_t),
                                  UCS_SYS_CACHE_LINE_SIZE, &config->tx.mp,
                                  iface->config.tx_queue_depth, uct_ub_iface_send_desc_init, "ub_send_desc");
    if (status != UCS_OK) {
        ucs_warn("Init tx memory pool err: %m");
        goto err_init_tx_mp;
    }

    status = uct_ub_iface_recv_mpool_init(iface, config, params, "ub_recv_desc", &iface->rx.mp);
    if (status != UCS_OK) {
        ucs_warn("Init receive memory pool err: %m");
        goto err_init_rx_mp;
    }

    status = uct_ub_iface_zcopy_ops_init(iface);
    if (status != UCS_OK) {
        ucs_warn("Init zcopy options err: %m");
        goto err_init_zcopy_ops_buf;
    }

    status = uct_ub_iface_short_desc_mpool_init(iface, config);
    if (status != UCS_OK) {
        ucs_warn("Init short desc memory pool err: %m");
        goto err_init_short_desc_mpool;
    }

    iface->ka_local_seg = (uct_ub_iface_ka_desc_t*)ucs_mpool_get_inline(&iface->rx.mp);
    if (ucs_unlikely(iface->ka_local_seg == NULL)) {
        ucs_warn("Init receive keepalive memory pool err: %m");
        status = UCS_ERR_IO_ERROR;
        goto err_get_ka_seg;
    }

    status = uct_ub_iface_send_op_mp_init(iface);
    if (status != UCS_OK) {
        ucs_warn("Init send options mpool err: %m");
        goto err_send_op_mp_init;
    }

    return UCS_OK;

err_send_op_mp_init:
    ucs_mpool_put_inline(iface->ka_local_seg);
err_get_ka_seg:
    ucs_mpool_cleanup(&iface->tx.short_desc_mp, 1);
err_init_short_desc_mpool:
    uct_ub_iface_zcopy_ops_cleanup(iface);
err_init_zcopy_ops_buf:
    ucs_mpool_cleanup(&iface->rx.mp, 1);
err_init_rx_mp:
    ucs_mpool_cleanup(&iface->tx.mp, 1);
err_init_tx_mp:
    return status;
}

ucs_status_t uct_ub_iface_add_ep(uct_ub_iface_t *iface, uct_ub_ep_t *ep)
{
    ep->ep_id = ucs_ptr_array_insert(&iface->eps, ep);
    if (ep->ep_id >= UCT_UB_IFACE_MAX_EPS) {
        ucs_error("Ep index out of range :%u", ep->ep_id);
        goto err;
    }

    ucs_trace("Ub iface add ep id %d.", ep->ep_id);
    return UCS_OK;
err:
    ucs_ptr_array_remove(&iface->eps, ep->ep_id);
    return UCS_ERR_NO_RESOURCE;
}

void uct_ub_ep_tx_outstanding_queue_init(uct_ub_tx_queue_t *tx_queue)
{
    ucs_queue_head_init(&tx_queue->outstanding);
}

void uct_ub_txcnt_init(uct_ub_tx_que_status_t *tx_que_status, unsigned que_bitmap_mask,
                       int que_bitmap_arr_num)
{
    tx_que_status->pi = 0;
    tx_que_status->ci = 0;
    tx_que_status->uncompltes = 0;
    tx_que_status->que_bitmap_mask = que_bitmap_mask;
    tx_que_status->que_bitmap_arr_num = que_bitmap_arr_num;
}

void uct_ub_iface_remove_ep(uct_ub_iface_t *iface, uct_ub_ep_t *ep)
{
    ucs_trace("Ub iface remove ep id %d.", ep->ep_id);
    ucs_ptr_array_remove(&iface->eps, ep->ep_id);
}

void uct_ub_destroy_jfc(urma_jfc_t *jfc)
{
    urma_status_t status;

    status = urma_delete_jfc(jfc);
    if (status != URMA_SUCCESS) {
        ucs_warn("Call urma_delete_jfc failed: %m. status:%d", status);
    }
}

static void uct_ub_destroy_jfce(urma_jfce_t *jfce)
{
    urma_status_t status;

    status = urma_delete_jfce(jfce);
    if (status != URMA_SUCCESS) {
        ucs_warn("Call urma_delete_jfce failed: %m. status:%d", status);
    }
}

void uct_ub_destroy_jfr(urma_jfr_t *jfr)
{
    urma_status_t status = URMA_SUCCESS;
    int cnt = 0;

    do {
        status = urma_delete_jfr(jfr);
        (void)usleep(RETRY_INTERVAL);
        cnt++;
    } while(status == URMA_EAGAIN && cnt < MAX_RETRY_TIME);

    if (status) {
        ucs_warn("Failed to urma_delete_jfr:%m");
    }
}

ucs_status_t uct_ub_iface_arm_jfc(uct_ub_iface_t *iface, uct_ub_dir_t dir,
                                  int solicited_only)
{
    urma_status_t status;

    /* req notify cq (jfc) */
    status = urma_rearm_jfc(iface->jfc[dir], solicited_only);
    if (status != URMA_SUCCESS) {
        ucs_error("Call urma_rearm_jfc(jfc[%d]) ErrCode: %d", dir, status);
        return UCS_ERR_IO_ERROR;
    }
    return UCS_OK;
}

ucs_status_t uct_ub_iface_pre_arm(uct_ub_iface_t *iface)
{
    urma_jfc_t *jfc = NULL;
    int send_cq_count = 0;
    int recv_cq_count = 0;
    uint32_t ack_cnt = 1;
    int cnt;

    /* poll get cq(jfc) event */
    do {
        cnt = urma_wait_jfc(iface->jfce, 1, UCT_UB_POLL_JFC_TIMEOUT, &jfc);
        if (cnt == UCT_UB_POLL_JFC_SUCCESS) {
            if (iface->jfc[UCT_UB_DIR_TX] == jfc) {
                urma_ack_jfc((urma_jfc_t **)&jfc, &ack_cnt, 1);
                ++send_cq_count;
            }
            if (iface->jfc[UCT_UB_DIR_RX] == jfc) {
                urma_ack_jfc((urma_jfc_t **)&jfc, &ack_cnt, 1);
                ++recv_cq_count;
            }
        }
    } while (cnt == UCT_UB_POLL_JFC_SUCCESS);

    if (cnt == UCT_UB_POLL_JFC_ERR) {
        ucs_error("Call urma_wait_jfc ErrCode: %d", cnt);
        return UCS_ERR_IO_ERROR;
    }
    /* avoid re-arming the interface if any events exists */

    if ((send_cq_count > 0) || (recv_cq_count > 0)) {
        ucs_trace("Arm cq: got %d send and %d recv events, returning BUSY",
                  send_cq_count, recv_cq_count);
        return UCS_ERR_BUSY;
    }
    return UCS_OK;
}

ucs_status_t uct_ub_iface_event_arm(uct_iface_h tl_iface, unsigned events)
{
    uct_ub_iface_t *iface = ucs_derived_of(tl_iface, uct_ub_iface_t);
    int arm_rx_solicited = 0;
    ucs_status_t status;
    int arm_rx_all = 0;

    status = uct_ub_iface_pre_arm(iface);
    if (status != UCS_OK) {
        return status;
    }

    if (events & UCT_EVENT_SEND_COMP) {
        status = iface->ops->arm_jfc(iface, UCT_UB_DIR_TX, 0);
        if (status != UCS_OK) {
            return status;
        }
    }

    if (events & UCT_EVENT_RECV) {
        arm_rx_solicited = 1; /* to wake up on active messages */
    }

    if ((events & UCT_EVENT_SEND_COMP) && iface->config.fc_enabled) {
        arm_rx_all = 1; /* to wake up on FC grants (or if forced) */
    }

    if (arm_rx_solicited || arm_rx_all) {
        status = iface->ops->arm_jfc(iface, UCT_UB_DIR_RX,
                                     arm_rx_solicited && !arm_rx_all);
        if (status != UCS_OK) {
            return status;
        }
    }
    return UCS_OK;
}

ucs_status_t uct_ub_create_jfc(uct_ub_iface_t *iface, uct_ub_dir_t dir,
                               const uct_ub_iface_init_attr_t *init_attr)
{
    uct_ub_device_t *dev = uct_ub_iface_device(iface);
    unsigned jfc_depth = init_attr->jfc_depth[dir];
    urma_jfc_t *jfc = NULL;

    urma_jfc_cfg_t jfc_cfg = {
        .depth = jfc_depth,
        .jfce = iface->jfce,
        .user_ctx = (uint64_t)NULL,
    };
    jfc_cfg.flag.bs.jfc_inline = 1;
    jfc_cfg.flag.bs.lock_free = 1;
    jfc_cfg.flag.bs.reserved = 0;
    jfc = urma_create_jfc(dev->urma_ctx, &jfc_cfg);
    if (!jfc) {
        ucs_error("Call urma_create_jfc(depth is %d) failed: %m", jfc_depth);
        return UCS_ERR_IO_ERROR;
    }

    iface->jfc[dir] = jfc;
    return UCS_OK;
}

static ucs_status_t uct_ub_iface_create_jfc(uct_ub_iface_t *iface, uct_ub_dir_t dir,
                                            const uct_ub_iface_init_attr_t *init_attr)
{
    ucs_status_t status;

    status = iface->ops->create_jfc(iface, dir, init_attr);

    return status;
}

ucs_status_t uct_ub_iface_create_jfr(uct_ub_iface_t *iface, urma_jfr_cfg_t *jfr_cfg)
{
    uct_ub_device_t *dev = uct_ub_iface_device(iface);

    iface->rx.jfr = urma_create_jfr(dev->urma_ctx, jfr_cfg);
    if (iface->rx.jfr == NULL) {
        ucs_error("Call urma_create_jfr(depth is %d) failed: %m", jfr_cfg->depth);
        return UCS_ERR_INVALID_PARAM;
    }

    return UCS_OK;
}

static ucs_status_t uct_ub_create_resources(uct_ub_iface_t *iface, const uct_ub_device_t *dev,
                                            const uct_ub_iface_init_attr_t *init_attr)
{
    ucs_status_t status;

    iface->jfce = urma_create_jfce(dev->urma_ctx);
    if (iface->jfce == NULL) {
        ucs_error("Call urma_create_jfce failed: %m");
        status = UCS_ERR_INVALID_PARAM;
        goto err;
    }

    status = uct_ub_iface_create_jfc(iface, UCT_UB_DIR_TX, init_attr);
    if (status != UCS_OK) {
        ucs_error("Fail to create_jfc_tx");
        goto err_destroy_jfce;
    }

    status = uct_ub_iface_create_jfc(iface, UCT_UB_DIR_RX, init_attr);
    if (status != UCS_OK) {
        ucs_error("Fail to reate_jfc_rx");
        goto err_destroy_send_jfc;
    }

    return UCS_OK;

err_destroy_send_jfc:
    uct_ub_destroy_jfc(iface->jfc[UCT_UB_DIR_TX]);
err_destroy_jfce:
    uct_ub_destroy_jfce(iface->jfce);
err:
    return status;
}

static void uct_ub_destroy_resources(uct_ub_iface_t *iface)
{
    iface->ops->destroy_jfc(iface, UCT_UB_DIR_TX);
    iface->ops->destroy_jfc(iface, UCT_UB_DIR_RX);
    uct_ub_destroy_jfce(iface->jfce);
}

void uct_ub_iface_atomic_handler_init(uct_ub_iface_t *iface, uct_ub_device_t *dev)
{
    iface->config.atomic64_handler = uct_ub_common_atomic64_le_handler;
    iface->config.atomic32_handler = uct_ub_common_atomic32_le_handler;
}

static UCS_F_ALWAYS_INLINE void uct_ub_iface_recv(uct_ub_iface_t *iface, unsigned count)
{
    uct_ub_iface_recv_desc_t *desc[count];
    urma_jfr_wr_t *bad_wr = NULL;
    unsigned tmp_count = 0;
    urma_status_t status;

    while(tmp_count < count) {
        UCT_TL_IFACE_GET_RX_DESC(&iface->super, &iface->rx.mp, desc[tmp_count], break);
        g_src_sge_buf[tmp_count].addr = (uint64_t)uct_ub_iface_recv_desc_hdr(iface, desc[tmp_count]);
        g_src_sge_buf[tmp_count].len  = (iface->config.rx_payload_offset - iface->config.rx_hdr_offset) +
                                       iface->config.seg_size;
        g_src_sge_buf[tmp_count].tseg = desc[tmp_count]->src_tseg;
        g_wrs[tmp_count].src.sge      = &g_src_sge_buf[tmp_count];
        g_wrs[tmp_count].src.num_sge  = 1;
        g_wrs[tmp_count].user_ctx     = (uintptr_t)desc[tmp_count];
        g_wrs[tmp_count].next         = &g_wrs[tmp_count + 1];
        tmp_count++;
    }

    if (ucs_unlikely(tmp_count == 0)) {
        return;
    }

    g_wrs[tmp_count - 1].next = NULL;

    status = urma_post_jfr_wr(iface->rx.jfr, g_wrs, &bad_wr);
    if (ucs_unlikely(status != URMA_SUCCESS)) {
        for (int err_count = 0; err_count < tmp_count; err_count++) {
            UCT_TL_IFACE_PUT_DESC(desc[err_count]);
        }
        ucs_fatal("Urma post jfr wr failed status:%u, %m.", status);
    }

    iface->rx.jfr_que_available -= tmp_count;
    UCS_STATS_SET_COUNTER(iface->stats, UCT_UB_IFACE_STAT_RX_JFR_AVAILABLE, iface->rx.jfr_que_available);

    return;
}

static UCS_F_ALWAYS_INLINE void uct_ub_iface_recv_common(uct_ub_iface_t *iface, unsigned count)
{
    while (count > UCT_IBP_MAX_RECV_WRS) {
        uct_ub_iface_recv(iface, UCT_IBP_MAX_RECV_WRS);
        count -= UCT_IBP_MAX_RECV_WRS;
    }
    uct_ub_iface_recv(iface, count);
}

void uct_ub_iface_init_recv_queue(uct_ub_iface_t *iface, unsigned init_count)
{
    iface->rx.jfr_que_available = init_count;
    uct_ub_iface_recv_common(iface, init_count);
}

static UCS_F_ALWAYS_INLINE void uct_ub_iface_recv_check(uct_ub_iface_t *iface)
{
    unsigned batch = iface->config.rx_max_batch;

    if (ucs_likely(iface->rx.jfr_que_available < batch)) {
        return;
    }

    uct_ub_iface_recv_common(iface, batch);
    return;
}

static UCS_F_ALWAYS_INLINE void uct_ub_iface_handle_am(uct_ub_iface_t *iface, uct_ub_hdr_t *hdr,
                                                       uct_ub_iface_recv_desc_t *desc, urma_cr_t *cr,
                                                       uint32_t fc_mask)
{
    uint32_t length = cr->completion_len;
    ucs_status_t status;
    void *udesc = NULL;

    if (ucs_unlikely(hdr->am_id & fc_mask)) {
        status = iface->ops->fc_handler(iface, hdr, cr, UCT_CB_PARAM_FLAG_DESC);
    } else {
        status = uct_iface_invoke_am(&iface->super, hdr->am_id, hdr + 1,
                                     length - sizeof(*hdr), UCT_CB_PARAM_FLAG_DESC);
    }
    if (ucs_unlikely(status == UCS_INPROGRESS)) {
        udesc = (char*)desc + iface->config.rx_headroom_offset;
        uct_recv_desc(udesc) = &iface->release_desc;
    } else {
        ucs_mpool_put_inline(desc);
    }
}

static UCS_F_ALWAYS_INLINE void uct_ub_iface_deal_with_recv_cr(uct_ub_iface_t *iface, urma_cr_t *cr,
                                                              int count, uint32_t fc_mask)
{
    uct_ub_iface_recv_desc_t *desc = NULL;
    uct_ub_hdr_t *hdr = NULL;
    int i;

    for (i = 0; i < count; i++) {
        if (ucs_unlikely(cr[i].status != URMA_CR_SUCCESS)) {
            UCS_STATS_UPDATE_COUNTER(iface->stats, UCT_UB_IFACE_STAT_RX_DL_CR_ERR, 1);
            ucs_error("The cr status is err: %m; status:%u.", cr[i].status);
            continue;
        }

        desc = (uct_ub_iface_recv_desc_t *)cr[i].user_ctx;
        hdr = (uct_ub_hdr_t *)uct_ub_iface_recv_desc_hdr(iface, desc);
        uct_ub_iface_handle_am(iface, hdr, desc, &cr[i], fc_mask);
    }

    iface->rx.jfr_que_available += count;
    UCS_STATS_SET_COUNTER(iface->stats, UCT_UB_IFACE_STAT_RX_JFR_AVAILABLE, iface->rx.jfr_que_available);
}

unsigned uct_ub_iface_poll_rx(uct_ub_iface_t *iface, uint32_t fc_mask)
{
    int cr_cnt = iface->config.rx_max_poll;
    urma_cr_t cr[cr_cnt];
    int count;

    count = urma_poll_jfc(iface->jfc[UCT_UB_DIR_RX], cr_cnt, cr);
    if (ucs_unlikely(count <= 0)) {
        count = 0;
        goto out;
    }
    UCS_STATS_UPDATE_COUNTER(iface->stats, UCT_UB_IFACE_STAT_RX_COMPLETION, count);
    uct_ub_iface_deal_with_recv_cr(iface, cr, count, fc_mask);

    uct_ub_iface_recv_check(iface);

out:
    return count;
}

ucs_status_t uct_ub_iface_completion_err_proc(uct_ub_iface_t *iface, urma_cr_t *cr, void *jetty,
                                              uct_ub_ep_t *ep, uct_ub_ep_t *fc_ep)
{
    ucs_log_level_t log_level = iface->super.config.failure_level;
    char err_info[UCT_COMPLETION_ERR_INFO_MAX_LEN] = {};
    uct_ub_device_t *dev = uct_ub_iface_device(iface);
    ucs_status_t status = UCS_ERR_IO_ERROR;
    uint64_t user_ctx;

    switch (cr->status) {
        case URMA_CR_LOC_LEN_ERR:
            snprintf(err_info, sizeof(err_info), "Local length");
            break;
        case URMA_CR_LOC_OPERATION_ERR:
            snprintf(err_info, sizeof(err_info), "Local operation");
            break;
        case URMA_CR_UNSUPPORTED_OPCODE_ERR:
            snprintf(err_info, sizeof(err_info), "Unsupported opcode err");
            break;
        case URMA_CR_REM_RESP_LEN_ERR:
            snprintf(err_info, sizeof(err_info), "Remote rsponse length error");
            break;
        case URMA_CR_LOC_ACCESS_ERR:
            snprintf(err_info, sizeof(err_info), "Local Access");
            break;
        case URMA_CR_REM_UNSUPPORTED_REQ_ERR:
            snprintf(err_info, sizeof(err_info), "Remote unsupported request");
            break;
        case URMA_CR_REM_OPERATION_ERR:
            snprintf(err_info, sizeof(err_info), "Remote operation err");;
            break;
        case URMA_CR_ACK_TIMEOUT_ERR:
            snprintf(err_info, sizeof(err_info), "Ack timeout");
            status = UCS_ERR_ENDPOINT_TIMEOUT;
            break;
        case URMA_CR_RNR_RETRY_CNT_EXC_ERR:
            snprintf(err_info, sizeof(err_info), "Transaction retry counter exceeded");
            status = UCS_ERR_ENDPOINT_TIMEOUT;
            break;
        case URMA_CR_WR_FLUSH_ERR:
            snprintf(err_info, sizeof(err_info), "Flush");
            break;
        default:
            snprintf(err_info, sizeof(err_info), "Unexpect");
            break;
    }

    if (!ucs_log_is_enabled(log_level)) {
        goto out;
    }

    user_ctx = cr->user_ctx;

    ucs_log(log_level, "%s on %s:%d."
            "Cr_opcode is %u, user_ctx is %lu\n",
            err_info, dev->dev_name, iface->port_num, cr->opcode, user_ctx);
out:
    return status;
}

static ucs_status_t uct_ub_iface_init_addr_prefix(uct_ub_iface_t *iface,
                                                  const uct_ub_iface_config_t *config)
{
    uct_ub_device_eid_info_t *eid_info = &iface->eid_info;
    uct_ub_device_t *dev = uct_ub_iface_device(iface);
    size_t addr_size, max_prefix_bits;
    struct sockaddr_storage mask;
    const void *mask_addr = NULL;
    char ndev_name[IFNAMSIZ];
    ucs_status_t status;

    if (!config->local_subnet) {
        iface->addr_prefix_bits = 0;
        return UCS_OK;
    }

    status = ucs_sockaddr_inet_addr_size(eid_info->addr_family, &addr_size);
    if (status != UCS_OK) {
        return status;
    }

    max_prefix_bits = 8 * addr_size;
    ucs_assertv(max_prefix_bits <= UINT8_MAX, "max_prefix_bits=%zu",
                max_prefix_bits);

    if (config->subnet_pfx_len == UCS_ULUNITS_INF) {
        iface->addr_prefix_bits = max_prefix_bits;
        return UCS_OK;
    } else if (config->subnet_pfx_len != UCS_ULUNITS_AUTO) {
        if (config->subnet_pfx_len > max_prefix_bits) {
            ucs_error("Invalid parameter for subnet frefix len, actual %zu, expected <= %zu",
                      config->subnet_pfx_len, max_prefix_bits);
            return UCS_ERR_INVALID_PARAM;
        }
        iface->addr_prefix_bits = config->subnet_pfx_len;
    }

    status = uct_ub_device_get_ndev_name(dev, ndev_name, sizeof(ndev_name));
    if (status != UCS_OK) {
        ucs_warn("Failed to get ndev name, status = %d",status);
        goto out_mask_info_failed;
    }

    status = ucs_netif_get_addr(ndev_name, AF_UNSPEC, NULL, (struct sockaddr*)&mask);
    if (status != UCS_OK) {
        ucs_warn("Failed to netif_get_addr, status = %d",  status);
        goto out_mask_info_failed;
    }

    mask_addr = ucs_sockaddr_get_inet_addr((struct sockaddr*)&mask);
    iface->addr_prefix_bits = max_prefix_bits - ucs_count_ptr_trailing_zero_bits(mask_addr, max_prefix_bits);

    return UCS_OK;

out_mask_info_failed:
    iface->addr_prefix_bits = 0;

    return UCS_OK;
}

static ucs_status_t uct_ub_iface_init_eid_info(uct_ub_iface_t* iface, uct_ub_iface_config_t *config)
{
    uct_ub_device_eid_info_t * eid_info = &iface->eid_info;
    uct_ub_device_t *dev = uct_ub_iface_device(iface);
    ucs_status_t status;

    eid_info->eid_index = dev->urma_ctx->eid_index;
    eid_info->type = dev->urma_ctx->dev->type;
    eid_info->eid = dev->urma_ctx->eid;
    eid_info->addr_family = uct_ub_device_get_addr_family(&eid_info->eid);

    status =  uct_ub_iface_init_addr_prefix(iface, config);
    if (status != UCS_OK) {
        ucs_warn("Failed to uct_ub_iface_init_addr_prefix, status = %d", status);
    }

    return status;
}

UCS_CLASS_INIT_FUNC(uct_ub_iface_t, uct_ub_iface_ops_t *ops,
                    uct_iface_ops_t *tl_ops, uct_md_h md, uct_worker_h worker,
                    const uct_iface_params_t *params,
                    const uct_iface_config_t *tl_config, const uct_ub_iface_init_attr_t *init_attr)
{
    uct_ub_iface_config_t *config = ucs_derived_of(tl_config, uct_ub_iface_config_t);
    uct_ub_md_t *ub_md = ucs_derived_of(md, uct_ub_md_t);
    uct_ub_device_t *dev = &ub_md->dev;
    ucs_status_t status;

    UCT_CHECK_PARAM(params->field_mask & UCT_IFACE_PARAM_FIELD_OPEN_MODE,
                    "UCT_IFACE_PARAM_FIELD_OPEN_MODE is not defined");
    if (!(params->open_mode & UCT_IFACE_OPEN_MODE_DEVICE)) {
        ucs_error("Only UCT_IFACE_OPEN_MODE_DEVICE is supported");
        return UCS_ERR_UNSUPPORTED;
    }

    UCS_CLASS_CALL_SUPER_INIT(uct_base_iface_t, tl_ops, &ops->super, md, worker, params,
                              tl_config UCS_STATS_ARG(
                              ((params->field_mask & UCT_IFACE_PARAM_FIELD_STATS_ROOT) &&
                              (params->stats_root != NULL)) ? params->stats_root : dev->stats)
                              UCS_STATS_ARG(params->mode.device.dev_name));

    status = UCS_STATS_NODE_ALLOC(&self->stats, &uct_ub_iface_stats_class,
                                  self->super.stats, "-%p", self);
    if (status != UCS_OK) {
        goto err;
    }

    status = uct_ub_iface_init_eid_info(self, config);
    if (status != UCS_OK) {
        goto err_destroy_stats;
    }

    status = uct_ub_init_iface_attr(self, config, params, init_attr, ops);
    if (status != UCS_OK) {
        goto err_destroy_stats;
    }

    uct_ub_init_iface_eid_uasid(self, dev);

    status = uct_ub_create_resources(self, dev, init_attr);
    if (status != UCS_OK) {
        goto err_destroy_ub_resources;
    }

    status = uct_ub_iface_mempool_init(self, config, params);
    if (status != UCS_OK) {
        ucs_warn("Init ub iface memory pool err");
        goto err_destroy_mempool;
    }

    uct_ub_iface_atomic_handler_init(self, dev);

    return UCS_OK;

err_destroy_mempool:
    uct_ub_destroy_resources(self);
err_destroy_ub_resources:
    ucs_ptr_array_cleanup(&self->eps, 1);
err_destroy_stats:
    UCS_STATS_NODE_FREE(self->stats);
err:
    return status;
}

void uct_ub_iface_mpool_cleanup(uct_ub_iface_t *iface)
{
    ucs_mpool_cleanup(&iface->tx.send_op_mp, 1);
    ucs_mpool_put_inline(iface->ka_local_seg);
    ucs_mpool_cleanup(&iface->tx.mp, 1);
    ucs_mpool_cleanup(&iface->rx.mp, 0);
    uct_ub_iface_zcopy_ops_cleanup(iface);
    ucs_mpool_cleanup(&iface->tx.short_desc_mp, 1);
}

static UCS_CLASS_CLEANUP_FUNC(uct_ub_iface_t)
{
    uct_ub_iface_mpool_cleanup(self);
    ucs_ptr_array_cleanup(&self->eps, 1);
    UCS_STATS_NODE_FREE(self->stats);
    uct_ub_destroy_resources(self);
}

UCS_CLASS_DEFINE(uct_ub_iface_t, uct_base_iface_t);
