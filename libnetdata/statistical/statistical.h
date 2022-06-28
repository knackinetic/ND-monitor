// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef NETDATA_STATISTICAL_H
#define NETDATA_STATISTICAL_H 1

#include "../libnetdata.h"

extern void log_series_to_stderr(NETDATA_DOUBLE *series, size_t entries, NETDATA_DOUBLE result, const char *msg);

extern NETDATA_DOUBLE average(const NETDATA_DOUBLE *series, size_t entries);
extern NETDATA_DOUBLE moving_average(const NETDATA_DOUBLE *series, size_t entries, size_t period);
extern NETDATA_DOUBLE median(const NETDATA_DOUBLE *series, size_t entries);
extern NETDATA_DOUBLE moving_median(const NETDATA_DOUBLE *series, size_t entries, size_t period);
extern NETDATA_DOUBLE running_median_estimate(const NETDATA_DOUBLE *series, size_t entries);
extern NETDATA_DOUBLE standard_deviation(const NETDATA_DOUBLE *series, size_t entries);
extern NETDATA_DOUBLE single_exponential_smoothing(const NETDATA_DOUBLE *series, size_t entries, NETDATA_DOUBLE alpha);
extern NETDATA_DOUBLE
single_exponential_smoothing_reverse(const NETDATA_DOUBLE *series, size_t entries, NETDATA_DOUBLE alpha);
extern NETDATA_DOUBLE double_exponential_smoothing(const NETDATA_DOUBLE *series, size_t entries,
    NETDATA_DOUBLE alpha,
    NETDATA_DOUBLE beta,
    NETDATA_DOUBLE *forecast);
extern NETDATA_DOUBLE holtwinters(const NETDATA_DOUBLE *series, size_t entries,
    NETDATA_DOUBLE alpha,
    NETDATA_DOUBLE beta,
    NETDATA_DOUBLE gamma,
    NETDATA_DOUBLE *forecast);
extern NETDATA_DOUBLE sum_and_count(const NETDATA_DOUBLE *series, size_t entries, size_t *count);
extern NETDATA_DOUBLE sum(const NETDATA_DOUBLE *series, size_t entries);
extern NETDATA_DOUBLE median_on_sorted_series(const NETDATA_DOUBLE *series, size_t entries);
extern NETDATA_DOUBLE *copy_series(const NETDATA_DOUBLE *series, size_t entries);
extern void sort_series(NETDATA_DOUBLE *series, size_t entries);

#endif //NETDATA_STATISTICAL_H
