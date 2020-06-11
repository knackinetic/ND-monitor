#include "aclk_stats.h"

netdata_mutex_t aclk_stats_mutex = NETDATA_MUTEX_INITIALIZER;

int aclk_stats_enabled;

struct aclk_metrics aclk_metrics = {
    .online = 0,
};

struct aclk_metrics_per_sample aclk_metrics_per_sample;

static void aclk_stats_collect(struct aclk_metrics_per_sample *per_sample, struct aclk_metrics *permanent)
{
    static RRDSET *st_aclkstats = NULL;
    static RRDDIM *rd_online_status = NULL;

    if (unlikely(!st_aclkstats)) {
        st_aclkstats = rrdset_create_localhost(
            "netdata", "aclk_status", NULL, "aclk_stats", NULL, "ACLK/Cloud connection status",
            "connected", "netdata", "stats", 200000, localhost->rrd_update_every, RRDSET_TYPE_LINE);

        rd_online_status = rrddim_add(st_aclkstats, "online", NULL, 1, 1, RRD_ALGORITHM_ABSOLUTE);
    } else
        rrdset_next(st_aclkstats);

    rrddim_set_by_pointer(st_aclkstats, rd_online_status, per_sample->offline_during_sample ? 0 : permanent->online);

    rrdset_done(st_aclkstats);
}

static void aclk_stats_query_thread(struct aclk_metrics_per_sample *per_sample)
{
    static RRDSET *st_query_thread = NULL;
    static RRDDIM *rd_queued = NULL;
    static RRDDIM *rd_dispatched = NULL;

    if (unlikely(!st_query_thread)) {
        st_query_thread = rrdset_create_localhost(
            "netdata", "aclk_query_per_second", NULL, "aclk_stats", NULL, "ACLK Queries per second", "queries/s",
            "netdata", "stats", 200001, localhost->rrd_update_every, RRDSET_TYPE_AREA);

        rd_queued = rrddim_add(st_query_thread, "added", NULL, 1, localhost->rrd_update_every, RRD_ALGORITHM_ABSOLUTE);
        rd_dispatched = rrddim_add(st_query_thread, "dispatched", NULL, 1, localhost->rrd_update_every, RRD_ALGORITHM_ABSOLUTE);
    } else
        rrdset_next(st_query_thread);

    rrddim_set_by_pointer(st_query_thread, rd_queued, per_sample->queries_queued);
    rrddim_set_by_pointer(st_query_thread, rd_dispatched, -per_sample->queries_dispatched);

    rrdset_done(st_query_thread);
}

#ifdef NETDATA_INTERNAL_CHECKS
static void aclk_stats_latency(struct aclk_metrics_per_sample *per_sample)
{
    static RRDSET *st = NULL;
    static RRDDIM *rd_avg = NULL;
    static RRDDIM *rd_max = NULL;

    if (unlikely(!st)) {
        st = rrdset_create_localhost(
            "netdata", "aclk_latency_mqtt", NULL, "aclk_stats", NULL, "ACLK Message Publish Latency", "ms",
            "netdata", "stats", 200002, localhost->rrd_update_every, RRDSET_TYPE_LINE);

        rd_avg = rrddim_add(st, "avg", NULL, 1, 1, RRD_ALGORITHM_ABSOLUTE);
        rd_max = rrddim_add(st, "max", NULL, 1, 1, RRD_ALGORITHM_ABSOLUTE);
    } else
        rrdset_next(st);
    if(per_sample->latency_count)
        rrddim_set_by_pointer(st, rd_avg, roundf((float)per_sample->latency_total / per_sample->latency_count));
    else
        rrddim_set_by_pointer(st, rd_avg, 0);

    rrddim_set_by_pointer(st, rd_max, per_sample->latency_max);

    rrdset_done(st);
}
#endif

static void aclk_stats_write_q(struct aclk_metrics_per_sample *per_sample)
{
    static RRDSET *st = NULL;
    static RRDDIM *rd_wq_add = NULL;
    static RRDDIM *rd_wq_consumed = NULL;

    if (unlikely(!st)) {
        st = rrdset_create_localhost(
            "netdata", "aclk_write_q", NULL, "aclk_stats", NULL, "Write Queue Mosq->Libwebsockets", "kB/s",
            "netdata", "stats", 200003, localhost->rrd_update_every, RRDSET_TYPE_AREA);

        rd_wq_add = rrddim_add(st, "added", NULL, 1, 1024 * localhost->rrd_update_every, RRD_ALGORITHM_ABSOLUTE);
        rd_wq_consumed = rrddim_add(st, "consumed", NULL, 1, -1024 * localhost->rrd_update_every, RRD_ALGORITHM_ABSOLUTE);
    } else
        rrdset_next(st);

    rrddim_set_by_pointer(st, rd_wq_add, per_sample->write_q_added);
    rrddim_set_by_pointer(st, rd_wq_consumed, per_sample->write_q_consumed);

    rrdset_done(st);
}

