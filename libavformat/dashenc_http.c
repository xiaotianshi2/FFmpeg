#include "dashenc_http.h"

#include "config.h"
#include "libavutil/avutil.h"
#include <pthread.h>

#include "avio_internal.h"
#include "utils.c"
#include "dashenc_pool.h"
#include "dashenc_stats.h"
#if CONFIG_HTTP_PROTOCOL
#include "http.h"
#endif

typedef struct _chunk_t {
    unsigned char *buf;
    int size;
    int nr;
} chunk_t;

typedef struct _connection_t {
    int nr;               /* Number of the connection, used to lookup this connection in the connections list */
    AVIOContext *out;     /* The TCP connection */
    int claimed;          /* This connection is claimed for a specific request */
    int opened;           /* TCP connection (out) is opened */
    int opened_error;     /* If 1 the connection could not be opened */
    int64_t release_time; /* Time the last request of the connection has finished */
    AVFormatContext *s;   /* Used to clean up the TCP connection if closing of a request fails */
    pthread_t w_thread;   /* Thread that is used to write the chunks */
    pthread_mutex_t count_mutex;
    pthread_cond_t count_threshold_cv;

    //Request specific data
    int must_succeed;       /* If 1 the request must succeed, otherwise we'll crash the program */
    int retry;              /* If 1 the request can be retried */
    int retry_nr;           /* Current retry number, used to limit the nr of retries */
    char *url;              /* url of the current request */
    AVDictionary *options;
    int http_persistent;
    chunk_t **chunks_ptr;   /* An array with pointers to chunks */
    int nr_of_chunks;       /* Nr of chunks available, guarded by count_mutex */
    int chunks_done;        /* Are all chunks for this request available in the buffer */
    int last_chunk_written; /* Last chunk number that has been written */
} connection_t;

static connection_t **connections = NULL; /* an array with pointers to connections */
static int nr_of_connections = 0;
static pthread_mutex_t connections_lock = PTHREAD_MUTEX_INITIALIZER;
static stats_t *time_stats;


/* This method expects the lock to be already done.*/
static void release_request(connection_t *conn) {
    int64_t release_time = av_gettime() / 1000;
    if (conn->claimed) {
        free(conn->url);
        pthread_mutex_lock(&conn->count_mutex);
        for(int i = 0; i < conn->nr_of_chunks; i++) {
            chunk_t *chunk = conn->chunks_ptr[i];
            free(chunk->buf);
        }
        av_freep(&conn->chunks_ptr);
        av_dict_free(&conn->options);
        pthread_mutex_unlock(&conn->count_mutex);
    }
    conn->claimed = 0;
    conn->release_time = release_time;
    conn->nr_of_chunks = 0;
    conn->chunks_done = 0;
    conn->retry_nr = 0;
    conn->opened_error = 0;
    conn->last_chunk_written = 0;
}

static void abort_if_needed(int mustSucceed) {
    if (mustSucceed) {
        av_log(NULL, AV_LOG_ERROR, "Abort because request needs to succeed and it did not.\n");
        abort();
    }
}

static void force_release_connection(connection_t *conn) {
    pthread_mutex_lock(&connections_lock);
    conn->opened = 0;
    release_request(conn);
    pthread_mutex_unlock(&connections_lock);
}

static void write_chunk(connection_t *conn, int chunk_nr) {
    int64_t start_time_ms;
    int64_t write_time_ms;
    int64_t flush_time_ms;
    int64_t after_write_time_ms;
    chunk_t *chunk;

    pthread_mutex_lock(&conn->count_mutex);
    chunk = conn->chunks_ptr[chunk_nr];
    pthread_mutex_unlock(&conn->count_mutex);

    start_time_ms = av_gettime() / 1000;
    if (chunk_nr > 282 || chunk->size > 50000) {
        av_log(NULL, AV_LOG_WARNING, "chunk issue? chunk_nr: %d, conn_nr: %d, size: %d\n", chunk_nr, conn->nr, chunk->size);
    }
    if (chunk_nr == 0) {
        av_log(NULL, AV_LOG_INFO, "first chunk chunk_nr: %d, conn_nr: %d, size: %d\n", chunk_nr, conn->nr, chunk->size);
    }

    if (chunk_nr != chunk->nr) {
        av_log(NULL, AV_LOG_ERROR, "chunk issue! chunk_nr: %d, conn_nr: %d, size: %d\n", chunk_nr, conn->nr, chunk->size);
    }

    avio_write(conn->out, chunk->buf, chunk->size);
    after_write_time_ms = av_gettime() / 1000;
    write_time_ms = after_write_time_ms - start_time_ms;
    if (write_time_ms > 100) {
        av_log(NULL, AV_LOG_WARNING, "It took %"PRId64"(ms) to write chunk %d. conn_nr: %d\n", write_time_ms, chunk_nr, conn->nr);
    }

    avio_flush(conn->out);
    flush_time_ms = av_gettime() / 1000 - after_write_time_ms;
    if (flush_time_ms > 100) {
        av_log(NULL, AV_LOG_WARNING, "It took %"PRId64"(ms) to flush chunk %d. conn_nr: %d\n", flush_time_ms, chunk_nr, conn->nr);
    }

    print_time_stats(time_stats, av_gettime() / 1000 - start_time_ms);
}

