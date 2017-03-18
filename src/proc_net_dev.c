#include "common.h"

struct netdev {
    char *name;
    uint32_t hash;
    size_t len;

    // flags
    int configured;
    int enabled;
    int updated;

    int do_bandwidth;
    int do_packets;
    int do_errors;
    int do_drops;
    int do_fifo;
    int do_compressed;
    int do_events;

    // data collected
    kernel_uint_t rbytes;
    kernel_uint_t rpackets;
    kernel_uint_t rerrors;
    kernel_uint_t rdrops;
    kernel_uint_t rfifo;
    kernel_uint_t rframe;
    kernel_uint_t rcompressed;
    kernel_uint_t rmulticast;

    kernel_uint_t tbytes;
    kernel_uint_t tpackets;
    kernel_uint_t terrors;
    kernel_uint_t tdrops;
    kernel_uint_t tfifo;
    kernel_uint_t tcollisions;
    kernel_uint_t tcarrier;
    kernel_uint_t tcompressed;

    // charts
    RRDSET *st_bandwidth;
    RRDSET *st_packets;
    RRDSET *st_errors;
    RRDSET *st_drops;
    RRDSET *st_fifo;
    RRDSET *st_compressed;
    RRDSET *st_events;

    // dimensions
    RRDDIM *rd_rbytes;
    RRDDIM *rd_rpackets;
    RRDDIM *rd_rerrors;
    RRDDIM *rd_rdrops;
    RRDDIM *rd_rfifo;
    RRDDIM *rd_rframe;
    RRDDIM *rd_rcompressed;
    RRDDIM *rd_rmulticast;

    RRDDIM *rd_tbytes;
    RRDDIM *rd_tpackets;
    RRDDIM *rd_terrors;
    RRDDIM *rd_tdrops;
    RRDDIM *rd_tfifo;
    RRDDIM *rd_tcollisions;
    RRDDIM *rd_tcarrier;
    RRDDIM *rd_tcompressed;

    struct netdev *next;
};

static struct netdev *netdev_root = NULL, *netdev_last_used = NULL;

static size_t netdev_added = 0, netdev_found = 0;

static void netdev_free(struct netdev *d) {
    if(d->st_bandwidth)  rrdset_flag_set(d->st_bandwidth,  RRDSET_FLAG_OBSOLETE);
    if(d->st_packets)    rrdset_flag_set(d->st_packets,    RRDSET_FLAG_OBSOLETE);
    if(d->st_errors)     rrdset_flag_set(d->st_errors,     RRDSET_FLAG_OBSOLETE);
    if(d->st_drops)      rrdset_flag_set(d->st_drops,      RRDSET_FLAG_OBSOLETE);
    if(d->st_fifo)       rrdset_flag_set(d->st_fifo,       RRDSET_FLAG_OBSOLETE);
    if(d->st_compressed) rrdset_flag_set(d->st_compressed, RRDSET_FLAG_OBSOLETE);
    if(d->st_events)     rrdset_flag_set(d->st_events,     RRDSET_FLAG_OBSOLETE);

    netdev_added--;
    freez(d->name);
    freez(d);
}

static void netdev_cleanup() {
    if(likely(netdev_found == netdev_added)) return;

    netdev_added = 0;
    struct netdev *d = netdev_root, *last = NULL;
    while(d) {
        if(unlikely(!d->updated)) {
            // info("Removing network device '%s', linked after '%s'", d->name, last?last->name:"ROOT");

            if(netdev_last_used == d)
                netdev_last_used = last;

            struct netdev *t = d;

            if(d == netdev_root || !last)
                netdev_root = d = d->next;

            else
                last->next = d = d->next;

            t->next = NULL;
            netdev_free(t);
        }
        else {
            netdev_added++;
            last = d;
            d->updated = 0;
            d = d->next;
        }
    }
}

static struct netdev *get_netdev(const char *name) {
    struct netdev *d;

    uint32_t hash = simple_hash(name);

    // search it, from the last position to the end
    for(d = netdev_last_used ; d ; d = d->next) {
        if(unlikely(hash == d->hash && !strcmp(name, d->name))) {
            netdev_last_used = d->next;
            return d;
        }
    }

    // search it from the beginning to the last position we used
    for(d = netdev_root ; d != netdev_last_used ; d = d->next) {
        if(unlikely(hash == d->hash && !strcmp(name, d->name))) {
            netdev_last_used = d->next;
            return d;
        }
    }

