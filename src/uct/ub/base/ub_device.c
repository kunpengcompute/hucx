/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2026. ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "ub_device.h"
#include "ub_md.h"

#include <ucs/arch/bitops.h>
#include <ucs/debug/memtrack.h>
#include <ucs/debug/log.h>
#include <ucs/async/async.h>
#include <ucs/sys/compiler.h>
#include <ucs/sys/string.h>
#include <ucs/sys/sock.h>
#include <ucs/sys/sys.h>
#include <sys/poll.h>
#include <libgen.h>
#include <sched.h>

#define BASE_NUM_16 16
#define BASE_SHIFT_32 32
#define SKIP_LAYERS_2 2
#define RET_OK 4

static UCS_F_ALWAYS_INLINE
khint32_t uct_ub_async_event_hash_func(uct_ub_async_event_t event)
{
    return kh_int64_hash_func(((uint64_t)event.event_type << UCT_UB_DEV_SHIFT_BITS) |
                              event.resource_id);
}

static UCS_F_ALWAYS_INLINE int
uct_ub_async_event_hash_equal(uct_ub_async_event_t event1,
                              uct_ub_async_event_t event2)
{
    return (event1.event_type  == event2.event_type) &&
           (event1.resource_id == event2.resource_id);
}

KHASH_IMPL(uct_ub_async_event, uct_ub_async_event_t, uct_ub_async_event_val_t, 1,
           uct_ub_async_event_hash_func, uct_ub_async_event_hash_equal)

#ifdef ENABLE_STATS
static ucs_stats_class_t uct_ub_device_stats_class = {
    .name           = "",
    .num_counters   = UCT_UB_DEVICE_STAT_LAST,
    .counter_names = {
        [UCT_UB_DEVICE_STAT_ASYNC_EVENT] = "async_event"
    }
};
#endif

static char *uct_ub_dirname(char *path, int num_layers)
{
    while (num_layers-- > 0) {
        path = dirname(path);
        if (path == NULL) {
            return NULL;
        }
    }
    return path;
}

static void uct_ub_device_get_locality(const char *dev_name,
                                       ucs_sys_cpuset_t *cpu_mask,
                                       int *numa_node, urma_transport_type_t mode)
{
    char buf[ucs_max(CPU_SETSIZE, 10)];
    char *dev_sysfs_numa_path = NULL;
    char *dev_sysfs_fmt_path = NULL;
    ucs_status_t status;
    char *p = NULL;
    ssize_t nread;
    uint32_t word;
    int base, k;
    long n;

    dev_sysfs_fmt_path = UCT_UB_DEVICE_SYSFS_FMT;
    dev_sysfs_numa_path = UCT_UB_DEVICE_SYSFS_NUMA;

    /* Read list of CPUs close to the device */
    CPU_ZERO(cpu_mask);

    nread = ucs_read_file(buf, sizeof(buf) - 1, 1, dev_sysfs_fmt_path,
                          dev_name, "local_cpus");

    if (nread >= 0) {
        buf[CPU_SETSIZE - 1] = '\0';
        base = 0;
        do {
            p = strrchr(buf, ',');
            if (p == NULL) {
                p = buf;
            } else if (*p == ',') {
                *(p++) = 0;
            }

            word = strtoul(p, 0, BASE_NUM_16);
            UCT_UB_DEV_SET_CPU_MASK(k, word, base, cpu_mask);
            base += BASE_SHIFT_32;
        } while ((base < CPU_SETSIZE) && (p != buf));
    } else {
        /* If affinity file is not present, treat all CPUs as local */
        for (k = 0; k < CPU_SETSIZE; ++k) {
            CPU_SET(k, cpu_mask);
        }
    }

    /* Read NUMA node number */
    status = ucs_read_file_number(&n, 1, dev_sysfs_numa_path,
                                  dev_name);
    *numa_node = (status == UCS_OK) ? n : -1;
}

static inline int uct_ub_device_spec_match(uct_ub_device_t *dev,
                                           const uct_ub_device_spec_t *spec)
{
    return (spec->pci_id.vendor == dev->pci_id.vendor) &&
           (spec->pci_id.device == dev->pci_id.device);
}

