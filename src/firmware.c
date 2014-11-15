#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#ifdef HAVE_GUDEV
#include <sys/wait.h>

#define G_UDEV_API_IS_SUBJECT_TO_CHANGE
#include <gudev/gudev.h>
#include <sys/utsname.h>

/* search path for firmare; second and fourth element are dynamically constructed
 * in missing_firmware_init(). */
const gchar* firmware_search_path[] = {
    "/lib/firmware", NULL,
    "/lib/firmware/updates", NULL,
    NULL };

gchar *hplip_helper[] = { "/usr/bin/hp-plugin-ubuntu", NULL };

static inline void
g_debug_firmware(const char *msg, ...)
{
   va_list va;
   va_start(va, msg);
   g_logv("firmware",G_LOG_LEVEL_DEBUG, msg, va);
   va_end(va);
}

static gboolean
deal_with_hplip_firmware(GUdevDevice *device)
{
    const gchar *id_vendor, *id_product, *id_model;
    GError *error = NULL;
    gint ret = 0;

    id_vendor = g_udev_device_get_sysfs_attr (device, "idVendor");
    id_product = g_udev_device_get_sysfs_attr (device, "idProduct");
    id_model = g_udev_device_get_property (device, "ID_MODEL");
    g_debug_firmware ("firmware.c id_vendor=%s, id_product=%s", id_vendor, id_product);

    // only idVendor=03f0, idProduct="??17" requires firmware
    if (g_strcmp0 (id_vendor, "03f0") != 0 || id_product == NULL || g_utf8_strlen(id_product, -1) != 4)
       return FALSE;
    if (id_product[2] != '1' || id_product[3] != '7')
       return FALSE;

    // firmware is only required if "hp-mkuri -c" returns 2 or 5
    const gchar *cmd = "/usr/bin/hp-mkuri -c";
    g_setenv("hp_model", id_model, TRUE);
    if (!g_spawn_command_line_sync (cmd, NULL, NULL, &ret, &error))
    {
       g_warning("error calling hp-mkuri");
       return FALSE;
    }

    // check return codes, 2 & 5 indicate that it has the firmware already
    if (WEXITSTATUS(ret) != 2 && WEXITSTATUS(ret) != 5) 
    {
       g_debug_firmware ("hp-mkuri indicates no firmware needed");
       return TRUE;
    }

    if (!g_spawn_async("/", hplip_helper, NULL, 0, NULL, NULL, NULL, NULL))
    {
       g_warning("error calling hplip_helper");
       return FALSE;
    }
    return TRUE;
}

static void
on_uevent (GUdevClient *client,
           gchar *action,
           GUdevDevice *device,
           gpointer user_data)
{
   const gchar* firmware_file;
    gchar *path;
    const gchar **i;
    gboolean found = FALSE;

    g_debug_firmware ("firmware.c on_uevent: action=%s, devpath=%s", action, g_udev_device_get_sysfs_path(device));

    if (g_strcmp0 (action, "add") != 0 && g_strcmp0 (action, "change") != 0)
	return;

    if (deal_with_hplip_firmware(device))
       return;

    firmware_file = g_udev_device_get_property (device, "FIRMWARE");

    if (!firmware_file) {
        g_debug_firmware ("on_uevent: discarding, no FIRMWARE property");
        return;
    }

    for (i = firmware_search_path; *i && !found; ++i) {
	path = g_build_path("/", *i, firmware_file, NULL);
	g_debug_firmware ("firmware on_uevent: searching for %s", path);
	if (g_file_test (path, G_FILE_TEST_EXISTS)) {
	    g_debug_firmware ("firmware on_uevent: found");
	    found = TRUE;
	}
	g_free (path);
    }

    if (found)
	return;

    g_debug_firmware ("calling jockey");
    gchar* argv[] = {"jockey-gtk", "-c", NULL };
    GError* error = NULL;
    if (!g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, &error)) {
	g_warning("jockey could not be called: %s", error->message);
	g_error_free (error);
    }
}

void
missing_firmware_init()
{
    const gchar* subsytems[] = {"firmware", "usb", NULL};
    
    /* build firmware search path */
    struct utsname u;
    if (uname (&u) != 0) {
	g_warning("uname() failed, not monitoring firmware");
	return;
    }
    firmware_search_path[1] = g_strdup_printf("/lib/firmware/%s", u.release);
    firmware_search_path[3] = g_strdup_printf("/lib/firmware/updates/%s", u.release);

    GUdevClient* gudev = g_udev_client_new (subsytems);
    g_signal_connect (gudev, "uevent", G_CALLBACK (on_uevent), NULL);

    /* cold plug */
    GList *usb_devices, *elem;
    usb_devices = g_udev_client_query_by_subsystem (gudev, "usb");
    for (elem = usb_devices; elem != NULL; elem = g_list_next(elem))
       deal_with_hplip_firmware(elem->data);
}
#else
#include <glib.h>
void
missing_firmware_init()
{
    g_warning("Installation of firmware disabled.");
}
#endif // HAVE_GUDEV
