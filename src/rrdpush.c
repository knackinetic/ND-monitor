#include "common.h"

/*
 * rrdpush
 *
 * 3 threads are involved for all stream operations
 *
 * 1. a random data collection thread, calling rrdset_done_push()
 *    this is called for each chart.
 *
 *    the output of this work is kept in a BUFFER in RRDHOST
 *    the sender thread is signalled via a pipe (also in RRDHOST)
 *
 * 2. a sender thread running at the sending netdata
 *    this is spawned automatically on the first chart to be pushed
 *
 *    It tries to push the metrics to the remote netdata, as fast
 *    as possible (i.e. immediately after they are collected).
 *
 * 3. a receiver thread, running at the receiving netdata
 *    this is spawned automatically when the sender connects to
 *    the receiver.
 *
 */

int default_rrdpush_enabled = 0;
static char *rrdpush_destination = NULL;
static char *rrdpush_api_key = NULL;

int rrdpush_init() {
    default_rrdpush_enabled   = config_get_boolean(CONFIG_SECTION_STREAM, "enabled", default_rrdpush_enabled);
    rrdpush_destination       = config_get(CONFIG_SECTION_STREAM, "destination", "");
    rrdpush_api_key           = config_get(CONFIG_SECTION_STREAM, "api key", "");

    if(default_rrdpush_enabled && (!rrdpush_destination || !*rrdpush_destination || !rrdpush_api_key || !*rrdpush_api_key)) {
        error("STREAM [send]: cannot enable sending thread - information is missing.");
        default_rrdpush_enabled = 0;
    }

    return default_rrdpush_enabled;
}

#define CONNECTED_TO_SIZE 100

// data collection happens from multiple threads
// each of these threads calls rrdset_done()
// which in turn calls rrdset_done_push()
// which uses this pipe to notify the streaming thread
// that there are more data ready to be sent
#define PIPE_READ 0
#define PIPE_WRITE 1

// to have the remote netdata re-sync the charts
// to its current clock, we send for this many
// iterations a BEGIN line without microseconds
// this is for the first iterations of each chart
static unsigned int remote_clock_resync_iterations = 60;

#define rrdpush_lock(host) pthread_mutex_lock(&((host)->rrdpush_mutex))
#define rrdpush_unlock(host) pthread_mutex_unlock(&((host)->rrdpush_mutex))

// checks if the current chart definition has been sent
static inline int need_to_send_chart_definition(RRDSET *st) {
    RRDDIM *rd;
    rrddim_foreach_read(rd, st)
        if(!rrddim_flag_check(rd, RRDDIM_FLAG_EXPOSED))
            return 1;

    return 0;
}

// sends the current chart definition
static inline void send_chart_definition(RRDSET *st) {
    buffer_sprintf(st->rrdhost->rrdpush_buffer, "CHART '%s' '%s' '%s' '%s' '%s' '%s' '%s' %ld %d\n"
                , st->id
                , st->name
                , st->title
                , st->units
                , st->family
                , st->context
                , rrdset_type_name(st->chart_type)
                , st->priority
                , st->update_every
    );

    RRDDIM *rd;
    rrddim_foreach_read(rd, st) {
        buffer_sprintf(st->rrdhost->rrdpush_buffer, "DIMENSION '%s' '%s' '%s' " COLLECTED_NUMBER_FORMAT " " COLLECTED_NUMBER_FORMAT " '%s %s'\n"
                       , rd->id
                       , rd->name
                       , rrd_algorithm_name(rd->algorithm)
                       , rd->multiplier
                       , rd->divisor
                       , rrddim_flag_check(rd, RRDDIM_FLAG_HIDDEN)?"hidden":""
                       , rrddim_flag_check(rd, RRDDIM_FLAG_DONT_DETECT_RESETS_OR_OVERFLOWS)?"noreset":""
        );
        rrddim_flag_set(rd, RRDDIM_FLAG_EXPOSED);
    }
}

