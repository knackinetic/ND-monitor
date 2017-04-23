#include "common.h"

#define STATSD_MAX_METRIC_LENGTH 200

// --------------------------------------------------------------------------------------

static LISTEN_SOCKETS statsd_sockets = {
        .config_section  = CONFIG_SECTION_STATSD,
        .default_bind_to = "udp:* tcp:*",
        .default_port    = STATSD_LISTEN_PORT,
        .backlog         = STATSD_LISTEN_BACKLOG
};

int statsd_listen_sockets_setup(void) {
    return listen_sockets_setup(&statsd_sockets);
}

// --------------------------------------------------------------------------------------

typedef enum statsd_metric_type {
    STATSD_METRIC_TYPE_GAUGE        = 'g',
    STATSD_METRIC_TYPE_COUNTER      = 'c',
    STATSD_METRIC_TYPE_TIMER        = 't',
    STATSD_METRIC_TYPE_HISTOGRAM    = 'h',
    STATSD_METRIC_TYPE_METER        = 'm'
} STATSD_METRIC_TYPE;

typedef struct statsd_metric {
    avl avl;                        // indexing

    const char *key;                // "type|name" for indexing
    uint32_t hash;                  // hash of the key

    const char *name;
    STATSD_METRIC_TYPE type;

    usec_t last_collected_ut;       // the last time this metric was updated
    usec_t last_exposed_ut;         // the last time this metric was sent to netdata
    size_t events;                  // the number of times this metrics has been collected

    size_t count;                   // number of events since the last exposure to netdata
    calculated_number last;         // the last value collected
    calculated_number total;        // the sum of all values collected since the last exposure to netdata
    calculated_number min;          // the min value collected since the last exposure to netdata
    calculated_number max;          // the max value collected since the last exposure to netdata

    RRDSET *st;
    RRDDIM *rd_min;
    RRDDIM *rd_max;
    RRDDIM *rd_avg;

    struct statsd_metric *next;
} STATSD_METRIC;


// --------------------------------------------------------------------------------------------------------------------
// statsd index

int statsd_compare(void* a, void* b) {
    if(((STATSD_METRIC *)a)->hash < ((STATSD_METRIC *)b)->hash) return -1;
    else if(((STATSD_METRIC *)a)->hash > ((STATSD_METRIC *)b)->hash) return 1;
    else return strcmp(((STATSD_METRIC *)a)->key, ((STATSD_METRIC *)b)->key);
}

static avl_tree statsd_index = {
        .compar = statsd_compare,
        .root = NULL
};

static inline STATSD_METRIC *stasd_metric_index_find(const char *key, uint32_t hash) {
    STATSD_METRIC tmp;
    tmp.key = key;
    tmp.hash = (hash)?hash:simple_hash(tmp.key);

    return (STATSD_METRIC *)avl_search(&statsd_index, (avl *)&tmp);
}


// --------------------------------------------------------------------------------------------------------------------
// statsd data collection

static inline void statsd_collected_value(STATSD_METRIC *m, calculated_number value, const char *options) {
    debug(D_STATSD, "Updating metric '%s'", m->key);

    (void)options;

    m->last_collected_ut = now_realtime_usec();
    m->events++;
    m->count++;

    switch(m->type) {
        case STATSD_METRIC_TYPE_HISTOGRAM:
            // FIXME: not implemented yet

        case STATSD_METRIC_TYPE_GAUGE:
        case STATSD_METRIC_TYPE_TIMER:
        case STATSD_METRIC_TYPE_METER:
            m->last = value;
            m->total += value;
            if(value < m->min)
                m->min = value;
            if(value > m->max)
                m->max = value;
            break;

        case STATSD_METRIC_TYPE_COUNTER:
            m->total = m->last = value;
            break;
    }

    debug(D_STATSD, "Updated metric '%s', name '%s', type '%c', events %zu, count %zu, last_collected %llu, last value %0.5Lf, total %0.5Lf, min %0.5Lf, max %0.5Lf"
          , m->key
          , m->name
          , (char)m->type
          , m->events
          , m->count
          , m->last_collected_ut
          , m->last
          , m->total
          , m->min
          , m->max
    );
}

static inline void statsd_process_metric(const char *metric, STATSD_METRIC_TYPE type, calculated_number value, const char *options) {
    debug(D_STATSD, "processing metric '%s', type '%c', value %0.5Lf, options '%s'", metric, (char)type, value, options?options:"");

    char key[STATSD_MAX_METRIC_LENGTH + 1];
    key[0] = (char)type;
    key[1] = '|';
    strncpyz(&key[2], metric, sizeof(key) - 3);

    uint32_t hash = simple_hash(key);

    STATSD_METRIC *m = stasd_metric_index_find(key, hash);
    if(unlikely(!m)) {
        debug(D_STATSD, "Creating new metric '%s'", key);

        m = (STATSD_METRIC *)callocz(sizeof(STATSD_METRIC), 1);
        m->key = strdupz(key);
        m->hash = hash;
        m->name = strdupz(metric);
        m->type = type;
        m = (STATSD_METRIC *)avl_insert(&statsd_index, (avl *)m);
    }

    statsd_collected_value(m, value, options);
}


// --------------------------------------------------------------------------------------------------------------------
// statsd parsing

static inline char *skip_to_next_separator(char *s) {
    while(*s && *s != ':' && *s != '|' && *s != ' ' && *s != '\t' && *s != '\r' && *s != '\n')
        s++;

    return s;
}

