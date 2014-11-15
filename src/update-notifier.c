/* update-notifier.c
 * Copyright (C) 2004 Lukas Lipka <lukas@pmad.net>
 *           (C) 2004 Michael Vogt <mvo@debian.org>
 *           (C) 2004 Michiel Sikkes <michiel@eyesopened.nl>
 *           (C) 2004-2009 Canonical
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor
 * Boston, MA  02110-1301 USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <unistd.h>
#include <sys/types.h>
#include <stdlib.h>
#include <signal.h>
#include <grp.h>
#include <pwd.h>
#include <limits.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <gio/gio.h>

#include "update-notifier.h"
#include "update.h"
#include "hooks.h"
#include "gdu.h"
#include "reboot.h"
#include "firmware.h"
#include "crash.h"
#include "avahi.h"

/* some prototypes */
extern gboolean up_get_clipboard (void);
gboolean update_timer_finished(gpointer data);

// the time when we check for fam events
#define TIMEOUT_FAM 1000*5 /* 5 sec */

// the timeout (in msec) for apt-get update (changes in 
// /var/lib/apt/lists{/partial})
#define TIMEOUT_APT_GET_UPDATE 1000*30 /* 30 sec */

// the timeout (in sec) when a further activity from dpkg/apt
// causes the applet to "ungray"
#define TIMEOUT_APT_RUN 600 /* 600 sec */

// force startup even if the user is not in the admin group
static gboolean FORCE_START=FALSE;

// force gksu for all menu actions
static gboolean FORCE_GKSU=FALSE;

// delay on startup (to make session startup faster)
static int STARTUP_DELAY=1;

// global debug options
int HOOK_DEBUG = 0;
int UPDATE_DEBUG = 0;
int INOTIFY_DEBUG = 0;
int FIRMWARE_DEBUG = 0;

// logging stuff
static void debug_log_handler (const gchar   *log_domain,
                              GLogLevelFlags log_level,
                              const gchar   *message,
                              gpointer       user_data)
{
   if (HOOK_DEBUG > 0 && strcmp(log_domain, "hooks") == 0)
       g_log_default_handler (log_domain, log_level, message, user_data);
   if (UPDATE_DEBUG > 0 && strcmp(log_domain, "update") == 0)
       g_log_default_handler (log_domain, log_level, message, user_data);
   if (INOTIFY_DEBUG > 0 && strcmp(log_domain, "inotify") == 0)
       g_log_default_handler (log_domain, log_level, message, user_data);
   if (FIRMWARE_DEBUG > 0 && strcmp(log_domain, "firmware") == 0)
       g_log_default_handler (log_domain, log_level, message, user_data);
}

static inline void
g_debug_inotify(const char *msg, ...)
{
   va_list va;
   va_start(va, msg);
   g_logv("inotify",G_LOG_LEVEL_DEBUG, msg, va);
   va_end(va);
}

void invoke(const gchar *cmd, const gchar *desktop, gboolean with_gksu)
{
   GdkAppLaunchContext *context;
   GAppInfo *appinfo;
   GError *error = NULL;
   static GtkWidget *w = NULL;

   // gksu
   if(with_gksu || FORCE_GKSU) {
      invoke_with_gksu(cmd, desktop, FALSE);
      return;
   }

   // fake window to get the current server time *urgh*
   if (!w) {
      w = gtk_window_new(GTK_WINDOW_TOPLEVEL);
      gtk_widget_realize (w);
   }

   // normal launch
   context = gdk_app_launch_context_new ();
   guint32 timestamp =  gdk_x11_get_server_time (w->window);
   appinfo = g_app_info_create_from_commandline(cmd, 
						cmd, 
						G_APP_INFO_CREATE_NONE,
						&error);
   gdk_app_launch_context_set_timestamp (context, timestamp);
   if (!g_app_info_launch (appinfo, NULL, (GAppLaunchContext*)context, &error))
      g_warning ("Launching failed: %s\n", error->message);
   g_object_unref (context);
   g_object_unref (appinfo);

}

void
invoke_with_gksu(const gchar *cmd, const gchar *descr, gboolean whole_message)
{
        //g_print("invoke_update_manager ()\n");
        gchar *argv[5];
	argv[0] = "/usr/bin/gksu";
	argv[1] = whole_message ? "--message" : "--desktop";
	argv[2] = (gchar*)descr;
	argv[3] = (gchar*)cmd;
	argv[4] = NULL;

	g_spawn_async (NULL, argv, NULL, 0, NULL, NULL, NULL, NULL);
}



