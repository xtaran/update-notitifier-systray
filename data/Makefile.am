@INTLTOOL_DESKTOP_RULE@

desktopdir       = $(sysconfdir)/xdg/autostart
desktop_in_files = update-notifier.desktop.in
desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)

schemadir = $(datadir)/gconf/schemas
schema_in_files = update-notifier.schemas.in
schema_DATA = $(schema_in_files:.schemas.in=.schemas)
@INTLTOOL_SCHEMAS_RULE@

man_MANS = update-notifier.1

helperdir = $(datadir)/update-notifier
helper_SCRIPTS = apt_check.py apt-cdrom-check cddistupgrader update-motd-reboot-required update-motd-updates-available update-motd-cpu-checker

notifydir = $(datadir)/update-notifier
notify_SCRIPTS = notify-reboot-required

EXTRA_DIST= $(helper_SCRIPTS) \
	    $(desktop_in_files) \
	    $(schema_in_files)

#install-data-local:
#	-GCONF_CONFIG_SOURCE=$(GCONF_SCHEMA_CONFIG_SOURCE) $(GCONFTOOL) --makefile-install-rule $(srcdir)/$(schema_DATA)

