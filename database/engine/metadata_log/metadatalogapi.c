// SPDX-License-Identifier: GPL-3.0-or-later
#define NETDATA_RRD_INTERNALS

#include "metadatalog.h"

static inline int metalog_is_initialized(struct metalog_instance *ctx)
{
    return ctx->rrdeng_ctx->metalog_ctx != NULL;
}

static inline void metalog_commit_creation_record(struct metalog_instance *ctx, BUFFER *buffer, uuid_t *uuid)
{
    metalog_commit_record(ctx, buffer, METALOG_COMMIT_CREATION_RECORD, uuid, 0);
}

static inline void metalog_commit_deletion_record(struct metalog_instance *ctx, BUFFER *buffer)
{
    metalog_commit_record(ctx, buffer, METALOG_COMMIT_DELETION_RECORD, NULL, 0);
}

BUFFER *metalog_update_host_buffer(RRDHOST *host)
{
    BUFFER *buffer;
    buffer = buffer_create(4096); /* This will be freed after it has been committed to the metadata log buffer */

    rrdhost_rdlock(host);

    buffer_sprintf(buffer,
                   "HOST \"%s\" \"%s\" \"%s\" %d \"%s\" \"%s\" \"%s\"\n",
//                 "\"%s\" \"%s\" \"%s\" \"%s\" \"%s\" \"%s\" \"%s\" \"%s\" \"%s\" \"%s\" \"%s\" \"%s\""  /* system */
//                 "\"%s\" \"%s\" \"%s\" \"%s\" \"%s\" \"%s\" \"%s\" \"%s\" \"%s\" \"%s\" \"%s\" \"%s\"", /* info   */
                   host->machine_guid,
                   host->hostname,
                   host->registry_hostname,
                   default_rrd_update_every,
                   host->os,
                   host->timezone,
                   (host->tags) ? host->tags : "");

    netdata_rwlock_rdlock(&host->labels_rwlock);
    struct label *labels = host->labels;
    while (labels) {
        buffer_sprintf(buffer
            , "LABEL \"%s\" = %d %s\n"
            , labels->key
            , (int)labels->label_source
            , labels->value);

        labels = labels->next;
    }
    netdata_rwlock_unlock(&host->labels_rwlock);

    buffer_strcat(buffer, "OVERWRITE labels\n");

    rrdhost_unlock(host);
    return buffer;
}

void metalog_commit_update_host(RRDHOST *host)
{
    struct metalog_instance *ctx;
    BUFFER *buffer;

    /* Metadata are only available with dbengine */
    if (!host->rrdeng_ctx)
        return;

    ctx = host->rrdeng_ctx->metalog_ctx;
    if (!ctx) /* metadata log has not been initialized yet */
        return;

    buffer = metalog_update_host_buffer(host);

    metalog_commit_creation_record(ctx, buffer, &host->host_uuid);
}

