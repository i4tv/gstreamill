/*
 * Copyright (c) 2012, 2013 iTV.cn
 * Author Zhang Ping <zhangping@itv.cn>
 */

#ifndef __HTTPSERVER_H__
#define __HTTPSERVER_H__

#include <netinet/in.h>
#include <gst/gst.h>

#include "config.h"

#define http_500 "HTTP/1.1 500 Internal Server Error\r\n" \
                 "Server: %s-%s\r\n" \
                 "Content-Type: text/html\r\n" \
                 "Content-Length: 30\r\n" \
                 "Connection: Close\r\n\r\n" \
                 "<h1>Internal Server Error</h1>"

#define http_404 "HTTP/1.1 404 Not Found\r\n" \
                 "Server: %s-%s\r\n" \
                 "Content-Type: text/html\r\n" \
                 "Content-Length: 18\r\n" \
                 "Connection: Close\r\n\r\n" \
                 "<h1>Not found</h1>"

#define http_400 "HTTP/1.1 400 Bad Request\r\n" \
                 "Server: %s-%s\r\n" \
                 "Content-Type: text/html\r\n" \
                 "Content-Length: 20\r\n" \
                 "Connection: Close\r\n\r\n" \
                 "<h1>Bad Request</h1>"

#define http_200 "HTTP/1.1 200 Ok\r\n" \
                 "Server: %s-%s\r\n" \
                 "Content-Type: %s\r\n" \
                 "Content-Length: %zu\r\n" \
                 "Access-Control-Allow-Origin: *\r\n" \
                 "Cache-Control: %s\r\n" \
                 "Connection: Close\r\n\r\n" \
                 "%s"

#define NO_CACHE "no-cache"
#define CACHE_60s "max-age=60"

#define http_chunked "HTTP/1.1 200 OK\r\n" \
                     "Content-Type: video/mpeg\r\n" \
                     "Server: %s-%s\r\n" \
                     "Connection: Close\r\n" \
                     "Transfer-Encoding: chunked\r\n" \
                     "Pragma: no-cache\r\n" \
                     "Cache-Control: no-cache, no-store, must-revalidate\r\n\r\n"

typedef struct _HTTPServer      HTTPServer;
typedef struct _HTTPServerClass HTTPServerClass;
typedef GstClockTime (*http_callback_t) (gpointer data, gpointer user_data);

enum request_method {
        HTTP_GET,
        HTTP_POST
};

enum http_version {
        HTTP_1_0,
        HTTP_1_1
};

struct http_headers {
        gchar *name;
        gchar *value;
};

enum session_status {
        HTTP_NONE,
        HTTP_CONNECTED,
        HTTP_REQUEST,
        HTTP_CONTINUE,
        HTTP_IDLE,
        HTTP_BLOCK,
        HTTP_FINISH
};

#define kRequestBufferSize 1024 * 1050
#define kMaxRequests 128

typedef struct _RequestData {
        gint id;
        gint sock;
        struct sockaddr client_addr;
        GstClockTime birth_time;
        guint64 bytes_send;
        GMutex events_mutex;
        guint32 events; /* epoll events */
        enum session_status status; /* live over http need keeping tcp link */
        GstClockTime wakeup_time; /* used in idle queue */
        gchar raw_request[kRequestBufferSize];
        gint request_length;
        enum request_method method;
        gchar uri[256];
        gchar parameters[1025];
        enum http_version version;
        gint header_size;
        gint num_headers;
        struct http_headers headers[64];
        gpointer priv_data; /* private user data */
} RequestData;

struct _HTTPServer {
        GObject parent;

        GstClock *system_clock;
        guint64 total_click; /* total access number, include manage api */
        guint64 encoder_click; /* access number of playing encoder */

        gchar *node;
        gchar *service;
        gint max_threads;
        gint listen_sock;
        gint epollfd;
        GThread *listen_thread;

        GMutex idle_queue_mutex;
        GCond idle_queue_cond;
        GTree *idle_queue;
        GThread *idle_thread;

        GMutex block_queue_mutex;
        GCond block_queue_cond;
        GQueue *block_queue;
        GThread *block_thread;

        GThreadPool *thread_pool;

        http_callback_t user_callback;
        gpointer user_data;
        gpointer request_data_pointers[kMaxRequests];
        GMutex request_data_queue_mutex;
        GQueue *request_data_queue;
};

struct _HTTPServerClass {
        GObjectClass parent;
};

#define TYPE_HTTPSERVER           (httpserver_get_type())
#define HTTPSERVER(obj)           (G_TYPE_CHECK_INSTANCE_CAST ((obj), TYPE_HTTPSERVER, HTTPServer))
#define HTTPSERVER_CLASS(cls)     (G_TYPE_CHECK_CLASS_CAST    ((cls), TYPE_HTTPSERVER, HTTPServerClass))
#define IS_HTTPSERVER(obj)        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), TYPE_HTTPSERVER))
#define IS_HTTPSERVER_CLASS(cls)  (G_TYPE_CHECK_CLASS_TYPE    ((cls), TYPE_HTTPSERVER))
#define HTTPSERVER_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS  ((obj), TYPE_HTTPSERVER, HTTPServerClass))
#define httpserver_new(...)       (g_object_new(TYPE_HTTPSERVER, ## __VA_ARGS__, NULL))

GType httpserver_get_type (void);
gint httpserver_start (HTTPServer *httpserver, http_callback_t user_callback, gpointer user_data);
gint httpserver_report_request_data (HTTPServer *http_server);

#endif /* __HTTPSERVER_H__ */
