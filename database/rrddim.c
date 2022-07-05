// SPDX-License-Identifier: GPL-3.0-or-later

#define NETDATA_RRD_INTERNALS
#include "rrd.h"
#ifdef ENABLE_DBENGINE
#include "database/engine/rrdengineapi.h"
#endif
#include "storage_engine.h"

// ----------------------------------------------------------------------------
// RRDDIM index

int rrddim_compare(void* a, void* b) {
    if(((RRDDIM *)a)->hash < ((RRDDIM *)b)->hash) return -1;
    else if(((RRDDIM *)a)->hash > ((RRDDIM *)b)->hash) return 1;
    else return strcmp(((RRDDIM *)a)->id, ((RRDDIM *)b)->id);
}

#define rrddim_index_add(st, rd) (RRDDIM *)avl_insert_lock(&((st)->dimensions_index), (avl_t *)(rd))
#define rrddim_index_del(st,rd ) (RRDDIM *)avl_remove_lock(&((st)->dimensions_index), (avl_t *)(rd))

static inline RRDDIM *rrddim_index_find(RRDSET *st, const char *id, uint32_t hash) {
    RRDDIM tmp = {
            .id = id,
            .hash = (hash)?hash:simple_hash(id)
    };
    return (RRDDIM *)avl_search_lock(&(st->dimensions_index), (avl_t *) &tmp);
}


// ----------------------------------------------------------------------------
// RRDDIM - find a dimension

inline RRDDIM *rrddim_find(RRDSET *st, const char *id) {
    debug(D_RRD_CALLS, "rrddim_find() for chart %s, dimension %s", st->name, id);

    return rrddim_index_find(st, id, 0);
}


// ----------------------------------------------------------------------------
// RRDDIM rename a dimension

inline int rrddim_set_name(RRDSET *st, RRDDIM *rd, const char *name) {
    if(unlikely(!name || !*name || (rd->name && !strcmp(rd->name, name))))
        return 0;

    debug(D_RRD_CALLS, "rrddim_set_name() from %s.%s to %s.%s", st->name, rd->name, st->name, name);

    if (rd->name)
        freez((void *) rd->name);

    rd->name = strdupz(name);
    rd->hash_name = simple_hash(rd->name);

    if (!st->state->is_ar_chart)
        rrddimvar_rename_all(rd);

    rd->exposed = 0;
    rrdset_flag_clear(st, RRDSET_FLAG_UPSTREAM_EXPOSED);

    ml_dimension_update_name(st, rd, name);

    return 1;
}

inline int rrddim_set_algorithm(RRDSET *st, RRDDIM *rd, RRD_ALGORITHM algorithm) {
    if(unlikely(rd->algorithm == algorithm))
        return 0;

    debug(D_RRD_CALLS, "Updating algorithm of dimension '%s/%s' from %s to %s", st->id, rd->name, rrd_algorithm_name(rd->algorithm), rrd_algorithm_name(algorithm));
    rd->algorithm = algorithm;
    rd->exposed = 0;
    rrdset_flag_set(st, RRDSET_FLAG_HOMOGENEOUS_CHECK);
    rrdset_flag_clear(st, RRDSET_FLAG_UPSTREAM_EXPOSED);
    return 1;
}

inline int rrddim_set_multiplier(RRDSET *st, RRDDIM *rd, collected_number multiplier) {
    if(unlikely(rd->multiplier == multiplier))
        return 0;

    debug(D_RRD_CALLS, "Updating multiplier of dimension '%s/%s' from " COLLECTED_NUMBER_FORMAT " to " COLLECTED_NUMBER_FORMAT, st->id, rd->name, rd->multiplier, multiplier);
    rd->multiplier = multiplier;
    rd->exposed = 0;
    rrdset_flag_set(st, RRDSET_FLAG_HOMOGENEOUS_CHECK);
    rrdset_flag_clear(st, RRDSET_FLAG_UPSTREAM_EXPOSED);
    return 1;
}