// Implement this function after waiting for urma to provide API
static ucs_status_t uct_ub_device_get_path_buffer(uct_ub_device_t *dev,
                                                  char *path_buffer, int path_max)
{
    char dev_sysfs_pci_path[path_max];
    char *resolved_path = NULL;

    ucs_snprintf_safe(dev_sysfs_pci_path, path_max - 1, UCT_UB_DEVICE_SYSFS_PFX, dev->dev_name);

    resolved_path = realpath(dev_sysfs_pci_path, path_buffer);
    if (resolved_path == NULL) {
        ucs_warn("Resolved_path == NULL");
        return UCS_ERR_IO_ERROR;
    }

    /* Make sure there is "/uburma/" substring in path_buffer */
    if (strstr(path_buffer, "/uburma/") == NULL) {
        ucs_warn("There is no /uburma substring in path_buffer");
        return UCS_ERR_IO_ERROR;
    }

    return UCS_OK;
}

// for ub device there is no pci bus id, the function will return UCS_SYS_DEVICE_ID_UNKNOWN;
static ucs_sys_device_t uct_ub_device_get_sys_dev(uct_ub_device_t *dev)
{
    ucs_sys_bus_id_t bus_id = {0};
    char path_buffer[PATH_MAX];
    ucs_sys_device_t sys_dev;
    char *pcie_bus = NULL;
    ucs_status_t status;
    int num_fields;

    /* realpath name is of form /sys/devices/.../0000:05:00.0/infiniband/mlx5_0
     * and bus_id is constructed from 0000:05:00.0 */
    status = uct_ub_device_get_path_buffer(dev, path_buffer, PATH_MAX);
    if (status != UCS_OK) {
        return UCS_SYS_DEVICE_ID_UNKNOWN;
    }

    pcie_bus = uct_ub_dirname(path_buffer, SKIP_LAYERS_2);
    if (pcie_bus == NULL) {
        return UCS_SYS_DEVICE_ID_UNKNOWN;
    }
    pcie_bus = basename(pcie_bus);
    if (pcie_bus == NULL) {
        return UCS_SYS_DEVICE_ID_UNKNOWN;
    }

    num_fields = sscanf(pcie_bus, "%hx:%hhx:%hhx.%hhx", &bus_id.domain,
                        &bus_id.bus, &bus_id.slot, &bus_id.function);
    if (num_fields != RET_OK) {
        return UCS_SYS_DEVICE_ID_UNKNOWN;
    }

    status = ucs_topo_find_device_by_bus_id(&bus_id, &sys_dev);
    if (status != UCS_OK) {
        return UCS_SYS_DEVICE_ID_UNKNOWN;
    }

    ucs_debug("%s bus id %hu:%hhu:%hhu.%hhu sys_dev %d",
              dev->dev_name, bus_id.domain, bus_id.bus, bus_id.slot,
              bus_id.function, sys_dev);
    return sys_dev;
}

// Implement this function after waiting for the md module to submit the code
const uct_ub_device_spec_t* uct_ub_device_spec(uct_ub_device_t *dev)
{
    uct_ub_md_t *md = ucs_container_of(dev, uct_ub_md_t, dev);
    uct_ub_device_spec_t *spec = NULL;

    /* search through devices specified in the configuration */
    for (spec = md->custom_devices.specs;
         spec < md->custom_devices.specs + md->custom_devices.count; ++spec) {
        if (uct_ub_device_spec_match(dev, spec)) {
            return spec;
        }
    }
    return spec; /* if no match is found, return the last entry, which contains
                    default settings for unknown devices */
}

static void uct_ub_async_event_report(urma_async_event_t event, char *dev_name)
{
    ucs_log_level_t level = UCS_LOG_LEVEL_ERROR;
    char event_info[UCT_UB_DEV_MAX_BUF_SIZE];

    switch (event.event_type) {
        case URMA_EVENT_JFC_ERR:
            ucs_snprintf_safe(event_info, sizeof(event_info), "JFC CHECK ERR");
            break;
        case URMA_EVENT_JFS_ERR:
            ucs_snprintf_safe(event_info, sizeof(event_info), "JFS CHECK ERR");
            break;
        case URMA_EVENT_JFR_ERR:
            ucs_snprintf_safe(event_info, sizeof(event_info), "JFR CHECK ERR");
            break;
        case URMA_EVENT_PORT_ACTIVE:
            ucs_snprintf_safe(event_info, sizeof(event_info), "URMA_EVENT_PORT_ACTIVE, port_id: %d",
                              event.element.port_id);
            level = UCS_LOG_LEVEL_INFO;
            break;
        case URMA_EVENT_PORT_DOWN:
            ucs_snprintf_safe(event_info, sizeof(event_info), "TRANSPORT PORT DOWN, port_id: %d",
                              event.element.port_id);
            break;
        case URMA_EVENT_DEV_FATAL:
            ucs_snprintf_safe(event_info, sizeof(event_info), "URMA_EVENT_DEV_FATAL");
            break;
        case URMA_EVENT_EID_CHANGE:
            ucs_snprintf_safe(event_info, sizeof(event_info), "URMA_EVENT_EID_CHANGE");
            level = UCS_LOG_LEVEL_WARN;
            break;
        default:
            ucs_snprintf_safe(event_info, sizeof(event_info), "UNKONW UB EVENT");
            break;
    }
    ucs_log(level, "UB Async event on %s: %s", dev_name, event_info);
    return;
}