/* compaction_id 0 means it was not called by compaction logic */
BUFFER *metalog_update_chart_buffer(RRDSET *st, uint32_t compaction_id)
{
    BUFFER *buffer;
    RRDHOST *host = st->rrdhost;

    buffer = buffer_create(1024); /* This will be freed after it has been committed to the metadata log buffer */

    rrdset_rdlock(st);

    buffer_sprintf(buffer, "CONTEXT %s\n", host->machine_guid);

    char uuid_str[37];
    uuid_unparse_lower(*st->chart_uuid, uuid_str);
    buffer_sprintf(buffer, "GUID %s\n", uuid_str);

    // properly set the name for the remote end to parse it
    char *name = "";
    if(likely(st->name)) {
        if(unlikely(strcmp(st->id, st->name))) {
            // they differ
            name = strchr(st->name, '.');
            if(name)
                name++;
            else
                name = "";
        }
    }

    // send the chart
    buffer_sprintf(
        buffer
        , "CHART \"%s\" \"%s\" \"%s\" \"%s\" \"%s\" \"%s\" \"%s\" %ld %d \"%s %s %s %s\" \"%s\" \"%s\"\n"
        , st->id
        , name
        , st->title
        , st->units
        , st->family
        , st->context
        , rrdset_type_name(st->chart_type)
        , st->priority
        , st->update_every
        , rrdset_flag_check(st, RRDSET_FLAG_OBSOLETE)?"obsolete":""
        , rrdset_flag_check(st, RRDSET_FLAG_DETAIL)?"detail":""
        , rrdset_flag_check(st, RRDSET_FLAG_STORE_FIRST)?"store_first":""
        , rrdset_flag_check(st, RRDSET_FLAG_HIDDEN)?"hidden":""
        , (st->plugin_name)?st->plugin_name:""
        , (st->module_name)?st->module_name:""
    );

    // send the dimensions
    RRDDIM *rd;
    rrddim_foreach_read(rd, st) {
        char uuid_str[37];

        uuid_unparse_lower(*rd->state->metric_uuid, uuid_str);
        buffer_sprintf(buffer, "GUID %s\n", uuid_str);

        buffer_sprintf(
            buffer
            , "DIMENSION \"%s\" \"%s\" \"%s\" " COLLECTED_NUMBER_FORMAT " " COLLECTED_NUMBER_FORMAT " \"%s %s %s\"\n"
            , rd->id
            , rd->name
            , rrd_algorithm_name(rd->algorithm)
            , rd->multiplier
            , rd->divisor
            , rrddim_flag_check(rd, RRDDIM_FLAG_OBSOLETE)?"obsolete":""
            , rrddim_flag_check(rd, RRDDIM_FLAG_HIDDEN)?"hidden":""
            , rrddim_flag_check(rd, RRDDIM_FLAG_DONT_DETECT_RESETS_OR_OVERFLOWS)?"noreset":""
        );
        if (compaction_id && compaction_id > rd->state->compaction_id) {
            /* No need to use this dimension again during this compaction cycle */
            rd->state->compaction_id = compaction_id;
        }
    }
    rrdset_unlock(st);
    return buffer;
}

void metalog_commit_update_chart(RRDSET *st)
{
    struct metalog_instance *ctx;
    BUFFER *buffer;
    RRDHOST *host = st->rrdhost;

    /* Metadata are only available with dbengine */
    if (!host->rrdeng_ctx || RRD_MEMORY_MODE_DBENGINE != st->rrd_memory_mode)
        return;

    ctx = host->rrdeng_ctx->metalog_ctx;
    if (!ctx) /* metadata log has not been initialized yet */
        return;

    buffer = metalog_update_chart_buffer(st, 0);

    metalog_commit_creation_record(ctx, buffer, st->chart_uuid);
}

void metalog_commit_delete_chart(RRDSET *st)
{
    struct metalog_instance *ctx;
    BUFFER *buffer;
    RRDHOST *host = st->rrdhost;
    char uuid_str[37];

    /* Metadata are only available with dbengine */
    if (!host->rrdeng_ctx || RRD_MEMORY_MODE_DBENGINE != st->rrd_memory_mode)
        return;

    ctx = host->rrdeng_ctx->metalog_ctx;
    if (!ctx) /* metadata log has not been initialized yet */
        return;
    buffer = buffer_create(64); /* This will be freed after it has been committed to the metadata log buffer */

    uuid_unparse_lower(*st->chart_uuid, uuid_str);
    buffer_sprintf(buffer, "TOMBSTONE %s\n", uuid_str);

    metalog_commit_deletion_record(ctx, buffer);
}

BUFFER *metalog_update_dimension_buffer(RRDDIM *rd)
{
    BUFFER *buffer;
    RRDSET *st = rd->rrdset;
    char uuid_str[37];

    buffer = buffer_create(128); /* This will be freed after it has been committed to the metadata log buffer */

    uuid_unparse_lower(*st->chart_uuid, uuid_str);
    buffer_sprintf(buffer, "CONTEXT %s\n", uuid_str);
    // Activate random GUID
    uuid_unparse_lower(*rd->state->metric_uuid, uuid_str);
    buffer_sprintf(buffer, "GUID %s\n", uuid_str);

    buffer_sprintf(
        buffer
        , "DIMENSION \"%s\" \"%s\" \"%s\" " COLLECTED_NUMBER_FORMAT " " COLLECTED_NUMBER_FORMAT " \"%s %s %s\"\n"
        , rd->id
        , rd->name
        , rrd_algorithm_name(rd->algorithm)
        , rd->multiplier
        , rd->divisor
        , rrddim_flag_check(rd, RRDDIM_FLAG_OBSOLETE)?"obsolete":""
        , rrddim_flag_check(rd, RRDDIM_FLAG_HIDDEN)?"hidden":""
        , rrddim_flag_check(rd, RRDDIM_FLAG_DONT_DETECT_RESETS_OR_OVERFLOWS)?"noreset":""
    );
    return buffer;
}