inline int rrddim_set_divisor(RRDSET *st, RRDDIM *rd, collected_number divisor) {
    if(unlikely(rd->divisor == divisor))
        return 0;

    debug(D_RRD_CALLS, "Updating divisor of dimension '%s/%s' from " COLLECTED_NUMBER_FORMAT " to " COLLECTED_NUMBER_FORMAT, st->id, rd->name, rd->divisor, divisor);
    rd->divisor = divisor;
    rd->exposed = 0;
    rrdset_flag_set(st, RRDSET_FLAG_HOMOGENEOUS_CHECK);
    rrdset_flag_clear(st, RRDSET_FLAG_UPSTREAM_EXPOSED);
    return 1;
}

// ----------------------------------------------------------------------------
// RRDDIM create a dimension

void rrdcalc_link_to_rrddim(RRDDIM *rd, RRDSET *st, RRDHOST *host) {
    RRDCALC *rrdc;
    
    for (rrdc = host->alarms_with_foreach; rrdc ; rrdc = rrdc->next) {
        if (simple_pattern_matches(rrdc->spdim, rd->id) || simple_pattern_matches(rrdc->spdim, rd->name)) {
            if (rrdc->hash_chart == st->hash_name || !strcmp(rrdc->chart, st->name) || !strcmp(rrdc->chart, st->id)) {
                char *name = alarm_name_with_dim(rrdc->name, strlen(rrdc->name), rd->name, strlen(rd->name));
                if(rrdcalc_exists(host, st->name, name, 0, 0)) {
                    freez(name);
                    continue;
                }

                netdata_rwlock_wrlock(&host->health_log.alarm_log_rwlock);
                RRDCALC *child = rrdcalc_create_from_rrdcalc(rrdc, host, name, rd->name);
                netdata_rwlock_unlock(&host->health_log.alarm_log_rwlock);

                if (child) {
                    rrdcalc_add_to_host(host, child);
                    RRDCALC *rdcmp  = (RRDCALC *) avl_insert_lock(&(host)->alarms_idx_health_log,(avl_t *)child);
                    if (rdcmp != child) {
                        error("Cannot insert the alarm index ID %s",child->name);
                    }
                }
                else {
                    error("Cannot allocate a new alarm.");
                    rrdc->foreachcounter--;
                }
            }
        }
    }
}

// Return either
//   0 : Dimension is live
//   last collected time : Dimension is not live

#ifdef ENABLE_ACLK
time_t calc_dimension_liveness(RRDDIM *rd, time_t now)
{
    time_t last_updated = rd->last_collected_time.tv_sec;
    int live;
    if (rd->state->aclk_live_status == 1)
        live =
            ((now - last_updated) <
             MIN(rrdset_free_obsolete_time, RRDSET_MINIMUM_DIM_OFFLINE_MULTIPLIER * rd->update_every));
    else
        live = ((now - last_updated) < RRDSET_MINIMUM_DIM_LIVE_MULTIPLIER * rd->update_every);
    return live ? 0 : last_updated;
}
#endif

