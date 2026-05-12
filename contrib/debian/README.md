
Debian
====================
This directory contains files used to package floofyd/floofy-qt
for Debian-based Linux systems. If you compile floofyd/floofy-qt yourself, there are some useful files here.

## floofy: URI support ##


floofy-qt.desktop  (Gnome / Open Desktop)
To install:

	sudo desktop-file-install floofy-qt.desktop
	sudo update-desktop-database

If you build yourself, you will either need to modify the paths in
the .desktop file or copy or symlink your floofy-qt binary to `/usr/bin`
and the `../../share/pixmaps/floofy128.png` to `/usr/share/pixmaps`

floofy-qt.protocol (KDE)

