#include "common.h"

struct plugind *pluginsd_root = NULL;

static inline int pluginsd_space(char c) {
    switch(c) {
    case ' ':
    case '\t':
    case '\r':
    case '\n':
    case '=':
        return 1;

    default:
        return 0;
    }
}

// split a text into words, respecting quotes
inline int pluginsd_split_words(char *str, char **words, int max_words) {
    char *s = str, quote = 0;
    int i = 0, j;

    // skip all white space
    while(unlikely(pluginsd_space(*s))) s++;

    // check for quote
    if(unlikely(*s == '\'' || *s == '"')) {
        quote = *s; // remember the quote
        s++;        // skip the quote
    }

    // store the first word
    words[i++] = s;

    // while we have something
    while(likely(*s)) {
        // if it is escape
        if(unlikely(*s == '\\' && s[1])) {
            s += 2;
            continue;
        }

        // if it is quote
        else if(unlikely(*s == quote)) {
            quote = 0;
            *s = ' ';
            continue;
        }

        // if it is a space
        else if(unlikely(quote == 0 && pluginsd_space(*s))) {

            // terminate the word
            *s++ = '\0';

            // skip all white space
            while(likely(pluginsd_space(*s))) s++;

            // check for quote
            if(unlikely(*s == '\'' || *s == '"')) {
                quote = *s; // remember the quote
                s++;        // skip the quote
            }

            // if we reached the end, stop
            if(unlikely(!*s)) break;

            // store the next word
            if(likely(i < max_words)) words[i++] = s;
            else break;
        }

        // anything else
        else s++;
    }

    // terminate the words
    j = i;
    while(likely(j < max_words)) words[j++] = NULL;

    return i;
}

