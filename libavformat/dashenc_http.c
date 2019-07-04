#include "dashenc_http.h"

#include "config.h"
#include "libavutil/avutil.h"
#include <pthread.h>

#include "avio_internal.h"
#include "utils.c"
#include "dashenc_pool.h"
#if CONFIG_HTTP_PROTOCOL
#include "http.h"
#endif

//TODO don't hardcode the max number of connections
#define nr_of_connections 20

typedef struct _connection_t {
    int nr;
    AVIOContext *out;
    int claimed; /* This connection is claimed for a specific request */
    int opened;  /* out is opened */
    int64_t release_time;

    //Request specific conn
    int must_succeed; /* If 1 the request must succeed, otherwise we'll crash the program */
} connection_t;

static connection_t connections[nr_of_connections];
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static void *thread_pool;


/**
 * Claims a free connection and returns the connection number.
 * Released connections are used first.
 */
static int claim_connection(char *url) {
    int64_t lowest_release_time = av_gettime() / 1000;
    int conn_nr = -1;
    pthread_mutex_lock(&lock);

    for(int i = 0; i < nr_of_connections; i++) {
        connection_t *conn = &connections[i];
        if (!conn->claimed) {
            if ((conn_nr == -1) || (conn->release_time != 0 && conn->release_time < lowest_release_time)) {
                conn_nr = i;
                lowest_release_time = conn->release_time;
            }
        }
    }

    if (conn_nr == -1) {
        av_log(NULL, AV_LOG_ERROR, "Could not claim connection for url: %s\n", url);
        pthread_mutex_unlock(&lock);
        return -1;
    }

    av_log(NULL, AV_LOG_INFO, "Claimed conn_id: %d, url: %s\n", conn_nr, url);
    connections[conn_nr].claimed = 1;
    connections[conn_nr].nr = conn_nr;
    pthread_mutex_unlock(&lock);
    return conn_nr;
}

/**
 * Opens a request on a free connection and returns the connection number
 */
static int open_request(AVFormatContext *s, char *url, AVDictionary **options) {
    int ret;
    int conn_nr = claim_connection(url);
    connection_t *conn = &connections[conn_nr];

    if (conn->opened)
        av_log(s, AV_LOG_WARNING, "open_request while connection might be open. This is TODO for when not using persistent connections. conn_nr: %d\n", conn_nr);

    ret = s->io_open(s, &conn->out, url, AVIO_FLAG_WRITE, options);
    if (ret >= 0) {
        ret = conn_nr;
        conn->opened = 1;
    }

    return ret;
}

static void force_release_connection(connection_t *conn) {
    pthread_mutex_lock(&lock);
    conn->opened = 0;
    conn->claimed = 0;
    pthread_mutex_unlock(&lock);
}

static void abort_if_needed(int mustSucceed) {
    if (mustSucceed) {
        av_log(NULL, AV_LOG_ERROR, "Abort because request needs to succeed and it did not.\n");
        abort();
    }
}

/**
 * Claim a connection and start a new request.
 * The claimed connection number is returned.
 */
int pool_io_open(AVFormatContext *s, char *filename,
                 AVDictionary **options, int http_persistent, int must_succeed) {

    int http_base_proto = filename ? ff_is_http_proto(filename) : 0;
    int ret = AVERROR_MUXER_NOT_FOUND;

    if (!http_base_proto || !http_persistent) {
        //open_request returns the newly claimed conn_nr
        ret = open_request(s, filename, options);
        av_log(s, AV_LOG_WARNING, "Non HTTP request %s\n", filename);
#if CONFIG_HTTP_PROTOCOL
    } else {
        connection_t *conn;
        URLContext *http_url_context;
        //claim new item from pool and open connection if needed
        int conn_nr = claim_connection(filename);
        //Crash when we cannot claim a new connection. We should restart ffmpeg in this case.
        av_assert0(conn_nr >= 0);

        conn = &connections[conn_nr];
        conn->must_succeed = must_succeed;
        if (!conn->opened) {
            ret = s->io_open(s, &conn->out, filename, AVIO_FLAG_WRITE, options);
            if (ret < 0) {
                av_log(s, AV_LOG_WARNING, "Could not open %s\n", filename);
                force_release_connection(conn);
                abort_if_needed(must_succeed);
                return ret;
            }

            pthread_mutex_lock(&lock);
            conn->opened = 1;
            pthread_mutex_unlock(&lock);
            return conn_nr;
        }

        http_url_context = ffio_geturlcontext(conn->out);
        av_assert0(http_url_context);

        ret = ff_http_do_new_request(http_url_context, filename);
        if (ret < 0) {
            int64_t curr_time_ms = av_gettime() / 1000;
            int64_t idle_tims_ms = curr_time_ms - conn->release_time;
            av_log(s, AV_LOG_WARNING, "pool_io_open error conn_nr: %d, idle_time: %"PRId64", error: %d, name: %s\n", conn_nr, idle_tims_ms, ret, filename);
            ff_format_io_close(s, &conn->out);
            force_release_connection(conn);
            abort_if_needed(must_succeed);
            return ret;
        }
        ret = conn_nr;
#endif
    }

    return ret;
}

