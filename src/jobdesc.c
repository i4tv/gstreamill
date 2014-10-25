/*
 * json type of job description parser
 *
 * Copyright (C) Zhang Ping <dqzhangp@163.com>
 *
 */

#include <stdio.h>
#include <string.h>

#include "parson.h"
#include "jobdesc.h"

GST_DEBUG_CATEGORY_EXTERN (GSTREAMILL);
#define GST_CAT_DEFAULT GSTREAMILL

gboolean jobdesc_is_valid (gchar *job)
{
        JSON_Value *val;
        JSON_Object *obj;
        GRegex *regex;
        GMatchInfo *match_info;
        gchar *name;

        val = json_parse_string_with_comments(job);
        if (val == NULL) {
                GST_ERROR ("parse job error.");
                return FALSE;

        } else if (json_value_get_type (val) != JSONObject){
                GST_ERROR ("job is not a json object.");
                json_value_free (val);
                return FALSE;
        }
        obj = json_value_get_object (val);

        name = (gchar *)json_object_get_string (obj, "name");
        if ((name == NULL) || (strlen (name) < 1)) {
                GST_ERROR ("invalid job with name property invalid");
                json_value_free (val);
                return FALSE;
        }

        regex = g_regex_new ("[`~!@$%^&*()+=|\\{[\\]}:\"\'<>?/ ]", G_REGEX_OPTIMIZE, 0, NULL);
        g_regex_match (regex, name, 0, &match_info);
        g_regex_unref (regex);
        if (g_match_info_matches (match_info)) {
                GST_ERROR ("invalid job name: %s", name);
                g_match_info_free (match_info);
                json_value_free (val);
                return FALSE;
        }
        json_value_free (val);

        return TRUE;
}

gchar * jobdesc_get_name (gchar *job)
{
        JSON_Value *val;
        JSON_Object *obj;
        gchar *name, *ret;

        val = json_parse_string_with_comments (job);
        obj = json_value_get_object (val);
        name = (gchar *)json_object_get_string (obj, "name");
        ret = g_strdup (name);
        json_value_free (val);

        return ret;
}

gint jobdesc_streams_count (gchar *job, gchar *pipeline)
{
        JSON_Value *val;
        JSON_Object *obj;
        JSON_Array *array;
        gsize size, i;
        gint count, index;
        gchar *bin, *ptype = NULL;

        val = json_parse_string_with_comments (job);
        obj = json_value_get_object (val);
        if (g_str_has_prefix (pipeline, "encoder")) {
                ptype = "appsrc";
                array = json_object_dotget_array (obj, "encoders");
                sscanf (pipeline, "encoder.%d", &index);
                obj = json_array_get_object (array, index);

        } else if (g_str_has_prefix (pipeline, "source")) {
                ptype = "appsink";
                obj = json_object_get_object (obj, "source");
        }
        array = json_object_dotget_array (obj, "bins");
        size = json_array_get_count (array);
        count = 0;
        for (i = 0; i < size; i++) {
                bin = (gchar *)json_array_get_string (array, i);
                if (g_strrstr (bin, ptype) != NULL)
                        count += 1;
        }
        json_value_free (val);

        return count;
}

gint jobdesc_encoders_count (gchar *job)
{
        JSON_Value *val;
        JSON_Object *obj;
        JSON_Array *encoders;
        gint count;

        val = json_parse_string_with_comments (job);
        obj = json_value_get_object (val);
        encoders = json_object_dotget_array (obj, "encoders");
        count = json_array_get_count (encoders);
        json_value_free (val);

        return count;
}

gboolean jobdesc_is_live (gchar *job)
{
        JSON_Value *val;
        JSON_Object *obj;

        val = json_parse_string_with_comments (job);
        obj = json_value_get_object (val);

        /* without is-live configure item, default is live */
        if (json_object_dotget_boolean (obj, "is-live")) {
                json_value_free (val);
                return TRUE;
        }
        json_value_free (val);

        return FALSE;
}

