#include "common.h"

// linux calculates this once every 5 seconds
#define MIN_LOADAVG_UPDATE_EVERY 5

int do_proc_loadavg(int update_every, usec_t dt) {
    static procfile *ff = NULL;
    static int do_loadavg = -1, do_all_processes = -1;
    static usec_t last_loadavg_usec = 0;
    static RRDSET *load_chart = NULL, *processes_chart = NULL;

    if(unlikely(!ff)) {
        char filename[FILENAME_MAX + 1];
        snprintfz(filename, FILENAME_MAX, "%s%s", global_host_prefix, "/proc/loadavg");

        ff = procfile_open(config_get("plugin:proc:/proc/loadavg", "filename to monitor", filename), " \t,:|/", PROCFILE_FLAG_DEFAULT);
        if(unlikely(!ff))
            return 1;
    }

    ff = procfile_readall(ff);
    if(unlikely(!ff))
        return 0; // we return 0, so that we will retry to open it next time

    if(unlikely(do_loadavg == -1)) {
        do_loadavg          = config_get_boolean("plugin:proc:/proc/loadavg", "enable load average", 1);
        do_all_processes    = config_get_boolean("plugin:proc:/proc/loadavg", "enable total processes", 1);
    }

    if(unlikely(procfile_lines(ff) < 1)) {
        error("/proc/loadavg has no lines.");
        return 1;
    }
    if(unlikely(procfile_linewords(ff, 0) < 6)) {
        error("/proc/loadavg has less than 6 words in it.");
        return 1;
    }

    double load1 = strtod(procfile_lineword(ff, 0, 0), NULL);
    double load5 = strtod(procfile_lineword(ff, 0, 1), NULL);
    double load15 = strtod(procfile_lineword(ff, 0, 2), NULL);

    //unsigned long long running_processes  = str2ull(procfile_lineword(ff, 0, 3));
    unsigned long long active_processes     = str2ull(procfile_lineword(ff, 0, 4));
    //unsigned long long next_pid           = str2ull(procfile_lineword(ff, 0, 5));


    // --------------------------------------------------------------------

    if(last_loadavg_usec <= dt) {
        if(likely(do_loadavg)) {
            if(unlikely(!load_chart)) {
                load_chart = rrdset_find_byname("system.load");
                if(unlikely(!load_chart)) {
                    load_chart = rrdset_create("system", "load", NULL, "load", NULL, "System Load Average", "load", 100, (update_every < MIN_LOADAVG_UPDATE_EVERY) ? MIN_LOADAVG_UPDATE_EVERY : update_every, RRDSET_TYPE_LINE);
                    rrddim_add(load_chart, "load1", NULL, 1, 1000, RRDDIM_ABSOLUTE);
                    rrddim_add(load_chart, "load5", NULL, 1, 1000, RRDDIM_ABSOLUTE);
                    rrddim_add(load_chart, "load15", NULL, 1, 1000, RRDDIM_ABSOLUTE);
                }
            }
            else
                rrdset_next(load_chart);

            rrddim_set(load_chart, "load1", (collected_number) (load1 * 1000));
            rrddim_set(load_chart, "load5", (collected_number) (load5 * 1000));
            rrddim_set(load_chart, "load15", (collected_number) (load15 * 1000));
            rrdset_done(load_chart);
        }

        last_loadavg_usec = load_chart->update_every * USEC_PER_SEC;
    }
    else last_loadavg_usec -= dt;

    // --------------------------------------------------------------------

    if(likely(do_all_processes)) {
        if(unlikely(!processes_chart)) {
            processes_chart = rrdset_find_byname("system.active_processes");
            if(unlikely(!processes_chart)) {
                processes_chart = rrdset_create("system", "active_processes", NULL, "processes", NULL, "System Active Processes", "processes", 750, update_every, RRDSET_TYPE_LINE);
                rrddim_add(processes_chart, "active", NULL, 1, 1, RRDDIM_ABSOLUTE);
            }
        }
        else rrdset_next(processes_chart);

        rrddim_set(processes_chart, "active", active_processes);
        rrdset_done(processes_chart);
    }

    return 0;
}
