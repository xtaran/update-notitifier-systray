Upgrade Notifier Tray Icon for Debian
=====================================

This is a small tray icon that backgrounds itself and checks for
upgrades of Debian packages. It needs a regular "apt-get update" to be
working. This is ensured by depending on apt-config-auto-update which
installs some options into /etc/apt/apt.conf.d to trigger a cron
update script. It monitors /var/lib/update-notifier/dpkg-run-stamp and
updates its status if it changes.

It's a fork of the last non-transitional, non-packagekit
update-notifier package to again get a lean update-notifier for the
systray with only very few dependencies.

Dependencies
------------

It only needs very few and lean dependencies to run. No more FAM, Gamin,
GNOME-VFS, PolicyKit, PackageKit and other stuff you usually don't
want on a lean desktop installation.

Features and No-More-Features
-----------------------------

Includes reboot notification support, but you need to reboot the
machine by other means.

Interactive hooks support and distribution CD-ROM detection currently
untested and probably not working. Maybe removed at some time, too.

History
-------

Based on ideas of Matt Zimmerman und Jeff Waught. Tray example from
Lukas Lipka <lukas@pmad.net>. Lot's of cleanups from Michiel Sikkes.

It was maintained by Michael Vogt and later by Julian Andres Klode.