// sends the current chart dimensions
static inline void send_chart_metrics(RRDSET *st) {
    buffer_sprintf(st->rrdhost->rrdpush_buffer, "BEGIN %s %llu\n", st->id, (st->counter_done > remote_clock_resync_iterations)?st->usec_since_last_update:0);

    RRDDIM *rd;
    rrddim_foreach_read(rd, st) {
        if(rrddim_flag_check(rd, RRDDIM_FLAG_UPDATED) && rrddim_flag_check(rd, RRDDIM_FLAG_EXPOSED))
            buffer_sprintf(st->rrdhost->rrdpush_buffer, "SET %s = " COLLECTED_NUMBER_FORMAT "\n"
                       , rd->id
                       , rd->collected_value
        );
    }

    buffer_strcat(st->rrdhost->rrdpush_buffer, "END\n");
}

void rrdpush_sender_thread_spawn(RRDHOST *host);

void rrdset_done_push(RRDSET *st) {
    RRDHOST *host = st->rrdhost;

    if(unlikely(!rrdset_flag_check(st, RRDSET_FLAG_ENABLED)))
        return;

    rrdpush_lock(host);

    if(unlikely(host->rrdpush_enabled && !host->rrdpush_spawn))
        rrdpush_sender_thread_spawn(host);

    if(unlikely(!host->rrdpush_buffer || !host->rrdpush_connected)) {
        if(unlikely(!host->rrdpush_error_shown))
            error("STREAM %s [send]: not ready - discarding collected metrics.", host->hostname);

        host->rrdpush_error_shown = 1;

        rrdpush_unlock(host);
        return;
    }
    else if(unlikely(host->rrdpush_error_shown)) {
        info("STREAM %s [send]: ready - sending metrics...", host->hostname);
        host->rrdpush_error_shown = 0;
    }

    if(need_to_send_chart_definition(st))
        send_chart_definition(st);

    send_chart_metrics(st);

    // signal the sender there are more data
    if(write(host->rrdpush_pipe[PIPE_WRITE], " ", 1) == -1)
        error("STREAM %s [send]: cannot write to internal pipe", host->hostname);

    rrdpush_unlock(host);
}

// ----------------------------------------------------------------------------
// rrdpush sender thread

// resets all the chart, so that their definitions
// will be resent to the central netdata
static void rrdpush_sender_thread_reset_all_charts(RRDHOST *host) {
    rrdhost_rdlock(host);

    RRDSET *st;
    rrdset_foreach_read(st, host) {

        // make it re-align the current time
        // on the remote host
        st->counter_done = 0;

        rrdset_rdlock(st);

        RRDDIM *rd;
        rrddim_foreach_read(rd, st)
            rrddim_flag_clear(rd, RRDDIM_FLAG_EXPOSED);

        rrdset_unlock(st);
    }

    rrdhost_unlock(host);
}

static inline void rrdpush_sender_thread_data_flush(RRDHOST *host) {
    rrdpush_lock(host);
    if(buffer_strlen(host->rrdpush_buffer))
        error("STREAM %s [send]: discarding %zu bytes of metrics already in the buffer.", host->hostname, buffer_strlen(host->rrdpush_buffer));

    buffer_flush(host->rrdpush_buffer);
    rrdpush_sender_thread_reset_all_charts(host);
    rrdpush_unlock(host);
}

static inline void rrdpush_sender_thread_lock(RRDHOST *host) {
    if(pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL) != 0)
        error("STREAM %s [send]: cannot set pthread cancel state to DISABLE.", host->hostname);

    rrdpush_lock(host);
}

static inline void rrdpush_sender_thread_unlock(RRDHOST *host) {
    if(pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL) != 0)
        error("STREAM %s [send]: cannot set pthread cancel state to DISABLE.", host->hostname);

    rrdpush_unlock(host);
}