/**
 * This method closes the request and reads the response.
 * It is supposed to be passed to pthread_create.
 */
static void *thr_io_close(void *arg) {
    connection_t *conn = (connection_t *)arg;
    int ret;
    int64_t release_time = av_gettime() / 1000;

    URLContext *http_url_context = ffio_geturlcontext(conn->out);
    av_assert0(http_url_context);
    avio_flush(conn->out);

    av_log(NULL, AV_LOG_DEBUG, "thr_io_close thread: %d, addr: %p \n", conn->nr, conn->out);

    ret = ffurl_shutdown(http_url_context, AVIO_FLAG_WRITE);
    pthread_mutex_lock(&lock);
    if (ret < 0) {
        //TODO: do we need to do some cleanup if ffurl_shutdown fails?
        av_log(NULL, AV_LOG_INFO, "-event- request failed ret=%d, conn_nr: %d, url: %s.\n", ret, conn->nr, ff_http_get_url(http_url_context));
        abort_if_needed(conn->must_succeed);
        conn->opened = 0;
    }

    conn->claimed = 0;
    conn->release_time = release_time;
    pthread_mutex_unlock(&lock);

    return NULL;
}

/**
 * Closes the request.
 */
void pool_io_close(AVFormatContext *s, char *filename, int conn_nr) {
    connection_t *conn;

    if (conn_nr < 0) {
        av_log(s, AV_LOG_WARNING, "Invalid conn_nr (pool_io_close) for filename: %s\n", filename);
        return;
    }

    conn = &connections[conn_nr];
    av_log(NULL, AV_LOG_DEBUG, "pool_io_close conn_nr: %d\n", conn_nr);

    if (!conn->opened) {
        av_log(s, AV_LOG_INFO, "Skip closing HTTP request because connection is not opened. Filename: %s\n", filename);
        abort_if_needed(conn->must_succeed);
        return;
    }

    pool_enqueue(thread_pool, &connections[conn_nr], 0);
}

void pool_free(AVFormatContext *s, int conn_nr) {
    connection_t *conn;

    if (conn_nr < 0) {
        av_log(s, AV_LOG_WARNING, "Invalid conn_nr (pool_free)\n");
        return;
    }

    conn = &connections[conn_nr];
    av_log(NULL, AV_LOG_DEBUG, "pool_free conn_nr: %d\n", conn_nr);

    ff_format_io_close(s, &conn->out);
    force_release_connection(conn);
}

void pool_free_all(AVFormatContext *s) {
    connection_t *conn;

    av_log(NULL, AV_LOG_DEBUG, "pool_free_all\n");
    for(int i = 0; i < nr_of_connections; i++) {
        conn = &connections[i];
        if (conn->out)
            pool_free(s, i);
    }
}

void pool_write_flush(const unsigned char *buf, int size, int conn_nr) {
    connection_t *conn;

    if (conn_nr < 0) {
        av_log(NULL, AV_LOG_WARNING, "Invalid conn_nr (pool_write_flush)\n");
        return;
    }

    conn = &connections[conn_nr];

    avio_write(conn->out, buf, size);
    avio_flush(conn->out);
}

int pool_avio_write(const unsigned char *buf, int size, int conn_nr) {
    connection_t *conn;

    if (conn_nr < 0) {
        av_log(NULL, AV_LOG_WARNING, "Invalid conn_nr (pool_avio_write)\n");
        return -1;
    }
    conn = &connections[conn_nr];

    if (conn->out)
        avio_write(conn->out, buf, size);

    return 0;
}

/**
 * Set out to the AVIOContext of the given conn_nr
 */
void pool_get_context(AVIOContext **out, int conn_nr) {
    //TODO we probably don't want to expose the AVIOContext
    connection_t *conn;

    if (conn_nr < 0) {
        av_log(NULL, AV_LOG_WARNING, "Invalid conn_nr (pool_get_context)\n");
        return;
    }
    conn = &connections[conn_nr];
    *out = conn->out;
}

void pool_init() {
    thread_pool = pool_start(thr_io_close, 20);
}