    // create a new one
    d = callocz(1, sizeof(struct netdev));
    d->name = strdupz(name);
    d->hash = simple_hash(d->name);
    d->len = strlen(d->name);
    netdev_added++;

    // link it to the end
    if(netdev_root) {
        struct netdev *e;
        for(e = netdev_root; e->next ; e = e->next) ;
        e->next = d;
    }
    else
        netdev_root = d;

    return d;
}

int do_proc_net_dev(int update_every, usec_t dt) {
    (void)dt;
    static SIMPLE_PATTERN *disabled_list = NULL;
    static procfile *ff = NULL;
    static int enable_new_interfaces = -1;
    static int do_bandwidth = -1, do_packets = -1, do_errors = -1, do_drops = -1, do_fifo = -1, do_compressed = -1, do_events = -1;

    if(unlikely(enable_new_interfaces == -1)) {
        enable_new_interfaces = config_get_boolean_ondemand("plugin:proc:/proc/net/dev", "enable new interfaces detected at runtime", CONFIG_BOOLEAN_AUTO);

        do_bandwidth    = config_get_boolean_ondemand("plugin:proc:/proc/net/dev", "bandwidth for all interfaces", CONFIG_BOOLEAN_AUTO);
        do_packets      = config_get_boolean_ondemand("plugin:proc:/proc/net/dev", "packets for all interfaces", CONFIG_BOOLEAN_AUTO);
        do_errors       = config_get_boolean_ondemand("plugin:proc:/proc/net/dev", "errors for all interfaces", CONFIG_BOOLEAN_AUTO);
        do_drops        = config_get_boolean_ondemand("plugin:proc:/proc/net/dev", "drops for all interfaces", CONFIG_BOOLEAN_AUTO);
        do_fifo         = config_get_boolean_ondemand("plugin:proc:/proc/net/dev", "fifo for all interfaces", CONFIG_BOOLEAN_AUTO);
        do_compressed   = config_get_boolean_ondemand("plugin:proc:/proc/net/dev", "compressed packets for all interfaces", CONFIG_BOOLEAN_AUTO);
        do_events       = config_get_boolean_ondemand("plugin:proc:/proc/net/dev", "frames, collisions, carrier counters for all interfaces", CONFIG_BOOLEAN_AUTO);

        disabled_list = simple_pattern_create(
                config_get("plugin:proc:/proc/net/dev", "disable by default interfaces matching", "lo fireqos* *-ifb")
                , SIMPLE_PATTERN_EXACT);
    }

    if(unlikely(!ff)) {
        char filename[FILENAME_MAX + 1];
        snprintfz(filename, FILENAME_MAX, "%s%s", netdata_configured_host_prefix, "/proc/net/dev");
        ff = procfile_open(config_get("plugin:proc:/proc/net/dev", "filename to monitor", filename), " \t,:|", PROCFILE_FLAG_DEFAULT);
        if(unlikely(!ff)) return 1;
    }

    ff = procfile_readall(ff);
    if(unlikely(!ff)) return 0; // we return 0, so that we will retry to open it next time

    netdev_found = 0;

    size_t lines = procfile_lines(ff), l;
    for(l = 2; l < lines ;l++) {
        // require 17 words on each line
        if(unlikely(procfile_linewords(ff, l) < 17)) continue;

        struct netdev *d = get_netdev(procfile_lineword(ff, l, 0));
        d->updated = 1;
        netdev_found++;

        if(unlikely(!d->configured)) {
            // this is the first time we see this interface

            // remember we configured it
            d->configured = 1;

            d->enabled = enable_new_interfaces;

            if(d->enabled)
                d->enabled = !simple_pattern_matches(disabled_list, d->name);

            char var_name[512 + 1];
            snprintfz(var_name, 512, "plugin:proc:/proc/net/dev:%s", d->name);
            d->enabled = config_get_boolean_ondemand(var_name, "enabled", d->enabled);

            if(d->enabled == CONFIG_BOOLEAN_NO)
                continue;

            d->do_bandwidth  = config_get_boolean_ondemand(var_name, "bandwidth", do_bandwidth);
            d->do_packets    = config_get_boolean_ondemand(var_name, "packets", do_packets);
            d->do_errors     = config_get_boolean_ondemand(var_name, "errors", do_errors);
            d->do_drops      = config_get_boolean_ondemand(var_name, "drops", do_drops);
            d->do_fifo       = config_get_boolean_ondemand(var_name, "fifo", do_fifo);
            d->do_compressed = config_get_boolean_ondemand(var_name, "compressed", do_compressed);
            d->do_events     = config_get_boolean_ondemand(var_name, "events", do_events);
        }

        if(unlikely(!d->enabled))
            continue;

        if(likely(d->do_bandwidth != CONFIG_BOOLEAN_NO)) {
            d->rbytes      = str2kernel_uint_t(procfile_lineword(ff, l, 1));
            d->tbytes      = str2kernel_uint_t(procfile_lineword(ff, l, 9));
        }

        if(likely(d->do_packets != CONFIG_BOOLEAN_NO)) {
            d->rpackets    = str2kernel_uint_t(procfile_lineword(ff, l, 2));
            d->rmulticast  = str2kernel_uint_t(procfile_lineword(ff, l, 8));
            d->tpackets    = str2kernel_uint_t(procfile_lineword(ff, l, 10));
        }

        if(likely(d->do_errors != CONFIG_BOOLEAN_NO)) {
            d->rerrors     = str2kernel_uint_t(procfile_lineword(ff, l, 3));
            d->terrors     = str2kernel_uint_t(procfile_lineword(ff, l, 11));
        }

        if(likely(d->do_drops != CONFIG_BOOLEAN_NO)) {
            d->rdrops      = str2kernel_uint_t(procfile_lineword(ff, l, 4));
            d->tdrops      = str2kernel_uint_t(procfile_lineword(ff, l, 12));
        }

        if(likely(d->do_fifo != CONFIG_BOOLEAN_NO)) {
            d->rfifo       = str2kernel_uint_t(procfile_lineword(ff, l, 5));
            d->tfifo       = str2kernel_uint_t(procfile_lineword(ff, l, 13));
        }

        if(likely(d->do_compressed != CONFIG_BOOLEAN_NO)) {
            d->rcompressed = str2kernel_uint_t(procfile_lineword(ff, l, 7));
            d->tcompressed = str2kernel_uint_t(procfile_lineword(ff, l, 16));
        }

        if(likely(d->do_events != CONFIG_BOOLEAN_NO)) {
            d->rframe      = str2kernel_uint_t(procfile_lineword(ff, l, 6));
            d->tcollisions = str2kernel_uint_t(procfile_lineword(ff, l, 14));
            d->tcarrier    = str2kernel_uint_t(procfile_lineword(ff, l, 15));
        }

        // --------------------------------------------------------------------

        if(unlikely((d->do_bandwidth == CONFIG_BOOLEAN_AUTO && (d->rbytes || d->tbytes))))
            d->do_bandwidth = CONFIG_BOOLEAN_YES;

        if(d->do_bandwidth == CONFIG_BOOLEAN_YES) {
            if(unlikely(!d->st_bandwidth)) {

                d->st_bandwidth = rrdset_create_localhost(
                        "net"
                        , d->name
                        , NULL
                        , d->name
                        , "net.net"
                        , "Bandwidth"
                        , "kilobits/s"
                        , 7000
                        , update_every
                        , RRDSET_TYPE_AREA
                );

                d->rd_rbytes = rrddim_add(d->st_bandwidth, "received", NULL, 8, 1024, RRD_ALGORITHM_INCREMENTAL);
                d->rd_tbytes = rrddim_add(d->st_bandwidth, "sent", NULL, -8, 1024, RRD_ALGORITHM_INCREMENTAL);
            }
            else rrdset_next(d->st_bandwidth);

            rrddim_set_by_pointer(d->st_bandwidth, d->rd_rbytes, (collected_number)d->rbytes);
            rrddim_set_by_pointer(d->st_bandwidth, d->rd_tbytes, (collected_number)d->tbytes);
            rrdset_done(d->st_bandwidth);
        }

        // --------------------------------------------------------------------

        if(unlikely((d->do_packets == CONFIG_BOOLEAN_AUTO && (d->rpackets || d->tpackets || d->rmulticast))))
            d->do_packets = CONFIG_BOOLEAN_YES;

        if(d->do_packets == CONFIG_BOOLEAN_YES) {
            if(unlikely(!d->st_packets)) {

                d->st_packets = rrdset_create_localhost(
                        "net_packets"
                        , d->name
                        , NULL
                        , d->name
                        , "net.packets"
                        , "Packets"
                        , "packets/s"
                        , 7001
                        , update_every
                        , RRDSET_TYPE_LINE
                );

                rrdset_flag_set(d->st_packets, RRDSET_FLAG_DETAIL);

                d->rd_rpackets = rrddim_add(d->st_packets, "received", NULL, 1, 1, RRD_ALGORITHM_INCREMENTAL);
                d->rd_tpackets = rrddim_add(d->st_packets, "sent", NULL, -1, 1, RRD_ALGORITHM_INCREMENTAL);
                d->rd_rmulticast = rrddim_add(d->st_packets, "multicast", NULL, 1, 1, RRD_ALGORITHM_INCREMENTAL);
            }
            else rrdset_next(d->st_packets);

            rrddim_set_by_pointer(d->st_packets, d->rd_rpackets, (collected_number)d->rpackets);
            rrddim_set_by_pointer(d->st_packets, d->rd_tpackets, (collected_number)d->tpackets);
            rrddim_set_by_pointer(d->st_packets, d->rd_rmulticast, (collected_number)d->rmulticast);
            rrdset_done(d->st_packets);
        }

        // --------------------------------------------------------------------

        if(unlikely((d->do_errors == CONFIG_BOOLEAN_AUTO && (d->rerrors || d->terrors))))
            d->do_errors = CONFIG_BOOLEAN_YES;

        if(d->do_errors == CONFIG_BOOLEAN_YES) {
            if(unlikely(!d->st_errors)) {

                d->st_errors = rrdset_create_localhost(
                        "net_errors"
                        , d->name
                        , NULL
                        , d->name
                        , "net.errors"
                        , "Interface Errors"
                        , "errors/s"
                        , 7002
                        , update_every
                        , RRDSET_TYPE_LINE
                );

                rrdset_flag_set(d->st_errors, RRDSET_FLAG_DETAIL);

                d->rd_rerrors = rrddim_add(d->st_errors, "inbound", NULL, 1, 1, RRD_ALGORITHM_INCREMENTAL);
                d->rd_terrors = rrddim_add(d->st_errors, "outbound", NULL, -1, 1, RRD_ALGORITHM_INCREMENTAL);
            }
            else rrdset_next(d->st_errors);

            rrddim_set_by_pointer(d->st_errors, d->rd_rerrors, (collected_number)d->rerrors);
            rrddim_set_by_pointer(d->st_errors, d->rd_terrors, (collected_number)d->terrors);
            rrdset_done(d->st_errors);
        }

        // --------------------------------------------------------------------

        if(unlikely((d->do_drops == CONFIG_BOOLEAN_AUTO && (d->rdrops || d->tdrops))))
            d->do_drops = CONFIG_BOOLEAN_YES;

        if(d->do_drops == CONFIG_BOOLEAN_YES) {
            if(unlikely(!d->st_drops)) {

                d->st_drops = rrdset_create_localhost(
                        "net_drops"
                        , d->name
                        , NULL
                        , d->name
                        , "net.drops"
                        , "Interface Drops"
                        , "drops/s"
                        , 7003
                        , update_every
                        , RRDSET_TYPE_LINE
                );

                rrdset_flag_set(d->st_drops, RRDSET_FLAG_DETAIL);

                d->rd_rdrops = rrddim_add(d->st_drops, "inbound", NULL, 1, 1, RRD_ALGORITHM_INCREMENTAL);
                d->rd_tdrops = rrddim_add(d->st_drops, "outbound", NULL, -1, 1, RRD_ALGORITHM_INCREMENTAL);
            }
            else rrdset_next(d->st_drops);

            rrddim_set_by_pointer(d->st_drops, d->rd_rdrops, (collected_number)d->rdrops);
            rrddim_set_by_pointer(d->st_drops, d->rd_tdrops, (collected_number)d->tdrops);
            rrdset_done(d->st_drops);
        }

        // --------------------------------------------------------------------

        if(unlikely((d->do_fifo == CONFIG_BOOLEAN_AUTO && (d->rfifo || d->tfifo))))
            d->do_fifo = CONFIG_BOOLEAN_YES;

        if(d->do_fifo == CONFIG_BOOLEAN_YES) {
            if(unlikely(!d->st_fifo)) {

                d->st_fifo = rrdset_create_localhost(
                        "net_fifo"
                        , d->name
                        , NULL
                        , d->name
                        , "net.fifo"
                        , "Interface FIFO Buffer Errors"
                        , "errors"
                        , 7004
                        , update_every
                        , RRDSET_TYPE_LINE
                );

                rrdset_flag_set(d->st_fifo, RRDSET_FLAG_DETAIL);

                d->rd_rfifo = rrddim_add(d->st_fifo, "receive", NULL, 1, 1, RRD_ALGORITHM_INCREMENTAL);
                d->rd_tfifo = rrddim_add(d->st_fifo, "transmit", NULL, -1, 1, RRD_ALGORITHM_INCREMENTAL);
            }
            else rrdset_next(d->st_fifo);

            rrddim_set_by_pointer(d->st_fifo, d->rd_rfifo, (collected_number)d->rfifo);
            rrddim_set_by_pointer(d->st_fifo, d->rd_tfifo, (collected_number)d->tfifo);
            rrdset_done(d->st_fifo);
        }

        // --------------------------------------------------------------------

        if(unlikely((d->do_compressed == CONFIG_BOOLEAN_AUTO && (d->rcompressed || d->tcompressed))))
            d->do_compressed = CONFIG_BOOLEAN_YES;

        if(d->do_compressed == CONFIG_BOOLEAN_YES) {
            if(unlikely(!d->st_compressed)) {

                d->st_compressed = rrdset_create_localhost(
                        "net_compressed"
                        , d->name
                        , NULL
                        , d->name
                        , "net.compressed"
                        , "Compressed Packets"
                        , "packets/s"
                        , 7005
                        , update_every
                        , RRDSET_TYPE_LINE
                );

                rrdset_flag_set(d->st_compressed, RRDSET_FLAG_DETAIL);

                d->rd_rcompressed = rrddim_add(d->st_compressed, "received", NULL, 1, 1, RRD_ALGORITHM_INCREMENTAL);
                d->rd_tcompressed = rrddim_add(d->st_compressed, "sent", NULL, -1, 1, RRD_ALGORITHM_INCREMENTAL);
            }
            else rrdset_next(d->st_compressed);

            rrddim_set_by_pointer(d->st_compressed, d->rd_rcompressed, (collected_number)d->rcompressed);
            rrddim_set_by_pointer(d->st_compressed, d->rd_tcompressed, (collected_number)d->tcompressed);
            rrdset_done(d->st_compressed);
        }

        // --------------------------------------------------------------------

        if(unlikely((d->do_events == CONFIG_BOOLEAN_AUTO && (d->rframe || d->tcollisions || d->tcarrier))))
            d->do_events = CONFIG_BOOLEAN_YES;

        if(d->do_events == CONFIG_BOOLEAN_YES) {
            if(unlikely(!d->st_events)) {

                d->st_events = rrdset_create_localhost(
                        "net_events"
                        , d->name
                        , NULL
                        , d->name
                        , "net.events"
                        , "Network Interface Events"
                        , "events/s"
                        , 7006
                        , update_every
                        , RRDSET_TYPE_LINE
                );

                rrdset_flag_set(d->st_events, RRDSET_FLAG_DETAIL);

                d->rd_rframe      = rrddim_add(d->st_events, "frames", NULL, 1, 1, RRD_ALGORITHM_INCREMENTAL);
                d->rd_tcollisions = rrddim_add(d->st_events, "collisions", NULL, -1, 1, RRD_ALGORITHM_INCREMENTAL);
                d->rd_tcarrier    = rrddim_add(d->st_events, "carrier", NULL, -1, 1, RRD_ALGORITHM_INCREMENTAL);
            }
            else rrdset_next(d->st_events);

            rrddim_set_by_pointer(d->st_events, d->rd_rframe,      (collected_number)d->rframe);
            rrddim_set_by_pointer(d->st_events, d->rd_tcollisions, (collected_number)d->tcollisions);
            rrddim_set_by_pointer(d->st_events, d->rd_tcarrier,    (collected_number)d->tcarrier);
            rrdset_done(d->st_events);
        }
    }

    netdev_cleanup();

    return 0;
}