RRDDIM *rrddim_add_custom(RRDSET *st, const char *id, const char *name, collected_number multiplier,
                          collected_number divisor, RRD_ALGORITHM algorithm, RRD_MEMORY_MODE memory_mode)
{
    RRDHOST *host = st->rrdhost;
    rrdset_wrlock(st);

    RRDDIM *rd = rrddim_find(st, id);
    if(unlikely(rd)) {
        debug(D_RRD_CALLS, "Cannot create rrd dimension '%s/%s', it already exists.", st->id, name?name:"<NONAME>");

        int rc = rrddim_set_name(st, rd, name);
        rc += rrddim_set_algorithm(st, rd, algorithm);
        rc += rrddim_set_multiplier(st, rd, multiplier);
        rc += rrddim_set_divisor(st, rd, divisor);
        if (rrddim_flag_check(rd, RRDDIM_FLAG_ARCHIVED)) {
            store_active_dimension(&rd->state->metric_uuid);
            rd->state->collect_ops.init(rd);
            rrddim_flag_clear(rd, RRDDIM_FLAG_ARCHIVED);
            rrddimvar_create(rd, RRDVAR_TYPE_CALCULATED, NULL, NULL, &rd->last_stored_value, RRDVAR_OPTION_DEFAULT);
            rrddimvar_create(rd, RRDVAR_TYPE_COLLECTED, NULL, "_raw", &rd->last_collected_value, RRDVAR_OPTION_DEFAULT);
            rrddimvar_create(rd, RRDVAR_TYPE_TIME_T, NULL, "_last_collected_t", &rd->last_collected_time.tv_sec, RRDVAR_OPTION_DEFAULT);

            rrddim_flag_set(rd, RRDDIM_FLAG_PENDING_FOREACH_ALARM);
            rrdset_flag_set(st, RRDSET_FLAG_PENDING_FOREACH_ALARMS);
            rrdhost_flag_set(host, RRDHOST_FLAG_PENDING_FOREACH_ALARMS);
        }
        if (unlikely(rc)) {
            debug(D_METADATALOG, "DIMENSION [%s] metadata updated", rd->id);
            (void)sql_store_dimension(&rd->state->metric_uuid, rd->rrdset->chart_uuid, rd->id, rd->name, rd->multiplier, rd->divisor,
                                      rd->algorithm);
#ifdef ENABLE_ACLK
            queue_dimension_to_aclk(rd, calc_dimension_liveness(rd, now_realtime_sec()));
#endif
            rrdset_flag_set(st, RRDSET_FLAG_SYNC_CLOCK);
            rrdset_flag_clear(st, RRDSET_FLAG_UPSTREAM_EXPOSED);
        }
        rrdset_unlock(st);
        return rd;
    }

    rrdset_flag_set(st, RRDSET_FLAG_SYNC_CLOCK);
    rrdset_flag_clear(st, RRDSET_FLAG_UPSTREAM_EXPOSED);

    rd = callocz(1, sizeof(RRDDIM));
    rd->id = strdupz(id);
    rd->hash = simple_hash(rd->id);

    rd->name = (name && *name)?strdupz(name):strdupz(rd->id);
    rd->hash_name = simple_hash(rd->name);

    rd->algorithm = algorithm;
    rd->multiplier = multiplier;
    rd->divisor = divisor;
    if(!rd->divisor) rd->divisor = 1;

    rd->entries = st->entries;
    rd->update_every = st->update_every;

    if(rrdset_flag_check(st, RRDSET_FLAG_STORE_FIRST))
        rd->collections_counter = 1;

    rd->rrdset = st;

    if(memory_mode == RRD_MEMORY_MODE_MAP || memory_mode == RRD_MEMORY_MODE_SAVE) {
        if(!rrddim_memory_load_or_create_map_save(st, rd, memory_mode)) {
            info("Failed to use memory mode %s for chart '%s', dimension '%s', falling back to ram", (memory_mode == RRD_MEMORY_MODE_MAP)?"map":"save", st->name, rd->name);
            memory_mode = RRD_MEMORY_MODE_RAM;
        }
    }

    if(memory_mode == RRD_MEMORY_MODE_RAM) {
        size_t entries = st->entries;
        if(!entries) entries = 5;

        rd->db = netdata_mmap(NULL, entries * sizeof(storage_number), MAP_PRIVATE, 1);
        if(!rd->db) {
            info("Failed to use memory mode ram for chart '%s', dimension '%s', falling back to alloc", st->name, rd->name);
            memory_mode = RRD_MEMORY_MODE_ALLOC;
        }
        else rd->memsize = entries * sizeof(storage_number);
    }

    if(memory_mode == RRD_MEMORY_MODE_ALLOC || memory_mode == RRD_MEMORY_MODE_NONE) {
        size_t entries = st->entries;
        if(entries < 5) entries = 5;

        rd->db = callocz(entries, sizeof(storage_number));
        rd->memsize = entries * sizeof(storage_number);
    }

    rd->rrd_memory_mode = memory_mode;

    rd->state = callocz(1, sizeof(*rd->state));
#ifdef ENABLE_ACLK
    rd->state->aclk_live_status = -1;
#endif
    (void) find_dimension_uuid(st, rd, &(rd->state->metric_uuid));

    STORAGE_ENGINE* eng = storage_engine_get(memory_mode);
    rd->state->collect_ops = eng->api.collect_ops;
    rd->state->query_ops = eng->api.query_ops;

#ifdef ENABLE_DBENGINE
    if(memory_mode == RRD_MEMORY_MODE_DBENGINE) {
        rrdeng_metric_init(rd);
    }
#endif
    store_active_dimension(&rd->state->metric_uuid);
    rd->state->collect_ops.init(rd);
    // append this dimension
    if(!st->dimensions)
        st->dimensions = rd;
    else {
        RRDDIM *td = st->dimensions;

        if(td->algorithm != rd->algorithm || ABS(td->multiplier) != ABS(rd->multiplier) || ABS(td->divisor) != ABS(rd->divisor)) {
            if(!rrdset_flag_check(st, RRDSET_FLAG_HETEROGENEOUS)) {
                #ifdef NETDATA_INTERNAL_CHECKS
                info("Dimension '%s' added on chart '%s' of host '%s' is not homogeneous to other dimensions already present (algorithm is '%s' vs '%s', multiplier is " COLLECTED_NUMBER_FORMAT " vs " COLLECTED_NUMBER_FORMAT ", divisor is " COLLECTED_NUMBER_FORMAT " vs " COLLECTED_NUMBER_FORMAT ").",
                        rd->name,
                        st->name,
                        host->hostname,
                        rrd_algorithm_name(rd->algorithm), rrd_algorithm_name(td->algorithm),
                        rd->multiplier, td->multiplier,
                        rd->divisor, td->divisor
                );
                #endif
                rrdset_flag_set(st, RRDSET_FLAG_HETEROGENEOUS);
            }
        }

        for(; td->next; td = td->next) ;
        td->next = rd;
    }

    if(host->health_enabled && !st->state->is_ar_chart) {
        rrddimvar_create(rd, RRDVAR_TYPE_CALCULATED, NULL, NULL, &rd->last_stored_value, RRDVAR_OPTION_DEFAULT);
        rrddimvar_create(rd, RRDVAR_TYPE_COLLECTED, NULL, "_raw", &rd->last_collected_value, RRDVAR_OPTION_DEFAULT);
        rrddimvar_create(rd, RRDVAR_TYPE_TIME_T, NULL, "_last_collected_t", &rd->last_collected_time.tv_sec, RRDVAR_OPTION_DEFAULT);
    }

    if(unlikely(rrddim_index_add(st, rd) != rd))
        error("RRDDIM: INTERNAL ERROR: attempt to index duplicate dimension '%s' on chart '%s'", rd->id, st->id);

    rrddim_flag_set(rd, RRDDIM_FLAG_PENDING_FOREACH_ALARM);
    rrdset_flag_set(st, RRDSET_FLAG_PENDING_FOREACH_ALARMS);
    rrdhost_flag_set(host, RRDHOST_FLAG_PENDING_FOREACH_ALARMS);

    ml_new_dimension(rd);

    rrdset_unlock(st);
    return(rd);
}