void metalog_commit_update_dimension(RRDDIM *rd)
{
    struct metalog_instance *ctx;
    BUFFER *buffer;
    RRDSET *st = rd->rrdset;
    RRDHOST *host = st->rrdhost;

    /* Metadata are only available with dbengine */
    if (!host->rrdeng_ctx || RRD_MEMORY_MODE_DBENGINE != st->rrd_memory_mode)
        return;

    ctx = host->rrdeng_ctx->metalog_ctx;
    if (!ctx) /* metadata log has not been initialized yet */
        return;

    buffer = metalog_update_dimension_buffer(rd);

    metalog_commit_creation_record(ctx, buffer, rd->state->metric_uuid);
}

void metalog_commit_delete_dimension(RRDDIM *rd)
{
    struct metalog_instance *ctx;
    BUFFER *buffer;
    RRDSET *st = rd->rrdset;
    RRDHOST *host = st->rrdhost;
    char uuid_str[37];

    /* Metadata are only available with dbengine */
    if (!host->rrdeng_ctx || RRD_MEMORY_MODE_DBENGINE != st->rrd_memory_mode)
        return;

    ctx = host->rrdeng_ctx->metalog_ctx;
    if (!ctx) /* metadata log has not been initialized yet */
        return;
    buffer = buffer_create(64); /* This will be freed after it has been committed to the metadata log buffer */

    uuid_unparse_lower(*rd->state->metric_uuid, uuid_str);
    buffer_sprintf(buffer, "TOMBSTONE %s\n", uuid_str);

    metalog_commit_deletion_record(ctx, buffer);
}

RRDSET *metalog_get_chart_from_uuid(struct metalog_instance *ctx, uuid_t *chart_uuid)
{
    GUID_TYPE ret;
    char chart_object[33], chart_fullid[RRD_ID_LENGTH_MAX + 1];
    uuid_t *machine_guid, *chart_char_guid;

    ret = find_object_by_guid(chart_uuid, chart_object, 33);
    assert(GUID_TYPE_CHART == ret);

    machine_guid = (uuid_t *)chart_object;
    RRDHOST *host = ctx->rrdeng_ctx->host;
    assert(!uuid_compare(host->host_uuid, *machine_guid));

    chart_char_guid = (uuid_t *)(chart_object + 16);

    ret = find_object_by_guid(chart_char_guid, chart_fullid, RRD_ID_LENGTH_MAX + 1);
    assert(GUID_TYPE_CHAR == ret);
    RRDSET *st = rrdset_find(host, chart_fullid);

    return st;
}

RRDDIM *metalog_get_dimension_from_uuid(struct metalog_instance *ctx, uuid_t *metric_uuid)
{
    GUID_TYPE ret;
    char dim_object[49], chart_object[33], id_str[PLUGINSD_LINE_MAX], chart_fullid[RRD_ID_LENGTH_MAX + 1];
    uuid_t *machine_guid, *chart_guid, *chart_char_guid, *dim_char_guid;

    ret = find_object_by_guid(metric_uuid, dim_object, sizeof(dim_object));
    if (GUID_TYPE_DIMENSION != ret) /* not found */
        return NULL;

    machine_guid = (uuid_t *)dim_object;
    RRDHOST *host = ctx->rrdeng_ctx->host;
    assert(!uuid_compare(host->host_uuid, *machine_guid));

    chart_guid = (uuid_t *)(dim_object + 16);
    dim_char_guid = (uuid_t *)(dim_object + 16 + 16);

    ret = find_object_by_guid(dim_char_guid, id_str, sizeof(id_str));
    assert(GUID_TYPE_CHAR == ret);

    ret = find_object_by_guid(chart_guid, chart_object, sizeof(chart_object));
    assert(GUID_TYPE_CHART == ret);
    chart_char_guid = (uuid_t *)(chart_object + 16);

    ret = find_object_by_guid(chart_char_guid, chart_fullid, RRD_ID_LENGTH_MAX + 1);
    assert(GUID_TYPE_CHAR == ret);
    RRDSET *st = rrdset_find(host, chart_fullid);
    assert(st);

    RRDDIM *rd = rrddim_find(st, id_str);

    return rd;
}

