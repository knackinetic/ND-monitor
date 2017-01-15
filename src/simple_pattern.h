#ifndef NETDATA_SIMPLE_PATTERN_H
#define NETDATA_SIMPLE_PATTERN_H

typedef enum {
    SIMPLE_PATTERN_EXACT,
    SIMPLE_PATTERN_PREFIX,
    SIMPLE_PATTERN_SUFFIX,
    SIMPLE_PATTERN_SUBSTRING
} SIMPLE_PREFIX_MODE;

typedef void SIMPLE_PATTERN;

// create a simple_pattern from the string given
// default_mode is used in cases where EXACT matches, without an asterisk,
// should be considered PREFIX matches.
extern SIMPLE_PATTERN *simple_pattern_create(const char *list, SIMPLE_PREFIX_MODE default_mode);

// test if string str is matched from the pattern
extern int simple_pattern_matches(SIMPLE_PATTERN *list, const char *str);

// free a simple_pattern that was created with simple_pattern_create()
// list can be NULL, in which case, this does nothing.
extern void simple_pattern_free(SIMPLE_PATTERN *list);

#endif //NETDATA_SIMPLE_PATTERN_H
