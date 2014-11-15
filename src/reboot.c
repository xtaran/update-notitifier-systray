#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <libnotify/notify.h>

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

static void
ask_reboot_required(TrayApplet *ta, gboolean focus_on_map)
{
   GtkWidget *dia;
   
   dia = GTK_WIDGET (gtk_builder_get_object (builder, "dialog_reboot"));
   gtk_window_set_focus_on_map(GTK_WINDOW(dia), focus_on_map);
   gtk_dialog_run (GTK_DIALOG(dia));
   gtk_widget_hide (dia);
}

static void
do_reboot_check (TrayApplet *ta)
{
	struct stat statbuf;

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

	/* Show the notification, after a delay so it doesn't look ugly
	 * if we've just logged in */
	g_timeout_add(5000, (GSourceFunc)(show_notification), ta);

}

gboolean
reboot_check (TrayApplet *ta)
{
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

        g_signal_connect (G_OBJECT(ta->tray_icon),
			  "activate",
			  G_CALLBACK (button_release_cb),
			  ta);

	gtk_image_set_from_pixbuf(GTK_IMAGE(widget), pixbuf);

	/* Check for updates for the first time */
	reboot_check (ta);
}
