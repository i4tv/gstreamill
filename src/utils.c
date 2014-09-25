
/*
 *  utils
 *
 *  Copyright (C) Zhang Ping <zhangping@163.com>
 */

#include <string.h>

#include "utils.h"

gchar * unicode_file_name_2_shm_name (gchar *filename)
{
        gchar *shm_name;
        gint i;

        shm_name = g_base64_encode (filename, strlen (filename));
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