// ----------------------------------------------------------------------------
// RRDDIM remove / free a dimension

void rrddim_free(RRDSET *st, RRDDIM *rd)
{
    ml_delete_dimension(rd);
    
    debug(D_RRD_CALLS, "rrddim_free() %s.%s", st->name, rd->name);

    if (!rrddim_flag_check(rd, RRDDIM_FLAG_ARCHIVED)) {
        uint8_t can_delete_metric = rd->state->collect_ops.finalize(rd);
        if (can_delete_metric && rd->rrd_memory_mode == RRD_MEMORY_MODE_DBENGINE) {
            /* This metric has no data and no references */
            delete_dimension_uuid(&rd->state->metric_uuid);
        }
    }

    if(rd == st->dimensions)
        st->dimensions = rd->next;
    else {
        RRDDIM *i;
        for (i = st->dimensions; i && i->next != rd; i = i->next) ;

        if (i && i->next == rd)
            i->next = rd->next;
        else
            error("Request to free dimension '%s.%s' but it is not linked.", st->id, rd->name);
    }
    rd->next = NULL;

    while(rd->variables)
        rrddimvar_free(rd->variables);

    if(unlikely(rrddim_index_del(st, rd) != rd))
        error("RRDDIM: INTERNAL ERROR: attempt to remove from index dimension '%s' on chart '%s', removed a different dimension.", rd->id, st->id);

    // free(rd->annotations);
//#ifdef ENABLE_ACLK
//    if (!netdata_exit)
//        aclk_send_dimension_update(rd);
//#endif

    freez((void *)rd->id);
    freez((void *)rd->name);
    freez(rd->state);

    // this will free MEMORY_MODE_SAVE and MEMORY_MODE_MAP structures
    rrddim_memory_file_free(rd);

    if(rd->db) {
        if(rd->rrd_memory_mode == RRD_MEMORY_MODE_RAM)
            munmap(rd->db, rd->memsize);
        else
            freez(rd->db);
    }

    freez(rd);
}