void rrdpush_sender_thread_cleanup(RRDHOST *host) {
    rrdpush_lock(host);

    host->rrdpush_connected = 0;

    if(host->rrdpush_socket != -1) close(host->rrdpush_socket);

    // close the pipe
    if(host->rrdpush_pipe[PIPE_READ] != -1)  close(host->rrdpush_pipe[PIPE_READ]);
    if(host->rrdpush_pipe[PIPE_WRITE] != -1) close(host->rrdpush_pipe[PIPE_WRITE]);
    host->rrdpush_pipe[PIPE_READ] = -1;
    host->rrdpush_pipe[PIPE_WRITE] = -1;

    buffer_free(host->rrdpush_buffer);
    host->rrdpush_buffer = NULL;

    host->rrdpush_spawn = 0;
    host->rrdpush_enabled = 0;

    rrdpush_unlock(host);
}

void *rrdpush_sender_thread(void *ptr) {
    RRDHOST *host = (RRDHOST *)ptr;

    info("STREAM %s [send]: thread created (task id %d)", host->hostname, gettid());

    if(pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL) != 0)
        error("STREAM %s [send]: cannot set pthread cancel type to DEFERRED.", host->hostname);

    if(pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL) != 0)
        error("STREAM %s [send]: cannot set pthread cancel state to ENABLE.", host->hostname);

    int timeout = (int)config_get_number(CONFIG_SECTION_STREAM, "timeout seconds", 60);
    int default_port = (int)config_get_number(CONFIG_SECTION_STREAM, "default port", 19999);
    size_t max_size = (size_t)config_get_number(CONFIG_SECTION_STREAM, "buffer size bytes", 1024 * 1024);
    unsigned int reconnect_delay = (unsigned int)config_get_number(CONFIG_SECTION_STREAM, "reconnect delay seconds", 5);
    remote_clock_resync_iterations = (unsigned int)config_get_number(CONFIG_SECTION_STREAM, "initial clock resync iterations", remote_clock_resync_iterations);
    char connected_to[CONNECTED_TO_SIZE + 1] = "";

    if(!host->rrdpush_enabled || !rrdpush_destination || !*rrdpush_destination || !rrdpush_api_key || !*rrdpush_api_key)
        goto cleanup;

    // initialize rrdpush globals
    host->rrdpush_buffer = buffer_create(1);
    host->rrdpush_connected = 0;
    if(pipe(host->rrdpush_pipe) == -1) fatal("STREAM %s [send]: cannot create required pipe.", host->hostname);

    // initialize local variables
    size_t begin = 0;
    size_t reconnects_counter = 0;
    size_t sent_bytes = 0;
    size_t sent_connection = 0;

    struct timeval tv = {
            .tv_sec = timeout,
            .tv_usec = 0
    };

    struct pollfd fds[2], *ifd, *ofd;
    nfds_t fdmax;

    ifd = &fds[0];
    ofd = &fds[1];

    for(; host->rrdpush_enabled && !netdata_exit ;) {

        if(unlikely(host->rrdpush_socket == -1)) {
            // stop appending data into rrdpush_buffer
            // they will be lost, so there is no point to do it
            host->rrdpush_connected = 0;

            info("STREAM %s [send to %s]: connecting...", host->hostname, rrdpush_destination);
            host->rrdpush_socket = connect_to_one_of(rrdpush_destination, default_port, &tv, &reconnects_counter, connected_to, CONNECTED_TO_SIZE);

            if(unlikely(host->rrdpush_socket == -1)) {
                error("STREAM %s [send to %s]: failed to connect", host->hostname, rrdpush_destination);
                sleep(reconnect_delay);
                continue;
            }

            info("STREAM %s [send to %s]: initializing communication...", host->hostname, connected_to);

            char http[1000 + 1];
            snprintfz(http, 1000,
                    "STREAM key=%s&hostname=%s&machine_guid=%s&os=%s&update_every=%d HTTP/1.1\r\n"
                    "User-Agent: netdata-push-service/%s\r\n"
                    "Accept: */*\r\n\r\n"
                      , rrdpush_api_key
                      , host->hostname
                      , host->machine_guid
                      , host->os
                      , default_rrd_update_every
                      , program_version
            );

            if(send_timeout(host->rrdpush_socket, http, strlen(http), 0, timeout) == -1) {
                close(host->rrdpush_socket);
                host->rrdpush_socket = -1;
                error("STREAM %s [send to %s]: failed to send http header to netdata", host->hostname, connected_to);
                sleep(reconnect_delay);
                continue;
            }

            info("STREAM %s [send to %s]: waiting response from remote netdata...", host->hostname, connected_to);

            if(recv_timeout(host->rrdpush_socket, http, 1000, 0, timeout) == -1) {
                close(host->rrdpush_socket);
                host->rrdpush_socket = -1;
                error("STREAM %s [send to %s]: failed to initialize communication", host->hostname, connected_to);
                sleep(reconnect_delay);
                continue;
            }

            if(strncmp(http, "STREAM", 6)) {
                close(host->rrdpush_socket);
                host->rrdpush_socket = -1;
                error("STREAM %s [send to %s]: server is not replying properly.", host->hostname, connected_to);
                sleep(reconnect_delay);
                continue;
            }

            info("STREAM %s [send to %s]: established communication - sending metrics...", host->hostname, connected_to);

            if(fcntl(host->rrdpush_socket, F_SETFL, O_NONBLOCK) < 0)
                error("STREAM %s [send to %s]: cannot set non-blocking mode for socket.", host->hostname, connected_to);

            rrdpush_sender_thread_data_flush(host);
            sent_connection = 0;

            // allow appending data into rrdpush_buffer
            host->rrdpush_connected = 1;
        }

        ifd->fd = host->rrdpush_pipe[PIPE_READ];
        ifd->events = POLLIN;
        ifd->revents = 0;

        ofd->fd = host->rrdpush_socket;
        ofd->revents = 0;
        if(begin < buffer_strlen(host->rrdpush_buffer)) {
            ofd->events = POLLOUT;
            fdmax = 2;
        }
        else {
            ofd->events = 0;
            fdmax = 1;
        }

        if(netdata_exit) break;
        int retval = poll(fds, fdmax, timeout * 1000);
        if(netdata_exit) break;

        if(unlikely(retval == -1)) {
            if(errno == EAGAIN || errno == EINTR)
                continue;

            error("STREAM %s [send to %s]: failed to poll().", host->hostname, connected_to);
            close(host->rrdpush_socket);
            host->rrdpush_socket = -1;
            break;
        }
        else if(unlikely(!retval)) {
            // timeout
            continue;
        }

        if(ifd->revents & POLLIN) {
            char buffer[1000 + 1];
            if(read(host->rrdpush_pipe[PIPE_READ], buffer, 1000) == -1)
                error("STREAM %s [send to %s]: cannot read from internal pipe.", host->hostname, connected_to);
        }

        if(ofd->revents & POLLOUT && begin < buffer_strlen(host->rrdpush_buffer)) {
            rrdpush_sender_thread_lock(host);
            ssize_t ret = send(host->rrdpush_socket, &host->rrdpush_buffer->buffer[begin], buffer_strlen(host->rrdpush_buffer) - begin, MSG_DONTWAIT);
            if(ret == -1) {
                if(errno != EAGAIN && errno != EINTR) {
                    error("STREAM %s [send to %s]: failed to send metrics - closing connection - we have sent %zu bytes on this connection.", host->hostname, connected_to, sent_connection);
                    close(host->rrdpush_socket);
                    host->rrdpush_socket = -1;
                }
            }
            else {
                sent_connection += ret;
                sent_bytes += ret;
                begin += ret;
                if(begin == buffer_strlen(host->rrdpush_buffer)) {
                    buffer_flush(host->rrdpush_buffer);
                    begin = 0;
                }
            }
            rrdpush_sender_thread_unlock(host);
        }

        // protection from overflow
        if(host->rrdpush_buffer->len > max_size) {
            errno = 0;
            error("STREAM %s [send to %s]: too many data pending - buffer is %zu bytes long, %zu unsent - we have sent %zu bytes in total, %zu on this connection. Closing connection to flush the data.", host->hostname, connected_to, host->rrdpush_buffer->len, host->rrdpush_buffer->len - begin, sent_bytes, sent_connection);
            if(host->rrdpush_socket != -1) {
                close(host->rrdpush_socket);
                host->rrdpush_socket = -1;
            }
        }
    }

