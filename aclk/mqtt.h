// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef NETDATA_MQTT_H
#define NETDATA_MQTT_H

#ifdef ENABLE_ACLK
#include "externaldeps/mosquitto/mosquitto.h"
#endif

void _show_mqtt_info();
int _link_event_loop(int timeout);
void _link_shutdown();
int _link_lib_init(char *aclk_hostname, int aclk_port, void (*on_connect)(void *), void (*on_disconnect)(void *));
int _link_subscribe(char *topic, int qos);
int _link_send_message(char *topic, char *message, int *mid);
const char *_link_strerror(int rc);

int aclk_handle_cloud_request(char *);

extern int aclk_connection_initialized;
extern int aclk_mqtt_connected;
extern char *aclk_hostname;
extern int aclk_port;

#endif //NETDATA_MQTT_H