static void uct_ub_async_event_handler(int fd, ucs_event_set_types_t events,
                                       void *arg)
{
    uct_ub_device_t *dev = (uct_ub_device_t *)arg;
    urma_async_event_t event = {0};

    urma_status_t ret = urma_get_async_event(dev->urma_ctx, &event);
    if (ret != URMA_SUCCESS) {
        if (errno != EAGAIN) {
            ucs_warn("Urma get async event failed: %m");
        }
        return;
    }
    uct_ub_async_event_report(event, dev->dev_name);
    UCS_STATS_UPDATE_COUNTER(dev->stats, UCT_UB_DEVICE_STAT_ASYNC_EVENT, +1);
    urma_ack_async_event(&event);
    return;
}

ucs_status_t uct_ub_device_query(urma_device_t *urma_dev, uct_ub_device_t *dev)
{
    ucs_status_t ret = UCS_OK;
    urma_status_t status;

    status = urma_query_device(urma_dev, &dev->dev_attr);

    if (status != URMA_SUCCESS) {
        ucs_error("Failed to urma_query_device, status = %x", status);
        ret = get_uct_ub_status(status);
        return ret;
    }
    dev->first_port = 1;
    dev->num_ports  = dev->dev_attr.port_cnt;
    if (dev->num_ports > UCT_UB_DEV_MAX_PORTS) {
        ucs_debug("%s has %d ports, but only up to %d are supported",
                  urma_dev->name, dev->num_ports,
                  UCT_UB_DEV_MAX_PORTS);
        dev->num_ports = UCT_UB_DEV_MAX_PORTS;
    }
    return ret;
}

static ucs_status_t uct_ub_set_event_handler(uct_ub_device_t *dev)
{
    ucs_status_t status = UCS_OK;

    status = ucs_async_set_event_handler(UCS_ASYNC_THREAD_LOCK_TYPE,
                                         dev->urma_ctx->async_fd,
                                         UCS_EVENT_SET_EVREAD,
                                         uct_ub_async_event_handler, dev,
                                         NULL);
    if (status != UCS_OK) {
        ucs_error("Set event handler fail.");
    }
    return status;
}