cleanup:
    debug(D_WEB_CLIENT, "STREAM %s [send]: sending thread exits.", host->hostname);

    rrdpush_sender_thread_cleanup(host);

    pthread_exit(NULL);
    return NULL;
}


// ----------------------------------------------------------------------------
// rrdpush receiver thread

int rrdpush_receive(int fd, const char *key, const char *hostname, const char *machine_guid, const char *os, int update_every, char *client_ip, char *client_port) {
    RRDHOST *host;
    int history = default_rrd_history_entries;
    RRD_MEMORY_MODE mode = default_rrd_memory_mode;
    int health_enabled = default_health_enabled;
    time_t alarms_delay = 60;

    update_every = (int)appconfig_get_number(&stream_config, machine_guid, "update every", update_every);
    if(update_every < 0) update_every = 1;

    history = (int)appconfig_get_number(&stream_config, key, "default history", history);
    history = (int)appconfig_get_number(&stream_config, machine_guid, "history", history);
    if(history < 5) history = 5;

    mode = rrd_memory_mode_id(appconfig_get(&stream_config, key, "default memory mode", rrd_memory_mode_name(mode)));
    mode = rrd_memory_mode_id(appconfig_get(&stream_config, machine_guid, "memory mode", rrd_memory_mode_name(mode)));

    health_enabled = appconfig_get_boolean_ondemand(&stream_config, key, "health enabled by default", health_enabled);
    health_enabled = appconfig_get_boolean_ondemand(&stream_config, machine_guid, "health enabled", health_enabled);

    alarms_delay = appconfig_get_number(&stream_config, key, "default postpone alarms on connect seconds", alarms_delay);
    alarms_delay = appconfig_get_number(&stream_config, machine_guid, "postpone alarms on connect seconds", alarms_delay);

    if(!strcmp(machine_guid, "localhost"))
        host = localhost;
    else
        host = rrdhost_find_or_create(hostname, machine_guid, os, update_every, history, mode, (health_enabled == CONFIG_BOOLEAN_NO)?0:1);

    if(!host) {
        close(fd);
        error("STREAM %s [receive from [%s]:%s]: failed to find/create host structure.", hostname, client_ip, client_port);
        return 1;
    }

    info("STREAM %s [receive from [%s]:%s]: metrics for host '%s' with machine_guid '%s': update every = %d, history = %d, memory mode = %s, health %s"
         , hostname
         , client_ip
         , client_port
         , host->hostname
         , host->machine_guid
         , host->rrd_update_every
         , host->rrd_history_entries
         , rrd_memory_mode_name(host->rrd_memory_mode)
         , (health_enabled == CONFIG_BOOLEAN_NO)?"disabled":((health_enabled == CONFIG_BOOLEAN_YES)?"enabled":"auto")
    );

    struct plugind cd = {
            .enabled = 1,
            .update_every = default_rrd_update_every,
            .pid = 0,
            .serial_failures = 0,
            .successful_collections = 0,
            .obsolete = 0,
            .started_t = now_realtime_sec(),
            .next = NULL,
    };

    // put the client IP and port into the buffers used by plugins.d
    snprintfz(cd.id,           CONFIG_MAX_NAME,  "%s:%s", client_ip, client_port);
    snprintfz(cd.filename,     FILENAME_MAX,     "%s:%s", client_ip, client_port);
    snprintfz(cd.fullfilename, FILENAME_MAX,     "%s:%s", client_ip, client_port);
    snprintfz(cd.cmd,          PLUGINSD_CMD_MAX, "%s:%s", client_ip, client_port);

    info("STREAM %s [receive from [%s]:%s]: initializing communication...", host->hostname, client_ip, client_port);
    if(send_timeout(fd, "STREAM", 6, 0, 60) != 6) {
        error("STREAM %s [receive from [%s]:%s]: cannot send STREAM command.", host->hostname, client_ip, client_port);
        return 0;
    }

    // remove the non-blocking flag from the socket
    if(fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) & ~O_NONBLOCK) == -1)
        error("STREAM %s [receive from [%s]:%s]: cannot remove the non-blocking flag from socket %d", host->hostname, client_ip, client_port, fd);

    // convert the socket to a FILE *
    FILE *fp = fdopen(fd, "r");
    if(!fp) {
        error("STREAM %s [receive from [%s]:%s]: failed to get a FILE for FD %d.", host->hostname, client_ip, client_port, fd);
        return 0;
    }

    rrdhost_wrlock(host);
    host->use_counter++;
    if(health_enabled != CONFIG_BOOLEAN_NO)
        host->health_delay_up_to = now_realtime_sec() + alarms_delay;
    rrdhost_unlock(host);

    // call the plugins.d processor to receive the metrics
    info("STREAM %s [receive from [%s]:%s]: receiving metrics... (host '%s', machine GUID '%s').", host->hostname, client_ip, client_port, host->hostname, host->machine_guid);
    size_t count = pluginsd_process(host, &cd, fp, 1);
    error("STREAM %s [receive from [%s]:%s]: disconnected (host '%s', machine GUID '%s', completed updates %zu).", host->hostname, client_ip, client_port, host->hostname, host->machine_guid, count);

    rrdhost_wrlock(host);
    host->use_counter--;
    if(!host->use_counter && health_enabled == CONFIG_BOOLEAN_AUTO)
        host->health_enabled = 0;
    rrdhost_unlock(host);

    // cleanup
    fclose(fp);

    return (int)count;
}

