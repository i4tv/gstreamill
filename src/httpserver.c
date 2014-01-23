/*
 * Copyright (c) 2012, 2013 iTV.cn
 * Author Zhang Ping <zhangping@itv.cn>
 *
 */

#include <stdlib.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <netinet/tcp.h>
#include <gst/gst.h>
#include <string.h>
#include <errno.h>

#include "httpserver.h"

GST_DEBUG_CATEGORY_EXTERN (GSTREAMILL);
#define GST_CAT_DEFAULT GSTREAMILL

enum {
        HTTPSERVER_PROP_0,
        HTTPSERVER_PROP_NODE,
        HTTPSERVER_PROP_SERVICE,
        HTTPSERVER_PROP_MAXTHREADS,
};

static void httpserver_class_init (HTTPServerClass *httpserverclass);
static void httpserver_init (HTTPServer *httpserver);
static GObject *httpserver_constructor (GType type, guint n_construct_properties, GObjectConstructParam *construct_properties);
static void httpserver_set_property (GObject *obj, guint prop_id, const GValue *value, GParamSpec *pspec);
static void httpserver_get_property (GObject *obj, guint prop_id, GValue *value, GParamSpec *pspec);

static void httpserver_class_init (HTTPServerClass *httpserverclass)
{
        GObjectClass *g_object_class = G_OBJECT_CLASS (httpserverclass);
        GParamSpec *param;

        g_object_class->constructor = httpserver_constructor;
        g_object_class->set_property = httpserver_set_property;
        g_object_class->get_property = httpserver_get_property;

        param = g_param_spec_string (
                "node",
                "nodef",
                "address or hostname",
                "0.0.0.0",
                G_PARAM_WRITABLE | G_PARAM_READABLE
        );
        g_object_class_install_property (g_object_class, HTTPSERVER_PROP_NODE, param);

        param = g_param_spec_string (
                "service",
                "servicef",
                "port or service name",
                NULL,
                G_PARAM_WRITABLE | G_PARAM_READABLE
        );
        g_object_class_install_property (g_object_class, HTTPSERVER_PROP_SERVICE, param);

        param = g_param_spec_int (
                "maxthreads",
                "maxthreadsf",
                "max threads",
                1,
                256,
                10,
                G_PARAM_WRITABLE | G_PARAM_READABLE
        );
        g_object_class_install_property (g_object_class, HTTPSERVER_PROP_MAXTHREADS, param);
}

static gint compare_func (gconstpointer a, gconstpointer b)
{
        GstClockTime aa = *(GstClockTime *)a;
        GstClockTime bb = *(GstClockTime *)b;

        return (aa < bb ? -1 : (aa > bb ? 1 : 0));
}

static void httpserver_init (HTTPServer *http_server)
{
        gint i;
        RequestData *request_data;

        http_server->listen_thread = NULL;
        http_server->thread_pool = NULL;
        g_mutex_init (&(http_server->request_data_queue_mutex));
        http_server->request_data_queue = g_queue_new ();
        for (i=0; i<kMaxRequests; i++) {
                request_data = (RequestData *)g_malloc (sizeof (RequestData));
                g_mutex_init (&(request_data->events_mutex));
                request_data->id = i;
                http_server->request_data_pointers[i] = request_data;
                g_queue_push_head (http_server->request_data_queue, &http_server->request_data_pointers[i]);
        }

        g_mutex_init (&(http_server->idle_queue_mutex));
        g_cond_init (&(http_server->idle_queue_cond));
        http_server->idle_queue = g_tree_new ((GCompareFunc)compare_func);

        g_mutex_init (&(http_server->block_queue_mutex));
        g_cond_init (&(http_server->block_queue_cond));
        http_server->block_queue = g_queue_new ();

        http_server->total_click = 0;
        http_server->encoder_click = 0;
        http_server->system_clock = gst_system_clock_obtain ();
        g_object_set (http_server->system_clock, "clock-type", GST_CLOCK_TYPE_REALTIME, NULL);
}

static GObject * httpserver_constructor (GType type, guint n_construct_properties, GObjectConstructParam *construct_properties)
{
        GObject *obj;
        GObjectClass *parent_class = g_type_class_peek (G_TYPE_OBJECT);

        obj = parent_class->constructor (type, n_construct_properties, construct_properties);

        return obj;
}

