/*
 *  transcodejob
 *
 *  Copyright (C) Zhang Ping <zhangping@163.com>
 */

#ifndef __TRANSCODEJOB_H__
#define __TRANSCODEJOB_H__

#include "config.h"
#include "source.h"
#include "encoder.h"

typedef struct _TranscodeJob TranscodeJob;
typedef struct _TranscodeJobClass TranscodeJobClass;

struct _TranscodeJob {
        GObject parent;

        gchar *job;
};

struct _TranscodeJobClass {
        GObjectClass parent;
};

#define TYPE_TRANSCODEJOB           (trancodejob_get_type())
#define TRANSCODEJOB(obj)           (G_TYPE_CHECK_INSTANCE_CAST ((obj), TYPE_TRANSCODEJOB, TranscodeJob))
#define TRANSCODEJOB_CLASS(cls)     (G_TYPE_CHECK_CLASS_CAST    ((cls), TYPE_TRANSCODEJOB, TranscodeJobClass))
#define IS_TRANSCODEJOB(obj)        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), TYPE_TRANSCODEJOB))
#define IS_TRANSCODEJOB_CLASS(cls)  (G_TYPE_CHECK_CLASS_TYPE    ((cls), TYPE_TRANSCODEJOB))
#define TRANSCODEJOB_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS  ((obj), TYPE_TRANSCODEJOB, TranscodeJobClass))
#define trancodejob_new(...)        (g_object_new(TYPE_TRANSCODEJOB, ## __VA_ARGS__, NULL))

GType trancodejob_get_type (void);

#endif /* __TRANSCODEJOB_H__ */
