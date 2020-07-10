#include "aclk_common.h"
#include "aclk_query.h"
#include "aclk_stats.h"

pthread_cond_t query_cond_wait = PTHREAD_COND_INITIALIZER;
pthread_mutex_t query_lock_wait = PTHREAD_MUTEX_INITIALIZER;
#define QUERY_THREAD_LOCK pthread_mutex_lock(&query_lock_wait)
#define QUERY_THREAD_UNLOCK pthread_mutex_unlock(&query_lock_wait)

volatile int aclk_connected = 0;

#ifndef __GNUC__
#pragma region ACLK_QUEUE
#endif

static netdata_mutex_t queue_mutex = NETDATA_MUTEX_INITIALIZER;
#define ACLK_QUEUE_LOCK netdata_mutex_lock(&queue_mutex)
#define ACLK_QUEUE_UNLOCK netdata_mutex_unlock(&queue_mutex)

struct aclk_query {
    time_t created;
    time_t run_after; // Delay run until after this time
    ACLK_CMD cmd;     // What command is this
    char *topic;      // Topic to respond to
    char *data;       // Internal data (NULL if request from the cloud)
    char *msg_id;     // msg_id generated by the cloud (NULL if internal)
    char *query;      // The actual query
    u_char deleted;   // Mark deleted for garbage collect
    struct aclk_query *next;
};

struct aclk_query_queue {
    struct aclk_query *aclk_query_head;
    struct aclk_query *aclk_query_tail;
    unsigned int count;
} aclk_queue = { .aclk_query_head = NULL, .aclk_query_tail = NULL, .count = 0 };


unsigned int aclk_query_size()
{
    int r;
    ACLK_QUEUE_LOCK;
    r = aclk_queue.count;
    ACLK_QUEUE_UNLOCK;
    return r;
}

/*
 * Free a query structure when done
 */
static void aclk_query_free(struct aclk_query *this_query)
{
    if (unlikely(!this_query))
        return;

    freez(this_query->topic);
    if (likely(this_query->query))
        freez(this_query->query);
    if (likely(this_query->data))
        freez(this_query->data);
    if (likely(this_query->msg_id))
        freez(this_query->msg_id);
    freez(this_query);
}

/*
 * Get the next query to process - NULL if nothing there
 * The caller needs to free memory by calling aclk_query_free()
 *
 *      topic
 *      query
 *      The structure itself
 *
 */
static struct aclk_query *aclk_queue_pop()
{
    struct aclk_query *this_query;

    ACLK_QUEUE_LOCK;

    if (likely(!aclk_queue.aclk_query_head)) {
        ACLK_QUEUE_UNLOCK;
        return NULL;
    }

    this_query = aclk_queue.aclk_query_head;

    // Get rid of the deleted entries
    while (this_query && this_query->deleted) {
        aclk_queue.count--;

        aclk_queue.aclk_query_head = aclk_queue.aclk_query_head->next;

        if (likely(!aclk_queue.aclk_query_head)) {
            aclk_queue.aclk_query_tail = NULL;
        }

        aclk_query_free(this_query);

        this_query = aclk_queue.aclk_query_head;
    }

    if (likely(!this_query)) {
        ACLK_QUEUE_UNLOCK;
        return NULL;
    }

    if (!this_query->deleted && this_query->run_after > now_realtime_sec()) {
        info("Query %s will run in %ld seconds", this_query->query, this_query->run_after - now_realtime_sec());
        ACLK_QUEUE_UNLOCK;
        return NULL;
    }

    aclk_queue.count--;
    aclk_queue.aclk_query_head = aclk_queue.aclk_query_head->next;

    if (likely(!aclk_queue.aclk_query_head)) {
        aclk_queue.aclk_query_tail = NULL;
    }

    ACLK_QUEUE_UNLOCK;
    return this_query;
}

// Returns the entry after which we need to create a new entry to run at the specified time
// If NULL is returned we need to add to HEAD
// Need to have a QUERY lock before calling this

static struct aclk_query *aclk_query_find_position(time_t time_to_run)
{
    struct aclk_query *tmp_query, *last_query;

    // Quick check if we will add to the end
    if (likely(aclk_queue.aclk_query_tail)) {
        if (aclk_queue.aclk_query_tail->run_after <= time_to_run)
            return aclk_queue.aclk_query_tail;
    }

    last_query = NULL;
    tmp_query = aclk_queue.aclk_query_head;

    while (tmp_query) {
        if (tmp_query->run_after > time_to_run)
            return last_query;
        last_query = tmp_query;
        tmp_query = tmp_query->next;
    }
    return last_query;
}