// ----------------------------------------------------------------------------
// RRDDIM - set dimension options

int rrddim_hide(RRDSET *st, const char *id) {
    debug(D_RRD_CALLS, "rrddim_hide() for chart %s, dimension %s", st->name, id);

    RRDHOST *host = st->rrdhost;

    RRDDIM *rd = rrddim_find(st, id);
    if(unlikely(!rd)) {
        error("Cannot find dimension with id '%s' on stats '%s' (%s) on host '%s'.", id, st->name, st->id, host->hostname);
        return 1;
    }
    if (!rrddim_flag_check(rd, RRDDIM_FLAG_META_HIDDEN))
        (void)sql_set_dimension_option(&rd->state->metric_uuid, "hidden");

    rrddim_flag_set(rd, RRDDIM_FLAG_HIDDEN);
    rrddim_flag_set(rd, RRDDIM_FLAG_META_HIDDEN);
    return 0;
}

int rrddim_unhide(RRDSET *st, const char *id) {
    debug(D_RRD_CALLS, "rrddim_unhide() for chart %s, dimension %s", st->name, id);

    RRDHOST *host = st->rrdhost;
    RRDDIM *rd = rrddim_find(st, id);
    if(unlikely(!rd)) {
        error("Cannot find dimension with id '%s' on stats '%s' (%s) on host '%s'.", id, st->name, st->id, host->hostname);
        return 1;
    }
    if (rrddim_flag_check(rd, RRDDIM_FLAG_META_HIDDEN))
        (void)sql_set_dimension_option(&rd->state->metric_uuid, NULL);

    rrddim_flag_clear(rd, RRDDIM_FLAG_HIDDEN);
    rrddim_flag_clear(rd, RRDDIM_FLAG_META_HIDDEN);
    return 0;
}

inline void rrddim_is_obsolete(RRDSET *st, RRDDIM *rd) {
    debug(D_RRD_CALLS, "rrddim_is_obsolete() for chart %s, dimension %s", st->name, rd->name);

    if(unlikely(rrddim_flag_check(rd, RRDDIM_FLAG_ARCHIVED))) {
        info("Cannot obsolete already archived dimension %s from chart %s", rd->name, st->name);
        return;
    }
    rrddim_flag_set(rd, RRDDIM_FLAG_OBSOLETE);
    rrdset_flag_set(st, RRDSET_FLAG_OBSOLETE_DIMENSIONS);
}

inline void rrddim_isnot_obsolete(RRDSET *st __maybe_unused, RRDDIM *rd) {
    debug(D_RRD_CALLS, "rrddim_isnot_obsolete() for chart %s, dimension %s", st->name, rd->name);

    rrddim_flag_clear(rd, RRDDIM_FLAG_OBSOLETE);
}

// ----------------------------------------------------------------------------
// RRDDIM - collect values for a dimension

inline collected_number rrddim_set_by_pointer(RRDSET *st __maybe_unused, RRDDIM *rd, collected_number value) {
    debug(D_RRD_CALLS, "rrddim_set_by_pointer() for chart %s, dimension %s, value " COLLECTED_NUMBER_FORMAT, st->name, rd->name, value);

    now_realtime_timeval(&rd->last_collected_time);
    rd->collected_value = value;
    rd->updated = 1;

    rd->collections_counter++;

    collected_number v = (value >= 0) ? value : -value;
    if(unlikely(v > rd->collected_value_max)) rd->collected_value_max = v;

    // fprintf(stderr, "%s.%s %llu " COLLECTED_NUMBER_FORMAT " dt %0.6f" " rate " NETDATA_DOUBLE_FORMAT "\n", st->name, rd->name, st->usec_since_last_update, value, (float)((double)st->usec_since_last_update / (double)1000000), (NETDATA_DOUBLE)((value - rd->last_collected_value) * (NETDATA_DOUBLE)rd->multiplier / (NETDATA_DOUBLE)rd->divisor * 1000000.0 / (NETDATA_DOUBLE)st->usec_since_last_update));

    return rd->last_collected_value;
}

