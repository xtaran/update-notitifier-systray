#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <libnotify/notify.h>

#include "update-notifier.h"
#include "update.h"




static gboolean
check_system_crashes() {
   int exitcode;

   if(!in_admin_group())
      return FALSE;

   // check for system crashes
   if(!g_spawn_command_line_sync(CRASHREPORT_HELPER " --system", NULL, NULL, 
				 &exitcode, NULL)) {
      g_warning("Can not run %s\n", CRASHREPORT_HELPER);
      return FALSE;
   }

   return exitcode == 0;
}

static gboolean 
run_apport(TrayApplet *ta)
{
   g_debug("fire up the crashreport tool\n");
   if (check_system_crashes()) {
       invoke_with_gksu(CRASHREPORT_REPORT_APP, 
	_("<span weight=\"bold\" size=\"larger\">Please enter your password to access problem reports of system programs</span>"),
	TRUE);
       return TRUE;
   } else
       return g_spawn_command_line_async(CRASHREPORT_REPORT_APP, NULL);
}

static gboolean
show_notification (TrayApplet *ta)
{
   NotifyNotification *n;

   // check if the update-icon is still visible (in the delay time a 
   // update may already have been performed)
   if(!gtk_status_icon_get_visible(ta->tray_icon))
      return FALSE;

   // now show a notification handle 
   n = notify_notification_new(
				     _("Crash report detected"),
				     _("An application has crashed on your "
				       "system (now or in the past). "
				       "Click on the notification icon to "
				       "display details. "
				       ),
				     GTK_STOCK_DIALOG_INFO);
   notify_notification_set_timeout (n, 60000);
   notify_notification_show (n, NULL);

   return FALSE;
}

static void 
hide_crash_applet(TrayApplet *ta)
{
   NotifyNotification *n;
 
   gtk_status_icon_set_visible(ta->tray_icon, FALSE);
   
   /* Hide any notification popup */
   n = g_object_get_data (G_OBJECT(ta->tray_icon), "notification");
   if (n)
      notify_notification_close (n, NULL);
   g_object_set_data (G_OBJECT(ta->tray_icon), "notification", NULL);
}

gboolean
crashreport_check (TrayApplet *ta)
{
   int crashreports_found = 0;
   static gboolean first_run = TRUE;
   gboolean system_crashes;

   //   g_debug("crashreport_check\n");

   // don't do anything if no apport-gtk is installed
   if(!g_file_test(CRASHREPORT_REPORT_APP, G_FILE_TEST_IS_EXECUTABLE))
      return FALSE;

   // Check whether the user doesn't want notifications
   if (!gconf_client_get_bool ((GConfClient*) ta->user_data,
       GCONF_KEY_APPORT_NOTIFICATIONS, NULL)) {
       g_debug("apport notifications disabled in gconf, not displaying crashes");
       return FALSE;
   }

   // check for (new) reports by calling CRASHREPORT_HELPER
   // and checking the return code
   int exitcode;
   if(!g_spawn_command_line_sync(CRASHREPORT_HELPER, NULL, NULL, 
				 &exitcode, NULL)) {
      g_warning("Can not run %s\n", CRASHREPORT_HELPER);
      return FALSE;
   }
   // exitcode == 0: repots found, else no reports
   system_crashes = check_system_crashes();
   crashreports_found = !exitcode || system_crashes;

   // crashreport found and first run: show notification bubble and
   // return
   gboolean visible = gtk_status_icon_get_visible(ta->tray_icon);

   //   g_print("reports: %i, visible: %i\n",crashreports_found,visible);

   if((crashreports_found > 0) && (system_crashes || first_run)) {
      gtk_status_icon_set_tooltip(ta->tray_icon,
				  _("Crash report detected"));
      gtk_status_icon_set_visible(ta->tray_icon, TRUE);
      /* Show the notification, after a delay so it doesn't look ugly
       * if we've just logged in */
      g_timeout_add(5000, (GSourceFunc)(show_notification), ta);
   }
   // crashreport found and already visible
   else if((crashreports_found > 0) && !(system_crashes || first_run)) {
      run_apport(ta);
      // if apport was run, we don't care anymore and hide the icon
      crashreports_found=0;
   }

   // no crashreports, but visible
   if((crashreports_found == 0) && visible) {
      hide_crash_applet(ta);
   }

   first_run = FALSE;
   return TRUE;
}

static gboolean
button_release_cb (GtkWidget *widget,
		   TrayApplet *ta)
{
run_apport(ta);
hide_crash_applet(ta);
	return TRUE;
}


void
crashreport_tray_icon_init (TrayApplet *ta)
{

	ta->user_data = gconf_client_get_default();
        g_signal_connect (G_OBJECT(ta->tray_icon),
			  "activate",
			  G_CALLBACK (button_release_cb),
			  ta);

	/* Check for crashes for the first time */
	crashreport_check (ta);
}
