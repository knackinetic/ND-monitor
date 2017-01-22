#include "common.h"

struct netdev {
    char *name;
    uint32_t hash;
    size_t len;

    // flags
    int configured;
    int enabled;

    int do_bandwidth;
    int do_packets;
    int do_errors;
    int do_drops;
    int do_fifo;
    int do_compressed;
    int do_events;

    // data collected
    unsigned long long rbytes;
    unsigned long long rpackets;
    unsigned long long rerrors;
    unsigned long long rdrops;
    unsigned long long rfifo;
    unsigned long long rframe;
    unsigned long long rcompressed;
    unsigned long long rmulticast;

    unsigned long long tbytes;
    unsigned long long tpackets;
    unsigned long long terrors;
    unsigned long long tdrops;
    unsigned long long tfifo;
    unsigned long long tcollisions;
    unsigned long long tcarrier;
    unsigned long long tcompressed;

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

static struct netdev *netdev_root = NULL;

static struct netdev *get_netdev(const char *name) {
    static struct netdev *last = NULL;
    struct netdev *d;

    uint32_t hash = simple_hash(name);

    // search it, from the last position to the end
    for(d = last ; d ; d = d->next) {
        if(unlikely(hash == d->hash && !strcmp(name, d->name))) {
            last = d->next;
            return d;
        }
    }

    // search it from the beginning to the last position we used
    for(d = netdev_root ; d != last ; d = d->next) {
        if(unlikely(hash == d->hash && !strcmp(name, d->name))) {
            last = d->next;
            return d;
        }
    }

    // create a new one
    d = callocz(1, sizeof(struct netdev));
    d->name = strdupz(name);
    d->hash = simple_hash(d->name);
    d->len = strlen(d->name);

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
        enable_new_interfaces = config_get_boolean_ondemand("plugin:proc:/proc/net/dev", "enable new interfaces detected at runtime", CONFIG_ONDEMAND_ONDEMAND);

        do_bandwidth    = config_get_boolean_ondemand("plugin:proc:/proc/net/dev", "bandwidth for all interfaces", CONFIG_ONDEMAND_ONDEMAND);
        do_packets      = config_get_boolean_ondemand("plugin:proc:/proc/net/dev", "packets for all interfaces", CONFIG_ONDEMAND_ONDEMAND);
        do_errors       = config_get_boolean_ondemand("plugin:proc:/proc/net/dev", "errors for all interfaces", CONFIG_ONDEMAND_ONDEMAND);
        do_drops        = config_get_boolean_ondemand("plugin:proc:/proc/net/dev", "drops for all interfaces", CONFIG_ONDEMAND_ONDEMAND);
        do_fifo         = config_get_boolean_ondemand("plugin:proc:/proc/net/dev", "fifo for all interfaces", CONFIG_ONDEMAND_ONDEMAND);
        do_compressed   = config_get_boolean_ondemand("plugin:proc:/proc/net/dev", "compressed packets for all interfaces", CONFIG_ONDEMAND_ONDEMAND);
        do_events       = config_get_boolean_ondemand("plugin:proc:/proc/net/dev", "frames, collisions, carrier counters for all interfaces", CONFIG_ONDEMAND_ONDEMAND);

        disabled_list = simple_pattern_create(
                config_get("plugin:proc:/proc/net/dev", "disable by default interfaces matching", "lo fireqos* *-ifb")
                , SIMPLE_PATTERN_EXACT);
    }

    if(unlikely(!ff)) {
        char filename[FILENAME_MAX + 1];
        snprintfz(filename, FILENAME_MAX, "%s%s", global_host_prefix, "/proc/net/dev");
        ff = procfile_open(config_get("plugin:proc:/proc/net/dev", "filename to monitor", filename), " \t,:|", PROCFILE_FLAG_DEFAULT);
        if(unlikely(!ff)) return 1;
    }

    ff = procfile_readall(ff);
    if(unlikely(!ff)) return 0; // we return 0, so that we will retry to open it next time

