#ifndef ETHTOOL_CTRS_H
#define ETHTOOL_CTRS_H

#include <net/if.h>
#include <linux/ethtool.h>
#include <ucs/debug/log.h>

typedef struct cmd_context {
    int fd;
    struct ifreq ifr;
} cmd_context_t;

/* structure that holds statistics of the device
    ctx     - open fd of the device
    n_stats - number of counters
    strings - counter names
    stats   - counter values */
typedef struct ucs_stats_handle {
    cmd_context_t ctx;
    unsigned n_stats;
    struct ethtool_gstrings *strings;
    struct ethtool_stats *stats;
} ucs_stats_handle;

typedef struct ethtool_stats_handle {
    ucs_stats_handle super;
} ethtool_stats_handle_t;

/* allocate memory for stats_handle of the device  */
ucs_status_t stats_alloc_handle(char *ndev_name, ethtool_stats_handle_t* stats_handle);

/* release the memory of stats_handle of the device  */
ucs_status_t stats_release_handle(ethtool_stats_handle_t* stats_handle);

/* query device counters and store them in stats_handle  */
ucs_status_t stats_query_device(ethtool_stats_handle_t* stats_handle, void *filter);

/* print the counters stored in stats_handle */
ucs_status_t stats_read_counters(ethtool_stats_handle_t* stats_handle);

#endif