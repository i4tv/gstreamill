/*
 * managment api over http
 *
 * Copyright (C) Zhang Ping <dqzhangp@163.com>
 *
 */

#ifndef __HTTPMGMT_H__
#define __HTTPMGMT_H__

#include <gst/gst.h>

#include "config.h"
#include "gstreamill.h"
#include "httpserver.h"

#define CONF_FILE "/etc/gstreamill.d/gstreamill.conf"
#define JOBS_DIR "/etc/gstreamill.d/jobs.d/"
#define ADMIN_LOCATION "/usr/share/gstreamill"

typedef struct _HTTPMgmtPrivateData {
        gint fd;
        gchar *p;
        gchar *buf;
        gsize buf_size;
        gint64 send_position;
} HTTPMgmtPrivateData;

typedef struct _HTTPMgmt      HTTPMgmt;
typedef struct _HTTPMgmtClass HTTPMgmtClass;

struct _HTTPMgmt {
        GObject parent;

        gchar *address;
        GstClock *system_clock;
        Gstreamill *gstreamill;
        HTTPServer *httpserver; /* management via http */
};

struct _HTTPMgmtClass {
        GObjectClass parent;
};

#define TYPE_HTTPMGMT           (httpmgmt_get_type())
#define HTTPMGMT(obj)           (G_TYPE_CHECK_INSTANCE_CAST ((obj), TYPE_HTTPMGMT, HTTPMgmt))
#define HTTPMGMT_CLASS(cls)     (G_TYPE_CHECK_CLASS_CAST    ((cls), TYPE_HTTPMGMT, HTTPMgmtClass))
#define IS_HTTPMGMT(obj)        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), TYPE_HTTPMGMT))
#define IS_HTTPMGMT_CLASS(cls)  (G_TYPE_CHECK_CLASS_TYPE    ((cls), TYPE_HTTPMGMT))
#define HTTPMGMT_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS  ((obj), TYPE_HTTPMGMT, HTTPMgmtClass))
#define httpmgmt_new(...)       (g_object_new(TYPE_HTTPMGMT, ## __VA_ARGS__, NULL))

GType httpmgmt_get_type (void);
gint httpmgmt_start (HTTPMgmt *httpmgmt);

#endif /* __HTTPMGMT_H__ */