/* This function is called by dbengine rotation logic when the metric has no writers */
void metalog_delete_dimension_by_uuid(struct metalog_instance *ctx, uuid_t *metric_uuid)
{
    RRDDIM *rd;
    RRDSET *st;
    RRDHOST *host;
    uint8_t empty_chart;

    rd = metalog_get_dimension_from_uuid(ctx, metric_uuid);
    if (!rd) { /* in the case of legacy UUID convert to multihost and try again */
        uuid_t multihost_uuid;

        rrdeng_convert_legacy_uuid_to_multihost(ctx->rrdeng_ctx->host->machine_guid, metric_uuid, &multihost_uuid);
        rd = metalog_get_dimension_from_uuid(ctx, &multihost_uuid);
    }
    if(!rd) {
        info("Rotated unknown archived metric.");
        return;
    }
    st = rd->rrdset;
    host = st->rrdhost;

    /* Since the metric has no writer it will not be commited to the metadata log by rrddim_free_custom().
     * It must be commited explicitly before calling rrddim_free_custom(). */
    metalog_commit_delete_dimension(rd);

    rrdset_wrlock(st);
    rrddim_free_custom(st, rd, 1);
    empty_chart = (NULL == st->dimensions);
    rrdset_unlock(st);

    if (empty_chart) {
        rrdhost_wrlock(host);
        rrdset_rdlock(st);
        rrdset_delete_custom(st, 1);
        rrdset_unlock(st);
        rrdset_free(st);
        rrdhost_unlock(host);
    }
}

/*
 * Returns 0 on success, negative on error
 */
int metalog_init(struct rrdengine_instance *rrdeng_parent_ctx)
{
    struct metalog_instance *ctx;
    int error;

    ctx = callocz(1, sizeof(*ctx));
    ctx->records_nr = 0;
    ctx->current_compaction_id = 0;
    ctx->quiesce = NO_QUIESCE;

    memset(&ctx->worker_config, 0, sizeof(ctx->worker_config));
    ctx->rrdeng_ctx = rrdeng_parent_ctx;
    ctx->worker_config.ctx = ctx;
    init_metadata_record_log(&ctx->records_log);
    error = init_metalog_files(ctx);
    if (error) {
        goto error_after_init_rrd_files;
    }

    init_completion(&ctx->metalog_completion);
    assert(0 == uv_thread_create(&ctx->worker_config.thread, metalog_worker, &ctx->worker_config));
    /* wait for worker thread to initialize */
    wait_for_completion(&ctx->metalog_completion);
    destroy_completion(&ctx->metalog_completion);
    uv_thread_set_name_np(ctx->worker_config.thread, "METALOG");
    if (ctx->worker_config.error) {
        goto error_after_rrdeng_worker;
    }
    rrdeng_parent_ctx->metalog_ctx = ctx; /* notify dbengine that the metadata log has finished initializing */
    return 0;

error_after_rrdeng_worker:
    finalize_metalog_files(ctx);
error_after_init_rrd_files:
    freez(ctx);
    return UV_EIO;
}

/*
 * Returns 0 on success, 1 on error
 */
int metalog_exit(struct metalog_instance *ctx)
{
    struct metalog_cmd cmd;

    if (NULL == ctx) {
        return 1;
    }

    cmd.opcode = METALOG_SHUTDOWN;
    metalog_enq_cmd(&ctx->worker_config, &cmd);

    assert(0 == uv_thread_join(&ctx->worker_config.thread));

    finalize_metalog_files(ctx);
    freez(ctx);

    return 0;
}

void metalog_prepare_exit(struct metalog_instance *ctx)
{
    struct metalog_cmd cmd;

    if (NULL == ctx) {
        return;
    }

    init_completion(&ctx->metalog_completion);
    cmd.opcode = METALOG_QUIESCE;
    metalog_enq_cmd(&ctx->worker_config, &cmd);

    /* wait for metadata log to quiesce */
    wait_for_completion(&ctx->metalog_completion);
    destroy_completion(&ctx->metalog_completion);
}
