#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <libintl.h>
#include <sys/wait.h>
#include <glob.h>

#include <glib.h>
#include <glib/gstdio.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <dbus/dbus-glib.h>

#include "update-notifier.h"
#include "update.h"

#define UPGRADE_CHECKER PACKAGE_LIB_DIR"/update-notifier/apt-check"

// command, description, desktopfile, needs_gksu
const char* actions[][4] = {
   { "/usr/bin/update-manager", N_("Show updates"), 
     "/usr/share/applications/update-manager.desktop", 
     GINT_TO_POINTER(FALSE) },

   { "/usr/sbin/synaptic --dist-upgrade-mode --non-interactive --hide-main-window -o Synaptic::AskRelated=true",
     N_("Install all updates"), "/usr/share/applications/synaptic.desktop", 
     GINT_TO_POINTER(TRUE)
   },
   { "/usr/sbin/synaptic --update-at-startup --non-interactive --hide-main-window", 
     N_("Check for updates"), "/usr/share/applications/synaptic.desktop", 
     GINT_TO_POINTER(TRUE) },
   { "/usr/sbin/synaptic", N_("Start package manager"), 
     "/usr/share/applications/synaptic.desktop", 
     GINT_TO_POINTER(TRUE)},
   { NULL, NULL, NULL }
};

enum { 
   NOTIFICATION_DEFAULT, 
   NOTIFICATION_IGNORE, 
   NOTIFICATION_SHOW_UPDATES 
};

static inline void
g_debug_update(const char *msg, ...)
{
   va_list va;
   va_start(va, msg);
   g_logv("update",G_LOG_LEVEL_DEBUG, msg, va);
   va_end(va);
}

static gboolean
activate_cb (GtkWidget *widget, 
	     TrayApplet *ta)
{
   UpdateTrayAppletPrivate *priv = (UpdateTrayAppletPrivate*)ta->user_data;
   int index = priv->apt_is_running ? 3 : 0;
   
   // on double click we get two activate signals, so slow down things
   // here a bit to avoid double starting
   static time_t last_action = 0;
   if (time(NULL) - last_action > 1) 
      invoke (actions[index][0], actions[index][2], (long)actions[index][3]);
   last_action = time(NULL);
   return TRUE;
}

static gboolean
popup_cb (GtkStatusIcon *status_icon,
	  guint          button,
	  guint          activate_time,
	  TrayApplet     *un)
{
   gtk_menu_set_screen (GTK_MENU (un->menu),
			gtk_status_icon_get_screen(un->tray_icon));
   gtk_menu_popup (GTK_MENU (un->menu), NULL, NULL, 
		   gtk_status_icon_position_menu, un->tray_icon,
		   button, activate_time);
   return TRUE;
}

void
update_trayicon_update_tooltip (TrayApplet *ta, int num_upgrades)
{
   //g_print("update_tooltip: %p %p %p\n", ta, ta->tooltip, ta->eventbox);
   gchar *updates;

   updates = g_strdup_printf(ngettext("There is %i update available",
				      "There are %i updates available",
				      num_upgrades),
			     num_upgrades);
   gtk_status_icon_set_tooltip(ta->tray_icon, updates);
   g_free(updates);
}


void 
cb_action(GObject *self, void *user_data)
{
   TrayApplet *ta = user_data;
	
   int i = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(self), "action"));
   invoke (actions[i][0], _(actions[i][2]), (long)actions[i][3]);

   UpdateTrayAppletPrivate *priv = (UpdateTrayAppletPrivate*)ta->user_data;
   gconf_client_set_int(priv->gconf, GCONF_KEY_DEFAULT_ACTION, i, NULL);
}

void 
cb_preferences(GObject *self, void *user_data)
{
   invoke_with_gksu("/usr/bin/software-properties-gtk",
		    "/usr/share/applications/software-properties.desktop",
		    FALSE);    
}