gchar * jobdesc_get_debug (gchar *job)
{
        JSON_Value *val;
        JSON_Object *obj;
        gchar *debug, *p;

        val = json_parse_string_with_comments (job);
        obj = json_value_get_object (val);
        p = (gchar *)json_object_get_string (obj, "debug");
        if (p == NULL) {
                debug = NULL;

        } else {
                debug = g_strdup (p);
        }
        json_value_free (val);

        return debug;
}

gchar * jobdesc_get_log_path (gchar *job)
{
        JSON_Value *val;
        JSON_Object *obj;
        gchar *log_path, *p;

        val = json_parse_string_with_comments (job);
        obj = json_value_get_object (val);
        p = (gchar *)json_object_get_string (obj, "log-path");
        if (p == NULL) {
                log_path = NULL;

        } else {
                log_path = g_strdup (p);
        }
        json_value_free (val);

        return log_path;
}

gchar ** jobdesc_bins (gchar *job, gchar *pipeline)
{
        JSON_Value *val;
        JSON_Object *obj;
        JSON_Array *array;
        gint i, count, index;
        gchar **p, *bin;

        val = json_parse_string_with_comments (job);
        obj = json_value_get_object (val);
        if (g_str_has_prefix (pipeline, "encoder")) {
                array = json_object_dotget_array (obj, "encoders");
                sscanf (pipeline, "encoder.%d", &index);
                obj = json_array_get_object (array, index);

        } else if (g_str_has_prefix (pipeline, "source")) {
                obj = json_object_get_object (obj, "source");
        }
        array = json_object_get_array (obj, "bins");
        count = json_array_get_count (array);
        p = g_malloc ((count + 1) * sizeof (gchar *));
        for (i = 0; i < count; i++) {
                bin = (gchar *)json_array_get_string (array, i);
                p[i] = g_strdup (bin);
        }
        p[i] = NULL;
        json_value_free (val);

        return p;
}

gchar ** jobdesc_element_properties (gchar *job, gchar *element)
{
        JSON_Value *val;
        JSON_Object *obj;
        JSON_Array *array;
        gchar *p, **properties, **pp;
        gsize count;
        gint i, index;

        val = json_parse_string_with_comments (job);
        obj = json_value_get_object (val);
        if (g_str_has_prefix (element, "encoder")) {
                array = json_object_get_array (obj, "encoders");
                sscanf (element, "encoder.%d", &index);
                obj = json_array_get_object (array, index);
                p = g_strrstr (element, "elements");
                obj = json_object_dotget_object (obj, p);

        } else if (g_str_has_prefix (element, "source")) {
                obj = json_object_dotget_object (obj, element);
        }
        if (obj == NULL) {
                json_value_free (val);
                return NULL;
        }
        count = json_object_get_count (obj);
        properties = (gchar **)g_malloc ((count + 1) * sizeof (gchar *));
        pp = properties;
        for (i = 0; i < count; i++) {
                p = (gchar *)json_object_get_name (obj, i);
                *pp = g_strdup (p);
                pp++;
        }
        *pp = NULL;
        json_value_free (val);

        return properties;
}

/**
 *
 * @property: (in): encoders.x.elements.element.property.name or source.elements.element.property.name
 */
gchar * jobdesc_element_property_value (gchar *job, gchar *property)
{
        JSON_Value *val;
        JSON_Object *obj;
        JSON_Array *array;
        JSON_Value_Type type;
        JSON_Value *value = NULL;
        gchar *p = NULL;
        gint64 i;
        gdouble n;
        gint index;

        val = json_parse_string_with_comments (job);
        obj = json_value_get_object (val);
        if (g_str_has_prefix (property, "encoder")) {
                array = json_object_dotget_array (obj, "encoders");
                sscanf (property, "encoder.%d", &index);
                obj = json_array_get_object (array, index);
                p = g_strrstr (property, "elements");
                value = json_object_dotget_value (obj, p);

        } else if (g_str_has_prefix (property, "source")) {
                value = json_object_dotget_value (obj, property);
        }
        if (value == NULL) {
                json_value_free (val);
                return NULL;
        }
        type = json_value_get_type (value);
        switch (type) {
        case JSONString:
                p = g_strdup (json_value_get_string (value));
                break;

        case JSONNumber:
                n = json_value_get_number (value);
                i = n;
                if (i == n) {
                        p = g_strdup_printf ("%ld", i);

                } else {
                        p = g_strdup_printf ("%f", n);
                }
                break;

        case JSONBoolean:
                if (json_value_get_boolean (value)) {
                        p = g_strdup ("TRUE");

                } else {
                        p = g_strdup ("FALSE");
                }
                break;

        default:
                GST_ERROR ("property value invalid.");
        }
        json_value_free (val);

        return p;
}

