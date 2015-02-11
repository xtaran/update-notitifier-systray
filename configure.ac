dnl Process this file with autoconf to produce a configure script.

AC_INIT([update-notifier], [0.41.5])
AC_CONFIG_SRCDIR([src/update-notifier.c])
AM_INIT_AUTOMAKE
AM_CONFIG_HEADER(config.h)
AM_MAINTAINER_MODE

AC_ISC_POSIX
AC_PROG_CC
AM_PROG_CC_STDC
AC_HEADER_STDC

#GNOME_COMMON_INIT

#pkg_modules="gtk+-2.0 gconf-2.0 dbus-glib-1 libnotify gio-2.0 x11"
pkg_modules="gtk+-2.0 libnotify x11"

PKG_CHECK_EXISTS(gdu, [ HAVE_GDU=1 ])
if test "x$HAVE_GDU" != "x"; then
    pkg_modules="$pkg_modules gdu"
    AC_DEFINE(HAVE_GDU, 1, [Use gnome-disk-utility])
else
    AC_WARN(No gnome-disk-utility, disable CD-ROM checking)
fi
PKG_CHECK_EXISTS(gudev-1.0, [ HAVE_GUDEV=1 ])
if test "x$HAVE_GUDEV" != "x"; then
    pkg_modules="$pkg_modules gudev-1.0"
    AC_DEFINE(HAVE_GUDEV, 1, [Use gudev-1.0])
else
    AC_WARN(No gudev-1.0, disable firmware installation)
fi


PKG_CHECK_MODULES(PACKAGE, [$pkg_modules])

GETTEXT_PACKAGE=update-notifier
AC_SUBST(GETTEXT_PACKAGE)
AC_DEFINE_UNQUOTED(GETTEXT_PACKAGE,"$GETTEXT_PACKAGE", [Gettext package.])
AC_PROG_INTLTOOL([0.23])

dnl Add the languages which your application supports here.
ALL_LINGUAS=`(cd "$srcdir/po" && ls *.po) | sed 's/\.po$//' | tr '\n' ' '`

AM_GLIB_GNU_GETTEXT

#AC_PATH_PROG(GCONFTOOL, gconftool-2)
#AM_GCONF_SOURCE_2

dnl AC_ARG_WITH(pkg-manager, [ --with-pkg-manager=PATH Package manager to use.])
dnl if test "x$with_pkg_manager" != "x"; then
dnl  AC_DEFINE_UNQUOTED(PKG_MANAGER, "$with_pkg_manager", [Package manager to use.])
dnl fi

dnl The following piece will be enabled when we release update-manager
dnl --------------
dnl AC_PATH_PROG(UPDATE_MANAGER, update-manager, no)
dnl if test "x$UPDATE_MANAGER" = "xno"; then
dnl   AC_MSG_ERROR(Couldn't find update-manager. Please install the update-manager package.)
dnl fi
dnl --------------

AC_OUTPUT([
Makefile
src/Makefile
data/Makefile
ui/Makefile
pixmaps/Makefile
pixmaps/16x16/Makefile
pixmaps/22x22/Makefile
pixmaps/24x24/Makefile
pixmaps/48x48/Makefile
po/Makefile.in
])