static void write_flush(const unsigned char *buf, int size, int conn_nr, int keep_thread) {
    connection_t *conn;
    chunk_t *chunk;


    if (conn_nr < 0) {
        av_log(NULL, AV_LOG_WARNING, "Invalid conn_nr (pool_write_flush): %d\n", conn_nr);
        return;
    }

    conn = connections[conn_nr];

    if (!conn->opened && !conn->opened_error) {
        av_log(NULL, AV_LOG_WARNING, "connection closed (pool_write_flush). conn_nr: %d, url: %s\n", conn_nr, conn->url);
        return;
    }

    //Save the chunk in memory
    chunk = malloc(sizeof(chunk_t));
    chunk->size = size;
    chunk->nr = conn->nr_of_chunks;
    chunk->buf = malloc(size);
    if (chunk->buf == NULL) {
        av_log(NULL, AV_LOG_WARNING, "Could not malloc (pool_write_flush)\n");
    }
    memcpy(chunk->buf, buf, size);

    pthread_mutex_lock(&conn->count_mutex);
    av_dynarray_add(&conn->chunks_ptr, &conn->nr_of_chunks, chunk);
    pthread_mutex_unlock(&conn->count_mutex);
    //av_log(NULL, AV_LOG_INFO, "(%d) claimed mem: %p, addr of first chunk: %p\n", conn->nr, chunk->buf, conn->chunks_ptr[0]->buf);

    if (conn->opened_error) {
        return;
    }

    if (keep_thread) {
        write_chunk(conn, chunk->nr);
    } else {
        pthread_mutex_lock(&conn->count_mutex);
        pthread_cond_signal(&conn->count_threshold_cv);
        pthread_mutex_unlock(&conn->count_mutex);
    }
}

/**
 * This will retry a previously failed request.
 * We assume this method is ran from one of our own threads so we can safely use usleep.
 */
static void retry(connection_t *conn) {
    int retry_conn_nr = -1;
    connection_t *retry_conn;
    int chunk_wait_timeout = 10;

    if (conn->retry_nr > 10) {
        av_log(NULL, AV_LOG_WARNING, "-event- request retry failed. Giving up. request: %s, ret=%d, attempt: %d, orig conn_nr: %d.\n",
               conn->url, retry_conn_nr, conn->retry_nr, conn->nr);
        return;
    }

    usleep(1 * 1000000);

    // Wait until all chunks are recorded
    while (!conn->chunks_done && chunk_wait_timeout > 0) {
        usleep(1 * 1000000);
        chunk_wait_timeout --;
    }
    if (!conn->chunks_done) {
        av_log(NULL, AV_LOG_ERROR, "Retry could not collect all chunks for request %s, attempt: %d, conn: %d\n", conn->url, conn->retry_nr, conn->nr);
    }

    av_log(NULL, AV_LOG_WARNING, "Starting retry for request %s, attempt: %d, conn: %d\n", conn->url, conn->retry_nr, conn->nr);
    retry_conn_nr = pool_io_open(conn->s, conn->url,
                                 &conn->options, conn->http_persistent, conn->must_succeed, 1, 1);

    if (retry_conn_nr < 0) {
        av_log(NULL, AV_LOG_WARNING, "-event- request retry failed request: %s, ret=%d, attempt: %d, orig conn_nr: %d.\n",
               conn->url, retry_conn_nr, conn->retry_nr, conn->nr);

        conn->retry_nr = conn->retry_nr + 1;
        //restart request with the same conn as we could not open the request and retry_conn is not initialized properly.
        retry(conn);
        return;
    }

    retry_conn = connections[retry_conn_nr];
    retry_conn->retry_nr = conn->retry_nr + 1;

    for(int i = 0; i < conn->nr_of_chunks; i++) {
        chunk_t *chunk;
        pthread_mutex_lock(&conn->count_mutex);
        chunk = conn->chunks_ptr[i];
        pthread_mutex_unlock(&conn->count_mutex);

        write_flush(chunk->buf, chunk->size, retry_conn_nr, 1);
    }

    pool_io_close(retry_conn->s, retry_conn->url, retry_conn_nr);
    av_log(NULL, AV_LOG_INFO, "request retry done. Request: %s, orig conn_nr: %d, new conn_nr:%d.\n", conn->url, conn->nr, retry_conn_nr);
}

/**
 * This method closes the request and reads the response.
 */
