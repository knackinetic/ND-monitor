// SPDX-License-Identifier: GPL-3.0-or-later
#define NETDATA_RRD_INTERNALS

#include "metadatalog.h"
#include "metalogpluginsd.h"

PARSER_RC metalog_pluginsd_chart_action(void *user, char *type, char *id, char *name, char *family, char *context,
                                        char *title, char *units, char *plugin, char *module, int priority,
                                        int update_every, RRDSET_TYPE chart_type, char *options)
{
    struct metalog_pluginsd_state *state = ((PARSER_USER_OBJECT *)user)->private;
    RRDSET *st = NULL;
    RRDHOST *host = ((PARSER_USER_OBJECT *) user)->host;
    uuid_t *chart_uuid;

    chart_uuid = uuid_is_null(state->uuid) ? NULL : &state->uuid;
    st = rrdset_create_custom(
        host, type, id, name, family, context, title, units,
        plugin, module, priority, update_every,
        chart_type, RRD_MEMORY_MODE_DBENGINE, (host)->rrd_history_entries, 1, chart_uuid);

    if (options && *options) {
        if (strstr(options, "obsolete"))
            rrdset_is_obsolete(st);
        else
            rrdset_isnot_obsolete(st);

        if (strstr(options, "detail"))
            rrdset_flag_set(st, RRDSET_FLAG_DETAIL);
        else
            rrdset_flag_clear(st, RRDSET_FLAG_DETAIL);

        if (strstr(options, "hidden"))
            rrdset_flag_set(st, RRDSET_FLAG_HIDDEN);
        else
            rrdset_flag_clear(st, RRDSET_FLAG_HIDDEN);

        if (strstr(options, "store_first"))
            rrdset_flag_set(st, RRDSET_FLAG_STORE_FIRST);
        else
            rrdset_flag_clear(st, RRDSET_FLAG_STORE_FIRST);
    } else {
        rrdset_isnot_obsolete(st);
        rrdset_flag_clear(st, RRDSET_FLAG_DETAIL);
        rrdset_flag_clear(st, RRDSET_FLAG_STORE_FIRST);
    }
    ((PARSER_USER_OBJECT *)user)->st = st;

    if (chart_uuid) { /* It's a valid object */
        struct metalog_record record;
        struct metadata_logfile *metalogfile = state->metalogfile;

        uuid_copy(record.uuid, state->uuid);
        mlf_record_insert(metalogfile, &record);
        uuid_clear(state->uuid); /* Consume UUID */
    }
    return PARSER_RC_OK;
}

PARSER_RC metalog_pluginsd_dimension_action(void *user, RRDSET *st, char *id, char *name, char *algorithm,
                                            long multiplier, long divisor, char *options, RRD_ALGORITHM algorithm_type)
{
    struct metalog_pluginsd_state *state = ((PARSER_USER_OBJECT *)user)->private;
    UNUSED(user);
    UNUSED(algorithm);
    uuid_t *dim_uuid;

    dim_uuid = uuid_is_null(state->uuid) ? NULL : &state->uuid;

    RRDDIM *rd = rrddim_add_custom(st, id, name, multiplier, divisor, algorithm_type, RRD_MEMORY_MODE_DBENGINE, 1,
                                   dim_uuid);
    rrddim_flag_clear(rd, RRDDIM_FLAG_HIDDEN);
    rrddim_flag_clear(rd, RRDDIM_FLAG_DONT_DETECT_RESETS_OR_OVERFLOWS);
    if (options && *options) {
        if (strstr(options, "obsolete") != NULL)
            rrddim_is_obsolete(st, rd);
        else
            rrddim_isnot_obsolete(st, rd);
        if (strstr(options, "hidden") != NULL)
            rrddim_flag_set(rd, RRDDIM_FLAG_HIDDEN);
        if (strstr(options, "noreset") != NULL)
            rrddim_flag_set(rd, RRDDIM_FLAG_DONT_DETECT_RESETS_OR_OVERFLOWS);
        if (strstr(options, "nooverflow") != NULL)
            rrddim_flag_set(rd, RRDDIM_FLAG_DONT_DETECT_RESETS_OR_OVERFLOWS);
    } else {
        rrddim_isnot_obsolete(st, rd);
    }
    if (dim_uuid) { /* It's a valid object */
        struct metalog_record record;
        struct metadata_logfile *metalogfile = state->metalogfile;

        uuid_copy(record.uuid, state->uuid);
        mlf_record_insert(metalogfile, &record);
        uuid_clear(state->uuid); /* Consume UUID */
    }
    return PARSER_RC_OK;
}

