#ifndef NETDATA_CONFIG_H
#define NETDATA_CONFIG_H 1

#define CONFIG_FILENAME "netdata.conf"

// these are used to limit the configuration names and values lengths
// they are not enforced by config.c functions (they will strdup() all strings, no matter of their length)
#define CONFIG_MAX_NAME 1024
#define CONFIG_MAX_VALUE 2048

struct config {
    struct section *sections;
    pthread_mutex_t mutex;
    avl_tree_lock index;
};

extern struct config
        netdata_config,
        stream_config;

#define CONFIG_BOOLEAN_NO   0
#define CONFIG_BOOLEAN_YES  1
#define CONFIG_BOOLEAN_AUTO 2

extern int appconfig_load(struct config *root, char *filename, int overwrite_used);

extern char *appconfig_get(struct config *root, const char *section, const char *name, const char *default_value);
extern long long appconfig_get_number(struct config *root, const char *section, const char *name, long long value);
extern int appconfig_get_boolean(struct config *root, const char *section, const char *name, int value);
extern int appconfig_get_boolean_ondemand(struct config *root, const char *section, const char *name, int value);

extern const char *appconfig_set(struct config *root, const char *section, const char *name, const char *value);
extern const char *appconfig_set_default(struct config *root, const char *section, const char *name, const char *value);
extern long long appconfig_set_number(struct config *root, const char *section, const char *name, long long value);
extern int appconfig_set_boolean(struct config *root, const char *section, const char *name, int value);

extern int appconfig_exists(struct config *root, const char *section, const char *name);
extern int appconfig_rename(struct config *root, const char *section, const char *old, const char *new);

extern void appconfig_generate(struct config *root, BUFFER *wb, int only_changed);

// ----------------------------------------------------------------------------
// shortcuts for the default netdata configuration

#define config_load(filename, overwrite_used) appconfig_load(&netdata_config, filename, overwrite_used)
#define config_get(section, name, default_value) appconfig_get(&netdata_config, section, name, default_value)
#define config_get_number(section, name, value) appconfig_get_number(&netdata_config, section, name, value)
#define config_get_boolean(section, name, value) appconfig_get_boolean(&netdata_config, section, name, value)
#define config_get_boolean_ondemand(section, name, value) appconfig_get_boolean_ondemand(&netdata_config, section, name, value)

#define config_set(section, name, default_value) appconfig_get(&netdata_config, section, name, default_value)
#define config_set_default(section, name, value) appconfig_set_default(&netdata_config, section, name, value)
#define config_set_number(section, name, value) appconfig_set_number(&netdata_config, section, name, value)
#define config_set_boolean(section, name, value) appconfig_set_boolean(&netdata_config, section, name, value)

#define config_exists(section, name) appconfig_exists(&netdata_config, section, name)
#define config_rename(section, old, new) appconfig_rename(&netdata_config, section, old, new)

#define config_generate(buffer, only_changed) appconfig_generate(&netdata_config, buffer, only_changed)

#endif /* NETDATA_CONFIG_H */