    size_t lines = procfile_lines(ff), l;
    for(l = 2; l < lines ;l++) {
        // require 17 words on each line
        if(unlikely(procfile_linewords(ff, l) < 17)) continue;

        struct netdev *d = get_netdev(procfile_lineword(ff, l, 0));

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

            if(d->enabled == CONFIG_ONDEMAND_NO)
                continue;

            d->do_bandwidth = config_get_boolean_ondemand(var_name, "bandwidth", do_bandwidth);
            d->do_packets = config_get_boolean_ondemand(var_name, "packets", do_packets);
            d->do_errors = config_get_boolean_ondemand(var_name, "errors", do_errors);
            d->do_drops = config_get_boolean_ondemand(var_name, "drops", do_drops);
            d->do_fifo = config_get_boolean_ondemand(var_name, "fifo", do_fifo);
            d->do_compressed = config_get_boolean_ondemand(var_name, "compressed", do_compressed);
            d->do_events = config_get_boolean_ondemand(var_name, "events", do_events);
        }

        if(unlikely(!d->enabled))
            continue;

        d->rbytes      = str2ull(procfile_lineword(ff, l, 1));
        d->rpackets    = str2ull(procfile_lineword(ff, l, 2));
        d->rerrors     = str2ull(procfile_lineword(ff, l, 3));
        d->rdrops      = str2ull(procfile_lineword(ff, l, 4));
        d->rfifo       = str2ull(procfile_lineword(ff, l, 5));
        d->rframe      = str2ull(procfile_lineword(ff, l, 6));
        d->rcompressed = str2ull(procfile_lineword(ff, l, 7));
        d->rmulticast  = str2ull(procfile_lineword(ff, l, 8));

        d->tbytes      = str2ull(procfile_lineword(ff, l, 9));
        d->tpackets    = str2ull(procfile_lineword(ff, l, 10));
        d->terrors     = str2ull(procfile_lineword(ff, l, 11));
        d->tdrops      = str2ull(procfile_lineword(ff, l, 12));
        d->tfifo       = str2ull(procfile_lineword(ff, l, 13));
        d->tcollisions = str2ull(procfile_lineword(ff, l, 14));
        d->tcarrier    = str2ull(procfile_lineword(ff, l, 15));
        d->tcompressed = str2ull(procfile_lineword(ff, l, 16));

        // --------------------------------------------------------------------

        if(unlikely((d->do_bandwidth == CONFIG_ONDEMAND_ONDEMAND && (d->rbytes || d->tbytes))))
            d->do_bandwidth = CONFIG_ONDEMAND_YES;

        if(d->do_bandwidth == CONFIG_ONDEMAND_YES) {
            if(unlikely(!d->st_bandwidth)) {
                d->st_bandwidth = rrdset_find_bytype("net", d->name);

                if(!d->st_bandwidth)
                    d->st_bandwidth = rrdset_create("net", d->name, NULL, d->name, "net.net", "Bandwidth", "kilobits/s", 7000, update_every, RRDSET_TYPE_AREA);

                d->rd_rbytes = rrddim_add(d->st_bandwidth, "received", NULL, 8, 1024, RRDDIM_INCREMENTAL);
                d->rd_tbytes = rrddim_add(d->st_bandwidth, "sent", NULL, -8, 1024, RRDDIM_INCREMENTAL);
            }
            else rrdset_next(d->st_bandwidth);

            rrddim_set_by_pointer(d->st_bandwidth, d->rd_rbytes, d->rbytes);
            rrddim_set_by_pointer(d->st_bandwidth, d->rd_tbytes, d->tbytes);
            rrdset_done(d->st_bandwidth);
        }

        // --------------------------------------------------------------------

        if(unlikely((d->do_packets == CONFIG_ONDEMAND_ONDEMAND && (d->rpackets || d->tpackets || d->rmulticast))))
            d->do_packets = CONFIG_ONDEMAND_YES;

