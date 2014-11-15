#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <libnotify/notify.h>
#include <dbus/dbus-glib.h>

#include "update-notifier.h"
#include "update.h"

static GtkBuilder *builder;

static gboolean
show_notification (TrayApplet *ta)
{
	NotifyNotification *n;

	// only show once the icon is realy availabe
	if(!gtk_status_icon_get_visible(ta->tray_icon))
	   return TRUE;

	/* Create and show the notification */
	n = notify_notification_new(
				     _("System restart required"),
				     _("To complete the update of your system, "
				       "please restart it.\n\n"
				       "Click on the notification icon for "
				       "details."),
				     GTK_STOCK_DIALOG_WARNING);
	notify_notification_set_timeout (n, 60000);
	notify_notification_show (n, NULL);

	return FALSE;
}

static gboolean
gdm_action_reboot()
{
   DBusGConnection *connection;
   GError *error;
   DBusGProxy *proxy;

   error = NULL;
   connection = dbus_g_bus_get (DBUS_BUS_SESSION, &error);
   if (connection == NULL) {
      g_error_free (error);
      return FALSE;
   }

  proxy = dbus_g_proxy_new_for_name (connection,
                                     "org.gnome.SessionManager",
				     "/org/gnome/SessionManager",
				     "org.gnome.SessionManager");
  if (proxy == NULL)
     return FALSE;

  error = NULL;
  if (!dbus_g_proxy_call (proxy, "Shutdown", &error, 
			  G_TYPE_INVALID, G_TYPE_INVALID)) {
     g_error_free (error);
     return FALSE;
  }
  return TRUE;
}

static gboolean
ck_action_reboot()
{
   DBusGConnection *connection;
   GError *error;
   DBusGProxy *proxy;

   error = NULL;
   connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
   if (connection == NULL) {
      g_error_free (error);
      return FALSE;
   }

  proxy = dbus_g_proxy_new_for_name (connection,
                                     "org.freedesktop.ConsoleKit",
                                     "/org/freedesktop/ConsoleKit/Manager",
                                     "org.freedesktop.ConsoleKit.Manager");
  if (proxy == NULL)
     return FALSE;

  error = NULL;
  if (!dbus_g_proxy_call (proxy, "Restart", &error,
                          G_TYPE_INVALID, G_TYPE_INVALID)) {
     g_error_free (error);
     return FALSE;
  }
  return TRUE;

}

static void
request_reboot (void)
{
   if(!gdm_action_reboot() && !ck_action_reboot()) {
      const char *fmt, *msg, *details;
      fmt = "<span weight=\"bold\" size=\"larger\">%s</span>\n\n%s\n";
      msg = _("Reboot failed");
      details = _("Failed to request reboot, please shutdown manually");
      GtkWidget *dlg = gtk_message_dialog_new_with_markup(NULL, 0,
							  GTK_MESSAGE_ERROR,
							  GTK_BUTTONS_CLOSE,
							  fmt, msg, details);
      gtk_dialog_run(GTK_DIALOG(dlg));
      gtk_widget_destroy(dlg);
   }
}


static void
ask_reboot_required(TrayApplet *ta, gboolean focus_on_map)
{
   GtkWidget *dia;
   
   dia = GTK_WIDGET (gtk_builder_get_object (builder, "dialog_reboot"));
   gtk_window_set_focus_on_map(GTK_WINDOW(dia), focus_on_map);
   if (gtk_dialog_run (GTK_DIALOG(dia)) == GTK_RESPONSE_OK)
      request_reboot ();
   gtk_widget_hide (dia);
}