static void aclk_stats_read_q(struct aclk_metrics_per_sample *per_sample)
{
    static RRDSET *st = NULL;
    static RRDDIM *rd_rq_add = NULL;
    static RRDDIM *rd_rq_consumed = NULL;

    if (unlikely(!st)) {
        st = rrdset_create_localhost(
            "netdata", "aclk_read_q", NULL, "aclk_stats", NULL, "Read Queue Libwebsockets->Mosq", "kB/s",
            "netdata", "stats", 200004, localhost->rrd_update_every, RRDSET_TYPE_AREA);

        rd_rq_add = rrddim_add(st, "added", NULL, 1, 1024 * localhost->rrd_update_every, RRD_ALGORITHM_ABSOLUTE);
        rd_rq_consumed = rrddim_add(st, "consumed", NULL, 1, -1024 * localhost->rrd_update_every, RRD_ALGORITHM_ABSOLUTE);
    } else
        rrdset_next(st);

    rrddim_set_by_pointer(st, rd_rq_add, per_sample->read_q_added);
    rrddim_set_by_pointer(st, rd_rq_consumed, per_sample->read_q_consumed);

    rrdset_done(st);
}

static void aclk_stats_cloud_req(struct aclk_metrics_per_sample *per_sample)
{
    static RRDSET *st = NULL;
    static RRDDIM *rd_rq_rcvd = NULL;
    static RRDDIM *rd_rq_err = NULL;

    if (unlikely(!st)) {
        st = rrdset_create_localhost(
            "netdata", "aclk_cloud_req", NULL, "aclk_stats", NULL, "Requests received from cloud", "req/s",
            "netdata", "stats", 200005, localhost->rrd_update_every, RRDSET_TYPE_STACKED);

        rd_rq_rcvd = rrddim_add(st, "received", NULL, 1, localhost->rrd_update_every, RRD_ALGORITHM_ABSOLUTE);
        rd_rq_err = rrddim_add(st, "malformed", NULL, 1, localhost->rrd_update_every, RRD_ALGORITHM_ABSOLUTE);
    } else
        rrdset_next(st);

    rrddim_set_by_pointer(st, rd_rq_rcvd, per_sample->cloud_req_recvd - per_sample->cloud_req_err);
    rrddim_set_by_pointer(st, rd_rq_err, per_sample->cloud_req_err);

    rrdset_done(st);
}

void *aclk_stats_main_thread(void *ptr)
{
    UNUSED(ptr);
    heartbeat_t hb;
    heartbeat_init(&hb);
    usec_t step_ut = localhost->rrd_update_every * USEC_PER_SEC;
    memset(&aclk_metrics_per_sample, 0, sizeof(struct aclk_metrics_per_sample));
    struct aclk_metrics_per_sample per_sample;
    struct aclk_metrics permanent;

    while (!netdata_exit) {
        netdata_thread_testcancel();
        // ------------------------------------------------------------------------
        // Wait for the next iteration point.

        heartbeat_next(&hb, step_ut);

        ACLK_STATS_LOCK;
        // to not hold lock longer than necessary, especially not to hold it
        // during database rrd* operations
        memcpy(&per_sample, &aclk_metrics_per_sample, sizeof(struct aclk_metrics_per_sample));
        memcpy(&permanent, &aclk_metrics, sizeof(struct aclk_metrics));
        memset(&aclk_metrics_per_sample, 0, sizeof(struct aclk_metrics_per_sample));
        ACLK_STATS_UNLOCK;

        aclk_stats_collect(&per_sample, &permanent);
        aclk_stats_query_thread(&per_sample);
#ifdef NETDATA_INTERNAL_CHECKS
        aclk_stats_latency(&per_sample);
#endif
        aclk_stats_write_q(&per_sample);
        aclk_stats_read_q(&per_sample);

        aclk_stats_cloud_req(&per_sample);
    }
    return 0;
}

void aclk_stats_upd_online(int online) {
    if(!aclk_stats_enabled)
        return;

    ACLK_STATS_LOCK;
    aclk_metrics.online = online;

    if(!online)
        aclk_metrics_per_sample.offline_during_sample = 1;
    ACLK_STATS_UNLOCK;
}
