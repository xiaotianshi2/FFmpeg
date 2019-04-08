#include "dashenc_http.h"

#include "libavutil/avutil.h"
#include <pthread.h>

#include "avio_internal.h"
#include "utils.c"
#include "http.h"

#define nr_of_threads 20

typedef struct _thread_data_t {
    int tid;
    pthread_t thread;
    AVIOContext *out;
} thread_data_t;

static thread_data_t thr_data[nr_of_threads];
static pthread_t thr[nr_of_threads];


int pool_io_open(AVFormatContext *s, char *filename,
                           AVDictionary **options, int conn_nr) {
    int ret;

    thread_data_t *data = &thr_data[conn_nr];

    URLContext *http_url_context = ffio_geturlcontext(data->out);
    av_assert0(http_url_context);
    av_log(NULL, AV_LOG_INFO, "pool_io_open conn_nr: %d, name: %s\n", conn_nr, filename);
    ret = ff_http_do_new_request(http_url_context, filename);
    if (ret < 0) {
        av_log(NULL, AV_LOG_INFO, "pool_io_open error conn_nr: %d, name: %s\n", conn_nr, filename);
        ff_format_io_close(s, &data->out);
    }


    return ret;
}

int pool_open(struct AVFormatContext *s, const char *url,
                   int flags, AVDictionary **opts) {
    int ret;
    int conn_nr;


    //TODO: lock
    for(int i = 0; i < nr_of_threads; i++) {
        thread_data_t *data = &thr_data[i];
        if (!data->out) {
            conn_nr = i;
            break;
        }
    }

    thread_data_t *data = &thr_data[conn_nr];
    ret = s->io_open(s, &data->out, url, AVIO_FLAG_WRITE, opts);
    if (ret < 0) {
        av_log(NULL, AV_LOG_INFO, "pool_open io error conn_nr: %d, name: %s\n", conn_nr, url);
        return ret;
    }

    printf("pool_open data.out pointer: %p \n", data->out);


    av_log(NULL, AV_LOG_INFO, "pool_open conn_nr: %d, name: %s\n", conn_nr, url);

    return conn_nr;
}


void pool_io_close(AVFormatContext *s, char *filename, int conn_nr) {
    thread_data_t *data = &thr_data[conn_nr];
    av_log(NULL, AV_LOG_INFO, "pool_io_close conn_nr: %d\n", conn_nr);
    printf("pool_close data.out pointer: %p \n", data->out);

    URLContext *http_url_context = ffio_geturlcontext(data->out);
    av_assert0(http_url_context);
    avio_flush(data->out);

    ffurl_shutdown(http_url_context, AVIO_FLAG_WRITE);
}

void pool_free(AVFormatContext *s, int conn_nr) {
     thread_data_t *data = &thr_data[conn_nr];
     av_log(NULL, AV_LOG_INFO, "pool_free conn_nr: %d\n", conn_nr);

     ff_format_io_close(s, data->out);
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