void 
cb_toggled_show_notifications(GObject *self, void *data)
{
      TrayApplet *ta = (TrayApplet*)data;
      UpdateTrayAppletPrivate *priv = (UpdateTrayAppletPrivate*)ta->user_data;

      gboolean b = gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(self));
      gconf_client_set_bool(priv->gconf, GCONF_KEY_NO_UPDATE_NOTIFICATIONS, 
			    !b,NULL);
      

      NotifyNotification *n = priv->active_notification;
      if(n != NULL) {
	 notify_notification_close(n, NULL);
	 priv->active_notification = NULL;
      }
}


void
update_trayicon_create_menu(TrayApplet *ta)
{
	GtkWidget *menuitem;
	int i;

	ta->menu = gtk_menu_new ();

	for(i=0;actions[i][0]!=NULL;i++) {
	    if (!g_file_test(actions[i][2], G_FILE_TEST_EXISTS))
	        continue;
	   menuitem = gtk_menu_item_new_with_label (_(actions[i][1]));

	   gtk_menu_shell_append (GTK_MENU_SHELL (ta->menu), menuitem);
	   g_object_set_data(G_OBJECT(menuitem), "action", GINT_TO_POINTER(i));
	   g_signal_connect(G_OBJECT(menuitem), "activate", 
			    G_CALLBACK(cb_action), ta);
	}

	menuitem = gtk_separator_menu_item_new();
	gtk_menu_shell_append (GTK_MENU_SHELL (ta->menu), menuitem);

	menuitem = gtk_check_menu_item_new_with_label(_("Show notifications"));
	UpdateTrayAppletPrivate *priv = (UpdateTrayAppletPrivate*)ta->user_data;
	gboolean b = gconf_client_get_bool(priv->gconf,
					   GCONF_KEY_NO_UPDATE_NOTIFICATIONS, 
					   NULL);
	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(menuitem), !b);
	gtk_menu_shell_append (GTK_MENU_SHELL (ta->menu), menuitem);
	g_signal_connect(G_OBJECT(menuitem), "toggled", 
			 G_CALLBACK(cb_toggled_show_notifications), ta);
	

	menuitem = gtk_image_menu_item_new_from_stock (GTK_STOCK_PREFERENCES, NULL);
	if (g_file_test("/usr/bin/software-properties-gtk", G_FILE_TEST_EXISTS))
	    gtk_menu_shell_append (GTK_MENU_SHELL (ta->menu), menuitem);
	g_signal_connect(G_OBJECT(menuitem), "activate", 
			 G_CALLBACK(cb_preferences), (void*)ta);

	gtk_widget_show_all (ta->menu);
}

/* this tells the trayicon that apt is downloading something */
void 
update_apt_is_running(TrayApplet *ta, gboolean is_running)
{
   g_debug_update("update_apt_is_running: %i\n",is_running);
   if(ta == NULL)
      return;

   // update internal status
   UpdateTrayAppletPrivate *priv = (UpdateTrayAppletPrivate*)ta->user_data;
   priv->apt_is_running = is_running;

   // if the user wants auto-launch mode, do not show the icon
   // the auto launch stuff will do its magic later
   if(gconf_client_get_bool(priv->gconf, GCONF_KEY_AUTO_LAUNCH, NULL))
      return;

   if(is_running) {

      // gray out the icon if apt is running, we do this only once,
      // after the first time, the storage_type changes from ICON_NAME to
      // IMAGE_PIXBUF
      if(gtk_status_icon_get_storage_type(ta->tray_icon) == GTK_IMAGE_ICON_NAME) {
	 GdkPixbuf *src = gtk_icon_theme_load_icon(gtk_icon_theme_get_default(),
						   gtk_status_icon_get_icon_name(ta->tray_icon),
						   gtk_status_icon_get_size(ta->tray_icon), 
						   0, 
						   NULL);
	 GdkPixbuf *inactive = gdk_pixbuf_copy(src);
	 gdk_pixbuf_saturate_and_pixelate(src, inactive, 0.0, FALSE);
	 gtk_status_icon_set_from_pixbuf(ta->tray_icon, inactive);
	 g_object_unref(src);
	 g_object_unref(inactive);
      }

      // and update the tooltip
      gtk_status_icon_set_tooltip(ta->tray_icon, _("A package manager is working"));
      // and show it
      gtk_status_icon_set_visible(ta->tray_icon, TRUE);

      if(priv->active_notification != NULL) {
	 notify_notification_close(priv->active_notification, NULL);
	 priv->active_notification = NULL;
      }
   } else {
      gtk_status_icon_set_from_icon_name(ta->tray_icon, ta->name);
   }
}