        if(d->do_packets == CONFIG_ONDEMAND_YES) {
            if(unlikely(!d->st_packets)) {
                d->st_packets = rrdset_find_bytype("net_packets", d->name);

                if(!d->st_packets)
                    d->st_packets = rrdset_create("net_packets", d->name, NULL, d->name, "net.packets", "Packets", "packets/s", 7001, update_every, RRDSET_TYPE_LINE);

                d->st_packets->isdetail = 1;

                d->rd_rpackets = rrddim_add(d->st_packets, "received", NULL, 1, 1, RRDDIM_INCREMENTAL);
                d->rd_tpackets = rrddim_add(d->st_packets, "sent", NULL, -1, 1, RRDDIM_INCREMENTAL);
                d->rd_rmulticast = rrddim_add(d->st_packets, "multicast", NULL, 1, 1, RRDDIM_INCREMENTAL);
            }
            else rrdset_next(d->st_packets);

            rrddim_set_by_pointer(d->st_packets, d->rd_rpackets, d->rpackets);
            rrddim_set_by_pointer(d->st_packets, d->rd_tpackets, d->tpackets);
            rrddim_set_by_pointer(d->st_packets, d->rd_rmulticast, d->rmulticast);
            rrdset_done(d->st_packets);
        }

        // --------------------------------------------------------------------

        if(unlikely((d->do_errors == CONFIG_ONDEMAND_ONDEMAND && (d->rerrors || d->terrors))))
            d->do_errors = CONFIG_ONDEMAND_YES;

        if(d->do_errors == CONFIG_ONDEMAND_YES) {
            if(unlikely(!d->st_errors)) {
                d->st_errors = rrdset_find_bytype("net_errors", d->name);

                if(!d->st_errors)
                    d->st_errors = rrdset_create("net_errors", d->name, NULL, d->name, "net.errors", "Interface Errors", "errors/s", 7002, update_every, RRDSET_TYPE_LINE);

                d->st_errors->isdetail = 1;

                d->rd_rerrors = rrddim_add(d->st_errors, "inbound", NULL, 1, 1, RRDDIM_INCREMENTAL);
                d->rd_terrors = rrddim_add(d->st_errors, "outbound", NULL, -1, 1, RRDDIM_INCREMENTAL);
            }
            else rrdset_next(d->st_errors);

            rrddim_set_by_pointer(d->st_errors, d->rd_rerrors, d->rerrors);
            rrddim_set_by_pointer(d->st_errors, d->rd_terrors, d->terrors);
            rrdset_done(d->st_errors);
        }

        // --------------------------------------------------------------------

        if(unlikely((d->do_drops == CONFIG_ONDEMAND_ONDEMAND && (d->rdrops || d->tdrops))))
            d->do_drops = CONFIG_ONDEMAND_YES;

        if(d->do_drops == CONFIG_ONDEMAND_YES) {
            if(unlikely(!d->st_drops)) {
                d->st_drops = rrdset_find_bytype("net_drops", d->name);

                if(!d->st_drops)
                    d->st_drops = rrdset_create("net_drops", d->name, NULL, d->name, "net.drops", "Interface Drops", "drops/s", 7003, update_every, RRDSET_TYPE_LINE);

                d->st_drops->isdetail = 1;

                d->rd_rdrops = rrddim_add(d->st_drops, "inbound", NULL, 1, 1, RRDDIM_INCREMENTAL);
                d->rd_tdrops = rrddim_add(d->st_drops, "outbound", NULL, -1, 1, RRDDIM_INCREMENTAL);
            }
            else rrdset_next(d->st_drops);

            rrddim_set_by_pointer(d->st_drops, d->rd_rdrops, d->rdrops);
            rrddim_set_by_pointer(d->st_drops, d->rd_tdrops, d->tdrops);
            rrdset_done(d->st_drops);
        }

        // --------------------------------------------------------------------

        if(unlikely((d->do_fifo == CONFIG_ONDEMAND_ONDEMAND && (d->rfifo || d->tfifo))))
            d->do_fifo = CONFIG_ONDEMAND_YES;