// Need to have a QUERY lock before calling this
static struct aclk_query *
aclk_query_find(char *topic, char *data, char *msg_id, char *query, ACLK_CMD cmd, struct aclk_query **last_query)
{
    struct aclk_query *tmp_query, *prev_query;
    UNUSED(cmd);

    tmp_query = aclk_queue.aclk_query_head;
    prev_query = NULL;
    while (tmp_query) {
        if (likely(!tmp_query->deleted)) {
            if (strcmp(tmp_query->topic, topic) == 0 && (!query || strcmp(tmp_query->query, query) == 0)) {
                if ((!data || (data && strcmp(data, tmp_query->data) == 0)) &&
                    (!msg_id || (msg_id && strcmp(msg_id, tmp_query->msg_id) == 0))) {
                    if (likely(last_query))
                        *last_query = prev_query;
                    return tmp_query;
                }
            }
        }
        prev_query = tmp_query;
        tmp_query = tmp_query->next;
    }
    return NULL;
}

/*
 * Add a query to execute, the result will be send to the specified topic
 */

int aclk_queue_query(char *topic, char *data, char *msg_id, char *query, int run_after, int internal, ACLK_CMD aclk_cmd)
{
    struct aclk_query *new_query, *tmp_query;

    // Ignore all commands while we wait for the agent to initialize
    if (unlikely(!aclk_connected))
        return 1;

    run_after = now_realtime_sec() + run_after;

    ACLK_QUEUE_LOCK;
    struct aclk_query *last_query = NULL;

    tmp_query = aclk_query_find(topic, data, msg_id, query, aclk_cmd, &last_query);
    if (unlikely(tmp_query)) {
        if (tmp_query->run_after == run_after) {
            ACLK_QUEUE_UNLOCK;
            QUERY_THREAD_WAKEUP;
            return 0;
        }

        if (last_query)
            last_query->next = tmp_query->next;
        else
            aclk_queue.aclk_query_head = tmp_query->next;

        debug(D_ACLK, "Removing double entry");
        aclk_query_free(tmp_query);
        aclk_queue.count--;
    }

    if (aclk_stats_enabled) {
        ACLK_STATS_LOCK;
        aclk_metrics_per_sample.queries_queued++;
        ACLK_STATS_UNLOCK;
    }

    new_query = callocz(1, sizeof(struct aclk_query));
    new_query->cmd = aclk_cmd;
    if (internal) {
        new_query->topic = strdupz(topic);
        if (likely(query))
            new_query->query = strdupz(query);
    } else {
        new_query->topic = topic;
        new_query->query = query;
        new_query->msg_id = msg_id;
    }

    if (data)
        new_query->data = strdupz(data);

    new_query->next = NULL;
    new_query->created = now_realtime_sec();
    new_query->run_after = run_after;

    debug(D_ACLK, "Added query (%s) (%s)", topic, query ? query : "");

    tmp_query = aclk_query_find_position(run_after);

    if (tmp_query) {
        new_query->next = tmp_query->next;
        tmp_query->next = new_query;
        if (tmp_query == aclk_queue.aclk_query_tail)
            aclk_queue.aclk_query_tail = new_query;
        aclk_queue.count++;
        ACLK_QUEUE_UNLOCK;
        QUERY_THREAD_WAKEUP;
        return 0;
    }

    new_query->next = aclk_queue.aclk_query_head;
    aclk_queue.aclk_query_head = new_query;
    aclk_queue.count++;

    ACLK_QUEUE_UNLOCK;
    QUERY_THREAD_WAKEUP;
    return 0;
}

#ifndef __GNUC__
#pragma endregion
#endif

#ifndef __GNUC__
#pragma region Helper Functions
#endif

/*
 * Take a buffer, encode it and rewrite it
 *
 */

static char *aclk_encode_response(char *src, size_t content_size, int keep_newlines)
{
    char *tmp_buffer = mallocz(content_size * 2);
    char *dst = tmp_buffer;
    while (content_size > 0) {
        switch (*src) {
            case '\n':
                if (keep_newlines)
                {
                    *dst++ = '\\';
                    *dst++ = 'n';
                }
                break;
            case '\t':
                break;
            case 0x01 ... 0x08:
            case 0x0b ... 0x1F:
                *dst++ = '\\';
                *dst++ = 'u';
                *dst++ = '0';
                *dst++ = '0';
                *dst++ = (*src < 0x0F) ? '0' : '1';
                *dst++ = to_hex(*src);
                break;
            case '\"':
                *dst++ = '\\';
                *dst++ = *src;
                break;
            default:
                *dst++ = *src;
        }
        src++;
        content_size--;
    }
    *dst = '\0';

    return tmp_buffer;
}

#ifndef __GNUC__
#pragma endregion
#endif

#ifndef __GNUC__
#pragma region ACLK_QUERY
#endif

