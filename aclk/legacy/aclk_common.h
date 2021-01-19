#ifndef ACLK_COMMON_H
#define ACLK_COMMON_H

#include "aclk_rrdhost_state.h"
#include "../../daemon/common.h"

extern netdata_mutex_t aclk_shared_state_mutex;
#define ACLK_SHARED_STATE_LOCK netdata_mutex_lock(&aclk_shared_state_mutex)
#define ACLK_SHARED_STATE_UNLOCK netdata_mutex_unlock(&aclk_shared_state_mutex)

// minimum and maximum supported version of ACLK
// in this version of agent
#define ACLK_VERSION_MIN 2
#define ACLK_VERSION_MAX 3

// Version negotiation messages have they own versioning
// this is also used for LWT message as we set that up
// before version negotiation
#define ACLK_VERSION_NEG_VERSION 1

// Maximum time to wait for version negotiation before aborting
// and defaulting to oldest supported version
#define VERSION_NEG_TIMEOUT 3

#if ACLK_VERSION_MIN > ACLK_VERSION_MAX
#error "ACLK_VERSION_MAX must be >= than ACLK_VERSION_MIN"
#endif

// Define ACLK Feature Version Boundaries Here
#define ACLK_V_COMPRESSION   2
#define ACLK_V_CHILDRENSTATE 3

#define ACLK_IS_HOST_INITIALIZING(host) (host->aclk_state.state == ACLK_HOST_INITIALIZING)
#define ACLK_IS_HOST_POPCORNING(host) (ACLK_IS_HOST_INITIALIZING(host) && host->aclk_state.t_last_popcorn_update)

extern struct aclk_shared_state {
    // optimization to avoid looping trough hosts
    // every time Query Thread wakes up
    RRDHOST *next_popcorn_host;

    // read only while ACLK connected
    // protect by lock otherwise
    int version_neg;
    usec_t version_neg_wait_till;
} aclk_shared_state;

typedef enum aclk_proxy_type {
    PROXY_TYPE_UNKNOWN = 0,
    PROXY_TYPE_SOCKS5,
    PROXY_TYPE_HTTP,
    PROXY_DISABLED,
    PROXY_NOT_SET,
} ACLK_PROXY_TYPE;

extern int aclk_kill_link; // Tells the agent to tear down the link
extern int aclk_disable_runtime;

const char *aclk_proxy_type_to_s(ACLK_PROXY_TYPE *type);

#define ACLK_PROXY_PROTO_ADDR_SEPARATOR "://"
#define ACLK_PROXY_ENV "env"
#define ACLK_PROXY_CONFIG_VAR "proxy"

ACLK_PROXY_TYPE aclk_verify_proxy(const char *string);
const char *aclk_lws_wss_get_proxy_setting(ACLK_PROXY_TYPE *type);
void safe_log_proxy_censor(char *proxy);
int aclk_decode_base_url(char *url, char **aclk_hostname, int *aclk_port);
const char *aclk_get_proxy(ACLK_PROXY_TYPE *type);

#endif //ACLK_COMMON_H
