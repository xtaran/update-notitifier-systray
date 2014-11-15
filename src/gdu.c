
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#ifdef HAVE_GDU
#include <glib.h>
#include <glib-object.h>

#include <sys/types.h>
#include <sys/wait.h>

#include "update-notifier.h"
#define GDU_API_IS_SUBJECT_TO_CHANGE
#include <gdu/gdu.h>
#include "gdu.h"

#define CDROM_CHECKER PACKAGE_LIB_DIR"/update-notifier/apt-cdrom-check"

/* reposonses for the dialog */
enum {
   RES_START_PM=1,
   RES_DIST_UPGRADER=2,
   RES_ADDON_CD=3,
   RES_APTONCD=4
};

/* Returnvalues from apt-cdrom-check:
    # 0 - no ubuntu CD
    # 1 - CD with packages 
    # 2 - dist-upgrader CD
    # 3 - addon CD
    # 4 - aptoncd media 
*/
enum {
   NO_CD, 
   CD_WITH_PACKAGES, 
   CD_WITH_DISTUPGRADER,
   CD_WITH_ADDONS,
   CD_WITH_APTONCD   
};

void distro_cd_detected(UpgradeNotifier *un, 
			int cdtype, 
			const char *mount_point)
{
   GtkWidget *dialog = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL,
					      GTK_MESSAGE_QUESTION, 
					      GTK_BUTTONS_NONE,
					      NULL );
   gchar *title, *markup;
   switch(cdtype) {
   case CD_WITH_PACKAGES:
      title = _("Software Packages Volume Detected");
      markup = _("<span weight=\"bold\" size=\"larger\">"
	    "A volume with software packages has "
	    "been detected.</span>\n\n"
	    "Would you like to open it with the "
	    "package manager?");
      gtk_dialog_add_buttons(GTK_DIALOG(dialog), 
			     GTK_STOCK_CANCEL,
			     GTK_RESPONSE_REJECT,
			     _("Start Package Manager"), 
			     RES_START_PM,
			     NULL);
      gtk_dialog_set_default_response (GTK_DIALOG(dialog), RES_START_PM);
      break;
   case CD_WITH_DISTUPGRADER:
      title = _("Upgrade volume detected");
      markup = _("<span weight=\"bold\" size=\"larger\">"
	    "A distribution volume with software packages has "
	    "been detected.</span>\n\n"
	    "Would you like to try to upgrade from it automatically? ");
      gtk_dialog_add_buttons(GTK_DIALOG(dialog), 
			     GTK_STOCK_CANCEL,
			     GTK_RESPONSE_REJECT,
			     _("Start package manager"), 
			     RES_START_PM,
			     _("Run upgrade"), 
			     RES_DIST_UPGRADER,
			     NULL);
      gtk_dialog_set_default_response (GTK_DIALOG(dialog), RES_DIST_UPGRADER);
      break;
   case CD_WITH_ADDONS:
      title = _("Addon volume detected");
      markup = _("<span weight=\"bold\" size=\"larger\">"
	    "An addon volume with software applications has "
	    "been detected.</span>\n\n"
	    "Would you like to view/install the content? ");
      gtk_dialog_add_buttons(GTK_DIALOG(dialog), 
			     GTK_STOCK_CANCEL,
			     GTK_RESPONSE_REJECT,
			     _("Start package manager"), 
			     RES_START_PM,
			     _("Start addon installer"), 
			     RES_ADDON_CD,
			     NULL);
      gtk_dialog_set_default_response (GTK_DIALOG(dialog), RES_ADDON_CD);
      break;
      
   case CD_WITH_APTONCD:
      title = _("APTonCD volume detected");
      markup = _("<span weight=\"bold\" size=\"larger\">"
	    "A volume with unofficial software packages has "
	    "been detected.</span>\n\n"
	    "Would you like to open it with the "
	    "package manager?");
      gtk_dialog_add_buttons(GTK_DIALOG(dialog), 
			     GTK_STOCK_CANCEL,
			     GTK_RESPONSE_REJECT,
			     _("Start package manager"), 
			     RES_START_PM,
			     NULL);
      gtk_dialog_set_default_response (GTK_DIALOG(dialog), RES_START_PM);
      break;      
   default:
      g_assert_not_reached();
   }

   gtk_window_set_title(GTK_WINDOW(dialog), title);
   gtk_window_set_skip_taskbar_hint (GTK_WINDOW(dialog), FALSE);
   gtk_message_dialog_set_markup(GTK_MESSAGE_DIALOG(dialog), markup);

   int res = gtk_dialog_run (GTK_DIALOG (dialog));
   char *cmd = NULL;
   switch(res) {
   gchar *argv[3];
   case RES_START_PM:
      cmd = g_strdup_printf("synaptic --add-cdrom '%s'",mount_point);
      invoke_with_gksu(cmd, "/usr/share/applications/synaptic.desktop", FALSE);
      break;
   case RES_DIST_UPGRADER:
      argv[0] = "/usr/lib/update-notifier/cddistupgrader";
      argv[1] = (gchar *)mount_point;
      argv[2] = NULL;
      g_spawn_async (NULL, argv, NULL, 0, NULL, NULL, NULL, NULL);
      break;
   case RES_ADDON_CD:
      cmd = g_strdup_printf("gnome-app-install --addon-cd='%s'", mount_point);
      invoke_with_gksu(cmd, "/usr/share/applications/gnome-app-install.desktop", FALSE);
      break;
   default:
      /* do nothing */
      break;
   }
   g_free(cmd);
   gtk_widget_destroy (dialog);
}