static inline char *skip_all_spaces(char *s) {
    while(*s == ' ' || *s == '\t')
        s++;

    return s;
}

static inline char *skip_all_spaces_and_newlines(char *s) {
    while(*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n')
        s++;

    return s;
}

static void statsd_process(char *buffer, size_t size) {
    STATSD_METRIC_TYPE type;

    buffer[size] = '\0';
    debug(D_STATSD, "RECEIVED: '%s'", buffer);

    char *s = buffer, *m, *v, *t;
    while(*s) {
        // skip all spaces and newlines
        s = skip_all_spaces_and_newlines(s);

        if(unlikely(!*s)) {
            // we have nothing
            continue;
        }

        // the beginning of the metric name
        m = s;

        // find the next separator
        s = skip_to_next_separator(s);

        // terminate the metric name
        if(*s != '\0')
            *s++ = '\0';

        // skip all spaces
        s = skip_all_spaces(s);

        if(unlikely(!*s || *s == '\r' || *s == '\n')) {
            // we have only the metric name
            statsd_process_metric(m, STATSD_METRIC_TYPE_METER, 1.0, 0);
            continue;
        }

        // the beginning of the metric value
        v = s;

        // find the next separator
        s = skip_to_next_separator(s);

        // terminate the value
        if(*s != '\0')
            *s++ = '\0';

        // skip all spaces
        s = skip_all_spaces(s);

        if(unlikely(!*s || *s == '\r' || *s == '\n')) {
            // we have only the metric name and value
            statsd_process_metric(m, STATSD_METRIC_TYPE_GAUGE, strtold(v, NULL), 0);
            continue;
        }

        // the beginning of type
        t = s;

        // find the next separator
        s = skip_to_next_separator(s);

        // terminate the type
        if(*s != '\0')
            *s++ = '\0';

        // skip all spaces
        s = skip_all_spaces(s);

        // we have the metric name, value and type
        switch(*t) {
            default:
            case 'g':
                type = STATSD_METRIC_TYPE_GAUGE;
                break;

            case 'c':
                type = STATSD_METRIC_TYPE_COUNTER;
                break;

            case 'm':
                if(t[1] == 's') type = STATSD_METRIC_TYPE_TIMER;
                else type = STATSD_METRIC_TYPE_METER;
                break;

            case 'h':
                type = STATSD_METRIC_TYPE_HISTOGRAM;
                break;
        }

        statsd_process_metric(m, type, strtold(v, NULL), 0);
    }
}


// --------------------------------------------------------------------------------------------------------------------
// statsd pollfd interface

static char statsd_read_buffer[65536];

// new TCP client connected
static void *statsd_add_callback(int fd, short int *events) {
    (void)fd;
    *events = POLLIN;

    return NULL;
}


// TCP client disconnected
static void statsd_del_callback(int fd, void *data) {
    (void)fd;
    (void)data;

    return;
}

// Receive data
static int statsd_rcv_callback(int fd, int socktype, void *data, short int *events) {
    (void)data;

    switch(socktype) {
        case SOCK_STREAM: {
            ssize_t rc;
            do {
                rc = recv(fd, statsd_read_buffer, sizeof(statsd_read_buffer), MSG_DONTWAIT);
                if (rc < 0) {
                    // read failed
                    if (errno != EWOULDBLOCK && errno != EAGAIN) {
                        error("STATSD: recv() failed.");
                        return -1;
                    }
                } else if (!rc) {
                    // connection closed
                    error("STATSD: client disconnected.");
                    return -1;
                } else {
                    // data received
                    statsd_process(statsd_read_buffer, (size_t) rc);
                }
            } while (rc != -1);
            break;
        }

        case SOCK_DGRAM: {
            ssize_t rc;
            do {
                // FIXME: collect sender information
                rc = recvfrom(fd, statsd_read_buffer, sizeof(statsd_read_buffer), MSG_DONTWAIT, NULL, NULL);
                if (rc < 0) {
                    // read failed
                    if (errno != EWOULDBLOCK && errno != EAGAIN) {
                        error("STATSD: recvfrom() failed.");
                        return -1;
                    }
                } else if (rc) {
                    // data received
                    statsd_process(statsd_read_buffer, (size_t) rc);
                }
            } while (rc != -1);
            break;
        }

        default: {
            error("STATSD: unknown socktype %d on socket %d", socktype, fd);
            return -1;
        }
    }

    *events = POLLIN;
    return 0;
}


// --------------------------------------------------------------------------------------------------------------------
// statsd main thread

void *statsd_main(void *ptr) {
    struct netdata_static_thread *static_thread = (struct netdata_static_thread *)ptr;

    info("STATSD thread created with task id %d", gettid());

    if(pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL) != 0)
        error("Cannot set pthread cancel type to DEFERRED.");

    if(pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL) != 0)
        error("Cannot set pthread cancel state to ENABLE.");

    statsd_listen_sockets_setup();
    if(!statsd_sockets.opened) {
        error("STATSD: No statsd sockets to listen to.");
        goto cleanup;
    }

    poll_events(&statsd_sockets
            , statsd_add_callback
            , statsd_del_callback
            , statsd_rcv_callback
    );

cleanup:
    debug(D_WEB_CLIENT, "STATSD: exit!");
    listen_sockets_close(&statsd_sockets);

    pthread_exit(NULL);
    return NULL;
}