static void httpserver_set_property (GObject *obj, guint prop_id, const GValue *value, GParamSpec *pspec)
{
        g_return_if_fail (IS_HTTPSERVER (obj));

        switch (prop_id) {
        case HTTPSERVER_PROP_NODE:
                HTTPSERVER (obj)->node = (gchar *)g_value_dup_string (value);
                break;

        case HTTPSERVER_PROP_SERVICE:
                HTTPSERVER (obj)->service = (gchar *)g_value_dup_string (value);
                break;

        case HTTPSERVER_PROP_MAXTHREADS:
                HTTPSERVER (obj)->max_threads = g_value_get_int (value);
                break;

        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
                break;
        }
}

static void httpserver_get_property (GObject *obj, guint prop_id, GValue *value, GParamSpec *pspec)
{
        HTTPServer *httpserver = HTTPSERVER (obj);

        switch (prop_id) {
        case HTTPSERVER_PROP_NODE:
                g_value_set_string (value, httpserver->node);
                break;

        case HTTPSERVER_PROP_SERVICE:
                g_value_set_string (value, httpserver->service);
                break;

        case HTTPSERVER_PROP_MAXTHREADS:
                g_value_set_int (value, httpserver->max_threads);
                break;

        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
                break;
        }
}

GType httpserver_get_type (void)
{
        static GType type = 0;

        if (type) return type;
        static const GTypeInfo info = {
                sizeof (HTTPServerClass),
                NULL, /* base class initializer */
                NULL, /* base class finalizer */
                (GClassInitFunc) httpserver_class_init,
                NULL,
                NULL,
                sizeof (HTTPServer),
                0,
                (GInstanceInitFunc) httpserver_init,
                NULL
        };
        type = g_type_register_static (G_TYPE_OBJECT, "HTTPServer", &info, 0);

        return type;
}

static gint read_request (RequestData *request_data)
{
        gint count, read_pos = request_data->request_length;
        gchar *buf = &(request_data->raw_request[0]);

        for (;;) {
                count = read (request_data->sock, buf + read_pos, kRequestBufferSize - read_pos);
                if (count == -1) {
                        if (errno != EAGAIN) {
                                GST_ERROR ("read error %s", g_strerror (errno));
                                return -1;

                        } else {
 				/* errno == EAGAIN means read complete */
                                GST_DEBUG ("read complete");
                                break;
                        }

                } else if (count == 0) {
 			/* closed by client */
                        GST_WARNING ("client closed");
                        return -2;

                } else if (count > 0) {
                        read_pos += count;
                        if (read_pos == kRequestBufferSize) {
                                GST_ERROR ("rquest size too large");
                                return -3;
                        }
                }
        }
        request_data->request_length = read_pos;
        buf[read_pos] = '\0'; /* string */

        return read_pos;
}

static gint parse_request (RequestData *request_data)
{
        gchar *buf = request_data->raw_request, *p1, *p2, *p3, *header;
        gchar *uri = &(request_data->uri[0]);
        gchar *parameters = &(request_data->parameters[0]);
        gint i, content_length;

        /* check header */
        p1 = strstr (buf, "\r\n\r\n");
        if (p1 == NULL) {
                /* header not completed */
                return 1;
        }
        request_data->header_size = p1 - buf + 4;

        GST_LOG ("head size: %d", request_data->header_size);
        header = g_strndup (buf, p1 - buf);
        p1 = strstr (header, "Content-Length:");
        if (p1 != NULL) {
                p1 += 15;
                while (*p1 == ' ') {
                        p1++;
                }
                p2 = p1;
                while (g_ascii_isdigit (*p2)) {
                        p2++;
                }
                p3 = g_strndup (p1, p2 - p1);
                content_length = atoi (p3);
                GST_LOG ("Content-Length: %d, request_length: %d", content_length, request_data->request_length);
                g_free (p3);
                if ((request_data->header_size + content_length) > request_data->request_length) {
                        /* body not completed, read more data. */
                        g_free (header);
                        return 1;
                }
        }
        g_free (header);

        if (strncmp (buf, "GET", 3) == 0) {
                request_data->method = HTTP_GET;
                buf += 3;

        } else if (strncmp (buf, "POST", 4) == 0) {
                request_data->method = HTTP_POST;
                buf += 4;

        } else {
                return 2; /* Bad request */
        }

        while (*buf == ' ') {
	 	/* skip space */
                buf++;
        }

        i = 0;
        while (*buf != ' ' && *buf != '?' && i++ < 255) { /* max length of uri is 255 */
                *uri = *buf;
                buf++;
                uri++;
        }
        if (i <= 255) {
                *uri = '\0';

        } else {
	 	/* Bad request, uri too long */
                return 3;
        }

        i = 0;
        if (*buf == '?') {
 		/* have parameters */
                buf++;
                while (*buf != ' ' && i++ < 1024) {
                        *parameters = *buf;
                        buf++;
                        parameters++;
                }
        }
        if (i <= 1024) {
                *parameters = '\0';

        } else {
 		/* Bad request, parameters too long */
                return 3;
        }

        while (*buf == ' ') {
 		/* skip space */
                buf++;
        }

        if (strncmp (buf, "HTTP/1.1", 8) == 0) {
 		/* http version must be 1.1 */
                request_data->version = HTTP_1_1; 

        } else if (strncmp (buf, "HTTP/1.0", 8) == 0) {
                request_data->version = HTTP_1_0;

        }else { /* Bad request, must be http 1.1 */
                return 4;
        }

        buf += 8;

        /* parse headers */
        i = 0;
        for (;;) {
                if ((g_ascii_strncasecmp (buf, "\n\n", 2) == 0) || (g_ascii_strncasecmp (buf, "\r\n\r\n", 4) == 0)) {
                        break;
                }

                while (*buf == '\r' || *buf == '\n') {
                        buf++;
                }

                /* parse name */
                p1 = buf;
                while (*buf != ':' && *buf != ' ' && *buf != '\0') {
                        buf++;
                }
                /*request_data->headers[i].name = g_strndup (p1, buf - p1);*/

                while (*buf == ':' || *buf == ' ') {
                        buf++;
                }

                /* parse value */
                p1 = buf;
                while (*buf != '\r' && *buf != '\n') {
                        buf++;
                }
                /*request_data->headers[i].value = g_strndup (p1, buf - p1); */
                i++;
        }
        request_data->num_headers = i;

        return 0;
}

