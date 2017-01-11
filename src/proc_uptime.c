#include "common.h"

int do_proc_uptime(int update_every, usec_t dt) {
    (void)dt;

    static RRDSET *st = NULL;
    collected_number uptime = 0;

#ifdef CLOCK_BOOTTIME_IS_AVAILABLE
    uptime = now_boottime_usec() / 1000;
#else
    static procfile *ff = NULL;

    if(unlikely(!ff)) {
        char filename[FILENAME_MAX + 1];
        snprintfz(filename, FILENAME_MAX, "%s%s", global_host_prefix, "/proc/uptime");

        ff = procfile_open(config_get("plugin:proc:/proc/uptime", "filename to monitor", filename), " \t", PROCFILE_FLAG_DEFAULT);
        if(unlikely(!ff))
            return 1;
    }

    ff = procfile_readall(ff);
    if(unlikely(!ff))
        return 0; // we return 0, so that we will retry to open it next time

    if(unlikely(procfile_lines(ff) < 1)) {
        error("/proc/uptime has no lines.");
        return 1;
    }
    if(unlikely(procfile_linewords(ff, 0) < 1)) {
        error("/proc/uptime has less than 1 word in it.");
        return 1;
    }

    uptime = (collected_number)(strtold(procfile_lineword(ff, 0, 0), NULL) * 1000.0);
#endif

    // --------------------------------------------------------------------

    if(unlikely(!st))
        st = rrdset_find("system.uptime");

    if(unlikely(!st)) {
        st = rrdset_create("system", "uptime", NULL, "uptime", NULL, "System Uptime", "seconds", 1000, update_every, RRDSET_TYPE_LINE);
        rrddim_add(st, "uptime", NULL, 1, 1000, RRDDIM_ABSOLUTE);
    }
    else rrdset_next(st);

    rrddim_set(st, "uptime", uptime);
    rrdset_done(st);

    return 0;
}