collected_number rrddim_set(RRDSET *st, const char *id, collected_number value) {
    RRDHOST *host = st->rrdhost;
    RRDDIM *rd = rrddim_find(st, id);
    if(unlikely(!rd)) {
        error("Cannot find dimension with id '%s' on stats '%s' (%s) on host '%s'.", id, st->name, st->id, host->hostname);
        return 0;
    }

    return rrddim_set_by_pointer(st, rd, value);
}


// ----------------------------------------------------------------------------
// compatibility layer for RRDDIM files v019

#define RRDDIMENSION_MAGIC_V019  "NETDATA RRD DIMENSION FILE V019"

struct avl_element_v019 {
    void *avl_link[2];
    signed char avl_balance;
};

struct rrddim_map_save_v019 {
    struct avl_element_v019 avl;                    // ignored
    void *id;                                       // ignored
    void *name;                                     // ignored
    uint32_t algorithm;                             // print warning on mismatch - update on load
    uint32_t rrd_memory_mode;                       // ignored
    long long multiplier;                           // print warning on mismatch - update on load
    long long divisor;                              // print warning on mismatch - update on load
    uint32_t flags;                                 // ignored
    uint32_t hash;                                  // ignored
    uint32_t hash_name;                             // ignored
    void *cache_filename;                           // ignored - we use it to keep the filename to save back
    size_t collections_counter;                     // ignored
    void *state;                                    // ignored
    size_t unused[8];                               // ignored
    long long collected_value_max;                  // ignored
    unsigned int updated:1;                         // ignored
    unsigned int exposed:1;                         // ignored
    struct timeval last_collected_time;             // check to reset all - ignored after load
    long double calculated_value;                   // ignored
    long double last_calculated_value;              // ignored
    long double last_stored_value;                  // ignored
    long long collected_value;                      // ignored
    long long last_collected_value;                 // ignored
    long double collected_volume;                   // ignored
    long double stored_volume;                      // ignored
    void *next;                                     // ignored
    void *rrdset;                                   // ignored
    long entries;                                   // check to reset all - update on load
    int update_every;                               // check to reset all - update on load
    size_t memsize;                                 // check to reset all - update on load
    char magic[sizeof(RRDDIMENSION_MAGIC_V019) + 1];// check to reset all - update on load
    void *variables;                                // ignored
    storage_number values[];                        // the array of values
};

size_t rrddim_memory_file_header_size(void) {
    return sizeof(struct rrddim_map_save_v019);
}

void rrddim_memory_file_update(RRDDIM *rd) {
    if(!rd->rd_on_file) return;
    struct rrddim_map_save_v019 *rd_on_file = rd->rd_on_file;

    rd_on_file->last_collected_time.tv_sec = rd->last_collected_time.tv_sec;
    rd_on_file->last_collected_time.tv_usec = rd->last_collected_time.tv_usec;
}

void rrddim_memory_file_free(RRDDIM *rd) {
    if(!rd->rd_on_file) return;

    // needed for memory mode map, to save the latest state
    rrddim_memory_file_update(rd);

    struct rrddim_map_save_v019 *rd_on_file = rd->rd_on_file;
    freez(rd_on_file->cache_filename);
    munmap(rd_on_file, rd_on_file->memsize);

    // remove the pointers from the RRDDIM
    rd->rd_on_file = NULL;
    rd->db = NULL;
}

const char *rrddim_cache_filename(RRDDIM *rd) {
    if(!rd->rd_on_file) return NULL;
    struct rrddim_map_save_v019 *rd_on_file = rd->rd_on_file;
    return rd_on_file->cache_filename;
}

void rrddim_memory_file_save(RRDDIM *rd) {
    if(!rd->rd_on_file) return;

    rrddim_memory_file_update(rd);

    struct rrddim_map_save_v019 *rd_on_file = rd->rd_on_file;
    if(rd_on_file->rrd_memory_mode != RRD_MEMORY_MODE_SAVE) return;

    memory_file_save(rd_on_file->cache_filename, rd_on_file, rd_on_file->memsize);
}