inline size_t pluginsd_process(RRDHOST *host, struct plugind *cd, FILE *fp, int trust_durations) {
    int enabled = cd->enabled;

    if(!fp || !enabled) {
        cd->enabled = 0;
        return 0;
    }

    size_t count = 0;

    char line[PLUGINSD_LINE_MAX + 1];

    char *words[PLUGINSD_MAX_WORDS] = { NULL };
    uint32_t BEGIN_HASH = simple_hash(PLUGINSD_KEYWORD_BEGIN);
    uint32_t END_HASH = simple_hash(PLUGINSD_KEYWORD_END);
    uint32_t FLUSH_HASH = simple_hash(PLUGINSD_KEYWORD_FLUSH);
    uint32_t CHART_HASH = simple_hash(PLUGINSD_KEYWORD_CHART);
    uint32_t DIMENSION_HASH = simple_hash(PLUGINSD_KEYWORD_DIMENSION);
    uint32_t DISABLE_HASH = simple_hash(PLUGINSD_KEYWORD_DISABLE);

    RRDSET *st = NULL;
    uint32_t hash;

    errno = 0;
    clearerr(fp);

    if(unlikely(fileno(fp) == -1)) {
        error("PLUGINSD: %s: file is not a valid stream.", cd->fullfilename);
        goto cleanup;
    }

    while(!ferror(fp)) {
        if(unlikely(netdata_exit)) break;

        char *r = fgets(line, PLUGINSD_LINE_MAX, fp);
        if(unlikely(!r)) {
            error("PLUGINSD: %s : read failed.", cd->fullfilename);
            break;
        }

        if(unlikely(netdata_exit)) break;

        line[PLUGINSD_LINE_MAX] = '\0';

        // debug(D_PLUGINSD, "PLUGINSD: %s: %s", cd->filename, line);

        int w = pluginsd_split_words(line, words, PLUGINSD_MAX_WORDS);
        char *s = words[0];
        if(unlikely(!s || !*s || !w)) {
            // debug(D_PLUGINSD, "PLUGINSD: empty line");
            continue;
        }

        // debug(D_PLUGINSD, "PLUGINSD: words 0='%s' 1='%s' 2='%s' 3='%s' 4='%s' 5='%s' 6='%s' 7='%s' 8='%s' 9='%s'", words[0], words[1], words[2], words[3], words[4], words[5], words[6], words[7], words[8], words[9]);

        if(likely(!simple_hash_strcmp(s, "SET", &hash))) {
            char *dimension = words[1];
            char *value = words[2];

            if(unlikely(!dimension || !*dimension)) {
                error("PLUGINSD: '%s' is requesting a SET on chart '%s' of host '%s', without a dimension. Disabling it.", cd->fullfilename, st->id, host->hostname);
                enabled = 0;
                break;
            }

            if(unlikely(!value || !*value)) value = NULL;

            if(unlikely(!st)) {
                error("PLUGINSD: '%s' is requesting a SET on dimension %s with value %s on host '%s', without a BEGIN. Disabling it.", cd->fullfilename, dimension, value?value:"<nothing>", host->hostname);
                enabled = 0;
                break;
            }

            if(unlikely(rrdset_flag_check(st, RRDSET_FLAG_DEBUG))) debug(D_PLUGINSD, "PLUGINSD: '%s' is setting dimension %s/%s to %s", cd->fullfilename, st->id, dimension, value?value:"<nothing>");

            if(value) {
                RRDDIM *rd = rrddim_find(st, dimension);
                if(unlikely(!rd)) {
                    error("PLUGINSD: '%s' is requesting a SET to dimension with id '%s' on stats '%s' (%s) on host '%s', which does not exist. Disabling it.", cd->fullfilename, dimension, st->name, st->id, st->rrdhost->hostname);
                    enabled = 0;
                    break;
                }
                else
                    rrddim_set_by_pointer(st, rd, strtoll(value, NULL, 0));
            }
        }
        else if(likely(hash == BEGIN_HASH && !strcmp(s, PLUGINSD_KEYWORD_BEGIN))) {
            char *id = words[1];
            char *microseconds_txt = words[2];

            if(unlikely(!id)) {
                error("PLUGINSD: '%s' is requesting a BEGIN without a chart id for host '%s'. Disabling it.", cd->fullfilename, host->hostname);
                enabled = 0;
                break;
            }

            st = rrdset_find(host, id);
            if(unlikely(!st)) {
                error("PLUGINSD: '%s' is requesting a BEGIN on chart '%s', which does not exist on host '%s'. Disabling it.", cd->fullfilename, id, host->hostname);
                enabled = 0;
                break;
            }

            if(likely(st->counter_done)) {
                usec_t microseconds = 0;
                if(microseconds_txt && *microseconds_txt) microseconds = str2ull(microseconds_txt);

                if(likely(microseconds)) {
                    if(trust_durations)
                        rrdset_next_usec_unfiltered(st, microseconds);
                    else
                        rrdset_next_usec(st, microseconds);
                }
                else rrdset_next(st);
            }
        }
        else if(likely(hash == END_HASH && !strcmp(s, PLUGINSD_KEYWORD_END))) {
            if(unlikely(!st)) {
                error("PLUGINSD: '%s' is requesting an END, without a BEGIN on host '%s'. Disabling it.", cd->fullfilename, host->hostname);
                enabled = 0;
                break;
            }

            if(unlikely(rrdset_flag_check(st, RRDSET_FLAG_DEBUG))) debug(D_PLUGINSD, "PLUGINSD: '%s' is requesting an END on chart %s", cd->fullfilename, st->id);

            rrdset_done(st);
            st = NULL;

            count++;
        }
        else if(likely(hash == FLUSH_HASH && !strcmp(s, PLUGINSD_KEYWORD_FLUSH))) {
            debug(D_PLUGINSD, "PLUGINSD: '%s' is requesting a FLUSH", cd->fullfilename);
            st = NULL;
        }
        else if(likely(hash == CHART_HASH && !strcmp(s, PLUGINSD_KEYWORD_CHART))) {
            int noname = 0;
            st = NULL;

            if((words[1]) != NULL && (words[2]) != NULL && strcmp(words[1], words[2]) == 0)
                noname = 1;

            char *type = words[1];
            char *id = NULL;
            if(likely(type)) {
                id = strchr(type, '.');
                if(likely(id)) { *id = '\0'; id++; }
            }
            char *name = words[2];
            char *title = words[3];
            char *units = words[4];
            char *family = words[5];
            char *context = words[6];
            char *chart = words[7];
            char *priority_s = words[8];
            char *update_every_s = words[9];

            if(unlikely(!type || !*type || !id || !*id)) {
                error("PLUGINSD: '%s' is requesting a CHART, without a type.id, on host '%s'. Disabling it.", cd->fullfilename, host->hostname);
                enabled = 0;
                break;
            }

            int priority = 1000;
            if(likely(priority_s)) priority = str2i(priority_s);

            int update_every = cd->update_every;
            if(likely(update_every_s)) update_every = str2i(update_every_s);
            if(unlikely(!update_every)) update_every = cd->update_every;

            RRDSET_TYPE chart_type = RRDSET_TYPE_LINE;
            if(unlikely(chart)) chart_type = rrdset_type_id(chart);

            if(unlikely(noname || !name || !*name || strcasecmp(name, "NULL") == 0 || strcasecmp(name, "(NULL)") == 0)) name = NULL;
            if(unlikely(!family || !*family)) family = NULL;
            if(unlikely(!context || !*context)) context = NULL;

            st = rrdset_find_bytype(host, type, id);
            if(unlikely(!st)) {
                debug(D_PLUGINSD, "PLUGINSD: Creating chart type='%s', id='%s', name='%s', family='%s', context='%s', chart='%s', priority=%d, update_every=%d"
                      , type, id
                      , name?name:""
                      , family?family:""
                      , context?context:""
                      , rrdset_type_name(chart_type)
                      , priority
                      , update_every
                );

                st = rrdset_create(host, type, id, name, family, context, title, units, priority, update_every, chart_type);
                cd->update_every = update_every;
            }
            else debug(D_PLUGINSD, "PLUGINSD: Chart '%s' already exists. Not adding it again.", st->id);
        }
        else if(likely(hash == DIMENSION_HASH && !strcmp(s, PLUGINSD_KEYWORD_DIMENSION))) {
            char *id = words[1];
            char *name = words[2];
            char *algorithm = words[3];
            char *multiplier_s = words[4];
            char *divisor_s = words[5];
            char *options = words[6];

            if(unlikely(!id || !*id)) {
                error("PLUGINSD: '%s' is requesting a DIMENSION, without an id, host '%s' and chart '%s'. Disabling it.", cd->fullfilename, host->hostname, st?st->id:"UNSET");
                enabled = 0;
                break;
            }

            if(unlikely(!st)) {
                error("PLUGINSD: '%s' is requesting a DIMENSION, without a CHART, on host '%s'. Disabling it.", cd->fullfilename, host->hostname);
                enabled = 0;
                break;
            }

            long multiplier = 1;
            if(multiplier_s && *multiplier_s) multiplier = strtol(multiplier_s, NULL, 0);
            if(unlikely(!multiplier)) multiplier = 1;

            long divisor = 1;
            if(likely(divisor_s && *divisor_s)) divisor = strtol(divisor_s, NULL, 0);
            if(unlikely(!divisor)) divisor = 1;

            if(unlikely(!algorithm || !*algorithm)) algorithm = "absolute";

            if(unlikely(rrdset_flag_check(st, RRDSET_FLAG_DEBUG)))
                debug(D_PLUGINSD, "PLUGINSD: Creating dimension in chart %s, id='%s', name='%s', algorithm='%s', multiplier=%ld, divisor=%ld, hidden='%s'"
                      , st->id
                      , id
                      , name?name:""
                      , rrd_algorithm_name(rrd_algorithm_id(algorithm))
                      , multiplier
                      , divisor
                      , options?options:""
                );

            RRDDIM *rd = rrddim_find(st, id);
            if(unlikely(!rd)) {
                rd = rrddim_add(st, id, name, multiplier, divisor, rrd_algorithm_id(algorithm));
                rrddim_flag_clear(rd, RRDDIM_FLAG_HIDDEN);
                rrddim_flag_clear(rd, RRDDIM_FLAG_DONT_DETECT_RESETS_OR_OVERFLOWS);
                if(options && *options) {
                    if(strstr(options, "hidden") != NULL) rrddim_flag_set(rd, RRDDIM_FLAG_HIDDEN);
                    if(strstr(options, "noreset") != NULL) rrddim_flag_set(rd, RRDDIM_FLAG_DONT_DETECT_RESETS_OR_OVERFLOWS);
                    if(strstr(options, "nooverflow") != NULL) rrddim_flag_set(rd, RRDDIM_FLAG_DONT_DETECT_RESETS_OR_OVERFLOWS);
                }
            }
            else if(unlikely(rrdset_flag_check(st, RRDSET_FLAG_DEBUG)))
                debug(D_PLUGINSD, "PLUGINSD: dimension %s/%s already exists. Not adding it again.", st->id, id);
        }
        else if(unlikely(hash == DISABLE_HASH && !strcmp(s, PLUGINSD_KEYWORD_DISABLE))) {
            info("PLUGINSD: '%s' called DISABLE. Disabling it.", cd->fullfilename);
            enabled = 0;
            break;
        }
        else {
            error("PLUGINSD: '%s' is sending command '%s' which is not known by netdata, for host '%s'. Disabling it.", cd->fullfilename, s, host->hostname);
            enabled = 0;
            break;
        }
    }

cleanup:
    cd->enabled = enabled;

    if(likely(count)) {
        cd->successful_collections += count;
        cd->serial_failures = 0;
    }
    else
        cd->serial_failures++;

    return count;
}

