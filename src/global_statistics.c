#include "common.h"

struct global_statistics global_statistics = { 0, 0ULL, 0ULL, 0ULL, 0ULL, 0ULL, 0ULL};

pthread_mutex_t global_statistics_mutex = PTHREAD_MUTEX_INITIALIZER;

void global_statistics_lock(void) {
	pthread_mutex_lock(&global_statistics_mutex);
}

void global_statistics_unlock(void) {
	pthread_mutex_unlock(&global_statistics_mutex);
}
