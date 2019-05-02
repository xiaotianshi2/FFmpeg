#include "dashenc_http.h"

#include "config.h"
#include "libavutil/avutil.h"
#include <pthread.h>

#include "avio_internal.h"
#include "utils.c"
#if CONFIG_HTTP_PROTOCOL
#include "http.h"
#endif

//TODO don't hardcode the max number of threads
#define nr_of_threads 20

typedef struct _thread_data_t {
    int tid;
    pthread_t thread;
    AVIOContext *out;
    int claimed;
    int opened;
} thread_data_t;

static thread_data_t thr_data[nr_of_threads];
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;


/**
 * Claims a free connection and returns the connection number
 */
static int claim_connection(void) {
    pthread_mutex_lock(&lock);
    for(int i = 0; i < nr_of_threads; i++) {
        thread_data_t *data = &thr_data[i];
        if (!data->claimed) {
            av_log(NULL, AV_LOG_DEBUG, "conn_id: %d claimed: %d\n", i, data->claimed);
            data->claimed = 1;
            data->tid = i;
            pthread_mutex_unlock(&lock);
            return i;
        }
    }

    pthread_mutex_unlock(&lock);

    return -1;
}

/**
 * Opens a request on a free connection and returns the connection number
 */
static int open_request(AVFormatContext *s, char *url, AVDictionary **options) {
    int ret;
    int conn_nr = claim_connection();
    thread_data_t *data = &thr_data[conn_nr];

    if (data->opened)
        av_log(s, AV_LOG_WARNING, "open_request while connection might be open. This is TODO for when not using persistent connections. conn_nr: %d\n", conn_nr);

    ret = s->io_open(s, &data->out, url, AVIO_FLAG_WRITE, options);
    if (ret >= 0) {
        ret = conn_nr;
        data->opened = 1;
    }

    return ret;
}

/**
 * Claim a connection and start a new request.
 * The claimed connection number is returned.
 */
int pool_io_open(AVFormatContext *s, char *filename,
                 AVDictionary **options, int http_persistent) {

    int http_base_proto = filename ? ff_is_http_proto(filename) : 0;
    int ret = AVERROR_MUXER_NOT_FOUND;

    if (!http_base_proto || !http_persistent) {
        //open_request returns the newly claimed conn_nr
        ret = open_request(s, filename, options);

#if CONFIG_HTTP_PROTOCOL
    } else {
        thread_data_t *data;
        URLContext *http_url_context;
        //claim new item from pool and open connection if needed
        int conn_nr = claim_connection();
        data = &thr_data[conn_nr];
        if (!data->opened) {
            ret = s->io_open(s, &data->out, filename, AVIO_FLAG_WRITE, options);
            if (ret < 0) {
                av_log(s, AV_LOG_WARNING, "Could not open the connection to %s\n", filename);
                return ret;
            }

            pthread_mutex_lock(&lock);
            data->opened = 1;
            pthread_mutex_unlock(&lock);
            return conn_nr;
        }

        http_url_context = ffio_geturlcontext(data->out);
        av_assert0(http_url_context);

        ret = ff_http_do_new_request(http_url_context, filename);
        if (ret < 0) {
            av_log(s, AV_LOG_WARNING, "pool_io_open error conn_nr: %d, error: %d, name: %s\n", conn_nr, ret, filename);
            ff_format_io_close(s, &data->out);
        }
        ret = conn_nr;
#endif
    }

    return ret;
}

static void *thr_io_close(void *arg) {
    thread_data_t *data = (thread_data_t *)arg;
    int ret;

    URLContext *http_url_context = ffio_geturlcontext(data->out);
    av_assert0(http_url_context);
    avio_flush(data->out);

    av_log(NULL, AV_LOG_DEBUG, "thr_io_close thread: %d, addr: %p \n", data->tid, data->out);

    ret = ffurl_shutdown(http_url_context, AVIO_FLAG_WRITE);
    pthread_mutex_lock(&lock);
    if (ret < 0) {
        //TODO: do we need to do some cleanup if ffurl_shutdown fails?
        av_log(NULL, AV_LOG_INFO, "-event- request failed: %d, url: %s.\n", ret, http_url_context->filename);
        data->opened = 0;
    }

    data->claimed = 0;
    pthread_mutex_unlock(&lock);

    pthread_exit(NULL);
}


void pool_io_close(AVFormatContext *s, char *filename, int conn_nr) {
    thread_data_t *data = &thr_data[conn_nr];

    av_log(NULL, AV_LOG_DEBUG, "pool_io_close conn_nr: %d\n", conn_nr);

    if(pthread_create(&data->thread, NULL, thr_io_close, &thr_data[conn_nr])) {
        av_log(NULL, AV_LOG_ERROR, "Error creating close thread for conn_nr: %d\n", conn_nr);
        return;
    }
}

void pool_free(AVFormatContext *s, int conn_nr) {
     thread_data_t *data = &thr_data[conn_nr];
     av_log(NULL, AV_LOG_DEBUG, "pool_free conn_nr: %d\n", conn_nr);

     ff_format_io_close(s, &data->out);
}

void pool_free_all(AVFormatContext *s) {
    av_log(NULL, AV_LOG_DEBUG, "pool_free_all\n");
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

void pool_avio_write(const unsigned char *buf, int size, int conn_nr) {
    thread_data_t *data = &thr_data[conn_nr];

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
