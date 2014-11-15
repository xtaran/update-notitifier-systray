## Process this file with automake to produce Makefile.in

INCLUDES = \
	-DPACKAGE_DATA_DIR=\""$(datadir)"\" \
	-DPACKAGE_LOCALE_DIR=\""$(prefix)/$(DATADIRNAME)/locale"\" \
	-DPACKAGE_LIB_DIR=\""$(libdir)"\" \
	-DUIDIR=\""$(datadir)/update-notifier/ui/"\" \
	$(PACKAGE_CFLAGS)

bin_PROGRAMS = update-notifier



update_notifier_SOURCES = update-notifier.c \
                           update-notifier.h \
			   clipboard.c \
			   rfc822.h\
			   rfc822.c\
			   hooks.h\
			   hooks.c\
			   reboot.c\
			   reboot.h\
			   crash.c\
			   crash.h\
			   avahi.h\
			   avahi.c\
			   update.c\
			   update.h\
			   firmware.c\
			   firmware.h\
			   gdu.c\
			   gdu.h

update_notifier_LDADD = $(PACKAGE_LIBS) $(INTLLIBS)
update_notifier_LDFLAGS = -export-dynamic