// actually show the notification 
static gint 
show_notification(gpointer user_data)
{
   TrayApplet *ta = (TrayApplet *)user_data;
   UpdateTrayAppletPrivate *priv = (UpdateTrayAppletPrivate*)ta->user_data;

   // apt is runing, no point in showing a notification
   if(priv->apt_is_running) 
      return TRUE;

   // check if the update-icon is still visible (in the delay time a 
   // update may already have been performed)
   if(!gtk_status_icon_get_visible(ta->tray_icon))
      return FALSE;

   // now show a notification handle 
   gchar *updates;
   updates = g_strdup_printf(ngettext("There is %i update available. "
				      "Click on the notification "
				      "icon to show the "
				      "available update.",
				      "There are %i updates available. "
				      "Click on the notification "
				      "icon to show the "
				      "available updates.",
				      priv->num_upgrades), 
			     priv->num_upgrades);
   NotifyNotification *n = notify_notification_new(
			      _("Software updates available"),
			      updates,
			      NULL);

   GdkPixbuf* pix= gtk_icon_theme_load_icon(gtk_icon_theme_get_default(), 
					    GTK_STOCK_DIALOG_INFO, 48,0,NULL);
   notify_notification_set_icon_from_pixbuf (n, pix);
   g_object_unref(pix);
   notify_notification_set_timeout (n, 60*1000);
   notify_notification_show(n, NULL);
   // save the notification handle
   priv->active_notification = n;

   // remove this from the timeout now
   g_free(updates);
   return FALSE;
}

void 
show_error(TrayApplet *ta, gchar *error_str)
{
   gtk_status_icon_set_tooltip(ta->tray_icon, error_str);
   gtk_status_icon_set_from_icon_name(ta->tray_icon, "dialog-error");
   gtk_status_icon_set_visible(ta->tray_icon, TRUE);
}

static gboolean
outdated_nag(TrayApplet *ta)
{
   struct stat buf;
   if ((stat("/var/lib/apt/periodic/update-success-stamp", &buf) == 0) &&
       (time(NULL) - buf.st_mtime > OUTDATED_NAG_AGE) ) {
      gtk_status_icon_set_visible (ta->tray_icon, TRUE);
      ta->name = "gtk-dialog-warning-panel";
      GdkPixbuf *src = gtk_icon_theme_load_icon(gtk_icon_theme_get_default(),
						ta->name, 48, 
						GTK_ICON_LOOKUP_GENERIC_FALLBACK, 
						NULL);
      gtk_status_icon_set_from_pixbuf(ta->tray_icon, src);
      g_object_unref(src);
      gtk_status_icon_set_tooltip(ta->tray_icon,
				  _("The update information is outdated. "
				    "This may be caused by network "
				    "problems or by a repository that "
				    "is no longer available. "
				    "Please update manually "
				    "by clicking on this icon and then "
				    "selecting 'Check for updates' and "
				    "check if some of the listed "
				    "repositories fail."
				 ));
   }
   return FALSE;
}

// use ubuntu-system-service (if available) to check
// if the dpkg lock is taken currently or not
//
// if uncertain, return FALSE 
static gboolean
dpkg_lock_is_taken ()
{
   DBusGConnection *connection;
   GError *error;
   DBusGProxy *proxy;
   gboolean locked = FALSE;
  
   error = NULL;
   connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
   if (connection == NULL) {
      g_debug_update ("Failed to open connection to bus: %s\n", error->message);
      g_error_free (error);
      return FALSE;
   }

  proxy = dbus_g_proxy_new_for_name (connection,
                                     "com.ubuntu.SystemService",
				     "/",
				     "com.ubuntu.SystemService");
  error = NULL;
  if (!dbus_g_proxy_call (proxy, "is_package_system_locked", &error, 
			  G_TYPE_INVALID,
                          G_TYPE_BOOLEAN, &locked, G_TYPE_INVALID)) {
     g_debug_update ("error during dbus call: %s\n", error->message);
     g_error_free (error);
     g_object_unref (proxy);
     return FALSE;
  }
  g_object_unref (proxy);

  g_debug_update ("is_package_system_locked: %i", locked);
  return locked;
}

