#include "dashenc_http.h"

#include "config.h"
#include "libavutil/avutil.h"
#include <pthread.h>

#include "avio_internal.h"
#include "utils.c"
#if CONFIG_HTTP_PROTOCOL
#include "http.h"
#endif

#define nr_of_threads 20

typedef struct _thread_data_t {
    int tid;
    pthread_t thread;
    AVIOContext *out;
    int claimed;
} thread_data_t;

static thread_data_t thr_data[nr_of_threads];
static pthread_t thr[nr_of_threads];


/**
 * Claims a free connection and returns the connection number
 */
static int claim_connection() {
    //TODO: lock

    for(int i = 0; i < nr_of_threads; i++) {
        thread_data_t *data = &thr_data[i];
        if (!data->claimed) {
            data->claimed = 1;
            return i;
        }
    }

    return -1;
}

/**
 * Opens a request on a free connection and returns the connection number
 */
static int open_request(AVFormatContext *s, char *url, AVDictionary **options) {
    int ret;
    int conn_nr = claim_connection();
    thread_data_t *data = &thr_data[conn_nr];

    ret = s->io_open(s, &data->out, url, AVIO_FLAG_WRITE, options);
    if (ret >= 0)
        ret = conn_nr;

    av_log(s, AV_LOG_INFO, "3: addr: %p\n", data->out);
    return ret;
}

/**
 * If conn_nr is -1, we claim a new connection and start a new request. The claimed connection number is returned.
 */
int pool_io_open(AVFormatContext *s, char *filename,
                 AVDictionary **options, int http_persistent, int conn_nr) {
    int http_base_proto = filename ? ff_is_http_proto(filename) : 0;
    int ret = AVERROR_MUXER_NOT_FOUND;
    thread_data_t *data = &thr_data[conn_nr];

    if (conn_nr == -1 || !http_base_proto || !http_persistent) {
        av_log(NULL, AV_LOG_INFO, "io open-if: %s\n", filename);
        ret = open_request(s, filename, options);
        //open_request returns the newly claimed conn_nr
#if CONFIG_HTTP_PROTOCOL
    } else {

        URLContext *http_url_context = ffio_geturlcontext(data->out);
        av_assert0(http_url_context);
        av_log(NULL, AV_LOG_INFO, "pool_io_open conn_nr: %d, name: %s\n", conn_nr, filename);
        ret = ff_http_do_new_request(http_url_context, filename);
        if (ret < 0) {
            av_log(NULL, AV_LOG_INFO, "pool_io_open error conn_nr: %d, name: %s\n", conn_nr, filename);
            ff_format_io_close(s, &data->out);
        }
#endif
    }

    return ret;
}

int pool_open(struct AVFormatContext *s, const char *url,
                   int flags, AVDictionary **opts) {
    int ret;
    int conn_nr;


    conn_nr = open_request(s, url, opts);
    if (conn_nr < 0) {
        av_log(NULL, AV_LOG_INFO, "pool_open io error conn_nr: %d, name: %s\n", conn_nr, url);
        return conn_nr;
    }

    av_log(NULL, AV_LOG_INFO, "pool_open conn_nr: %d, name: %s\n", conn_nr, url);

    return conn_nr;
}


void pool_io_close(AVFormatContext *s, char *filename, int conn_nr) {
    //TODO: lock
    thread_data_t *data = &thr_data[conn_nr];
    av_log(NULL, AV_LOG_INFO, "pool_io_close conn_nr: %d\n", conn_nr);
    printf("pool_close data.out pointer: %p \n", data->out);

    URLContext *http_url_context = ffio_geturlcontext(data->out);
    av_assert0(http_url_context);
    avio_flush(data->out);

    ffurl_shutdown(http_url_context, AVIO_FLAG_WRITE);
    data->claimed = 0;
}

void pool_free(AVFormatContext *s, int conn_nr) {
     thread_data_t *data = &thr_data[conn_nr];
     av_log(NULL, AV_LOG_INFO, "pool_free conn_nr: %d\n", conn_nr);

     ff_format_io_close(s, data->out);
}

void pool_free_all(AVFormatContext *s) {
    //TODO: lock

     for(int i = 0; i < nr_of_threads; i++) {
        thread_data_t *data = &thr_data[i];
        if (data->out)
            pool_free(s, i);
     }
}

void pool_write_flush(const unsigned char *buf, int size, int conn_nr) {
    thread_data_t *data = &thr_data[conn_nr];

    avio_write(data->out, buf, size);
    avio_flush(data->out);
}


void pool_avio_write(const unsigned char *buf, int size, int conn_nr)
{
    thread_data_t *data = &thr_data[conn_nr];

    av_log(NULL, AV_LOG_INFO, "pool_avio_write conn_nr: %d, data.out pointer: %p \n", conn_nr, data->out);

    if (data->out)
        avio_write(data->out, buf, size);
}

/**
 * Set out to the AVIOContext of the given conn_nr
 */
void pool_get_context(AVIOContext **out, int conn_nr) {
    //TODO we probably don't want to expose the AVIOContext
    thread_data_t *data = &thr_data[conn_nr];
    *out = data->out;
}