PARSER_RC metalog_pluginsd_guid_action(void *user, uuid_t *uuid)
{
    struct metalog_pluginsd_state *state = ((PARSER_USER_OBJECT *)user)->private;

    uuid_copy(state->uuid, *uuid);

    return PARSER_RC_OK;
}

PARSER_RC metalog_pluginsd_context_action(void *user, uuid_t *uuid)
{
    GUID_TYPE ret;
    struct metalog_pluginsd_state *state = ((PARSER_USER_OBJECT *)user)->private;
    struct metalog_instance *ctx = state->ctx;
    char object[49], chart_object[33], id_str[1024];
    uuid_t *chart_guid, *chart_char_guid;
    RRDHOST *host;

    ret = find_object_by_guid(uuid, object, 49);
    switch (ret) {
    case GUID_TYPE_CHAR:
        assert(0);
        break;
    case GUID_TYPE_CHART:
    case GUID_TYPE_DIMENSION:
        host = ctx->rrdeng_ctx->host;
        switch (ret) {
        case GUID_TYPE_CHART:
            chart_char_guid = (uuid_t *)(object + 16);

            ret = find_object_by_guid(chart_char_guid, id_str, RRD_ID_LENGTH_MAX + 1);
            assert(GUID_TYPE_CHAR == ret);
            ((PARSER_USER_OBJECT *) user)->st = rrdset_find(host, id_str);
            break;
        case GUID_TYPE_DIMENSION:
            chart_guid = (uuid_t *)(object + 16);

            ret = find_object_by_guid(chart_guid, chart_object, 33);
            assert(GUID_TYPE_CHART == ret);
            chart_char_guid = (uuid_t *)(chart_object + 16);

            ret = find_object_by_guid(chart_char_guid, id_str, RRD_ID_LENGTH_MAX + 1);
            assert(GUID_TYPE_CHAR == ret);
            ((PARSER_USER_OBJECT *) user)->st = rrdset_find(host, id_str);
            break;
        default:
            assert(0);
            break;
        }
        break;
    case GUID_TYPE_HOST:
        /* Ignore for now */
        break;
    default:
        break;
    }

    return PARSER_RC_OK;
}

PARSER_RC metalog_pluginsd_tombstone_action(void *user, uuid_t *uuid)
{
    GUID_TYPE ret;
    struct metalog_pluginsd_state *state = ((PARSER_USER_OBJECT *)user)->private;
    struct metalog_instance *ctx = state->ctx;
    RRDHOST *host = ctx->rrdeng_ctx->host;
    RRDSET *st;
    RRDDIM *rd;

    ret = find_object_by_guid(uuid, NULL, 0);
    switch (ret) {
        case GUID_TYPE_CHAR:
            assert(0);
            break;
        case GUID_TYPE_CHART:
            st = metalog_get_chart_from_uuid(ctx, uuid);
            if (st) {
                rrdhost_wrlock(host);
                rrdset_free(st);
                rrdhost_unlock(host);
            }
            break;
        case GUID_TYPE_DIMENSION:
            rd = metalog_get_dimension_from_uuid(ctx, uuid);
            if (rd) {
                st = rd->rrdset;
                rrdset_wrlock(st);
                rrddim_free_custom(st, rd, 0);
                rrdset_unlock(st);
            }
            break;
        case GUID_TYPE_HOST:
            /* Ignore for now */
            break;
        default:
            break;
    }

    return PARSER_RC_OK;
}

void metalog_pluginsd_state_init(struct metalog_pluginsd_state *state, struct metalog_instance *ctx)
{
    state->ctx = ctx;
    state->skip_record = 0;
    uuid_clear(state->uuid);
    state->metalogfile = NULL;
}