ucs_status_t uct_ub_device_init(uct_ub_device_t *dev, const char *md_name, int async_events
                                UCS_STATS_ARG(ucs_stats_node_t *stats_parent))
{
    int name_len = strlen(md_name);
    ucs_status_t status;

    if (dev->urma_ctx->dev->type != URMA_TRANSPORT_UB) {
        ucs_error("Invalid config mode: %d", dev->urma_ctx->dev->type);
        return UCS_ERR_INVALID_PARAM;
    }
    if (name_len > UCT_DEVICE_NAME_MAX - 1 || name_len == 0) {
        ucs_error("Invalid md_name: %d", name_len);
        return UCS_ERR_INVALID_PARAM;
    }
    ucs_strncpy_safe(dev->dev_name, md_name, name_len + 1);
    dev->async_events = async_events;

    uct_ub_device_get_locality(dev->dev_name, &dev->local_cpus,
                               &dev->numa_node, dev->urma_ctx->dev->type);

    status = UCS_STATS_NODE_ALLOC(&dev->stats, &uct_ub_device_stats_class,
                                  stats_parent, "device");
    if (status != UCS_OK) {
        goto err;
    }

    /* Register to ub async events */
    if (dev->async_events) {
        status = uct_ub_set_event_handler(dev);
        if (status != UCS_OK) {
            goto err_release_stats;
        }
    }

    /* init async event hash table */
    kh_init_inplace(uct_ub_async_event, &dev->async_events_hash);
    ucs_spinlock_init(&dev->async_event_lock, 0);

    /* init ep connect hash table */
    dev->ep_hash = (khash_t(uct_rm_urma_ep_conn_hash) *)calloc(1, sizeof(khash_t(uct_rm_urma_ep_conn_hash)));
    if (dev->ep_hash == NULL) {
        ucs_error("Calloc failed for ep hash");
        status = UCS_ERR_NO_MEMORY;
        goto ep_hash_destroy;
    }
    kh_init_inplace(uct_rm_urma_ep_conn_hash, dev->ep_hash);
    ucs_trace("N_bucketes:%u", dev->ep_hash->n_buckets);

    /* init link hash table */
    dev->link_hash = (khash_t(uct_rm_urma_link_hash) *)calloc(1, sizeof(khash_t(uct_rm_urma_link_hash)));
    if (dev->link_hash == NULL) {
        ucs_error("Calloc failed for link hash");
        status = UCS_ERR_NO_MEMORY;
        goto link_hash_destroy;
    }
    kh_init_inplace(uct_rm_urma_link_hash, dev->link_hash);

    return UCS_OK;

link_hash_destroy:
    kh_destroy_inplace(uct_rm_urma_ep_conn_hash, dev->ep_hash);
    free(dev->ep_hash);
    dev->ep_hash = NULL;
ep_hash_destroy:
    kh_destroy_inplace(uct_ub_async_event, &dev->async_events_hash);
    ucs_async_remove_handler(dev->urma_ctx->async_fd, 1);
err_release_stats:
    UCS_STATS_NODE_FREE(dev->stats);
err:
    return status;
}

void uct_ub_device_cleanup(uct_ub_device_t *dev)
{
    ucs_status_t ret = UCS_OK;

    ucs_debug("Destroying ub device %s", dev->dev_name);

    if (kh_size(&dev->async_events_hash) != 0) {
        ucs_warn("Async_events_hash not empty");
    }

    kh_destroy_inplace(uct_ub_async_event, &dev->async_events_hash);
    ucs_spinlock_destroy(&dev->async_event_lock);

    if (dev->ep_hash != NULL) {
        kh_destroy_inplace(uct_rm_urma_ep_conn_hash, dev->ep_hash);
        free(dev->ep_hash);
        dev->ep_hash = NULL;
    }

    if (dev->link_hash != NULL) {
        kh_destroy_inplace(uct_rm_urma_link_hash, dev->link_hash);
        free(dev->link_hash);
        dev->link_hash = NULL;
    }

    if (dev->async_events) {
        ret = ucs_async_remove_handler(dev->urma_ctx->async_fd, 1);
        if (ret != UCS_OK) {
            ucs_debug("Failed to ucs_async_remove_handler in uct_ub_device_cleanup");
        }
    }
    UCS_STATS_NODE_FREE(dev->stats);
}

// Implement this function after waiting for urma to provide API
ucs_status_t uct_ub_device_port_check(uct_ub_device_t *dev, uint8_t port_num,
                                      unsigned flags)
{
    uint8_t expected_state;

    if (port_num < dev->first_port || port_num >= dev->first_port + dev->num_ports) {
        return UCS_ERR_NO_DEVICE;
    }
    expected_state = UCT_UB_DEV_PORT_ACTIVE;
    /* check dev port state */
    if (dev->dev_attr.port_attr[port_num - dev->first_port].state != expected_state) {
        ucs_trace("%s:%d is not active (state: %d)", dev->dev_name,
                  port_num , dev->dev_attr.port_attr[port_num].state);
        return UCS_ERR_UNREACHABLE;
    }

    return UCS_OK;
}

