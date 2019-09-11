#ifndef AVFORMAT_DASH_STATS_H
#define AVFORMAT_DASH_STATS_H

#include <pthread.h>
#include "avformat.h"

typedef struct stats_t {
    int64_t lastLog;
    int64_t maxTime;
    int64_t minTime;
    int64_t totalTime;
    int64_t nrOfSamples;
    int logInterval;
    const char *name;
    pthread_mutex_t stats_lock;
} stats_t;

void print_time_stats(stats_t *stats, int64_t value);
stats_t *init_time_stats(const char *name, int logInterval);
void free_time_stats(stats_t *stats);

#endif /* AVFORMAT_DASH_HTTP_H */