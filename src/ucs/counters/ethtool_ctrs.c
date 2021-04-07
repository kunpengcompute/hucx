#include "ethtool_ctrs.h"
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <linux/sockios.h>
#include <linux/if.h>
#include <stddef.h>
#include <errno.h>
#include <stdlib.h>
#include <ucs/debug/log.h>
#include <ucs/sys/sock.h>
#include <string.h>

int send_ioctl(cmd_context_t *ctx, void *cmd) {
    ctx->ifr.ifr_data = cmd;
    return ioctl(ctx->fd, SIOCETHTOOL, &ctx->ifr);
}

static struct ethtool_gstrings *
get_stringset(cmd_context_t *ctx, enum ethtool_stringset set_id,
              ptrdiff_t drvinfo_offset, int null_terminate) {
    struct {
            struct ethtool_sset_info hdr;
            uint32_t buf[1];
    } sset_info;
    struct ethtool_drvinfo drvinfo;
    uint32_t len, i;
    struct ethtool_gstrings *strings;

    sset_info.hdr.cmd = ETHTOOL_GSSET_INFO;
    sset_info.hdr.reserved = 0;
    sset_info.hdr.sset_mask = 1ULL << set_id;
    if (send_ioctl(ctx, &sset_info) == 0) {
        const uint32_t *sset_lengths = sset_info.hdr.data;
        len = sset_info.hdr.sset_mask ? sset_lengths[0] : 0;
    } else if (errno == EOPNOTSUPP && drvinfo_offset != 0) {
        /* Fallback for old kernel versions */
        drvinfo.cmd = ETHTOOL_GDRVINFO;
        if (send_ioctl(ctx, &drvinfo))
                return NULL;
        len = *(uint32_t *)((char *)&drvinfo + drvinfo_offset);
    } else {
        return NULL;
    }

    strings = calloc(1, sizeof(*strings) + len * ETH_GSTRING_LEN);
    if (!strings)
        return NULL;

    strings->cmd = ETHTOOL_GSTRINGS;
    strings->string_set = set_id;
    strings->len = len;
    if (len != 0 && send_ioctl(ctx, strings)) {
        free(strings);
        return NULL;
    }

    if (null_terminate)
        for (i = 0; i < len; i++)
            strings->data[(i + 1) * ETH_GSTRING_LEN - 1] = 0;

    return strings;
}

int ioctl_init(cmd_context_t *ctx, char *ndev_name)
{
    struct ifreq ifr;
    ucs_status_t status = ucs_netif_ioctl(ndev_name, SIOCGIFADDR, &ifr);
    ucs_debug("device ip loaded %s.", inet_ntoa(((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr));
    if (status != UCS_OK) {
        return UCS_ERR_INVALID_PARAM;
    }

    /* Setup our control structures. */
    memset(&ctx->ifr, 0, sizeof(ctx->ifr));
    memcpy(&ctx->ifr, &ifr, sizeof(ctx->ifr));

    /* Open control socket. */
    ctx->fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (ctx->fd < 0) {
        ucs_debug("ioctl_init: socket failed.");
    }
    return 0;
}

ucs_status_t stats_alloc_handle(char *ndev_name, ethtool_stats_handle_t* stats_handle) {
    unsigned int sz_stats;
    memset(&stats_handle->super.ctx, 0 , sizeof(cmd_context_t));

    int ret = ioctl_init(&stats_handle->super.ctx, ndev_name);
    if (ret) {
        ucs_debug("read_mlx5_counters: ioctl_init failed!");
        return UCS_ERR_IO_ERROR;
    }

    stats_handle->super.strings = get_stringset(&stats_handle->super.ctx, ETH_SS_STATS,
                            offsetof(struct ethtool_drvinfo, n_stats),
                            0);
    if (!stats_handle->super.strings) {
        ucs_debug("read_mlx5_counters: get_stringset failed!");
        return UCS_ERR_IO_ERROR;
    }

    stats_handle->super.n_stats = stats_handle->super.strings->len;
    if (stats_handle->super.n_stats < 1) {
        ucs_debug("read_mlx5_counters: no stats available!");
        free(stats_handle->super.strings);
        return UCS_ERR_IO_ERROR;
    }

    sz_stats = stats_handle->super.n_stats * sizeof(uint64_t);

    stats_handle->super.stats = calloc(1, sz_stats + sizeof(struct ethtool_stats));
    if (!stats_handle->super.stats) {
        ucs_debug(" read_mlx5_counters: no memory available!");
        free(stats_handle->super.strings);
        return UCS_ERR_NO_MEMORY;
    }
    return UCS_OK;
}


ucs_status_t stats_query_device(ethtool_stats_handle_t* stats_handle, void *filter) {
    stats_handle->super.stats->cmd = ETHTOOL_GSTATS;
    stats_handle->super.stats->n_stats = stats_handle->super.n_stats;
    int err = send_ioctl(&stats_handle->super.ctx, stats_handle->super.stats);
    if (err < 0) {
        ucs_debug(" read_mlx5_counters: Cannot get stats information!");
        return UCS_ERR_IO_ERROR;
    }
    return UCS_OK;
}

ucs_status_t stats_read_counters(ethtool_stats_handle_t* stats_handle) {
    int i;
    ucs_debug("MLX5 statistics:");
    for (i = 0; i < stats_handle->super.n_stats; i++) {
        ucs_debug("     %.*s: %llu\n",
                    ETH_GSTRING_LEN,
                    &stats_handle->super.strings->data[i * ETH_GSTRING_LEN],
                    stats_handle->super.stats->data[i]);
    }
    return UCS_OK;
}

ucs_status_t stats_release_handle(ethtool_stats_handle_t* stats_handle) {
    if (&stats_handle->super) {
        if (stats_handle->super.strings)
            free(stats_handle->super.strings);
        if (stats_handle->super.stats)
            free(stats_handle->super.stats);
    }
    return UCS_OK;
}