#if 0
// ask devicekit.Power if we run on battery
static gboolean
running_on_battery ()
{
   DBusGConnection *connection;
   GError *error;
   DBusGProxy *proxy;
   gboolean on_battery = FALSE;
  
   error = NULL;
   connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
   if (connection == NULL) {
      g_debug_update ("Failed to open connection to bus: %s\n", error->message);
      g_error_free (error);
      return FALSE;
   }

  proxy = dbus_g_proxy_new_for_name (connection,
				     "org.freedesktop.DeviceKit.Power", 
				     "/org/freedesktop/DeviceKit/Power",
				     "org.freedesktop.DBus.Properties");
  error = NULL;
  if (!dbus_g_proxy_call (proxy, "Get", &error, 
			  G_TYPE_STRING, "org.freedesktop.DeviceKit.Power", 
			  G_TYPE_STRING, "on_battery",
			  G_TYPE_INVALID,
                          G_TYPE_BOOLEAN, &on_battery, 
			  G_TYPE_INVALID)) {
     g_debug_update ("failed Get dbus call: %s\n", 
		     error?error->message:"no error given");
     if(error)
	g_error_free (error);
     return FALSE;
  }
  g_debug_update ("on_battery: %i", on_battery);
  return on_battery;
}
#endif

// check if the security auto launch interval is over
// and if the user is not using auto install of security
// updates
static gboolean
auto_launch_security_now(UpdateTrayAppletPrivate *priv, 
			 time_t now, 
			 time_t last_launch)
{
   char *cmd[3];
   int ret;
   GError *error = NULL;

   // no security updates
   if (priv->num_security == 0)
      return FALSE;
	 
   // security updates, but already launched recently
   if ((last_launch + AUTOLAUNCH_MINIMAL_SECURITY_INTERVAL) > now) {
      g_debug_update("security updates, but update-manager was launched "
		     "within the AUTOLAUNCH_MINIMAL_SECURITY_INTERVAL\n");
      return FALSE;
   }

   // run apt_check.py in "check if we install updates unattended" mode
   // a exit status > 0 indicates that its unattended-upgrades is used
   if ( g_file_test ("/usr/bin/unattended-upgrades", 
		     G_FILE_TEST_IS_EXECUTABLE) ) {
      cmd[0] = UPGRADE_CHECKER;
      cmd[1] = "--security-updates-unattended";
      cmd[2] = NULL;
      if (g_spawn_sync("/", cmd, NULL, 0, NULL, NULL, 
		       NULL, NULL, &ret, &error)) {
	 g_debug("--security-updates-unattended: %i\n", WEXITSTATUS(ret));
	 if( WEXITSTATUS(ret) > 0 )
	    return FALSE;
      } else if (error) {
	 g_print("error: %s\n", error->message);
      }
   }

   g_debug_update("security updates, auto-launching");
   return TRUE;
}

// check the logs of dpkg and apt for timestamps, the idea
// is to not auto launch if dpkg/apt were run manually by the
// user
static time_t
newest_log_file_timestamp()
{
   struct stat buf;
   int i, j;
   time_t newest_log_stamp = 0;

   char *log_glob[] = { "/var/log/dpkg.log*",
		    "/var/log/apt/term.log*",
		    NULL };

   for (i=0; log_glob[i] != NULL; i++) 
   {
      glob_t pglob;
      if(glob(log_glob[i], 0, NULL, &pglob) != 0) {
	 g_warning("error from glob %s\n", log_glob[i]);
	 continue;
      }
      for(j=0;j < pglob.gl_pathc; j++) {

	 const char *log = pglob.gl_pathv[j];
	 if(g_stat(log, &buf) <0) {
	    g_warning("can't stat %s\n", log);
	    continue;
	 }
	 if(buf.st_size == 0) {
	    g_warning("log file empty (logrotate?) %s\n", log);
	    continue;
	 }
	 time_t mtime = buf.st_mtime;
	 g_debug_update ("mtime from %s: %i (%s)\n", log, mtime, ctime(&mtime));
	 time_t bctime = buf.st_ctime;
	 g_debug_update ("ctime from %s: %i (%s)\n", log, bctime, ctime(&bctime));
	 newest_log_stamp = MAX(MAX(mtime, bctime), newest_log_stamp);
	 g_debug_update ("last_launch from %s: %i (%s)\n", log, newest_log_stamp, ctime(&newest_log_stamp));
      }
      globfree(&pglob);
   }
   return newest_log_stamp;
}

