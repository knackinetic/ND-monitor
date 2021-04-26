// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef ACLK_UTIL_H
#define ACLK_UTIL_H

#include "libnetdata/libnetdata.h"
#include "mqtt_wss_client.h"

// Helper stuff which should not have any further inside ACLK dependency
// and are supposed not to be needed outside of ACLK

typedef enum {
    ACLK_ENC_UNKNOWN = 0,
    ACLK_ENC_JSON,
    ACLK_ENC_PROTO
} aclk_encoding_type_t;

typedef enum {
    ACLK_TRP_UNKNOWN = 0,
    ACLK_TRP_MQTT_3_1_1,
    ACLK_TRP_MQTT_5
} aclk_transport_type_t;

typedef struct {
    char *endpoint;
    aclk_transport_type_t type;
} aclk_transport_desc_t;

typedef struct {
    int base;
    int max_s;
    int min_s;
} aclk_backoff_t;

typedef struct {
    char *auth_endpoint;
    aclk_encoding_type_t encoding;

    aclk_transport_desc_t **transports;
    size_t transport_count;

    char **capabilities;
    size_t capability_count;

    aclk_backoff_t backoff;
} aclk_env_t;

aclk_encoding_type_t aclk_encoding_type_t_from_str(const char *str);
aclk_transport_type_t aclk_transport_type_t_from_str(const char *str);

void aclk_transport_desc_t_destroy(aclk_transport_desc_t *trp_desc);
void aclk_env_t_destroy(aclk_env_t *env);

enum aclk_topics {
    ACLK_TOPICID_UNKNOWN  = 0,
    ACLK_TOPICID_CHART    = 1,
    ACLK_TOPICID_ALARMS   = 2,
    ACLK_TOPICID_METADATA = 3,
    ACLK_TOPICID_COMMAND  = 4
};

const char *aclk_get_topic(enum aclk_topics topic);
int aclk_generate_topic_cache(struct json_object *json);
void free_topic_cache(void);
// TODO
// aclk_topics_reload //when claim id changes

#ifdef ACLK_LOG_CONVERSATION_DIR
extern volatile int aclk_conversation_log_counter;
#if defined(HAVE_C___ATOMIC) && !defined(NETDATA_NO_ATOMIC_INSTRUCTIONS)
#define ACLK_GET_CONV_LOG_NEXT() __atomic_fetch_add(&aclk_conversation_log_counter, 1, __ATOMIC_SEQ_CST)
#else
extern netdata_mutex_t aclk_conversation_log_mutex;
int aclk_get_conv_log_next();
#define ACLK_GET_CONV_LOG_NEXT() aclk_get_conv_log_next()
#endif
#endif

unsigned long int aclk_tbeb_delay(int reset, int base, unsigned long int min, unsigned long int max);
#define aclk_tbeb_reset(x) aclk_tbeb_delay(1, 0, 0, 0)

typedef enum aclk_proxy_type {
    PROXY_TYPE_UNKNOWN = 0,
    PROXY_TYPE_SOCKS5,
    PROXY_TYPE_HTTP,
    PROXY_DISABLED,
    PROXY_NOT_SET,
} ACLK_PROXY_TYPE;

const char *aclk_proxy_type_to_s(ACLK_PROXY_TYPE *type);
ACLK_PROXY_TYPE aclk_verify_proxy(const char *string);
const char *aclk_lws_wss_get_proxy_setting(ACLK_PROXY_TYPE *type);
void safe_log_proxy_censor(char *proxy);
const char *aclk_get_proxy(ACLK_PROXY_TYPE *type);

void aclk_set_proxy(char **ohost, int *port, enum mqtt_wss_proxy_type *type);

#endif /* ACLK_UTIL_H */
