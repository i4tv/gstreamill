/*
 *  transcodejob
 *
 *  Copyright (C) Zhang Ping <zhangping@163.com>
 */

#include "transcodejob.h"

GST_DEBUG_CATEGORY_EXTERN (GSTREAMILL);
#define GST_CAT_DEFAULT GSTREAMILL

static void transcodejob_set_property (GObject *obj, guint prop_id, const GValue *value, GParamSpec *pspec);
static void transcodejob_get_property (GObject *obj, guint prop_id, GValue *value, GParamSpec *pspec);
static void transcodejob_dispose (GObject *obj);
static void transcodejob_finalize (GObject *obj);