static gboolean
aptdaemon_pending_transactions ()
{
   DBusGConnection *connection;
   GError *error;
   DBusGProxy *proxy;
   char *current = NULL;
   char **pending = NULL;
  
   error = NULL;
   connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
   if (connection == NULL) {
      g_debug ("Failed to open connection to bus: %s\n", error->message);
      g_error_free (error);
      return FALSE;
   }

  proxy = dbus_g_proxy_new_for_name (connection,
                                     "org.debian.apt",
				     "/org/debian/apt",
				     "org.debian.apt");
  error = NULL;
  if (!dbus_g_proxy_call (proxy, "GetActiveTransactions", &error, 
			  G_TYPE_INVALID,
                          G_TYPE_STRING, &current, 
			  G_TYPE_STRV, &pending,
			  G_TYPE_INVALID)) {
     if (!g_error_matches(error, DBUS_GERROR, DBUS_GERROR_SERVICE_UNKNOWN))
       g_debug ("error during dbus call: %s\n", error->message);
     g_error_free (error);
     g_object_unref (proxy);
     return FALSE;
  }

  gboolean has_pending = FALSE;
  if ((current && strcmp(current,"") != 0) || g_strv_length(pending) > 0)
     has_pending = TRUE;

  g_object_unref (proxy);
  g_free (current);
  g_strfreev (pending);

  return has_pending;
}

static void
do_reboot_check (TrayApplet *ta)
{
	struct stat statbuf;

	// if we are not supposed to show the reboot notification
	// just skip it 
	if(gconf_client_get_bool((GConfClient*) ta->user_data,
				 GCONF_KEY_HIDE_REBOOT, NULL))
	   return;
	// no auto-open of this dialog 
	if(gconf_client_get_bool((GConfClient*) ta->user_data,
				 GCONF_KEY_AUTO_LAUNCH, NULL)) {
	   g_debug ("Skipping reboot required");
	   return;
	}

	/* If the file doesn't exist, we don't need to reboot */
	if (stat (REBOOT_FILE, &statbuf)) {
		NotifyNotification *n;

		gtk_status_icon_set_visible (ta->tray_icon, FALSE);
		/* Hide any notification popup */
		n = g_object_get_data (G_OBJECT(ta->tray_icon), "notification");
		if (n)
			notify_notification_close (n, NULL);
		g_object_set_data (G_OBJECT(ta->tray_icon), "notification", NULL);

		return;
	}

	/* Skip the rest if the icon is already visible */
	if (gtk_status_icon_get_visible (ta->tray_icon))
	   return;
	gtk_status_icon_set_tooltip (ta->tray_icon,  
				     _("System restart required"));
	gtk_status_icon_set_visible (ta->tray_icon, TRUE);

	/* Check whether the user doesn't like notifications */
	if (gconf_client_get_bool ((GConfClient*) ta->user_data,
				   GCONF_KEY_NO_UPDATE_NOTIFICATIONS, NULL))
		return;

	/* Show the notification, after a delay so it doesn't look ugly
	 * if we've just logged in */
	g_timeout_add(5000, (GSourceFunc)(show_notification), ta);

}

gboolean
reboot_check (TrayApplet *ta)
{
   if (aptdaemon_pending_transactions())
      g_timeout_add_seconds (5, (GSourceFunc)reboot_check, ta);
   else
      do_reboot_check(ta);
   return FALSE;
}

static gboolean
button_release_cb (GtkWidget *widget,
		   TrayApplet *ta)
{
   ask_reboot_required(ta, TRUE);

   return TRUE;
}


void
reboot_tray_icon_init (TrayApplet *ta)
{
	GtkWidget *widget;
	GError* error = NULL;

	builder = gtk_builder_new ();
	if (!gtk_builder_add_from_file (builder, UIDIR"reboot-dialog.ui", &error)) {
		g_warning ("Couldn't load builder file: %s", error->message);
		g_error_free (error);
	}

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "image"));
	GtkIconTheme* icon_theme = gtk_icon_theme_get_default();
	GdkPixbuf *pixbuf = gtk_icon_theme_load_icon (icon_theme, "un-reboot",
						      48, 0,NULL);
	gtk_status_icon_set_from_pixbuf (ta->tray_icon, pixbuf);
	ta->user_data = gconf_client_get_default();

        g_signal_connect (G_OBJECT(ta->tray_icon),
			  "activate",
			  G_CALLBACK (button_release_cb),
			  ta);

	gtk_image_set_from_pixbuf(GTK_IMAGE(widget), pixbuf);

	/* Check for updates for the first time */
	reboot_check (ta);
}