static gint set_nonblock (int fd)
{
        int flags;

        if (-1 == (flags = fcntl (fd, F_GETFL, 0)))
                flags = 0;
        return fcntl (fd, F_SETFL, flags | O_NONBLOCK);
}

static void close_socket_gracefully (gint sock)
{
        struct linger linger;
        gint count;
        gchar buf[1024];

        /*
         * Set linger option to avoid socket hanging out after close. This prevent
         * ephemeral port exhaust problem under high QPS.
         */
        linger.l_onoff = 1;
        linger.l_linger = 1;
        setsockopt (sock, SOL_SOCKET, SO_LINGER, (char *) &linger, sizeof (linger));

        (void) shutdown (sock, SHUT_WR);

        /* Read and discard pending incoming data. */
        do {
                count = read (sock, &buf[0], 1024);
        } while (count > 0);
        
        /* Now we know that our FIN is ACK-ed, safe to close */
        (void) close (sock);
}

static gint accept_socket (HTTPServer *http_server)
{
        struct epoll_event ee;
        gint accepted_sock, ret;
        struct sockaddr in_addr;
        socklen_t in_len;
        RequestData **request_data_pointer;
        RequestData *request_data;
        gint request_data_queue_len;

        for (;;) {
 		/* repeat accept until -1 returned */
                accepted_sock = accept (http_server->listen_sock, &in_addr, &in_len);
                if (accepted_sock == -1) {
                        if (( errno == EAGAIN) || (errno == EWOULDBLOCK)) {
				/* We have processed all incoming connections. */
                                break;

                        } else {
                                GST_ERROR ("accept error  %s", g_strerror (errno));
                                break;
                        }
                }
                g_mutex_lock (&(http_server->request_data_queue_mutex));
                request_data_queue_len = g_queue_get_length (http_server->request_data_queue);
                g_mutex_unlock (&(http_server->request_data_queue_mutex));
                if (request_data_queue_len == 0) {
                        GST_ERROR ("event queue empty");
                        close_socket_gracefully (accepted_sock);
                        continue;
                }
                GST_DEBUG ("new request arrived, accepted_sock %d", accepted_sock);
                http_server->total_click += 1;

                int on = 1;
                setsockopt (accepted_sock, SOL_TCP, TCP_CORK, &on, sizeof (on));
                set_nonblock (accepted_sock);
                g_mutex_lock (&(http_server->request_data_queue_mutex));
                request_data_pointer = g_queue_pop_tail (http_server->request_data_queue);
                g_mutex_unlock (&(http_server->request_data_queue_mutex));
                if (request_data_pointer == NULL) {
                        GST_WARNING ("No NONE request, refuse this request.");
                        close_socket_gracefully (accepted_sock);
                        continue;
                }
                request_data = *request_data_pointer;
                GST_DEBUG ("pop up request data, id %d, sock %d, events %d", request_data->id, accepted_sock, request_data->events);
                /* clear events, there may be events from last request. */
                request_data->events = 0;
                request_data->client_addr = in_addr;
                request_data->sock = accepted_sock;
                request_data->birth_time = gst_clock_get_time (http_server->system_clock);
                request_data->status = HTTP_CONNECTED;
                request_data->request_length = 0;
                ee.events = EPOLLIN | EPOLLOUT | EPOLLET;
                ee.data.ptr = request_data_pointer;
                ret = epoll_ctl (http_server->epollfd, EPOLL_CTL_ADD, accepted_sock, &ee);
                if (ret == -1) {
                        GST_ERROR ("epoll_ctl add error %s sock %d", g_strerror (errno), accepted_sock);
                        close_socket_gracefully (accepted_sock);
                        request_data->status = HTTP_NONE;
                        g_mutex_lock (&(http_server->request_data_queue_mutex));
                        request_data->events = 0;
                        g_queue_push_head (http_server->request_data_queue, request_data_pointer);
                        g_mutex_unlock (&(http_server->request_data_queue_mutex));
                        return -1;

                } else {
                        GST_DEBUG ("pop request data, sock %d", request_data->sock);
                }
        }

        return 0;
}