gchar * jobdesc_element_caps (gchar *job, gchar *element)
{
        JSON_Value *val;
        JSON_Object *obj;
        JSON_Array *array;
        gchar *p, *caps;
        gint index;

        val = json_parse_string_with_comments (job);
        obj = json_value_get_object (val);
        if (g_str_has_prefix (element, "encoder")) {
                array = json_object_dotget_array (obj, "encoders");
                sscanf (element, "encoder.%d", &index);
                obj = json_array_get_object (array, index);

        } else {
                obj = json_object_get_object (obj, "source");
        }
        p = g_strrstr (element, "elements");
        caps = (gchar *)json_object_dotget_string (obj, p);
        if (caps == NULL) {
                p = NULL;

        } else {
	        p = g_strdup (caps);
        }
        json_value_free (val);

        return p;
}

gchar * jobdesc_udpstreaming (gchar *job, gchar *pipeline)
{
        JSON_Value *val;
        JSON_Object *obj;
        JSON_Array *array;
        gint index;
        gchar *p, *udpstreaming;

        val = json_parse_string_with_comments (job);
        obj = json_value_get_object (val);
        array = json_object_dotget_array (obj, "encoders");
        sscanf (pipeline, "encoder.%d", &index);
        obj = json_array_get_object (array, index);
        p = (gchar *)json_object_get_string (obj, "udpstreaming");
        if (p == NULL) {
                udpstreaming = NULL;

        } else {
                udpstreaming = g_strdup (p);
        }
        json_value_free (val);

        return udpstreaming;
}

gboolean jobdesc_m3u8streaming (gchar *job)
{
        JSON_Value *val;
        JSON_Object *obj;
        gboolean m3u8streaming;

        val = json_parse_string_with_comments (job);
        obj = json_value_get_object (val);
        if (json_object_get_object (obj, "m3u8streaming") != NULL) {
                m3u8streaming = TRUE;

        } else {
                m3u8streaming = FALSE;
        }
        json_value_free (val);

        return m3u8streaming;
}

guint jobdesc_m3u8streaming_version (gchar *job)
{
        JSON_Value *val;
        JSON_Object *obj;
        guint version;

        val = json_parse_string_with_comments (job);
        obj = json_value_get_object (val);
        version = json_object_dotget_number (obj, "m3u8streaming.version");
        json_value_free (val);

        return version;
}

guint jobdesc_m3u8streaming_window_size (gchar *job)
{
        JSON_Value *val;
        JSON_Object *obj;
        guint window_size;

        val = json_parse_string_with_comments (job);
        obj = json_value_get_object (val);
        window_size = json_object_dotget_number (obj, "m3u8streaming.window-size");
        json_value_free (val);

        return window_size;
}

GstClockTime jobdesc_m3u8streaming_segment_duration (gchar *job)
{
        JSON_Value *val;
        JSON_Object *obj;
        GstClockTime segment_duration;

        val = json_parse_string_with_comments (job);
        obj = json_value_get_object (val);
        segment_duration = GST_SECOND * json_object_dotget_number (obj, "m3u8streaming.segment-duration");
        json_value_free (val);

        return segment_duration;
}

guint64 jobdesc_dvr_duration (gchar *job)
{
        JSON_Value *val;
        JSON_Object *obj;
        guint64 duration;

        val = json_parse_string_with_comments (job);
        obj = json_value_get_object (val);
        duration = json_object_get_number (obj, "dvr_duration");
        json_value_free (val);

        return duration;
}