// check if the auto launch interval is over and its 
// time to launch again and if the dpkg lock is currently 
// not taken
static gboolean 
auto_launch_now (UpdateTrayAppletPrivate *priv)
{
   int interval_days = 0;
   time_t last_launch = 0;

   if (dpkg_lock_is_taken())
      return FALSE;

#if 0 // disabled because we need more cleverness here in order to cover
      // the case where people always work on battery (and charge overnight)
   if (running_on_battery())
      return FALSE;
#endif

   // when checking for regular updates honor the 
   // regular_auto_launch_interval
   interval_days = gconf_client_get_int(priv->gconf,
					GCONF_KEY_AUTO_LAUNCH_INTERVAL, 
					NULL);
   g_debug_update ("interval_days from gconf: %i\n", interval_days);

   if (interval_days <= 0) 
      return TRUE;

   // check last launch time 
   last_launch = gconf_client_get_int(priv->gconf,
				      GCONF_KEY_LAST_LAUNCH,
				      NULL);
   g_debug_update ("last_launch from gconf: %i (%s)\n", last_launch, ctime(&last_launch));

   time_t now = time(NULL);
   if (auto_launch_security_now(priv, now, last_launch))
      return TRUE;

   time_t log_files = newest_log_file_timestamp();
   last_launch = MAX(log_files, last_launch);

   g_debug_update("time now %i (%s), delta: %i", now, ctime(&now), now-last_launch);
   if ((last_launch + (24*60*60*interval_days)) < now) {
      g_debug_update ("need to auto launch");
      return TRUE;
   }

   return FALSE;
}