        if(d->do_fifo == CONFIG_ONDEMAND_YES) {
            if(unlikely(!d->st_fifo)) {
                d->st_fifo = rrdset_find_bytype("net_fifo", d->name);

                if(!d->st_fifo)
                    d->st_fifo = rrdset_create("net_fifo", d->name, NULL, d->name, "net.fifo", "Interface FIFO Buffer Errors", "errors", 7004, update_every, RRDSET_TYPE_LINE);

                d->st_fifo->isdetail = 1;

                d->rd_rfifo = rrddim_add(d->st_fifo, "receive", NULL, 1, 1, RRDDIM_INCREMENTAL);
                d->rd_tfifo = rrddim_add(d->st_fifo, "transmit", NULL, -1, 1, RRDDIM_INCREMENTAL);
            }
            else rrdset_next(d->st_fifo);

            rrddim_set_by_pointer(d->st_fifo, d->rd_rfifo, d->rfifo);
            rrddim_set_by_pointer(d->st_fifo, d->rd_tfifo, d->tfifo);
            rrdset_done(d->st_fifo);
        }

        // --------------------------------------------------------------------

        if(unlikely((d->do_compressed == CONFIG_ONDEMAND_ONDEMAND && (d->rcompressed || d->tcompressed))))
            d->do_compressed = CONFIG_ONDEMAND_YES;

        if(d->do_compressed == CONFIG_ONDEMAND_YES) {
            if(unlikely(!d->st_compressed)) {
                d->st_compressed = rrdset_find_bytype("net_compressed", d->name);
                if(!d->st_compressed)
                    d->st_compressed = rrdset_create("net_compressed", d->name, NULL, d->name, "net.compressed", "Compressed Packets", "packets/s", 7005, update_every, RRDSET_TYPE_LINE);

                d->st_compressed->isdetail = 1;

                d->rd_rcompressed = rrddim_add(d->st_compressed, "received", NULL, 1, 1, RRDDIM_INCREMENTAL);
                d->rd_tcompressed = rrddim_add(d->st_compressed, "sent", NULL, -1, 1, RRDDIM_INCREMENTAL);
            }
            else rrdset_next(d->st_compressed);

            rrddim_set_by_pointer(d->st_compressed, d->rd_rcompressed, d->rcompressed);
            rrddim_set_by_pointer(d->st_compressed, d->rd_tcompressed, d->tcompressed);
            rrdset_done(d->st_compressed);
        }

        // --------------------------------------------------------------------

        if(unlikely((d->do_events == CONFIG_ONDEMAND_ONDEMAND && (d->rframe || d->tcollisions || d->tcarrier))))
            d->do_events = CONFIG_ONDEMAND_YES;

        if(d->do_events == CONFIG_ONDEMAND_YES) {
            if(unlikely(!d->st_events)) {
                d->st_events = rrdset_find_bytype("net_events", d->name);
                if(!d->st_events)
                    d->st_events = rrdset_create("net_events", d->name, NULL, d->name, "net.events", "Network Interface Events", "events/s", 7006, update_every, RRDSET_TYPE_LINE);

                d->st_events->isdetail = 1;

                d->rd_rframe      = rrddim_add(d->st_events, "frames", NULL, 1, 1, RRDDIM_INCREMENTAL);
                d->rd_tcollisions = rrddim_add(d->st_events, "collisions", NULL, -1, 1, RRDDIM_INCREMENTAL);
                d->rd_tcarrier    = rrddim_add(d->st_events, "carrier", NULL, -1, 1, RRDDIM_INCREMENTAL);
            }
            else rrdset_next(d->st_events);

            rrddim_set_by_pointer(d->st_events, d->rd_rframe,      d->rframe);
            rrddim_set_by_pointer(d->st_events, d->rd_tcollisions, d->tcollisions);
            rrddim_set_by_pointer(d->st_events, d->rd_tcarrier,    d->tcarrier);
            rrdset_done(d->st_events);
        }
    }

    return 0;
}