static void *thr_io_close(void *arg) {
    connection_t *conn = (connection_t *)arg;
    int ret;
    int response_code;

    //av_log(NULL, AV_LOG_INFO, "thr_io_close conn_nr: %d, out_addr: %p \n", conn->nr, conn->out);

    if (conn->opened_error) {
        ret = -1;
        response_code = 0;
    } else {
        URLContext *http_url_context = ffio_geturlcontext(conn->out);
        av_assert0(http_url_context);
        avio_flush(conn->out);

        //av_log(NULL, AV_LOG_INFO, "thr_io_close conn_nr: %d, out_addr: %p \n", conn->nr, conn->out);

        ret = ffurl_shutdown(http_url_context, AVIO_FLAG_WRITE);
        response_code = ff_http_get_code(http_url_context);
    }

    if (ret < 0 || response_code >= 500) {
        av_log(NULL, AV_LOG_INFO, "-event- request failed ret=%d, conn_nr: %d, response_code: %d, url: %s.\n", ret, conn->nr, response_code, conn->url);
        abort_if_needed(conn->must_succeed);
        ff_format_io_close(conn->s, &conn->out);
        pthread_mutex_lock(&connections_lock);
        conn->opened = 0;
        pthread_mutex_unlock(&connections_lock);

        if (conn->retry)
            retry(conn);
    }

    pthread_mutex_lock(&connections_lock);
    release_request(conn);
    pthread_mutex_unlock(&connections_lock);

    return NULL;
}

/**
 * This method writes the chunks.
 * It is supposed to be passed to pthread_create.
 */
static void *thr_io_write(void *arg) {
    connection_t *conn = (connection_t *)arg;
    //https://computing.llnl.gov/tutorials/pthreads/#ConditionVariables

    for (;;) {
        pthread_mutex_lock(&conn->count_mutex);

        while (conn->last_chunk_written >= conn->nr_of_chunks && !conn->chunks_done) {
            pthread_cond_wait(&conn->count_threshold_cv, &conn->count_mutex);
        }

        pthread_mutex_unlock(&conn->count_mutex);

        if (conn->last_chunk_written < conn->nr_of_chunks) {
            write_chunk(conn, conn->last_chunk_written);
        }

        conn->last_chunk_written++;

        if (conn->chunks_done) {
            thr_io_close(conn);
            // after this no other action should be done on conn until a new request is started so make sure there are no statements below this.
            continue;
        }
    }

    return NULL;
}

/**
 * Claims a free connection and returns the connection number.
 * Released connections are used first.
 */
static int claim_connection(char *url, int need_new_connection) {
    int64_t lowest_release_time = av_gettime() / 1000;
    int conn_nr = -1;
    connection_t *conn;
    size_t len;
    pthread_mutex_lock(&connections_lock);

    for(int i = 0; i < nr_of_connections; i++) {
        connection_t *conn_l = connections[i];
        if (!conn_l->claimed) {
            if ((conn_nr == -1) || (conn->release_time != 0 && conn_l->release_time < lowest_release_time)) {
                conn_nr = i;
                conn = conn_l;
                lowest_release_time = conn->release_time;
            }
        }
    }

    if (conn_nr == -1) {
        conn = calloc(1, sizeof(*conn));
        conn_nr = nr_of_connections;
        conn->last_chunk_written = 0;
        conn->nr_of_chunks = 0;
        conn->nr = conn_nr;
        pthread_mutex_init(&conn->count_mutex, NULL);
        pthread_cond_init(&conn->count_threshold_cv, NULL);

        if(pthread_create(&conn->w_thread, NULL, thr_io_write, conn)) {
            fprintf(stderr, "Error creating thread\n");
            return 1;
        }

        av_dynarray_add(&connections, &nr_of_connections, conn);
        av_log(NULL, AV_LOG_INFO, "No free connections so added one. Nr of connections: %d, url: %s\n", nr_of_connections, url);
    }

    if (need_new_connection && conn->opened) {
        conn->opened = 0;
        ff_format_io_close(conn->s, &conn->out);
    }

    av_log(NULL, AV_LOG_INFO, "Claimed conn_id: %d, url: %s\n", conn_nr, url);
    len = strlen(url) + 1;
    conn->url = malloc(len);
    av_strlcpy(conn->url, url, len);
    conn->claimed = 1;
    conn->nr = conn_nr;
    pthread_mutex_unlock(&connections_lock);
    return conn_nr;
}

/**
 * Opens a request on a free connection and returns the connection number
 */
static int open_request(AVFormatContext *s, char *url, AVDictionary **options) {
    int ret;
    int conn_nr = claim_connection(url, 0);
    connection_t *conn = connections[conn_nr];

    if (conn->opened)
        av_log(s, AV_LOG_WARNING, "open_request while connection might be open. This is TODO for when not using persistent connections. conn_nr: %d\n", conn_nr);

    ret = s->io_open(s, &conn->out, url, AVIO_FLAG_WRITE, options);
    if (ret >= 0) {
        ret = conn_nr;
        conn->opened = 1;
    }

    return ret;
}