void *pluginsd_worker_thread(void *arg) {
    struct plugind *cd = (struct plugind *)arg;
    cd->obsolete = 0;

    size_t count = 0;

    for(;;) {
        if(unlikely(netdata_exit)) break;

        FILE *fp = mypopen(cd->cmd, &cd->pid);
        if(unlikely(!fp)) {
            error("Cannot popen(\"%s\", \"r\").", cd->cmd);
            break;
        }

        info("PLUGINSD: '%s' running on pid %d", cd->fullfilename, cd->pid);

        count = pluginsd_process(localhost, cd, fp, 0);
        error("PLUGINSD: plugin '%s' disconnected.", cd->fullfilename);

        killpid(cd->pid, SIGTERM);

        info("PLUGINSD: '%s' on pid %d stopped after %zu successful data collections (ENDs).", cd->fullfilename, cd->pid, count);

        // get the return code
        int code = mypclose(fp, cd->pid);

        if(unlikely(netdata_exit)) break;
        else if(code != 0) {
            // the plugin reports failure

            if(likely(!cd->successful_collections)) {
                // nothing collected - disable it
                error("PLUGINSD: '%s' exited with error code %d. Disabling it.", cd->fullfilename, code);
                cd->enabled = 0;
            }
            else {
                // we have collected something

                if(likely(cd->serial_failures <= 10)) {
                    error("PLUGINSD: '%s' exited with error code %d, but has given useful output in the past (%zu times). %s", cd->fullfilename, code, cd->successful_collections, cd->enabled?"Waiting a bit before starting it again.":"Will not start it again - it is disabled.");
                    sleep((unsigned int) (cd->update_every * 10));
                }
                else {
                    error("PLUGINSD: '%s' exited with error code %d, but has given useful output in the past (%zu times). We tried %zu times to restart it, but it failed to generate data. Disabling it.", cd->fullfilename, code, cd->successful_collections, cd->serial_failures);
                    cd->enabled = 0;
                }
            }
        }
        else {
            // the plugin reports success

            if(unlikely(!cd->successful_collections)) {
                // we have collected nothing so far

                if(likely(cd->serial_failures <= 10)) {
                    error("PLUGINSD: '%s' (pid %d) does not generate useful output but it reports success (exits with 0). %s.", cd->fullfilename, cd->pid, cd->enabled?"Waiting a bit before starting it again.":"Will not start it again - it is disabled.");
                    sleep((unsigned int) (cd->update_every * 10));
                }
                else {
                    error("PLUGINSD: '%s' (pid %d) does not generate useful output, although it reports success (exits with 0), but we have tried %zu times to collect something. Disabling it.", cd->fullfilename, cd->pid, cd->serial_failures);
                    cd->enabled = 0;
                }
            }
            else
                sleep((unsigned int) cd->update_every);
        }
        cd->pid = 0;

        if(unlikely(!cd->enabled)) break;
    }

    info("PLUGINSD: '%s' thread exiting", cd->fullfilename);

    cd->obsolete = 1;
    pthread_exit(NULL);
    return NULL;
}