static int aclk_execute_query(struct aclk_query *this_query)
{
    if (strncmp(this_query->query, "/api/v1/", 8) == 0) {
        struct web_client *w = (struct web_client *)callocz(1, sizeof(struct web_client));
        w->response.data = buffer_create(NETDATA_WEB_RESPONSE_INITIAL_SIZE);
        w->response.header = buffer_create(NETDATA_WEB_RESPONSE_HEADER_SIZE);
        w->response.header_output = buffer_create(NETDATA_WEB_RESPONSE_HEADER_SIZE);
        strcpy(w->origin, "*"); // Simulate web_client_create_on_fd()
        w->cookie1[0] = 0;      // Simulate web_client_create_on_fd()
        w->cookie2[0] = 0;      // Simulate web_client_create_on_fd()
        w->acl = 0x1f;

        char *mysep = strchr(this_query->query, '?');
        if (mysep) {
            strncpyz(w->decoded_query_string, mysep, NETDATA_WEB_REQUEST_URL_SIZE);
            *mysep = '\0';
        } else
            strncpyz(w->decoded_query_string, this_query->query, NETDATA_WEB_REQUEST_URL_SIZE);

        mysep = strrchr(this_query->query, '/');

        // TODO: handle bad response perhaps in a different way. For now it does to the payload
        w->response.code = web_client_api_request_v1(localhost, w, mysep ? mysep + 1 : "noop");
        now_realtime_timeval(&w->tv_ready);
        w->response.data->date = w->tv_ready.tv_sec;
        web_client_build_http_header(w);  // TODO: this function should offset from date, not tv_ready
        BUFFER *local_buffer = buffer_create(NETDATA_WEB_RESPONSE_INITIAL_SIZE);
        buffer_flush(local_buffer);
        local_buffer->contenttype = CT_APPLICATION_JSON;

        aclk_create_header(local_buffer, "http", this_query->msg_id, 0, 0);
        buffer_strcat(local_buffer, ",\n\t\"payload\": ");
        char *encoded_response = aclk_encode_response(w->response.data->buffer, w->response.data->len, 0);
        char *encoded_header = aclk_encode_response(w->response.header_output->buffer, w->response.header_output->len, 1);

        buffer_sprintf(
            local_buffer, "{\n\"code\": %d,\n\"body\": \"%s\",\n\"headers\": \"%s\"\n}", 
            w->response.code, encoded_response, encoded_header);

        buffer_sprintf(local_buffer, "\n}");

        debug(D_ACLK, "Response:%s", encoded_header);

        aclk_send_message(this_query->topic, local_buffer->buffer, this_query->msg_id);

        buffer_free(w->response.data);
        buffer_free(w->response.header);
        buffer_free(w->response.header_output);
        freez(w);
        buffer_free(local_buffer);
        freez(encoded_response);
        freez(encoded_header);
        return 0;
    }
    return 1;
}

/*
 * This function will fetch the next pending command and process it
 *
 */
static int aclk_process_query(int t_idx)
{
    struct aclk_query *this_query;
    static long int query_count = 0;
    ACLK_METADATA_STATE meta_state;
    usec_t t = 0;

    if (!aclk_connected)
        return 0;

    this_query = aclk_queue_pop();
    if (likely(!this_query)) {
        return 0;
    }

    if (unlikely(this_query->deleted)) {
        debug(D_ACLK, "Garbage collect query %s:%s", this_query->topic, this_query->query);
        aclk_query_free(this_query);
        return 1;
    }
    query_count++;

    debug(
        D_ACLK, "Query #%ld (%s) size=%zu in queue %d seconds", query_count, this_query->topic,
        this_query->query ? strlen(this_query->query) : 0, (int)(now_realtime_sec() - this_query->created));

    switch (this_query->cmd) {
        case ACLK_CMD_ONCONNECT:
            debug(D_ACLK, "EXECUTING on connect metadata command");
            ACLK_SHARED_STATE_LOCK;
            meta_state = aclk_shared_state.metadata_submitted;
            aclk_shared_state.metadata_submitted = ACLK_METADATA_SENT;
            ACLK_SHARED_STATE_UNLOCK;
            aclk_send_metadata(meta_state);
            break;

        case ACLK_CMD_CHART:
            debug(D_ACLK, "EXECUTING a chart update command");
            aclk_send_single_chart(this_query->data, this_query->query);
            break;

        case ACLK_CMD_CHARTDEL:
            debug(D_ACLK, "EXECUTING a chart delete command");
            //TODO: This send the info metadata for now
            aclk_send_info_metadata(ACLK_METADATA_SENT);
            break;

        case ACLK_CMD_ALARM:
            debug(D_ACLK, "EXECUTING an alarm update command");
            aclk_send_message(this_query->topic, this_query->query, this_query->msg_id);
            break;

        case ACLK_CMD_CLOUD:
            t = now_monotonic_high_precision_usec();
            debug(D_ACLK, "EXECUTING a cloud command");
            aclk_execute_query(this_query);
            t = now_monotonic_high_precision_usec() - t;
            break;

        default:
            break;
    }
    debug(D_ACLK, "Query #%ld (%s) done", query_count, this_query->topic);

    if (aclk_stats_enabled) {
        ACLK_STATS_LOCK;
        aclk_metrics_per_sample.queries_dispatched++;
        aclk_queries_per_thread[t_idx]++;
        if(this_query->cmd == ACLK_CMD_CLOUD) {
            aclk_metrics_per_sample.cloud_q_process_total += t;
            aclk_metrics_per_sample.cloud_q_process_count++;
            if(aclk_metrics_per_sample.cloud_q_process_max < t)
                aclk_metrics_per_sample.cloud_q_process_max = t;
        }
        ACLK_STATS_UNLOCK;
    }

    aclk_query_free(this_query);

    return 1;
}