static gchar * epoll_event_string (struct epoll_event event)
{
        if (event.events & EPOLLIN) {
                return "EPOLLIN";
        }

        if (event.events & EPOLLOUT) {
                return "EPOLLOUT";
        }

        if (event.events & EPOLLERR) {
                return "EPOLLERR";
        } 

        if (event.events & EPOLLHUP) {
                return "EPOLLHUP";
        }

        if (event.events & EPOLLRDBAND) {
                return "EPOLLRDBAND";
        }

        if (event.events & EPOLLRDNORM) { 
                return "EPOLLRDNORM";
        }

        if (event.events & EPOLLWRNORM) {
                return "EPOLLWRNORM";
        }

        if (event.events & EPOLLWRBAND) {
                return "EPOLLWRBAND";
        }

        if (event.events & EPOLLRDHUP) {
                return "EPOLLRDHUP";
        }
}

static gint socket_prepare (HTTPServer *http_server)
{
        struct addrinfo hints;
        struct addrinfo *result, *rp;
        gint ret, listen_sock;
        struct epoll_event event;

        memset (&hints, 0, sizeof (struct addrinfo));
        hints.ai_family = AF_UNSPEC; /* Return IPv4 and IPv6 choices */
        hints.ai_socktype = SOCK_STREAM; /* We want a TCP socket */
        hints.ai_flags = AI_PASSIVE; /* All interfaces */
        ret = getaddrinfo (http_server->node, http_server->service, &hints, &result);
        if (ret != 0) {
                GST_ERROR ("node %s, service: %s, getaddrinfo error: %s\n", http_server->node, http_server->service, gai_strerror (ret));
                return 1;
        }

        GST_INFO ("start http server on %s:%s", http_server->node, http_server->service);
        for (rp = result; rp != NULL; rp = rp->ai_next) {
                int opt = 1;

                listen_sock = socket (rp->ai_family, rp->ai_socktype, rp->ai_protocol);
                setsockopt (listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof (opt));
                if (listen_sock == -1)
                        continue;
                ret = bind (listen_sock, rp->ai_addr, rp->ai_addrlen);
                if (ret == 0) {
                        /* bind successfully! */
                        GST_INFO ("listen socket %d", listen_sock);
                        break;

                } else if (ret == -1) {
                        GST_ERROR ("Bind socket %d error: %s", listen_sock, g_strerror (errno));
                        return 1;
                }
                close_socket_gracefully (listen_sock);
        }

        if (rp == NULL) {
                GST_ERROR ("Could not bind %s\n", http_server->service);
                return 1;
        }

        freeaddrinfo (result);

        ret = listen (listen_sock, SOMAXCONN);
        if (ret == -1) {
                GST_ERROR ("listen error");
                return 1;
        }

        set_nonblock (listen_sock);
        http_server->listen_sock = listen_sock;
        http_server->epollfd = epoll_create1 (0);
        if (http_server->epollfd == -1) {
                GST_ERROR ("epoll_create error %s", g_strerror (errno));
                return 1;
        }

        event.data.ptr = NULL;
        event.events = EPOLLIN | EPOLLOUT | EPOLLET;
        ret = epoll_ctl (http_server->epollfd, EPOLL_CTL_ADD, listen_sock, &event);
        if (ret == -1) {
                GST_ERROR ("epoll_ctl add epollfd error %s", g_strerror (errno));
                return 1;
        }

        return 0;
}