gboolean
trayapplet_create (TrayApplet *un, char *name)
{
        //g_print("trayicon_create()\n");

	/* setup widgets */
	un->tray_icon = gtk_status_icon_new_from_icon_name (name);
	un->name = name;
	gtk_status_icon_set_visible (un->tray_icon, FALSE);

	return TRUE;
}


/* 
 the following files change:
 on "install":
  - /var/lib/dpkg/lock
  - /var/lib/dpkg/ *
  - /var/lib/update-notifier/dpkg-run-stamp
 on "update":
  - /var/lib/apt/lists/lock
  - /var/lib/apt/lists/ *
  - /var/lib/dpkg/lock
  - /var/lib/apt/periodic/update-success-stamp
*/
void
monitor_cb(GFileMonitor *handle,
	   GFile *monitor_f,
	   GFile *other_monitor_f,
	   GFileMonitorEvent event_type,
	   gpointer user_data)
{
   UpgradeNotifier *un = (UpgradeNotifier*)user_data;

   gchar *info_uri = g_file_get_path(monitor_f);
#if 0
   g_debug_inotify("info_uri: %s\n", info_uri);
   g_debug_inotify("event_type: %i\n",event_type);
#endif

   // we ignore lock file events because we can only get 
   // when a lock was taken, but not when it was removed
   if(g_str_has_suffix(info_uri, "lock"))
      return;

   // look for apt-get install/update
   if(g_str_has_prefix(info_uri,"/var/lib/apt/") 
      || g_str_has_prefix(info_uri,"/var/cache/apt/")
      || strcmp(info_uri,"/var/lib/dpkg/status") == 0) {
      g_debug_inotify("apt_get_running=TRUE");
      un->apt_get_runing=TRUE;
   } 
   if(strstr(info_uri, "/var/lib/update-notifier/dpkg-run-stamp") ||
      strstr(info_uri, "/var/lib/apt/periodic/update-success-stamp")) {
      g_debug_inotify("dpkg_was_run=TRUE");
      un->dpkg_was_run = TRUE;
   } 
   if(strstr(info_uri, REBOOT_FILE)) {
      g_debug_inotify("reboot required\n");
      un->reboot_pending = TRUE;
   }
   if(strstr(info_uri, HOOKS_DIR)) {
      g_debug_inotify("new hook!\n");
      un->hook_pending = TRUE;
   }
   if(strstr(info_uri, CRASHREPORT_DIR)) {
      g_debug_inotify("crashreport found\n");
      un->crashreport_pending = TRUE;
   }
   if(strstr(info_uri, UNICAST_LOCAL_AVAHI_FILE)) {
      g_debug_inotify("avahi disabled due to unicast .local domain\n");
      un->unicast_local_avahi_pending = TRUE;
   }
}

/*
 * We periodically check here what actions happend in this "time-slice". 
 * This can be:
 * - dpkg_was_run=TRUE: set when apt wrote the "dpkg-run-stamp" file
 * - apt_get_runing: set when apt/dpkg activity is detected (in the 
 *                   lists-dir, archive-cache, or /var/lib/dpkg/status
 * - hook_pending: we have new upgrade hoook information
 * - reboot_pending: we need to reboot
 * - crashreport_pending: we have a new crashreport
 * - unicast_local_avahi_pending: avahi got disabled due to a unicast .local domain
 *
 */
gboolean file_monitor_periodic_check(gpointer data)

{
   UpgradeNotifier *un = (UpgradeNotifier *)data;

   // we are not ready yet, wait for the next timeslice
   if((un->reboot == NULL) || (un->crashreport == NULL))
      return TRUE;

   // DPkg::Post-Invoke has written a stamp file, that means a install/remove
   // operation finished, we can show hooks/reboot notifications then
   if(un->dpkg_was_run) {

      // check updates
      update_check(un->update);

      // any apt-get update  must be finished, otherwise 
      // apt-get install wouldn't be finished
      update_apt_is_running(un->update, FALSE);
      if(un->update_finished_timer > 0) 
	 g_source_remove(un->update_finished_timer);
      
      // show pending hooks/reboots
      if(un->hook_pending) {
	 //g_print("checking hooks now\n");
	 check_update_hooks(un->hook);
	 un->hook_pending = FALSE;
      }
      if(un->reboot_pending) {
	 //g_print("checking reboot now\n");
	 reboot_check (un->reboot);
	 un->reboot_pending = FALSE;
      }

      // apt must be finished when a new stamp file was writen, so we
      // reset the apt_get_runing time-slice field because its not 
      // important anymore (it finished runing)
      //
      // This may leave a 5s race condition when a apt-get install finished
      // and something new (apt-get) was started
      un->apt_get_runing = FALSE;
      un->last_apt_action = 0;
   }

   // apt-get update/install or dpkg is runing (and updates files in 
   // it's list/cache dir) or in /var/lib/dpkg/status
   if(un->apt_get_runing) 
      update_apt_is_running(un->update, TRUE);

   // update time information for apt/dpkg
   time_t now = time(NULL);
   if(un->apt_get_runing) 
      un->last_apt_action = now;

   // no apt operation for a long time
   if(un->last_apt_action > 0 &&
      (now - un->last_apt_action) >  TIMEOUT_APT_RUN) {
      update_apt_is_running(un->update, FALSE);
      update_check(un->update);
      un->last_apt_action = 0;
   }

   if(un->crashreport_pending) {
      g_print("checking for valid crashreport now\n");
      crashreport_check (un->crashreport);
      un->crashreport_pending = FALSE;
   }

   if(un->unicast_local_avahi_pending) {
      g_print("checking for disabled avahi due to unicast .local domain now\n");
      avahi_disabled_check ();
      un->unicast_local_avahi_pending = FALSE;
   }

   // reset the bitfields (for the next "time-slice")
   un->dpkg_was_run = FALSE;
   un->apt_get_runing = FALSE;

   return TRUE;
}




