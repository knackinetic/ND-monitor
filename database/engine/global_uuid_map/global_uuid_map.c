// SPDX-License-Identifier: GPL-3.0-or-later

#include "global_uuid_map.h"

static Pvoid_t JGUID_map = (Pvoid_t) NULL;
static Pvoid_t JGUID_object_map = (Pvoid_t) NULL;
static uv_rwlock_t guid_lock;
static uv_rwlock_t object_lock;
static uv_rwlock_t global_lock;


void   dump_object(uuid_t *index, void *object)
{
    char uuid_s[36 + 1];
    uuid_unparse_lower(*index, uuid_s);
    char local_object[3 * 36 + 2 + 1];

    switch (*(char *) object) {
        case GUID_TYPE_CHAR:
            debug(D_GUIDLOG, "OBJECT GUID %s on [%s]", uuid_s, (char *)object + 1);
            break;
        case GUID_TYPE_CHART:
            uuid_unparse_lower((const unsigned char *)object + 1, local_object);
            uuid_unparse_lower((const unsigned char *)object + 17, local_object+37);
            local_object[36] = ':';
            local_object[74] = '\0';
            debug(D_GUIDLOG, "CHART GUID %s on [%s]", uuid_s, local_object);
            break;
        case GUID_TYPE_DIMENSION:
            uuid_unparse_lower((const unsigned char *)object + 1, local_object);
            uuid_unparse_lower((const unsigned char *)object + 17, local_object + 37);
            uuid_unparse_lower((const unsigned char *)object + 33, local_object + 74);
            local_object[36] = ':';
            local_object[73] = ':';
            local_object[110] = '\0';
            debug(D_GUIDLOG, "DIM GUID %s on [%s]", uuid_s, local_object);
            break;
        default:
            debug(D_GUIDLOG, "Unknown object");
    }
}

/* Returns 0 if it successfully stores the uuid-object mapping or if an identical mapping already exists */
static inline int guid_store_nolock(uuid_t *uuid, void *object, GUID_TYPE object_type)
{
    char *existing_object;
    GUID_TYPE existing_object_type;

    if (unlikely(!object) || uuid == NULL)
        return 0;

    Pvoid_t *PValue;

    PValue = JudyHSIns(&JGUID_map, (void *) uuid, (Word_t) sizeof(uuid_t), PJE0);
    if (PPJERR == PValue)
        fatal("JudyHSIns() fatal error.");
    if (*PValue) {
        existing_object = *PValue;
        existing_object_type = existing_object[0];
        if (existing_object_type != object_type)
            return 1;
        switch (existing_object_type) {
            case GUID_TYPE_DIMENSION:
                if (memcmp(existing_object, object, 1 + 16 + 16 + 16))
                    return 1;
                break;
            case GUID_TYPE_CHART:
                if (memcmp(existing_object, object, 1 + 16 + 16))
                    return 1;
                break;
            case GUID_TYPE_CHAR:
                if (strcmp(existing_object + 1, (char *)object))
                    return 1;
                break;
            default:
                return 1;
        }
        freez(existing_object);
    }

    *PValue = (Pvoid_t *) object;

    PValue = JudyHSIns(&JGUID_object_map, (void *)object, (Word_t) object_type?(object_type * 16)+1:strlen((char *) object+1)+2, PJE0);
    if (PPJERR == PValue)
        fatal("JudyHSIns() fatal error.");
    if (*PValue == NULL) {
        uuid_t *value = (uuid_t *) mallocz(sizeof(uuid_t));
        uuid_copy(*value, *uuid);
        *PValue = value;
    }

#ifdef NETDATA_INTERNAL_CHECKS
    static uint32_t count = 0;
    count++;
    char uuid_s[36 + 1];
    uuid_unparse_lower(*uuid, uuid_s);
    debug(D_GUIDLOG,"GUID added item %" PRIu32" [%s] as:", count, uuid_s);
    dump_object(uuid, object);
#endif
    return 0;
}


inline int guid_store(uuid_t *uuid, char *object, GUID_TYPE object_type)
{
    uv_rwlock_wrlock(&global_lock);
    int rc = guid_store_nolock(uuid, object, object_type);
    uv_rwlock_wrunlock(&global_lock);
    return rc;
}

/*
 * This can be used to bulk load entries into the global map
 *
 * A lock must be aquired since it will call guid_store_nolock
 * with a "no lock" parameter.
 *
 * Note: object memory must be allocated by caller and not released
 */
int guid_bulk_load(char *uuid, char *object)
{
    uuid_t target_uuid;
    if (likely(!uuid_parse(uuid, target_uuid))) {
#ifdef NETDATA_INTERNAL_CHECKS
        debug(D_GUIDLOG,"Mapping GUID [%s] on [%s]", uuid, object);
#endif
        return guid_store_nolock(&target_uuid, object, GUID_TYPE_CHAR);
    }
    return 1;
}

/*
 * Given a GUID, find if an object is stored
 *   - Optionally return the object
 */