gboolean
update_check (TrayApplet *ta)
{
   if(ta == NULL)
      return FALSE;

   g_debug_update ("update_check()\n");
   UpdateTrayAppletPrivate *priv = (UpdateTrayAppletPrivate*)ta->user_data;

   priv->num_upgrades = 0;
   priv->num_security = 0;
   int exit_status = 0;
   GError *error;

   char *cmd[] = { UPGRADE_CHECKER, NULL };
   char *ret;

   if(g_spawn_sync(NULL, cmd, NULL,  
		   G_SPAWN_STDOUT_TO_DEV_NULL, 
		   NULL, NULL, NULL, &ret,
		   &exit_status, &error)) {

      // check if we are running from the live-cd, it
      // symlinks apt-check to /bin/true
      if(exit_status == 0 && strlen(ret) == 0 &&
	 g_file_test (UPGRADE_CHECKER, G_FILE_TEST_IS_SYMLINK) ) {
	 g_free(ret);
	 return TRUE;
      }

      // read the value from childs stderr, E: indicates a problem
      if(ret[0] == 'E') {
	 GString *error = g_string_new("");
	 if(strlen(ret) > 3) 
	    g_string_append_printf(error,
				   _("An error occurred, please run "
				      "Package Manager from the "
				      "right-click menu or apt-get in "
				      "a terminal to see what is wrong.\n"
				      "The error message was: '%s'"),
				    &ret[2]);
	 else
	    g_string_append(error, _("An error occurred, please run "
				      "Package Manager from the "
				      "right-click menu or apt-get in "
				      "a terminal to see what is wrong."));
	 g_string_append(error, _("This usually means that your installed "
				    "packages have unmet dependencies"));
	 show_error(ta, error->str);
	 g_string_free(error, TRUE);
	 g_free(ret);
	 return TRUE;
	 
      }

      // we get "number_of_upgrades;number_of_security_upgrades" 
      // (e.g. "15;2")
      gchar **ret_array = g_strsplit(ret, ";", 2);
      if(g_strv_length(ret_array) < 2)  {
	 show_error(ta, _("A problem occurred when checking for the updates."));
 	 g_free(ret);
	 g_strfreev(ret_array);
	 return TRUE;
      }
      priv->num_upgrades = atoi(ret_array[0]);
      priv->num_security = atoi(ret_array[1]);
      g_free(ret);
      g_strfreev(ret_array);
   } else
      g_warning("Error launching %s", UPGRADE_CHECKER);
   
   g_debug_update("%s returned %i (security: %i)", UPGRADE_CHECKER, priv->num_upgrades, priv->num_security);

   if (priv->num_upgrades == 0) {

      // check if the periodic file is very old
      struct stat buf;
      if ((stat("/var/lib/apt/periodic/update-success-stamp", &buf) == 0) &&
	  (time(NULL) - buf.st_mtime > OUTDATED_NAG_AGE) ) 
	 g_timeout_add_seconds(OUTDATED_NAG_WAIT, (GSourceFunc)outdated_nag, ta);

      gtk_status_icon_set_visible (ta->tray_icon, FALSE);
      
      if(priv->active_notification != NULL) {
	 notify_notification_close(priv->active_notification, NULL);
	 priv->active_notification = NULL; 
      }
      return TRUE;
   } 

   if (priv->num_security == 0)
      ta->name = "software-update-available";
   else
      ta->name = "software-update-urgent";
   gtk_status_icon_set_from_icon_name(ta->tray_icon, ta->name);

   // update the tooltip
   update_trayicon_update_tooltip(ta, priv->num_upgrades);

   // only admins are interested in available updates, for others we just
   // display when an update is in progress to inform them about it.
   if (!in_admin_group()) {
      // we really do not want to show them available updates (Debian #604694)
      if(gtk_status_icon_get_visible (ta->tray_icon))
         gtk_status_icon_set_visible (ta->tray_icon, FALSE);
      return TRUE;
   }

   // check if the user wants to see the icon or launch
   // the default action
   if(gconf_client_get_bool(priv->gconf,
			    GCONF_KEY_AUTO_LAUNCH, NULL)) 
   {
      gtk_status_icon_set_visible(ta->tray_icon, FALSE);
      if (auto_launch_now(priv)) 
      {
	 g_spawn_command_line_async("nice ionice -c3 update-manager "
				    "--no-focus-on-map", NULL);
      }
      return TRUE;
   }

   // if we are already visible, skip the rest
   if(gtk_status_icon_get_visible (ta->tray_icon))
      return TRUE;

   // show the icon
   gtk_status_icon_set_visible(ta->tray_icon, TRUE);
   
   // the user does not no notification messages
   if(gconf_client_get_bool(priv->gconf,
			    GCONF_KEY_NO_UPDATE_NOTIFICATIONS, NULL))
      return TRUE;

   // show the notification with some delay. otherwise on a login
   // the origin of the window is 0,0 and that looks ugly
   g_timeout_add_seconds(5, show_notification, ta);

   return TRUE;
}


void 
update_tray_icon_init(TrayApplet *ta)
{
   // create the private data struct
   UpdateTrayAppletPrivate *priv = g_new0(UpdateTrayAppletPrivate, 1);
   priv->gconf = gconf_client_get_default();
   priv->apt_is_running = FALSE;
   priv->active_notification = NULL;
   ta->user_data = priv;

   // connect the signals we need
   g_signal_connect (G_OBJECT(ta->tray_icon),
		     "activate",
		     G_CALLBACK (activate_cb),
		     ta);
   g_signal_connect (G_OBJECT(ta->tray_icon),
		     "popup-menu",
		     G_CALLBACK (popup_cb),
		     ta);

   /* Menu initialization */
   update_trayicon_create_menu (ta);
   
   /* Check for updates for the first time */
   update_check (ta);
}