/* u_abort seems like an internal error notification.
 * End user might not understand the message at all */
void u_abort(gchar *msg)
{
   const char *fmt = "<span weight=\"bold\" size=\"larger\">%s</span>\n\n%s\n";
   gtk_dialog_run(GTK_DIALOG(gtk_message_dialog_new_with_markup(NULL, 0,
						     GTK_MESSAGE_ERROR,
						     GTK_BUTTONS_CLOSE,
						     fmt,
						     _("Internal error"),
						     msg)));
   g_free(msg);
   exit(1);
}

// FIXME: get the apt directories with apt-config or something
gboolean 
monitor_init(UpgradeNotifier *un)
{
   int i;
   GFileMonitor *monitor_handle;

   // monitor thise dirs
   static const char *monitor_dirs[] = { 
      "/var/lib/apt/lists/", "/var/lib/apt/lists/partial/", 
      "/var/cache/apt/archives/", "/var/cache/apt/archives/partial/", 
      HOOKS_DIR, 
      CRASHREPORT_DIR,
      NULL};
   for(i=0;monitor_dirs[i] != NULL;i++) {
      GError *error = NULL;
      GFile *gf = g_file_new_for_path(monitor_dirs[i]);
      monitor_handle = g_file_monitor_directory(gf, 0, NULL, &error);
      if(monitor_handle)
	 g_signal_connect(monitor_handle, "changed", (GCallback)monitor_cb, un);
      else
	 g_warning("can not add '%s'\n", monitor_dirs[i]);
   }

   // and those files
   static const char *monitor_files[] = { 
      "/var/lib/dpkg/status", 
      "/var/lib/update-notifier/dpkg-run-stamp", 
      "/var/lib/apt/periodic/update-success-stamp",
      REBOOT_FILE,
      UNICAST_LOCAL_AVAHI_FILE,
      NULL};
   for(i=0;monitor_files[i] != NULL;i++) {
      GError *error = NULL;
      GFile *gf = g_file_new_for_path(monitor_files[i]);
      monitor_handle = g_file_monitor_file(gf, 0, NULL, &error);
      if(monitor_handle)
	 g_signal_connect(monitor_handle, "changed", (GCallback)monitor_cb, un);
      else
	 g_warning("can not add '%s'\n", monitor_dirs[i]);
   }

   g_timeout_add (TIMEOUT_FAM, (GSourceFunc)file_monitor_periodic_check, un);


   return TRUE;
}




static gboolean
tray_icons_init(UpgradeNotifier *un)
{
   //g_debug_inotify("tray_icons_init");

   /* new upates tray icon */
   un->update = g_new0 (TrayApplet, 1);

   // check if the updates icon should be displayed /* Debian: yes */
   if (TRUE || in_admin_group() || FORCE_START) {
      trayapplet_create(un->update, "software-update-available");
      update_tray_icon_init(un->update);
   } else 
      un->update = NULL;

   /* update hook icon*/
   un->hook = g_new0 (TrayApplet, 1);
   trayapplet_create(un->hook, "hook-notifier");
   hook_tray_icon_init(un->hook);

   /* reboot required icon */
   un->reboot = g_new0 (TrayApplet, 1);
   trayapplet_create(un->reboot, "reboot-notifier");
   reboot_tray_icon_init(un->reboot);

   /* crashreport detected icon */
   un->crashreport = g_new0 (TrayApplet, 1);
   trayapplet_create(un->crashreport, "apport");
   crashreport_tray_icon_init(un->crashreport);

   return FALSE; // for the tray_destroyed_cb
}