static gpointer listen_thread (gpointer data)
{
        HTTPServer *http_server = (HTTPServer *)data;
        struct epoll_event event_list[kMaxRequests];
        gint n, i;

        for (;;) {
                n = epoll_wait (http_server->epollfd, event_list, kMaxRequests, -1);
                if (n == -1) {
                        GST_ERROR ("epoll_wait error %s", g_strerror (errno));
                        continue;
                }
                for (i = 0; i < n; i++) {
                        RequestData *request_data;

                        if (event_list[i].data.ptr == NULL) {
                                /* new request arrived */
                                accept_socket (http_server);
                                continue;
                        }

                        request_data = *(RequestData **)(event_list[i].data.ptr);
                        g_mutex_lock (&(request_data->events_mutex));
                        request_data->events |= event_list[i].events;
                        g_mutex_unlock (&(request_data->events_mutex));

			/* push to thread pool queue */
                        if ((event_list[i].events & EPOLLIN) && (request_data->status == HTTP_CONNECTED)) {
                                GError *err = NULL;

                                GST_DEBUG ("event on sock %d events %d", request_data->sock, request_data->events);
                                request_data->status = HTTP_REQUEST;
                                g_thread_pool_push (http_server->thread_pool, event_list[i].data.ptr, &err);
                                if (err != NULL) {
                                        GST_FIXME ("Thread pool push error %s", err->message);
                                        g_error_free (err);
                                }
                        } 

                        if (event_list[i].events & (EPOLLOUT | EPOLLIN | EPOLLHUP | EPOLLERR)) {
                                if (request_data->status == HTTP_BLOCK) {
                                        g_mutex_lock (&(http_server->block_queue_mutex));
                                        g_cond_signal (&(http_server->block_queue_cond));
                                        g_mutex_unlock (&(http_server->block_queue_mutex));
                                }
                        }

                        GST_DEBUG ("event on sock %d events %s", request_data->sock, epoll_event_string (event_list[i]));
                }
        }
}

typedef struct _ForeachFuncData {
        GSList **wakeup_list; /* point to list of wakeuped */
        HTTPServer *http_server;
        gint64 wakeup_time;
} ForeachFuncData;

static gboolean gtree_foreach_func (gpointer key, gpointer value, gpointer data)
{
        ForeachFuncData *func_data = data;
        HTTPServer *http_server = func_data->http_server;
        GSList **wakeup_list = func_data->wakeup_list;
        GstClockTime current_time;
        
        current_time = gst_clock_get_time (http_server->system_clock);
        if (current_time > *(GstClockTime *)key) {
                *wakeup_list = g_slist_append (*wakeup_list, value);
                return FALSE;

        } else {
                func_data->wakeup_time = g_get_monotonic_time () + ((*(GstClockTime *)key) - current_time)/ 1000;
                return TRUE;
        }
}

static void gslist_foreach_func (gpointer data, gpointer user_data)
{
        RequestData **request_data_pointer = data;
        RequestData *request_data = *request_data_pointer;
        HTTPServer *http_server = user_data;
        GError *err = NULL;

        g_tree_remove (http_server->idle_queue, &(request_data->wakeup_time));
        g_thread_pool_push (http_server->thread_pool, request_data_pointer, &err);
        if (err != NULL) {
                GST_FIXME ("Thread pool push error %s", err->message);
                g_error_free (err);
        }
}

static gpointer idle_thread (gpointer data)
{
        HTTPServer *http_server = (HTTPServer *)data;
        ForeachFuncData func_data;
        GSList *wakeup_list = NULL;

        func_data.http_server = http_server;
        func_data.wakeup_list = &wakeup_list;
        for (;;) {
                g_mutex_lock (&(http_server->idle_queue_mutex));
                while (g_tree_nnodes (http_server->idle_queue) == 0) {
                        g_cond_wait (&(http_server->idle_queue_cond), &(http_server->idle_queue_mutex));
                }
                func_data.wakeup_time = 0;
                g_tree_foreach (http_server->idle_queue, gtree_foreach_func, &func_data);
                if (wakeup_list != NULL) {
                        g_slist_foreach (wakeup_list, gslist_foreach_func, http_server);
                        g_slist_free (wakeup_list);
                        wakeup_list = NULL;
                }
                if (func_data.wakeup_time != 0) {
                        /* more than one idle request in the idle queue, wait until. */
                        g_cond_wait_until (&(http_server->idle_queue_cond), &(http_server->idle_queue_mutex), func_data.wakeup_time);
                }
                g_mutex_unlock (&(http_server->idle_queue_mutex));
        }
}