GUID_TYPE find_object_by_guid(uuid_t *uuid, char *object, size_t max_bytes)
{
    Pvoid_t *PValue;
    GUID_TYPE   value_type;

    uv_rwlock_rdlock(&global_lock);
    PValue = JudyHSGet(JGUID_map, (void *) uuid, (Word_t) sizeof(uuid_t));
    if (unlikely(!PValue)) {
        uv_rwlock_rdunlock(&global_lock);
        return GUID_TYPE_NOTFOUND;
    }

    value_type = *(char *) *PValue;

    if (likely(object && max_bytes)) {
        switch (value_type) {
            case GUID_TYPE_CHAR:
                if (unlikely(max_bytes - 1 < strlen((char *) *PValue+1)))
                    return GUID_TYPE_NOSPACE;
                strncpyz(object, (char *) *PValue+1, max_bytes - 1);
                break;
            case GUID_TYPE_CHART:
            case GUID_TYPE_DIMENSION:
                if (unlikely(max_bytes < (size_t) value_type * 16))
                    return GUID_TYPE_NOSPACE;
                memcpy(object, *PValue+1, value_type * 16);
                break;
            default:
                uv_rwlock_rdunlock(&global_lock);
                return GUID_TYPE_NOTFOUND;
        }
    }

#ifdef NETDATA_INTERNAL_CHECKS
    dump_object(uuid, *PValue);
#endif
    uv_rwlock_rdunlock(&global_lock);
    return value_type;
}

/*
 * Find a GUID of an object
 *   - Optionally return the GUID
 *
 */

int find_guid_by_object(char *object, uuid_t *uuid, GUID_TYPE object_type)
{
    Pvoid_t *PValue;

    uv_rwlock_rdlock(&global_lock);
    PValue = JudyHSGet(JGUID_object_map, (void *)object, (Word_t)object_type?object_type*16+1:strlen(object+1)+2);
    if (unlikely(!PValue)) {
        uv_rwlock_rdunlock(&global_lock);
        return 1;
    }

    if (likely(uuid))
        uuid_copy(*uuid, *PValue);
    uv_rwlock_rdunlock(&global_lock);
    return 0;
}

int find_or_generate_guid(void *object, uuid_t *uuid, GUID_TYPE object_type, int replace_instead_of_generate)
{
    char  *target_object;
    uuid_t temp_uuid;
    int rc;

    switch (object_type) {
        case GUID_TYPE_DIMENSION:
            if (unlikely(find_or_generate_guid((void *) ((RRDDIM *)object)->id, &temp_uuid, GUID_TYPE_CHAR, 0)))
                return 1;
            target_object = mallocz(49);
            target_object[0] = object_type;
            memcpy(target_object + 1, ((RRDDIM *)object)->rrdset->rrdhost->host_uuid, 16);
            memcpy(target_object + 17, ((RRDDIM *)object)->rrdset->chart_uuid, 16);
            memcpy(target_object + 33, temp_uuid, 16);
            break;
        case GUID_TYPE_CHART:
            if (unlikely(find_or_generate_guid((void *) ((RRDSET *)object)->id, &temp_uuid, GUID_TYPE_CHAR, 0)))
                return 1;
            target_object = mallocz(33);
            target_object[0] = object_type;
            memcpy(target_object + 1, (((RRDSET *)object))->rrdhost->host_uuid, 16);
            memcpy(target_object + 17, temp_uuid, 16);
            break;
        case GUID_TYPE_CHAR:
            target_object = mallocz(strlen((char *) object)+2);
            target_object[0] = object_type;
            strcpy(target_object+1, (char *) object);
            break;
        default:
            return 1;
    }
    rc = find_guid_by_object(target_object, uuid, object_type);
    if (rc) {
        if (!replace_instead_of_generate) /* else take *uuid as user input */
            uuid_generate(*uuid);
        uv_rwlock_wrlock(&global_lock);
        int rc = guid_store_nolock(uuid, target_object, object_type);
        uv_rwlock_wrunlock(&global_lock);
        return rc;
    }
    //uv_rwlock_wrunlock(&global_lock);
#ifdef NETDATA_INTERNAL_CHECKS
    dump_object(uuid, target_object);
#endif
    return 0;
}

void init_global_guid_map()
{
    static int init = 0;

    if (init)
        return;

    init = 1;
    info("Configuring locking mechanism for global GUID map");
    assert(0 == uv_rwlock_init(&guid_lock));
    assert(0 == uv_rwlock_init(&object_lock));
    assert(0 == uv_rwlock_init(&global_lock));

//    int rc = guid_bulk_load("6fc56a64-05d7-47a7-bc82-7f3235d8cbda","d6b37186-74db-11ea-88b2-0bf5095b1f9e/cgroup_qemu_ubuntu18.04.cpu_per_core/cpu3");
//    rc = guid_bulk_load("75c6fa02-97cc-40c1-aacd-a0132190472e","d6b37186-74db-11ea-88b2-0bf5095b1f9e/services.throttle_io_ops_write/system.slice_setvtrgb.service");
//    if (rc == 0)
//        info("BULK GUID load successful");

    return;
}