void aclk_query_threads_cleanup(struct aclk_query_threads *query_threads)
{
    if (query_threads && query_threads->thread_list) {
        for (int i = 0; i < query_threads->count; i++) {
            netdata_thread_join(query_threads->thread_list[i].thread, NULL);
        }
        freez(query_threads->thread_list);
    }

    struct aclk_query *this_query;

    do {
        this_query = aclk_queue_pop();
        aclk_query_free(this_query);
    } while (this_query);
}

#define TASK_LEN_MAX 16
void aclk_query_threads_start(struct aclk_query_threads *query_threads)
{
    info("Starting %d query threads.", query_threads->count);

    char thread_name[TASK_LEN_MAX];
    query_threads->thread_list = callocz(query_threads->count, sizeof(struct aclk_query_thread));
    for (int i = 0; i < query_threads->count; i++) {
        query_threads->thread_list[i].idx = i; //thread needs to know its index for statistics

        snprintf(thread_name, TASK_LEN_MAX, "%s_%d", ACLK_THREAD_NAME, i);
        netdata_thread_create(
            &query_threads->thread_list[i].thread, thread_name, NETDATA_THREAD_OPTION_JOINABLE, aclk_query_main_thread,
            &query_threads->thread_list[i]);
    }
}

/**
 * Main query processing thread
 *
 * On startup wait for the agent collectors to initialize
 * Expect at least a time of ACLK_STABLE_TIMEOUT seconds
 * of no new collectors coming in in order to mark the agent
 * as stable (set agent_state = AGENT_STABLE)
 */
void *aclk_query_main_thread(void *ptr)
{
    struct aclk_query_thread *info = ptr;
    time_t previous_popcorn_interrupt = 0;

    while (!netdata_exit) {
        ACLK_SHARED_STATE_LOCK;
        if (aclk_shared_state.agent_state != AGENT_INITIALIZING) {
            ACLK_SHARED_STATE_UNLOCK;
            break;
        }

        time_t checkpoint = now_realtime_sec() - aclk_shared_state.last_popcorn_interrupt;

        if (checkpoint > ACLK_STABLE_TIMEOUT) {
            aclk_shared_state.agent_state = AGENT_STABLE;
            ACLK_SHARED_STATE_UNLOCK;
            info("AGENT stable, last collector initialization activity was %ld seconds ago", checkpoint);
#ifdef ACLK_DEBUG
            _dump_collector_list();
#endif
            break;
        }

        if (previous_popcorn_interrupt != aclk_shared_state.last_popcorn_interrupt) {
            info("Waiting %ds from this moment for agent collectors to initialize." , ACLK_STABLE_TIMEOUT);
            previous_popcorn_interrupt = aclk_shared_state.last_popcorn_interrupt;
        }
        ACLK_SHARED_STATE_UNLOCK;
        sleep_usec(USEC_PER_SEC * 1);
    }

    while (!netdata_exit) {
        ACLK_SHARED_STATE_LOCK;
        if (unlikely(!aclk_shared_state.metadata_submitted)) {
            ACLK_SHARED_STATE_UNLOCK;
            if (unlikely(aclk_queue_query("on_connect", NULL, NULL, NULL, 0, 1, ACLK_CMD_ONCONNECT))) {
                errno = 0;
                error("ACLK failed to queue on_connect command");
                sleep(1);
                continue;
            }
            ACLK_SHARED_STATE_LOCK;
            aclk_shared_state.metadata_submitted = ACLK_METADATA_CMD_QUEUED;
        }
        ACLK_SHARED_STATE_UNLOCK;

        while (aclk_process_query(info->idx)) {
            // Process all commands
        };

        QUERY_THREAD_LOCK;

        // TODO: Need to check if there are queries awaiting already
        if (unlikely(pthread_cond_wait(&query_cond_wait, &query_lock_wait)))
            sleep_usec(USEC_PER_SEC * 1);

        QUERY_THREAD_UNLOCK;
    }

    return NULL;
}

#ifndef __GNUC__
#pragma endregion
#endif