static void block_queue_foreach_func (gpointer data, gpointer user_data)
{
        RequestData **request_data_pointer = data;
        RequestData *request_data = *request_data_pointer;
        HTTPServer *http_server = user_data;
        GError *err = NULL;

        if ((request_data->events & (EPOLLOUT | EPOLLIN | EPOLLHUP | EPOLLERR)) ||
            (request_data->wakeup_time < gst_clock_get_time (http_server->system_clock))) {
                /* EPOLL event or block time out, popup request from block queue and push to thread pool. */
                g_queue_remove (http_server->block_queue, request_data_pointer);
                g_thread_pool_push (http_server->thread_pool, request_data_pointer, &err);
                if (err != NULL) {
                        GST_FIXME ("Thread pool push error %s", err->message);
                        g_error_free (err);
                }
        }
}

static gpointer block_thread (gpointer data)
{
        HTTPServer *http_server = (HTTPServer *)data;
        gint64 wakeup_time;

        for (;;) {
                g_mutex_lock (&(http_server->block_queue_mutex));
                wakeup_time = g_get_monotonic_time () + G_TIME_SPAN_SECOND;
                g_cond_wait_until (&(http_server->block_queue_cond), &(http_server->block_queue_mutex), wakeup_time);
                g_queue_foreach (http_server->block_queue, block_queue_foreach_func, http_server);
                g_mutex_unlock (&(http_server->block_queue_mutex));
        }
}