bool rrddim_memory_load_or_create_map_save(RRDSET *st, RRDDIM *rd, RRD_MEMORY_MODE memory_mode) {
    if(memory_mode != RRD_MEMORY_MODE_SAVE && memory_mode != RRD_MEMORY_MODE_MAP)
        return false;

    struct rrddim_map_save_v019 *rd_on_file = NULL;

    unsigned long size = sizeof(struct rrddim_map_save_v019) + (st->entries * sizeof(storage_number));

    char filename[FILENAME_MAX + 1];
    char fullfilename[FILENAME_MAX + 1];
    rrdset_strncpyz_name(filename, rd->id, FILENAME_MAX);
    snprintfz(fullfilename, FILENAME_MAX, "%s/%s.db", st->cache_dir, filename);

    rd_on_file = (struct rrddim_map_save_v019 *)netdata_mmap(fullfilename, size,
        ((memory_mode == RRD_MEMORY_MODE_MAP) ? MAP_SHARED : MAP_PRIVATE), 1);

    if(unlikely(!rd_on_file)) return false;

    struct timeval now;
    now_realtime_timeval(&now);

    int reset = 0;
    rd_on_file->magic[sizeof(RRDDIMENSION_MAGIC_V019)] = '\0';
    if(strcmp(rd_on_file->magic, RRDDIMENSION_MAGIC_V019) != 0) {
        info("Initializing file %s.", fullfilename);
        memset(rd_on_file, 0, size);
        reset = 1;
    }
    else if(rd_on_file->memsize != size) {
        error("File %s does not have the desired size, expected %lu but found %lu. Clearing it.", fullfilename, size, rd_on_file->memsize);
        memset(rd_on_file, 0, size);
        reset = 1;
    }
    else if(rd_on_file->update_every != st->update_every) {
        error("File %s does not have the same update frequency, expected %d but found %d. Clearing it.", fullfilename, st->update_every, rd_on_file->update_every);
        memset(rd_on_file, 0, size);
        reset = 1;
    }
    else if(dt_usec(&now, &rd_on_file->last_collected_time) > (rd_on_file->entries * rd_on_file->update_every * USEC_PER_SEC)) {
        info("File %s is too old (last collected %llu seconds ago, but the database is %ld seconds). Clearing it.", fullfilename, dt_usec(&now, &rd_on_file->last_collected_time) / USEC_PER_SEC, rd_on_file->entries * rd_on_file->update_every);
        memset(rd_on_file, 0, size);
        reset = 1;
    }

    if(!reset) {
        if(rd_on_file->algorithm != rd->algorithm)
            info("File %s does not have the expected algorithm (expected %u '%s', found %u '%s'). Previous values may be wrong.",
                 fullfilename, rd->algorithm, rrd_algorithm_name(rd->algorithm), rd_on_file->algorithm, rrd_algorithm_name(rd_on_file->algorithm));

        if(rd_on_file->multiplier != rd->multiplier)
            info("File %s does not have the expected multiplier (expected " COLLECTED_NUMBER_FORMAT ", found " COLLECTED_NUMBER_FORMAT "). Previous values may be wrong.", fullfilename, rd->multiplier, rd_on_file->multiplier);

        if(rd_on_file->divisor != rd->divisor)
            info("File %s does not have the expected divisor (expected " COLLECTED_NUMBER_FORMAT ", found " COLLECTED_NUMBER_FORMAT "). Previous values may be wrong.", fullfilename, rd->divisor, rd_on_file->divisor);
    }

    // zero the entire header
    memset(rd_on_file, 0, sizeof(struct rrddim_map_save_v019));

    // set the important fields
    strcpy(rd_on_file->magic, RRDDIMENSION_MAGIC_V019);
    rd_on_file->algorithm = rd->algorithm;
    rd_on_file->multiplier = rd->multiplier;
    rd_on_file->divisor = rd->divisor;
    rd_on_file->entries = st->entries;
    rd_on_file->update_every = rd->update_every;
    rd_on_file->memsize = size;
    rd_on_file->rrd_memory_mode = memory_mode;
    rd_on_file->cache_filename = strdupz(fullfilename);

    rd->db = &rd_on_file->values[0];
    rd->rd_on_file = rd_on_file;
    rd->memsize = size;
    rrddim_memory_file_update(rd);

    return true;
}
