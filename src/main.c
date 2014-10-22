/*
 * gstreamill main.
 *
 * Copyright (C) Zhang Ping <dqzhangp@163.com>
 *
 */

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <glib/gstdio.h>
#include <gst/gst.h>
#include <string.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <locale.h>
#include <pwd.h>
#include <grp.h>

#include "gstreamill.h"
#include "httpstreaming.h"
#include "httpmgmt.h"
#include "parson.h"
#include "jobdesc.h"
#include "log.h"

#define GSTREAMILL_USER "gstreamill"
#define GSTREAMILL_GROUP "gstreamill"
#define PID_FILE "/var/run/gstreamill/pid"

GST_DEBUG_CATEGORY(GSTREAMILL);
#define GST_CAT_DEFAULT GSTREAMILL

Log *_log;

static void sighandler (gint number)
{
        log_reopen (_log);
}

static void stop_job (gint number)
{
        exit (0);
}

static void print_version_info ()
{
        guint major, minor, micro, nano;
        const gchar *nano_str;

        gst_version (&major, &minor, &micro, &nano);
        if (nano == 1) {
                nano_str = "(git)";

        } else if (nano == 2) {
                nano_str = "(Prerelease)";

        } else {
                nano_str = "";
        }

        g_print ("gstreamill version: %s\n", VERSION);
        g_print ("gstreamill build: %s %s\n", __DATE__, __TIME__);
        g_print ("gstreamer version : %d.%d.%d %s\n", major, minor, micro, nano_str);
}

static gint set_user_and_group ()
{
        struct passwd *pwd;
        struct group *grp;

        /* set group id */
        errno = 0;
        grp = getgrnam (GSTREAMILL_GROUP);
        if ((grp == NULL) && (errno == -1)) {
                perror ("getgrnam failed");
                return 4;

        } else if (grp == NULL) {
                g_print ("getgrnam(\"%s\") failed\n", GSTREAMILL_GROUP);
                return 5;
        }
        if (setgid (grp->gr_gid) == -1) {
                perror ("setuid failure");
                return 6;
        }

        /* set user id */
        errno = 0;
        pwd = getpwnam (GSTREAMILL_USER);
        if ((pwd == NULL) && (errno == -1)) {
                perror ("getpwnam failed");
                return 1;

        } else if (pwd == NULL) {
                g_print ("getpwnam(\"%s\") failed\n", GSTREAMILL_USER);
                return 2;
        }
        if (setuid (pwd->pw_uid) == -1) {
                perror ("setuid failure");
                return 3;
        }

        return 0;
}

static gint init_log (gchar *log_path)
{
        gint ret;

        _log = log_new ("log_path", log_path, NULL);

        ret = log_set_log_handler (_log);
        if (ret != 0) {
                return ret;
        }

        /* remove gstInfo default handler. */
        gst_debug_remove_log_function (gst_debug_log_default);

        return 0;
}

static gboolean stop = FALSE;
static gboolean version = FALSE;
static gchar *job_file = NULL;
static gchar *log_dir = "/var/log/gstreamill";
static gchar *media_dir = "/var/gstreamill";
static gchar *http_mgmt = "0.0.0.0:20118";
static gchar *http_streaming = "0.0.0.0:20119";
static gchar *shm_name = NULL;
static gint job_length = -1;
static GOptionEntry options[] = {
        {"job", 'j', 0, G_OPTION_ARG_FILENAME, &job_file, ("-j /full/path/to/job.file: Specify a job file, full path is must."), NULL},
        {"log", 'l', 0, G_OPTION_ARG_FILENAME, &log_dir, ("-l /full/path/to/log: Specify log path, full path is must."), NULL},
        {"media", 'd', 0, G_OPTION_ARG_FILENAME, &media_dir, ("-l /full/path/to/media: Specify media path for dvr and transcode, full path is must."), NULL},
        {"httpmgmt", 'm', 0, G_OPTION_ARG_STRING, &http_mgmt, ("-m http managment address, default is 0.0.0.0:20118."), NULL},
        {"httpstreaming", 'a', 0, G_OPTION_ARG_STRING, &http_streaming, ("-a http streaming address, default is 0.0.0.0:20119."), NULL},
        {"name", 'n', G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_STRING, &shm_name, NULL, NULL},
        {"joblength", 'q', G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_INT, &job_length, NULL, NULL},
        {"stop", 's', 0, G_OPTION_ARG_NONE, &stop, ("Stop gstreamill."), NULL},
        {"version", 'v', 0, G_OPTION_ARG_NONE, &version, ("display version information and exit."), NULL},
        {NULL}
};