ucs_status_t uct_ub_device_query_ports(uct_ub_device_t *dev, unsigned flags,
                                       uct_tl_device_resource_t **tl_devices_p,
                                       unsigned *num_tl_devices_p)
{
    uct_tl_device_resource_t *tl_devices = NULL;
    unsigned num_tl_devices;
    ucs_status_t status;
    uint8_t port_num;

    /* Allocate resources array
     * We may allocate more memory than really required, but it's not so bad. */
    tl_devices = (uct_tl_device_resource_t *)ucs_calloc(dev->num_ports, sizeof(*tl_devices), "ub device resource");
    if (tl_devices == NULL) {
        status = UCS_ERR_NO_MEMORY;
        goto err;
    }

    /* Second pass: fill port information */
    num_tl_devices = 0;
    for (port_num = dev->first_port; port_num < dev->first_port + dev->num_ports;
         ++port_num) {
        /* Check port capabilities */
        status = uct_ub_device_port_check(dev, port_num, flags);
        if (status != UCS_OK) {
           ucs_trace("%s:%d does not support flags 0x%x: %s",
                     dev->dev_name, port_num, flags,
                     ucs_status_string(status));
           continue;
        }

        /* Save device information */
        ucs_snprintf_zero(tl_devices[num_tl_devices].name,
                          sizeof(tl_devices[num_tl_devices].name),
                          "%s:%d", dev->dev_name, port_num);
        tl_devices[num_tl_devices].type       = UCT_DEVICE_TYPE_NET;
        tl_devices[num_tl_devices].sys_device = uct_ub_device_get_sys_dev(dev);
        ++num_tl_devices;
    }

    if (num_tl_devices == 0) {
        ucs_debug("No compatuble ub ports found for flags 0x%x", flags);
        status = UCS_ERR_NO_DEVICE;
        goto err_free;
    }

    *num_tl_devices_p = num_tl_devices;
    *tl_devices_p     = tl_devices;
    return UCS_OK;

err_free:
    ucs_free(tl_devices);
err:
    return status;
}

ucs_status_t uct_ub_device_find_port(uct_ub_device_t *dev, const char *resource_dev_name, uint8_t *p_port_num)
{
    const char *ubdev_name = NULL;
    size_t devname_len;
    uint8_t port_num;
    char *p = NULL;

    p = (char *)strrchr(resource_dev_name, ':');
    if (p == NULL) {
        goto err;
    }
    devname_len = p - resource_dev_name;

    ubdev_name = &dev->dev_name[0];
    if ((strlen(ubdev_name) != devname_len) ||
        strncmp(ubdev_name, resource_dev_name, devname_len)) {
        goto err;
    }

    port_num = atoi(p + 1);
    if ((port_num < dev->first_port) || (port_num >= dev->first_port + dev->num_ports)) {
        goto err;
    }

    *p_port_num = port_num;
    return UCS_OK;

err:
    ucs_error("%s: do not find port.", resource_dev_name);
    return UCS_ERR_NO_DEVICE;
}

size_t uct_ub_mtu_value(urma_mtu_t mtu)
{
    switch (mtu) {
        case URMA_MTU_256:
            return 256;
        case URMA_MTU_512:
            return 512;
        case URMA_MTU_1024:
            return 1024;
        case URMA_MTU_2048:
            return 2048;
        case URMA_MTU_4096:
            return 4096;
        case URMA_MTU_8192:
            return 8192;
        default:
            ucs_error("Invalid MTU value (%d)", mtu);
            return 0;
    }
}

static int uct_ub_device_is_addr_ipv4_mcast(const struct in6_addr *raw,
                                            const uint32_t addr_last_bits)
{
    return (raw->s6_addr32[0] == htonl(UCT_UB_S6_ADDR0)) &&
           !(raw->s6_addr32[1] | addr_last_bits);
}

sa_family_t uct_ub_device_get_addr_family(urma_eid_t *eid)
{
    const struct in6_addr *raw    = (struct in6_addr *)eid->raw;
    const uint32_t addr_last_bits = raw->s6_addr32[2] ^ htonl(UCT_UB_S6_ADDR2);

    if (!((raw->s6_addr32[0] | raw->s6_addr32[1]) | addr_last_bits) ||
        uct_ub_device_is_addr_ipv4_mcast(raw, addr_last_bits)) {
        return AF_INET;
    } else {
        return AF_INET6;
    }
}

ucs_status_t uct_ub_device_get_ndev_name(uct_ub_device_t *dev, char *ndev_name, size_t max)
{
    ssize_t nread;

    nread = ucs_read_file_str(ndev_name, max, 1, UCT_UB_DEVICE_SYSFS_NDEV, dev->urma_ctx->dev->name,
                              dev->urma_ctx->dev->name);
    if (nread < 0) {
        ucs_diag("Failed to read ndev, dev_name: %s.", dev->urma_ctx->dev->name);
        return UCS_ERR_NO_DEVICE;
    }

    ucs_strtrim(ndev_name);

    return UCS_OK;
}
