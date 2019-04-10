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
    int opened;
} thread_data_t;

static thread_data_t thr_data[nr_of_threads];
// static pthread_t thr[nr_of_threads];
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;


/**
 * Claims a free connection and returns the connection number
 */
static int claim_connection() {

    pthread_mutex_lock(&lock);
    av_log(NULL, AV_LOG_INFO, "-----pool------\n");
    for(int i = 0; i < nr_of_threads; i++) {
        thread_data_t *data = &thr_data[i];
        if (!data->claimed) {
            av_log(NULL, AV_LOG_INFO, "conn_id: %d claimed: %d\n", i, data->claimed);
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

    ret = s->io_open(s, &data->out, url, AVIO_FLAG_WRITE, options);
    if (ret >= 0) {
        ret = conn_nr;
        data->opened = 1;
    }


    av_log(s, AV_LOG_INFO, "conn_id: %d opened, addr: %p\n", conn_nr, data->out);
    return ret;
}

/**
 * If conn_nr is -1, we claim a new connection and start a new request.
 * The claimed connection number is returned.
 *
 * If conn_nr is 0 or higher we re-use the connection.
 * conn_nr is returned.
 */
int pool_io_open(AVFormatContext *s, char *filename,
                 AVDictionary **options, int http_persistent, int conn_nr) {
    //TODO: rewrite this thing so it just picks the first open connection and otherwise opens a new one.

    int http_base_proto = filename ? ff_is_http_proto(filename) : 0;
    int ret = AVERROR_MUXER_NOT_FOUND;
    thread_data_t *data = &thr_data[conn_nr];

    if (conn_nr == -1 || !http_base_proto || !http_persistent) {
        av_log(s, AV_LOG_INFO, "pool_io_open new: %s, old conn_nr: %d\n", filename, conn_nr);
        ret = open_request(s, filename, options);
        //open_request returns the newly claimed conn_nr
        av_log(s, AV_LOG_INFO, "pool_io_open new: %s, new conn_nr: %d, old conn_nr: %d, claimed: %d\n", filename, ret, conn_nr, data->claimed);
#if CONFIG_HTTP_PROTOCOL
    } else {
        //claim new item from pool and open connection if needed
        if (data->claimed) {
            conn_nr = claim_connection();
            data = &thr_data[conn_nr];
            if (!data->opened) {
                ret = s->io_open(s, &data->out, filename, AVIO_FLAG_WRITE, options);
                if (ret >= 0) {
                    data->opened = 1;
                    return conn_nr;
                }
            }
        }

        URLContext *http_url_context = ffio_geturlcontext(data->out);
        av_assert0(http_url_context);
        av_log(s, AV_LOG_INFO, "pool_io_open conn_nr: %d, claimed: %d, name: %s\n", conn_nr, data->claimed, filename);

        pthread_mutex_lock(&lock);
        data->claimed = 1;
        pthread_mutex_unlock(&lock);

        ret = ff_http_do_new_request(http_url_context, filename);
        if (ret < 0) {
            av_log(s, AV_LOG_INFO, "pool_io_open error conn_nr: %d, name: %s\n", conn_nr, filename);
            ff_format_io_close(s, &data->out);
        }
        ret = conn_nr;
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


static void *thr_io_close(void *arg) {
    thread_data_t *data = (thread_data_t *)arg;



    URLContext *http_url_context = ffio_geturlcontext(data->out);
    av_assert0(http_url_context);
    avio_flush(data->out);

    av_log(NULL, AV_LOG_INFO, "thr_io_close thread: %d, addr: %p \n", data->tid, data->out);

    ffurl_shutdown(http_url_context, AVIO_FLAG_WRITE);
    av_log(NULL, AV_LOG_INFO, "thr_io_close 2 thread: %d, addr: %p \n", data->tid, data->out);

    pthread_mutex_lock(&lock);
    data->claimed = 0;
    pthread_mutex_unlock(&lock);

    av_log(NULL, AV_LOG_INFO, "conn_id: %d released, addr: %p \n", data->tid, data->out);

    pthread_exit(NULL);
}


void pool_io_close(AVFormatContext *s, char *filename, int conn_nr) {

    thread_data_t *data = &thr_data[conn_nr];
    av_log(NULL, AV_LOG_INFO, "pool_io_close conn_nr: %d, pointer: %p, filename: %s\n", conn_nr, data->out, filename);

    if(pthread_create(&data->thread, NULL, thr_io_close, &thr_data[conn_nr])) {
        fprintf(stderr, "Error creating close thread for conn_nr: %d\n", conn_nr);
        return -1;
    }

    //pthread_join( data->thread, NULL);
}

void pool_free(AVFormatContext *s, int conn_nr) {
     thread_data_t *data = &thr_data[conn_nr];
     av_log(NULL, AV_LOG_INFO, "pool_free conn_nr: %d\n", conn_nr);

     ff_format_io_close(s, data->out);
}

void pool_free_all(AVFormatContext *s) {

    av_log(NULL, AV_LOG_INFO, "pool_free_all do not use the pools after this\n");
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

void pool_init() {
    if (pthread_mutex_init(&lock, NULL) != 0)
    {
        printf("\n mutex init failed\n");
        return 1;
    }
}