// this function checks if the user is in the admin group
// if there is no admin group, we return true becuase there
// is no way to figure if the user is a admin or not
gboolean
in_admin_group()
{
   int i, ng = 0;
   gid_t *groups = NULL;

   struct group *grp = getgrnam("sudo");
   if(grp == NULL)
      return TRUE;
   /* The group is empty => treat everyone as admin */
   if (grp->gr_mem == NULL || *grp->gr_mem == NULL)
      return TRUE;
   if (FORCE_START)
      return TRUE;
   ng = getgroups (0, NULL);
   groups = (gid_t *) malloc (ng * sizeof (gid_t));

   i = getgroups (ng, groups);
   if (i != ng) {
     free (groups);
     return TRUE;
   }
   
   for(i=0;i<ng;i++) {
      if(groups[i] == grp->gr_gid) {
        free(groups);
        return TRUE;
      }
   }
   
   if(groups != NULL)
      free(groups);

   return FALSE;
}

static GOptionEntry entries[] = 
{
   { "debug-hooks", 0, 0, G_OPTION_ARG_NONE, &HOOK_DEBUG, "Enable hooks debugging"},
   { "debug-updates", 0, 0, G_OPTION_ARG_NONE, &UPDATE_DEBUG, "Enable updates/autolaunch debugging"},
   { "debug-inotify", 0, 0, G_OPTION_ARG_NONE, &INOTIFY_DEBUG, "Enable inotify debugging"},
   { "debug-firmware", 0, 0, G_OPTION_ARG_NONE, &FIRMWARE_DEBUG, "Enable firmware debugging"},
   { "force", 0, 0, G_OPTION_ARG_NONE, &FORCE_START, "Force start even if the user is not in the admin group"},
   { "force-use-gksu", 0, 0, G_OPTION_ARG_NONE, &FORCE_GKSU, "Force running all commands (update-manager, synaptic) with gksu" },
   { "startup-delay", 0, 0, G_OPTION_ARG_INT, &STARTUP_DELAY, "Delay startup by given amount of seconds" },
   { NULL }
};

int 
main (int argc, char **argv)
{
	UpgradeNotifier *un;
	GError *error = NULL;

	// init
	if(!gtk_init_with_args (&argc, &argv, 
				_("- inform about updates"), entries, 
				"update-notifier", &error) ) {
	   fprintf(stderr, _("Failed to init the UI: %s\n"), 
		   error ? error->message : _("unknown error"));
	   exit(1);
	}

	notify_init("update-notifier");
        bindtextdomain(PACKAGE, PACKAGE_LOCALE_DIR);
        bind_textdomain_codeset(PACKAGE, "UTF-8");
        textdomain(PACKAGE);

	// setup a custom debug log handler
	g_log_set_handler ("inotify", G_LOG_LEVEL_DEBUG,
			   debug_log_handler, NULL);
	g_log_set_handler ("hooks", G_LOG_LEVEL_DEBUG,
			   debug_log_handler, NULL);
	g_log_set_handler ("update", G_LOG_LEVEL_DEBUG,
			   debug_log_handler, NULL);
	g_log_set_handler ("firmware", G_LOG_LEVEL_DEBUG,
			   debug_log_handler, NULL);

	g_set_application_name (_("update-notifier"));
	gtk_window_set_default_icon_name ("update-notifier");

	//g_print("starting update-notifier\n");

	// check if it is running already
	if (!up_get_clipboard ()) {
	   g_warning ("already running?\n");
	   return 1;
	}

	/* Create the UpgradeNotifier object */
	un = g_new0 (UpgradeNotifier, 1);

	// check for .update-notifier dir and create if needed
	gchar *dirname = g_strdup_printf("%s/.update-notifier",
					 g_get_home_dir());
	if(!g_file_test(dirname, G_FILE_TEST_IS_DIR))
	   g_mkdir(dirname, 0700);
	g_free(dirname);

	// delay icon creation for 30s so that the desktop 
	// login is not slowed down by update-notifier
	g_timeout_add_seconds(STARTUP_DELAY, 
			      (GSourceFunc)(tray_icons_init), un);

        // initial check for avahi
        avahi_disabled_check();
	
	/* setup hal so that inserted cdroms can be checked */
	if(!up_do_hal_init(un)) {
	   g_warning("initializing gdu failed");
	}

	// init missing firmware monitoring
	missing_firmware_init();

	// init gio file monitoring
	monitor_init (un);
	
	/* Start the main gtk loop */
	gtk_main ();

	return 0;
}
