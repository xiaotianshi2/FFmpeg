#include "dashenc_stats.h"

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include "libavutil/time.h"

void print_time_stats(stats_t *stats, int64_t value)
{
    int64_t avgTime;
    int64_t curr_time = av_gettime_relative();

    if (stats == NULL) {
        return;
    }

    pthread_mutex_lock(&stats->stats_lock);
    stats->nrOfSamples++;
    stats->totalTime += value;
    if (stats->maxTime < value)
        stats->maxTime = value;

    if (stats->minTime > value || stats->minTime == 0)
        stats->minTime = value;

    if (stats->lastLog == 0)
        stats->lastLog = curr_time;

    if (curr_time - stats->lastLog > stats->logInterval)
    {
        stats->lastLog = curr_time;
        avgTime = stats->totalTime / stats->nrOfSamples;

        av_log(NULL, AV_LOG_INFO, "%s (ms) min: %"PRId64", max: %"PRId64", avg: %"PRId64", time: %"PRId64"\n",
            stats->name,
            stats->minTime,
            stats->maxTime,
            avgTime,
            curr_time);

        stats->minTime = 0;
        stats->maxTime = 0;
        stats->totalTime = 0;
        stats->nrOfSamples = 0;
    }
    pthread_mutex_unlock(&stats->stats_lock);
}

stats_t *init_time_stats(const char *name, int logInterval)
{
    stats_t *stats = calloc(1, sizeof(struct _stats_t));
    stats->logInterval = logInterval;
    stats->name = name;
    pthread_mutex_init(&stats->stats_lock, NULL);
    return stats;
}

void free_time_stats(stats_t *stats)
{
    pthread_mutex_destroy(&stats->stats_lock);
}