static void thread_pool_func (gpointer data, gpointer user_data)
{
        HTTPServer *http_server = (HTTPServer *)user_data;
        RequestData **request_data_pointer = data;
        RequestData *request_data = *request_data_pointer;
        gint ret;
        GstClockTime cb_ret;
        
        GST_DEBUG ("EVENT %d, status %d, sock %d", request_data->events, request_data->status, request_data->sock);
        g_mutex_lock (&(request_data->events_mutex));
        if (request_data->events & (EPOLLHUP | EPOLLERR)) {
                request_data->status = HTTP_FINISH;
                request_data->events = 0;

        } else if (request_data->events & EPOLLOUT) {
                if ((request_data->status == HTTP_IDLE) || (request_data->status == HTTP_BLOCK)) {
                        request_data->status = HTTP_CONTINUE;
                }
                request_data->events ^= EPOLLOUT;

        } else if (request_data->events & EPOLLIN) {
                if ((request_data->status == HTTP_IDLE) || (request_data->status == HTTP_BLOCK)) {
                        /* in normal play status */
                        ret = read_request (request_data);
                        if (ret == -2) {
                                GST_DEBUG ("Clinet close, FIN received.");
                                request_data->status = HTTP_FINISH;

                        } else {
                                GST_DEBUG ("Unexpected request arrived, ignore.");
                                request_data->status = HTTP_CONTINUE;
                        }
                } 
                /* HTTP_REQUEST status */
                request_data->events ^= EPOLLIN;

        } else if ((request_data->status == HTTP_IDLE) || (request_data->status == HTTP_BLOCK)) {
                /* no event, popup from idle queue or block queue */
                request_data->status = HTTP_CONTINUE;

        } else {
                GST_ERROR ("warning!!! unprocessed event, sock %d status %d events %d", request_data->sock, request_data->status, request_data->events);
        }
        g_mutex_unlock (&(request_data->events_mutex));
        
        if (request_data->status == HTTP_REQUEST) {
                ret = read_request (request_data);
                if (ret <= 0) {
                        GST_ERROR ("no data, sock is %d", request_data->sock);
                        request_data->status = HTTP_NONE;
                        close_socket_gracefully (request_data->sock);
                        g_mutex_lock (&(http_server->request_data_queue_mutex));
                        request_data->events = 0;
                        g_queue_push_head (http_server->request_data_queue, request_data_pointer);
                        g_mutex_unlock (&(http_server->request_data_queue_mutex));
                        return;
                } 

                ret = parse_request (request_data);
                if (ret == 0) {
                        /* parse complete, call back user function */
                        request_data->events ^= EPOLLIN;
                        cb_ret = http_server->user_callback (request_data, http_server->user_data);
                        if (cb_ret > 0) {
                                /* idle */
                                GST_DEBUG ("insert idle queue end, sock %d wakeuptime %lu", request_data->sock, cb_ret);
                                http_server->encoder_click += 1;
                                request_data->wakeup_time = cb_ret;
                                g_mutex_lock (&(http_server->idle_queue_mutex));
                                while (g_tree_lookup (http_server->idle_queue, &(request_data->wakeup_time)) != NULL) {
                                        /* avoid time conflict */
                                        request_data->wakeup_time++;
                                }
                                request_data->status = HTTP_IDLE;
                                g_tree_insert (http_server->idle_queue, &(request_data->wakeup_time), request_data_pointer);
                                g_cond_signal (&(http_server->idle_queue_cond));
                                g_mutex_unlock (&(http_server->idle_queue_mutex));

                        } else {
                                /* finish */
                                GST_DEBUG ("callback return 0, request finish, sock %d", request_data->sock);
                                request_data->status = HTTP_NONE;
                                close_socket_gracefully (request_data->sock);
                                g_mutex_lock (&(http_server->request_data_queue_mutex));
                                request_data->events = 0;
                                g_queue_push_head (http_server->request_data_queue, request_data_pointer);
                                g_mutex_unlock (&(http_server->request_data_queue_mutex));
                        }

                } else if (ret == 1) {
                        /* need read more data */
                        g_mutex_lock (&(http_server->block_queue_mutex));
                        g_queue_push_head (http_server->block_queue, request_data_pointer);
                        g_mutex_unlock (&(http_server->block_queue_mutex));
                        return;

                } else {
                        /* Bad Request */
                        GST_ERROR ("Bad request, return is %d, sock is %d", ret, request_data->sock);
                        gchar *buf = g_strdup_printf (http_400, PACKAGE_NAME, PACKAGE_VERSION);
                        if (httpserver_write (request_data->sock, buf, strlen (buf)) != strlen (buf)) {
                                GST_ERROR ("write sock %d error.", request_data->sock);
                        }
                        g_free (buf);
                        request_data->status = HTTP_NONE;
                        close_socket_gracefully (request_data->sock);
                        g_mutex_lock (&(http_server->request_data_queue_mutex));
                        request_data->events = 0;
                        g_queue_push_head (http_server->request_data_queue, request_data_pointer);
                        g_mutex_unlock (&(http_server->request_data_queue_mutex));
                }

        } else if (request_data->status == HTTP_CONTINUE) {
                cb_ret = http_server->user_callback (request_data, http_server->user_data);
                if (cb_ret == GST_CLOCK_TIME_NONE) {
                        /* block */
                        g_mutex_lock (&(http_server->block_queue_mutex));
                        request_data->status = HTTP_BLOCK;
                        /* block time out is 300ms */
                        request_data->wakeup_time = gst_clock_get_time (http_server->system_clock) + 300 * GST_MSECOND;
                        g_queue_push_head (http_server->block_queue, request_data_pointer);
                        g_mutex_unlock (&(http_server->block_queue_mutex));

                } else if (cb_ret > 0) {
                        /* idle */
                        int iiii=0;
                        request_data->wakeup_time = cb_ret;
                        g_mutex_lock (&(http_server->idle_queue_mutex));
                        if (request_data->status != HTTP_CONTINUE) {
                                GST_FIXME ("insert a un continue request to idle queue sock %d", request_data->sock);
                        }
                        if (g_tree_nnodes (http_server->idle_queue) > 0) {
                                RequestData **rr;
                                RequestData *r;
                                while ((rr = (RequestData **)g_tree_lookup (http_server->idle_queue, &(request_data->wakeup_time))) != NULL) {
                                        /* avoid time conflict */
                                        r = *rr;
                                        GST_WARNING ("tree node number %d find sock %d wakeuptime %lu", g_tree_nnodes (http_server->idle_queue), request_data->sock, request_data->wakeup_time);
                                        GST_WARNING ("look up, find sock %d wakeuptime %lu", r->sock, r->wakeup_time);
                                        request_data->wakeup_time++;
                                        if (iiii++==10) exit (0);
                                }
                        }
                        request_data->status = HTTP_IDLE;
                        GST_DEBUG ("insert idle queue end, sock %d wakeuptime %lu", request_data->sock, cb_ret);
                        g_tree_insert (http_server->idle_queue, &(request_data->wakeup_time), request_data_pointer);
                        g_cond_signal (&(http_server->idle_queue_cond));
                        g_mutex_unlock (&(http_server->idle_queue_mutex));

                } else {
                        /* finish */
                        g_mutex_lock (&(http_server->idle_queue_mutex));
                        g_tree_remove (http_server->idle_queue, &(request_data->wakeup_time));
                        g_mutex_unlock (&(http_server->idle_queue_mutex));
                        request_data->status = HTTP_NONE;
                        close_socket_gracefully (request_data->sock);
                        g_mutex_lock (&(http_server->request_data_queue_mutex));
                        request_data->events = 0;
                        g_queue_push_head (http_server->request_data_queue, request_data_pointer);
                        g_mutex_unlock (&(http_server->request_data_queue_mutex));
                }

        } else if (request_data->status == HTTP_FINISH) { // FIXME: how about if have continue request in idle queue??
                cb_ret = http_server->user_callback (request_data, http_server->user_data);
                GST_DEBUG ("request finish %d callback return %lu, send %lu", request_data->sock, cb_ret, request_data->bytes_send);
                if (cb_ret == 0) {
                        g_mutex_lock (&(http_server->idle_queue_mutex));
                        g_tree_remove (http_server->idle_queue, &(request_data->wakeup_time));
                        g_mutex_unlock (&(http_server->idle_queue_mutex));
                        request_data->status = HTTP_NONE;
                        close_socket_gracefully (request_data->sock);
                        g_mutex_lock (&(http_server->request_data_queue_mutex));
                        request_data->events = 0;
                        g_queue_push_head (http_server->request_data_queue, request_data_pointer);
                        g_mutex_unlock (&(http_server->request_data_queue_mutex));
                }
        }
}