struct rrdpush_thread {
    int fd;
    char *key;
    char *hostname;
    char *machine_guid;
    char *os;
    char *client_ip;
    char *client_port;
    int update_every;
};

void *rrdpush_receiver_thread(void *ptr) {
    struct rrdpush_thread *rpt = (struct rrdpush_thread *)ptr;

    if (pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL) != 0)
        error("STREAM %s [receive]: cannot set pthread cancel type to DEFERRED.", rpt->hostname);

    if (pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL) != 0)
        error("STREAM %s [receive]: cannot set pthread cancel state to ENABLE.", rpt->hostname);


    info("STREAM %s [%s]:%s: receive thread created (task id %d)", rpt->hostname, rpt->client_ip, rpt->client_port, gettid());
    rrdpush_receive(rpt->fd, rpt->key, rpt->hostname, rpt->machine_guid, rpt->os, rpt->update_every, rpt->client_ip, rpt->client_port);
    info("STREAM %s [receive from [%s]:%s]: receive thread ended (task id %d)", rpt->hostname, rpt->client_ip, rpt->client_port, gettid());

    close(rpt->fd);
    freez(rpt->key);
    freez(rpt->hostname);
    freez(rpt->machine_guid);
    freez(rpt->os);
    freez(rpt->client_ip);
    freez(rpt->client_port);
    freez(rpt);

    pthread_exit(NULL);
    return NULL;
}