static gint create_pid_file ()
{
        gchar *pid;
        GError *err = NULL;

        pid = g_strdup_printf ("%d", getpid ());
        if (!g_file_set_contents (PID_FILE, pid, strlen (pid), &err)) {
                g_printf ("write pid %s failure: %s\n", PID_FILE, err->message);
                g_error_free (err);
                g_free (pid);
                return 1;
        }
        g_free (pid);

        return 0;
}

static void remove_pid_file ()
{
        if (g_unlink (PID_FILE) == -1) {
                GST_ERROR ("unlink pid file error: %s", g_strerror (errno));
        }
}

Gstreamill *gstreamill;

static void stop_gstreamill (gint number)
{
        gstreamill_stop (gstreamill);
        remove_pid_file ();
}

int main (int argc, char *argv[])
{
        HTTPMgmt *httpmgmt;
        HTTPStreaming *httpstreaming;
        GMainLoop *loop;
        GOptionContext *ctx;
        GError *err = NULL;
        gboolean foreground;
        struct rlimit rlim;

        setlocale (LC_ALL, "");

        ctx = g_option_context_new (NULL);
        g_option_context_add_main_entries (ctx, options, NULL);
        g_option_context_add_group (ctx, gst_init_get_option_group ());
        if (!g_option_context_parse (ctx, &argc, &argv, &err)) {
                g_print ("Error initializing: %s\n", GST_STR_NULL (err->message));
                exit (1);
        }
        g_option_context_free (ctx);
        GST_DEBUG_CATEGORY_INIT (GSTREAMILL, "gstreamill", 0, "gstreamill log");

        if (version) {
                print_version_info ();
                exit (0);
        }

        if (set_user_and_group () != 0) {
                g_print ("set user and group failure\n");
                exit (2);
        }

        /* stop gstreamill. */
        if (stop) {
                gchar *pid_str;
                gint pid;

                g_file_get_contents (PID_FILE, &pid_str, NULL, NULL);
                if (pid_str == NULL) {
                        g_print ("File %s not found, check if gstreamill is running.\n", PID_FILE);
                        exit (1);
                }
                pid = atoi (pid_str);
                g_free (pid_str);
                g_print ("stoping gstreamill with pid %d ...\n", pid);
                kill (pid, SIGUSR2);
                exit (0);
        }

        if (job_file != NULL) {
                /* gstreamill command with job, run in foreground */
                foreground = TRUE;

        } else {
                /* gstreamill command without job, run in background */
                foreground = FALSE;
        }

        if (gst_debug_get_default_threshold () < GST_LEVEL_WARNING) {
                gst_debug_set_default_threshold (GST_LEVEL_WARNING);
        }

        /* subprocess, create_job_process */
        if (shm_name != NULL) {
                gint fd;
                gchar *job_desc, *p;
                Job *job;
                gchar *log_path, *name;
                gint ret;

                /* read job description from share memory */
                job_desc = NULL;
                fd = shm_open (shm_name, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
                if (ftruncate (fd, job_length) == -1) {
                        exit (3);
                }
                p = mmap (NULL, job_length, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
                job_desc = g_strdup (p);

                if ((job_desc != NULL) && (!jobdesc_is_valid (job_desc))) {
                        exit (4);
                }

                name = (gchar *)jobdesc_get_name (job_desc);
                /* initialize log */
                if (!jobdesc_is_live (job_desc)) {
                        gchar *p;

                        p = jobdesc_get_log_path (job_desc);
                        log_path = g_build_filename (p, "gstreamill.log", NULL);
                        g_free (p);

                } else {
                        log_path = g_build_filename (log_dir, name, "gstreamill.log", NULL);
                }
                ret = init_log (log_path);
                g_free (log_path);
                if (ret != 0) {
                        exit (5);
                }

                /* launch a job. */
                job = job_new ("name", name, "job", job_desc, NULL);
                job->is_live = jobdesc_is_live (job_desc);
                job->eos = FALSE;
                signal (SIGPIPE, SIG_IGN);
                signal (SIGUSR1, sighandler);
                signal (SIGUSR2, stop_job);
                loop = g_main_loop_new (NULL, FALSE);
                if (job_initialize (job, TRUE, media_dir) != 0) {
                        GST_ERROR ("initialize livejob failure, exit");
                        exit (6);
                }
                if (job_start (job) != 0) {
                        GST_ERROR ("start livejob failure, exit");
                        exit (7);
                }
                GST_WARNING ("livejob %s starting ...", name);
                g_free (name);
                g_free (job_desc);

                g_main_loop_run (loop);

        } else {
                /* set parent process maximum of core file */
                rlim.rlim_cur = RLIM_INFINITY;
                rlim.rlim_max = RLIM_INFINITY;
                if (setrlimit (RLIMIT_CORE, &rlim) == -1) {
                        GST_ERROR ("setrlimit error: %s", g_strerror (errno));
                }
        }

        /* run in background? */
        if (!foreground) {
                gchar *path;
                gint ret;

                /* pid file exist? */
                if (g_file_test (PID_FILE, G_FILE_TEST_EXISTS)) {
                        g_print ("file %s found, gstreamill already running !!!\n", PID_FILE);
                        exit (1);
                }

                /* media directory */
                path = g_strdup_printf ("%s/dvr", media_dir);
                if (!g_file_test (path, G_FILE_TEST_EXISTS)) {
                        g_printf ("Create DVR directory: %s", path);
                        if (g_mkdir_with_parents (path, 0755) != 0) {
                                g_printf ("Create DVR directory failure: %s", path);
                        }
                }
                g_free (path);
                path = g_strdup_printf ("%s/transcode/in", media_dir);
                if (!g_file_test (path, G_FILE_TEST_EXISTS)) {
                        g_printf ("Create transcode directory: %s", path);
                        if (g_mkdir_with_parents (path, 0755) != 0) {
                                g_printf ("Create transcode directory failure: %s", path);
                        }
                }
                g_free (path);
                path = g_strdup_printf ("%s/transcode/out", media_dir);
                if (!g_file_test (path, G_FILE_TEST_EXISTS)) {
                        g_printf ("Create transcode directory: %s", path);
                        if (g_mkdir_with_parents (path, 0755) != 0) {
                                g_printf ("Create transcode directory failure: %s", path);
                        }
                }
                g_free (path);

                /* log to file */
                path = g_build_filename (log_dir, "gstreamill.log", NULL);
                ret = init_log (path);
                g_free (path);
                if (ret != 0) {
                        g_print ("Init log error, ret %d.\n", ret);
                        exit (1);
                }

                /* create pid file */
                if (create_pid_file () != 0) {
                        exit (1);
                }

                /* daemonize */
                if (daemon (0, 0) != 0) {
                        g_print ("Failed to daemonize");
                        remove_pid_file ();
                        exit (1);
                }

                /* customize signal */
                signal (SIGUSR1, sighandler);
                signal (SIGUSR2, stop_gstreamill);
        }

        /* ignore SIGPIPE */
        signal (SIGPIPE, SIG_IGN);

        GST_WARNING ("gstreamill started ...");

        loop = g_main_loop_new (NULL, FALSE);

        /* gstreamill */
        gstreamill = gstreamill_new ("daemon", !foreground, "log_dir", log_dir, "media_dir", media_dir, NULL);
        if (gstreamill_start (gstreamill) != 0) {
                GST_ERROR ("start gstreamill error, exit.");
                remove_pid_file ();
                exit (1);
        }

        /* httpstreaming, pull */
        httpstreaming = httpstreaming_new ("gstreamill", gstreamill, "address", http_streaming, NULL);
        if (httpstreaming_start (httpstreaming, 10) != 0) {
                GST_ERROR ("start httpstreaming error, exit.");
                remove_pid_file ();
                exit (1);
        }

        if (!foreground) {
                /* run in background, management via http */
                httpmgmt = httpmgmt_new ("gstreamill", gstreamill, "address", http_mgmt, NULL);
                if (httpmgmt_start (httpmgmt) != 0) {
                        GST_ERROR ("start http mangment error, exit.");
                        remove_pid_file ();
                        exit (1);
                }

        } else {
                /* run in foreground, start job */
                gchar *job, *p, *result;
                JSON_Value *val;
                JSON_Object *obj;

                if (!g_file_get_contents (job_file, &job, NULL, NULL)) {
                        GST_ERROR ("Read job file %s error.", job_file);
                        exit (1);
                }
                p = gstreamill_job_start (gstreamill, job);
                val = json_parse_string (p);
                obj = json_value_get_object (val);
                result = (gchar *)json_object_get_string (obj, "result");
                GST_WARNING ("start job result: %s.", result);
                if (g_strcmp0 (result, "success") != 0) {
                        exit (1);
                }
                json_value_free (val);
                g_free (p);
        }

        g_main_loop_run (loop);

        return 0;
}