gint httpserver_start (HTTPServer *http_server, http_callback_t user_callback, gpointer user_data)
{
        GError *err = NULL;

        http_server->thread_pool = g_thread_pool_new (thread_pool_func, http_server, http_server->max_threads, TRUE, &err);
        if (err != NULL) {
                GST_ERROR ("Create thread pool error %s", err->message);
                g_error_free (err);
                return -1;
        }
        http_server->user_callback = user_callback;
        http_server->user_data = user_data;
        if (socket_prepare (http_server) != 0) {
                return 1;
        }

        http_server->listen_thread = g_thread_new ("listen_thread", listen_thread, http_server);
        http_server->idle_thread = g_thread_new ("idle_thread", idle_thread, http_server);
        http_server->block_thread = g_thread_new ("block_thread", block_thread, http_server);

        return 0;
}

/**
 * httpserver_write:
 * @sock: (in): socket
 * @buf: (in): buffer to be sent
 * @count: (in): size of buffer to be sent
 *
 * loop write until complete all of buf
 *
 * Returns: send count
 */
gint httpserver_write (gint sock, gchar *buf, gsize count)
{
        gsize sent;
        gint ret, len;
        
        sent = 0;
        while (sent < count) {
                len = INT_MAX < count - sent ? INT_MAX : count - sent;
                ret = write (sock, buf + sent, len);
                if (ret == -1) {
                        if (errno == EAGAIN) {
                                /* block, wait 50ms */
                                g_usleep (50000);
                                continue;
                        }
                        GST_INFO ("Write error: %s", g_strerror (errno));
                        break;
                }
                sent += ret;
        }

        return sent;
}

gint httpserver_report_request_data (HTTPServer *http_server)
{
        gint i, count;
        RequestData *request_data;
        gint request_data_queue_len=2;

        count = 0;
        for (i = 0; i < kMaxRequests; i++) {
                request_data = http_server->request_data_pointers[i];
                if (request_data->status != HTTP_NONE) {
                        GST_INFO ("%d : status %d sock %d uri %s wakeuptime %lu", i, request_data->status, request_data->sock, request_data->uri, request_data->wakeup_time);

                } else {
                        count += 1;
                }
        }

        g_mutex_lock (&(http_server->request_data_queue_mutex));
        request_data_queue_len = g_queue_get_length (http_server->request_data_queue);
        g_mutex_unlock (&(http_server->request_data_queue_mutex));
        GST_INFO ("status None %d, request queue length %d, blockq %d, total click %lu, encoder click %lu",
                  count,
                  request_data_queue_len,
                  g_queue_get_length (http_server->block_queue),
                  http_server->total_click,
                  http_server->encoder_click);

        return 0;
}
