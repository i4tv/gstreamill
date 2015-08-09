
/*
 *  utils
 *
 *  Copyright (C) Zhang Ping <dqzhangp@163.com>
 */

#define _XOPEN_SOURCE
#include <time.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "utils.h"

gchar * unicode_file_name_2_shm_name (gchar *filename)
{
    const guchar *data;
    gsize len;
    gchar *shm_name;
    gint i;

    data = (guchar *)filename;
    len = strlen (filename);
    shm_name = g_base64_encode (data, len);
    for (i = 0; i < strlen (shm_name); i++) {
        if (shm_name[i] == '+') {
            shm_name[i] = '=';
        }
        if (shm_name[i] == '/') {
            shm_name[i] = '_';
        }
    }

    return shm_name;
}

gchar * get_address (struct sockaddr in_addr)
{
    struct sockaddr_in *addr;

    addr = (struct sockaddr_in *)&in_addr;
    return inet_ntoa (addr->sin_addr);
}

gushort get_port (struct sockaddr in_addr)
{
    struct sockaddr_in *addr;

    addr = (struct sockaddr_in *)&in_addr;
    return ntohs (addr->sin_port);
}

/**
 * timestamp_to_segment_dir:
 * @timestamp: (in): timestamp to be converted segment dir
 *
 * Convert timestamp to segment dir: yyyymmddhh.
 *
 * Returns: char type of segment dir
 */
gchar *timestamp_to_segment_dir (time_t timestamp)
{
    struct tm tm;
    gchar *seg_path;

    if (NULL == localtime_r (&timestamp, &tm)) {
        GST_ERROR ("timestamp to segment dir error: %lu", timestamp);
        return NULL;
    }
    seg_path = g_strdup_printf ("%04d%02d%02d%02d",
            tm.tm_year + 1900,
            tm.tm_mon + 1,
            tm.tm_mday,
            tm.tm_hour);

    return seg_path;
}

gint segment_dir_to_timestamp (gchar *dir, time_t *timestamp)
{
    gint number, year, month, mday, hour;
    struct tm tm;
    gchar date[20];

    number = sscanf (dir, "%04d%02d%02d%02d", &year, &month, &mday, &hour);
    if (number != 4) {
        GST_WARNING ("segment dir to timestamp error: %s", dir);
        return -1;
    }

    sprintf (date, "%04d-%02d-%02d %02d:00:00", year, month, mday, hour);
    memset (&tm, 0, sizeof (struct tm));
    strptime (date, "%Y-%m-%d %H:%M:%S", &tm);
    *timestamp = mktime (&tm);

    return 0;
}