/**
 * Claim a connection and start a new request.
 * The claimed connection number is returned.
 */
int pool_io_open(AVFormatContext *s, char *filename,
                 AVDictionary **options, int http_persistent, int must_succeed, int retry, int need_new_connection) {

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
        AVDictionary *d = NULL;

        //claim new item from pool and open connection if needed
        int conn_nr = claim_connection(filename, need_new_connection);
        //Crash when we cannot claim a new connection. We should restart ffmpeg in this case.
        av_assert0(conn_nr >= 0);

        conn = connections[conn_nr];
        conn->must_succeed = must_succeed;
        conn->retry = retry;
        conn->s = s;

        conn->options = d;
        ret = av_dict_copy(&conn->options, *options, 0);
        if (ret < 0) {
            av_log(s, AV_LOG_WARNING, "Could not copy options for %s\n", filename);
            abort_if_needed(must_succeed);
            return ret;
        }

        conn->http_persistent = http_persistent;
        if (!conn->opened) {
            av_log(s, AV_LOG_INFO, "Connection(%d) not yet open %s\n", conn_nr, filename);

            ret = s->io_open(s, &(conn->out), filename, AVIO_FLAG_WRITE, options);
            if (ret < 0) {
                av_log(s, AV_LOG_WARNING, "Could not open %s\n", filename);
                abort_if_needed(must_succeed);
                conn->opened_error = 1;
                if (!conn->retry) {
                    return ret;
                }
                return conn_nr;
            }

            pthread_mutex_lock(&connections_lock);
            conn->opened = 1;
            pthread_mutex_unlock(&connections_lock);
            return conn_nr;
        }

        http_url_context = ffio_geturlcontext(conn->out);
        av_assert0(http_url_context);

        ret = ff_http_do_new_request(http_url_context, filename);
        if (ret != 0) {
            int64_t curr_time_ms = av_gettime() / 1000;
            int64_t idle_tims_ms = curr_time_ms - conn->release_time;
            av_log(s, AV_LOG_WARNING, "pool_io_open error conn_nr: %d, idle_time: %"PRId64", error: %d, name: %s\n", conn_nr, idle_tims_ms, ret, filename);
            ff_format_io_close(s, &conn->out);
            abort_if_needed(must_succeed);
            return ret;
        }
        ret = conn_nr;
#endif
    }

    return ret;
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

    conn = connections[conn_nr];
    av_log(NULL, AV_LOG_DEBUG, "pool_io_close conn_nr: %d, nr_of_chunks: %d\n", conn_nr, conn->nr_of_chunks);

    if (!conn->opened && !conn->opened_error) {
        av_log(s, AV_LOG_INFO, "Skip closing HTTP request because connection is not opened. Filename: %s\n", filename);
        abort_if_needed(conn->must_succeed);
        return;
    }

    conn->chunks_done = 1;

    pthread_mutex_lock(&conn->count_mutex);
    pthread_cond_signal(&conn->count_threshold_cv);
    pthread_mutex_unlock(&conn->count_mutex);
}

void pool_free(AVFormatContext *s, int conn_nr) {
    connection_t *conn;

    if (conn_nr < 0) {
        av_log(s, AV_LOG_WARNING, "Invalid conn_nr (pool_free)\n");
        return;
    }

    conn = connections[conn_nr];
    av_log(NULL, AV_LOG_DEBUG, "pool_free conn_nr: %d\n", conn_nr);

    ff_format_io_close(s, &conn->out);
    force_release_connection(conn);
}

void pool_free_all(AVFormatContext *s) {
    connection_t *conn;

    av_log(NULL, AV_LOG_DEBUG, "pool_free_all\n");
    for(int i = 0; i < nr_of_connections; i++) {
        conn = connections[i];
        if (conn->out)
            pool_free(s, i);
    }
}

void pool_write_flush(const unsigned char *buf, int size, int conn_nr) {
    write_flush(buf, size, conn_nr, 0);
}

int pool_avio_write(const unsigned char *buf, int size, int conn_nr) {
    connection_t *conn;

    if (conn_nr < 0) {
        av_log(NULL, AV_LOG_WARNING, "Invalid conn_nr (pool_avio_write)\n");
        return -1;
    }
    conn = connections[conn_nr];

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
    conn = connections[conn_nr];
    *out = conn->out;
}

void pool_init() {
    time_stats = init_time_stats("chunk_write", 5 * 1000000);
}

/*
  TODO: cleanup?
  pthread_mutex_destroy(&count_mutex);
  pthread_cond_destroy(&count_threshold_cv);
 */
