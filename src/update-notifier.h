/* update-notifier.h
 * Copyright (C) 2004 Michiel Sikkes <michiel@eyesopened.nl>
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

#ifndef __UPGRADE_NOTIFIER_H__
#define __UPGRADE_NOTIFIER_H__

#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <gconf/gconf.h>
#include <gconf/gconf-client.h>

#define CLIPBOARD_NAME 	"UPGRADE_NOTIFIER_SELECTION"
#define GCONF_KEY_DEFAULT_ACTION "/apps/update-notifier/default_action"
#define GCONF_KEY_NO_UPDATE_NOTIFICATIONS "/apps/update-notifier/no_show_notifications"
#define GCONF_KEY_APPORT_NOTIFICATIONS "/apps/update-notifier/show_apport_crashes"
#define GCONF_KEY_END_SYSTEM_UIDS "/apps/update-notifier/end_system_uids"
#define GCONF_KEY_AUTO_LAUNCH "/apps/update-notifier/auto_launch"
#define GCONF_KEY_AUTO_LAUNCH_INTERVAL "/apps/update-notifier/regular_auto_launch_interval"
#define GCONF_KEY_HIDE_REBOOT "/apps/update-notifier/hide_reboot_notification"
#define GCONF_KEY_LAST_LAUNCH "/apps/update-manager/launch_time"

#define HOOKS_DIR "/var/lib/update-notifier/user.d/"
#define CRASHREPORT_HELPER "/usr/share/apport/apport-checkreports"
#define CRASHREPORT_REPORT_APP "/usr/share/apport/apport-gtk"
#define CRASHREPORT_DIR "/var/crash/"
#define REBOOT_FILE "/var/run/reboot-required"
#define UNICAST_LOCAL_AVAHI_FILE "/var/run/avahi-daemon/disabled-for-unicast-local"

// security update autolaunch minimal time (12h)
#define AUTOLAUNCH_MINIMAL_SECURITY_INTERVAL 12*60*60

// this is the age that we tolerate for updates (7 dayd)
#define OUTDATED_NAG_AGE 60*60*24*7
// this is the time we wait when we found outdated information for 
// anacron(and friends) to update the system (2h)
#define OUTDATED_NAG_WAIT 60*60*2

#if 0
// testing
#define OUTDATED_NAG_AGE 60*60
#define OUTDATED_NAG_WAIT 6
#endif

void invoke_with_gksu(const gchar *cmd, const gchar *descr, gboolean whole_message);
void invoke(const gchar *cmd, const gchar *desktop, gboolean with_gksu);
gboolean in_admin_group();

typedef struct _TrayApplet TrayApplet;
struct _TrayApplet 
{
   GtkStatusIcon *tray_icon;
   GtkWidget *menu;
   char *name;
   void *user_data;
};

typedef struct _UpgradeNotifier UpgradeNotifier;
struct _UpgradeNotifier
{
   TrayApplet *update;
   TrayApplet *reboot;
   TrayApplet *hook;
   TrayApplet *crashreport;

   GConfClient *gconf;

   guint update_finished_timer; 

   
   // some states for the file montioring (these field
   // are the state for the current "time-slice")
   gboolean dpkg_was_run;
   gboolean apt_get_runing;

   // these field are "global" (time-wise)
   gboolean reboot_pending;
   gboolean hook_pending;
   gboolean crashreport_pending;
   gboolean unicast_local_avahi_pending;
   time_t last_apt_action;

};

#endif /* __UPGRADE_NOTIFIER_H__ */
