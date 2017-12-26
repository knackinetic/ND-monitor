#define NETDATA_HEALTH_INTERNALS
#include "common.h"

// ----------------------------------------------------------------------------
// RRDCALCTEMPLATE management

void rrdcalctemplate_link_matching(RRDSET *st) {
    RRDHOST *host = st->rrdhost;
    RRDCALCTEMPLATE *rt;

    for(rt = host->templates; rt ; rt = rt->next) {
        if(rt->hash_context == st->hash_context && !strcmp(rt->context, st->context)
           && (!rt->family_pattern || simple_pattern_matches(rt->family_pattern, st->family))) {
            RRDCALC *rc = rrdcalc_create(host, rt, st->id);
            if(unlikely(!rc))
                info("Health tried to create alarm from template '%s' on chart '%s' of host '%s', but it failed", rt->name, st->id, host->hostname);

#ifdef NETDATA_INTERNAL_CHECKS
            else if(rc->rrdset != st)
                error("Health alarm '%s.%s' should be linked to chart '%s', but it is not", rc->chart?rc->chart:"NOCHART", rc->name, st->id);
#endif
        }
    }
}

inline void rrdcalctemplate_free(RRDCALCTEMPLATE *rt) {
    if(unlikely(!rt)) return;

    expression_free(rt->calculation);
    expression_free(rt->warning);
    expression_free(rt->critical);

    freez(rt->family_match);
    simple_pattern_free(rt->family_pattern);

    freez(rt->name);
    freez(rt->exec);
    freez(rt->recipient);
    freez(rt->context);
    freez(rt->source);
    freez(rt->units);
    freez(rt->info);
    freez(rt->dimensions);
    freez(rt);
}

inline void rrdcalctemplate_unlink_and_free(RRDHOST *host, RRDCALCTEMPLATE *rt) {
    if(unlikely(!rt)) return;

    debug(D_HEALTH, "Health removing template '%s' of host '%s'", rt->name, host->hostname);

    if(host->templates == rt) {
        host->templates = rt->next;
    }
    else {
        RRDCALCTEMPLATE *t;
        for (t = host->templates; t && t->next != rt; t = t->next ) ;
        if(t) {
            t->next = rt->next;
            rt->next = NULL;
        }
        else
            error("Cannot find RRDCALCTEMPLATE '%s' linked in host '%s'", rt->name, host->hostname);
    }

    rrdcalctemplate_free(rt);
}


