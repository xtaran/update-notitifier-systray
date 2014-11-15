#!/bin/sh
# Run this to generate all the initial makefiles, etc.

srcdir=`dirname $0`
test -z "$srcdir" && srcdir=.

PKG_NAME="Upgrade Notifier"

(test -f $srcdir/configure.in \
  && test -f $srcdir/README) || {
    echo -n "**Error**: Directory "\`$srcdir\'" does not look like the"
    echo " top-level gnome directory"
    exit 1
}

# do not use gnome-autogen.sh as long as it's broken
cd "$srcdir"
autoreconf --force -i -v    
intltoolize -c
aclocal
rm -rf autom4te.cache
cd po; intltool-update -p --verbose
exit 0

which gnome-autogen.sh || {
    echo "You need to install gnome-common"
    exit 1
}

USE_GNOME2_MACROS=1 . gnome-autogen.sh
