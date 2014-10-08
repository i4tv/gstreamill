
/*
 *  utils
 *
 *  Copyright (C) Zhang Ping <zhangping@163.com>
 */

#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

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

gchar * get_address (struct sockaddr in_addr)
{
        struct sockaddr_in *addr;

        addr = (struct sockaddr_in *)&in_addr;
        return inet_ntoa (addr->sin_addr);
}

gshort get_port (struct sockaddr in_addr)
{
        struct sockaddr_in *addr;

        addr = (struct sockaddr_in *)&in_addr;
        return ntohs (addr->sin_port);
}