void *pluginsd_main(void *ptr) {
    struct netdata_static_thread *static_thread = (struct netdata_static_thread *)ptr;

    info("PLUGINS.D thread created with task id %d", gettid());

    if(pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL) != 0)
        error("Cannot set pthread cancel type to DEFERRED.");

    if(pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL) != 0)
        error("Cannot set pthread cancel state to ENABLE.");

    int automatic_run = config_get_boolean(CONFIG_SECTION_PLUGINS, "enable running new plugins", 1);
    int scan_frequency = (int) config_get_number(CONFIG_SECTION_PLUGINS, "check for new plugins every", 60);
    DIR *dir = NULL;
    struct dirent *file = NULL;
    struct plugind *cd;

    // enable the apps plugin by default
    // config_get_boolean(CONFIG_SECTION_PLUGINS, "apps", 1);

    if(scan_frequency < 1) scan_frequency = 1;

    for(;;) {
        if(unlikely(netdata_exit)) break;

        dir = opendir(netdata_configured_plugins_dir);
        if(unlikely(!dir)) {
            error("Cannot open directory '%s'.", netdata_configured_plugins_dir);
            goto cleanup;
        }

        while(likely((file = readdir(dir)))) {
            if(unlikely(netdata_exit)) break;

            debug(D_PLUGINSD, "PLUGINSD: Examining file '%s'", file->d_name);

            if(unlikely(strcmp(file->d_name, ".") == 0 || strcmp(file->d_name, "..") == 0)) continue;

            int len = (int) strlen(file->d_name);
            if(unlikely(len <= (int)PLUGINSD_FILE_SUFFIX_LEN)) continue;
            if(unlikely(strcmp(PLUGINSD_FILE_SUFFIX, &file->d_name[len - (int)PLUGINSD_FILE_SUFFIX_LEN]) != 0)) {
                debug(D_PLUGINSD, "PLUGINSD: File '%s' does not end in '%s'.", file->d_name, PLUGINSD_FILE_SUFFIX);
                continue;
            }

            char pluginname[CONFIG_MAX_NAME + 1];
            snprintfz(pluginname, CONFIG_MAX_NAME, "%.*s", (int)(len - PLUGINSD_FILE_SUFFIX_LEN), file->d_name);
            int enabled = config_get_boolean(CONFIG_SECTION_PLUGINS, pluginname, automatic_run);

            if(unlikely(!enabled)) {
                debug(D_PLUGINSD, "PLUGINSD: plugin '%s' is not enabled", file->d_name);
                continue;
            }

            // check if it runs already
            for(cd = pluginsd_root ; cd ; cd = cd->next)
                if(unlikely(strcmp(cd->filename, file->d_name) == 0)) break;

            if(likely(cd && !cd->obsolete)) {
                debug(D_PLUGINSD, "PLUGINSD: plugin '%s' is already running", cd->filename);
                continue;
            }

            // it is not running
            // allocate a new one, or use the obsolete one
            if(unlikely(!cd)) {
                cd = callocz(sizeof(struct plugind), 1);

                snprintfz(cd->id, CONFIG_MAX_NAME, "plugin:%s", pluginname);

                strncpyz(cd->filename, file->d_name, FILENAME_MAX);
                snprintfz(cd->fullfilename, FILENAME_MAX, "%s/%s", netdata_configured_plugins_dir, cd->filename);

                cd->enabled = enabled;
                cd->update_every = (int) config_get_number(cd->id, "update every", localhost->rrd_update_every);
                cd->started_t = now_realtime_sec();

                char *def = "";
                snprintfz(cd->cmd, PLUGINSD_CMD_MAX, "exec %s %d %s", cd->fullfilename, cd->update_every, config_get(cd->id, "command options", def));

                // link it
                if(likely(pluginsd_root)) cd->next = pluginsd_root;
                pluginsd_root = cd;

                // it is not currently running
                cd->obsolete = 1;

                if(cd->enabled) {
                    // spawn a new thread for it
                    if(unlikely(pthread_create(&cd->thread, NULL, pluginsd_worker_thread, cd) != 0))
                        error("PLUGINSD: failed to create new thread for plugin '%s'.", cd->filename);

                    else if(unlikely(pthread_detach(cd->thread) != 0))
                        error("PLUGINSD: Cannot request detach of newly created thread for plugin '%s'.", cd->filename);
                }
            }
        }

        closedir(dir);
        sleep((unsigned int) scan_frequency);
    }

cleanup:
    info("PLUGINS.D thread exiting");

    static_thread->enabled = 0;
    pthread_exit(NULL);
    return NULL;
}