static inline int rrdpush_receive_validate_api_key(const char *key) {
    return appconfig_get_boolean(&stream_config, key, "enabled", 0);
}

void rrdpush_sender_thread_spawn(RRDHOST *host) {
    if(pthread_create(&host->rrdpush_thread, NULL, rrdpush_sender_thread, (void *)host))
        error("STREAM %s [send]: failed to create new thread for client.", host->hostname);

    else if(pthread_detach(host->rrdpush_thread))
        error("STREAM %s [send]: cannot request detach newly created thread.", host->hostname);

    host->rrdpush_spawn = 1;
}

int rrdpush_receiver_thread_spawn(RRDHOST *host, struct web_client *w, char *url) {
    (void)host;

    info("STREAM [receive from [%s]:%s]: new client connection.", w->client_ip, w->client_port);

    char *key = NULL, *hostname = NULL, *machine_guid = NULL, *os = NULL;
    int update_every = default_rrd_update_every;

    while(url) {
        char *value = mystrsep(&url, "?&");
        if(!value || !*value) continue;

        char *name = mystrsep(&value, "=");
        if(!name || !*name) continue;
        if(!value || !*value) continue;

        if(!strcmp(name, "key"))
            key = value;
        else if(!strcmp(name, "hostname"))
            hostname = value;
        else if(!strcmp(name, "machine_guid"))
            machine_guid = value;
        else if(!strcmp(name, "update_every"))
            update_every = (int)strtoul(value, NULL, 0);
        else if(!strcmp(name, "os"))
            os = value;
    }

    if(!key || !*key) {
        error("STREAM [receive from [%s]:%s]: request without an API key. Forbidding access.", w->client_ip, w->client_port);
        buffer_flush(w->response.data);
        buffer_sprintf(w->response.data, "You need an API key for this request.");
        return 401;
    }

    if(!hostname || !*hostname) {
        error("STREAM [receive from [%s]:%s]: request without a hostname. Forbidding access.", w->client_ip, w->client_port);
        buffer_flush(w->response.data);
        buffer_sprintf(w->response.data, "You need to send a hostname too.");
        return 400;
    }

    if(!machine_guid || !*machine_guid) {
        error("STREAM [receive from [%s]:%s]: request without a machine GUID. Forbidding access.", w->client_ip, w->client_port);
        buffer_flush(w->response.data);
        buffer_sprintf(w->response.data, "You need to send a machine GUID too.");
        return 400;
    }

    if(!rrdpush_receive_validate_api_key(key)) {
        error("STREAM [receive from [%s]:%s]: API key '%s' is not allowed. Forbidding access.", w->client_ip, w->client_port, key);
        buffer_flush(w->response.data);
        buffer_sprintf(w->response.data, "Your API key is not permitted access.");
        return 401;
    }

    if(!appconfig_get_boolean(&stream_config, machine_guid, "enabled", 1)) {
        error("STREAM [receive from [%s]:%s]: machine GUID '%s' is not allowed. Forbidding access.", w->client_ip, w->client_port, machine_guid);
        buffer_flush(w->response.data);
        buffer_sprintf(w->response.data, "Your machine guide is not permitted access.");
        return 404;
    }

    struct rrdpush_thread *rpt = mallocz(sizeof(struct rrdpush_thread));
    rpt->fd           = w->ifd;
    rpt->key          = strdupz(key);
    rpt->hostname     = strdupz(hostname);
    rpt->machine_guid = strdupz(machine_guid);
    rpt->os           = strdupz(os);
    rpt->client_ip    = strdupz(w->client_ip);
    rpt->client_port  = strdupz(w->client_port);
    rpt->update_every = update_every;
    pthread_t thread;

    debug(D_SYSTEM, "STREAM [receive from [%s]:%s]: starting receiving thread.", w->client_ip, w->client_port);

    if(pthread_create(&thread, NULL, rrdpush_receiver_thread, (void *)rpt))
        error("STREAM [receive from [%s]:%s]: failed to create new thread for client.", w->client_ip, w->client_port);

    else if(pthread_detach(thread))
        error("STREAM [receive from [%s]:%s]: cannot request detach newly created thread.", w->client_ip, w->client_port);

    // prevent the caller from closing the streaming socket
    if(w->ifd == w->ofd)
        w->ifd = w->ofd = -1;
    else
        w->ifd = -1;

    buffer_flush(w->response.data);
    return 200;
}
