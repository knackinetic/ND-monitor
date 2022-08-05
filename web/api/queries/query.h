// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef NETDATA_API_DATA_QUERY_H
#define NETDATA_API_DATA_QUERY_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum rrdr_grouping {
    RRDR_GROUPING_UNDEFINED = 0,
    RRDR_GROUPING_AVERAGE,
    RRDR_GROUPING_MIN,
    RRDR_GROUPING_MAX,
    RRDR_GROUPING_SUM,
    RRDR_GROUPING_INCREMENTAL_SUM,
    RRDR_GROUPING_TRIMMED_MEAN1,
    RRDR_GROUPING_TRIMMED_MEAN2,
    RRDR_GROUPING_TRIMMED_MEAN3,
    RRDR_GROUPING_TRIMMED_MEAN5,
    RRDR_GROUPING_TRIMMED_MEAN10,
    RRDR_GROUPING_TRIMMED_MEAN15,
    RRDR_GROUPING_TRIMMED_MEAN20,
    RRDR_GROUPING_TRIMMED_MEAN25,
    RRDR_GROUPING_MEDIAN,
    RRDR_GROUPING_TRIMMED_MEDIAN1,
    RRDR_GROUPING_TRIMMED_MEDIAN2,
    RRDR_GROUPING_TRIMMED_MEDIAN3,
    RRDR_GROUPING_TRIMMED_MEDIAN5,
    RRDR_GROUPING_TRIMMED_MEDIAN10,
    RRDR_GROUPING_TRIMMED_MEDIAN15,
    RRDR_GROUPING_TRIMMED_MEDIAN20,
    RRDR_GROUPING_TRIMMED_MEDIAN25,
    RRDR_GROUPING_PERCENTILE25,
    RRDR_GROUPING_PERCENTILE50,
    RRDR_GROUPING_PERCENTILE75,
    RRDR_GROUPING_PERCENTILE80,
    RRDR_GROUPING_PERCENTILE90,
    RRDR_GROUPING_PERCENTILE95,
    RRDR_GROUPING_PERCENTILE97,
    RRDR_GROUPING_PERCENTILE98,
    RRDR_GROUPING_PERCENTILE99,
    RRDR_GROUPING_STDDEV,
    RRDR_GROUPING_CV,
    RRDR_GROUPING_SES,
    RRDR_GROUPING_DES,
    RRDR_GROUPING_COUNTIF,
} RRDR_GROUPING;

extern const char *group_method2string(RRDR_GROUPING group);
extern void web_client_api_v1_init_grouping(void);
extern RRDR_GROUPING web_client_api_request_v1_data_group(const char *name, RRDR_GROUPING def);
extern const char *web_client_api_request_v1_data_group_to_string(RRDR_GROUPING group);

#ifdef __cplusplus
}
#endif

#endif //NETDATA_API_DATA_QUERY_H