void 
up_check_mount_point_for_packages (const char *mount_point, gpointer data)
{
   if (!mount_point)
      return;

   char *ubuntu_dir = g_strdup_printf("%s/ubuntu",mount_point);
   char *aptoncd_file = g_strdup_printf("%s/aptoncd.info",mount_point);
   if(! (g_file_test (ubuntu_dir, G_FILE_TEST_IS_SYMLINK) ||
	 g_file_test (aptoncd_file, G_FILE_TEST_IS_REGULAR) )) {
      g_free(ubuntu_dir);
      g_free(aptoncd_file);
      return;
   }
   g_free(ubuntu_dir);
   g_free(aptoncd_file);

   /* this looks like a ubuntu CD, run the checker script to verify
    * this. We expect the following return codes:
    # 0 - no ubuntu CD
    # 1 - CD with packages 
    # 2 - dist-upgrader CD
    # 3 - addon CD
    # 4 - aptoncd media
    * (see data/apt-cdrom-check)
    */
   //g_print("this looks like a ubuntu-cdrom\n");
   char *cmd = g_strdup_printf(CDROM_CHECKER" '%s'",mount_point);
   int retval=-1;
   g_spawn_command_line_sync(cmd, NULL, NULL,  &retval, NULL);
   
   //g_print("retval: %i \n", WEXITSTATUS(retval));
   int cdtype = WEXITSTATUS(retval);
   if(cdtype > 0) {
      distro_cd_detected(data, cdtype, mount_point);
   }

   g_free(cmd);
}

void 
up_device_changed (GduPool *pool, GduDevice *device, gpointer data)
{
   //g_print("up_device_changed %s\n", gdu_device_get_device_file (device));

   // check if that is a removable device
   if (!gdu_device_is_removable(device))
      return;

   // we only care about the first mount point
   const gchar *p = gdu_device_get_mount_path (device);
   //g_print("checking mount point %s\n", p);
   up_check_mount_point_for_packages (p, data);
}


void
up_check_mounted_devices (GduPool *pool, gpointer data)
{
   GList *devices = gdu_pool_get_devices (pool);
   
   while(devices != NULL) {
      up_device_changed (pool, devices->data, data);
      devices = g_list_next(devices);
   }
   g_list_free(devices);
}

gboolean
up_do_hal_init (UpgradeNotifier *un)
{
   GduPool *pool = gdu_pool_new ();
   if (pool == NULL)
      return FALSE;

   g_signal_connect (pool, "device_changed", (GCallback)up_device_changed, un);
   // now check what devices we have
   up_check_mounted_devices(pool, un);

   return TRUE;
}


#else
#include <glib.h>

#include "update-notifier.h"

gboolean
up_do_hal_init (UpgradeNotifier *un)
{
    g_warning("Detection and monitoring of CD-ROMs disabled.");
    return FALSE;
}
#endif // HAVE_GUDEV
