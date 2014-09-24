
/*
 *  job
 *
 *  Copyright (C) Zhang Ping <zhangping@163.com>
 */

#include <string.h>

#include "utils.h"

gchar * bin2hexstr (gchar *binary)
{
        gint i;
        gchar *hexstr, *p;

        hexstr = g_strdup_printf ("%02x", binary[0]);
        for (i = 1; i < strlen (binary); i++) {
                p = hexstr;
                hexstr = g_strdup_printf ("%s%02x", p, binary[i]);
                g_free (p);
        }
GST_ERROR ("hex string: %s", hexstr);
        return hexstr;
}

