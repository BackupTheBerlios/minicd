#!/bin/sh
#
# MiniCD iso build script
# Copyright (C) 2002-2003, Jaco Greeff
#
#    This program is free software; you can redistribute it and/or modify
#    it under the terms of the GNU General Public License as published by
#    the Free Software Foundation; either version 2 of the License, or
#    (at your option) any later version.
#
#    This program is distributed in the hope that it will be useful,
#    but WITHOUT ANY WARRANTY; without even the implied warranty of
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#    GNU General Public License for more details.
#
#    You should have received a copy of the GNU General Public License
#    along with this program; if not, write to the Free Software
#    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
#
# Changelog:
#
# * 07 March 2003, Jaco Greeff <jaco@puxedo.org>
# - A bit late to start the changelog now that everything has been working OK
#   for a while, but considder this the first entry
# - Don't allocate an extra 1MB for the initrd, we should have enough space
# - Don't copy our /usr/bin/kdedesktop-links file for now, Mandrake doesn't
#   create the "Removable Media", "Store", "Expert" links this 9.1-rc2 (This
#   might have to return in the near future)
#

### version information for stuff we are to build
BASH_SRC="bash-2.05b"
BUSYBOX_SRC="busybox-0.60.5"
MODUTILS_SRC="modutils-2.4.22"
CLOOP_SRC="cloop-0.67"
DEV_SRC="devices-20030212"
SPLASH_SRC="splash-1.0.0"

### some global default settings (just edit to change defaults)
QUIET=0
VERBOSE=0
DO_FS=1
DO_BUILD=1
DO_CLEAN=0
DO_MODULES=1
DO_CLOOP=1
DO_ISOLINUX=1
DO_INITRD=1
DO_ISO=1

### unless you are working on fine-tuning the script, you problably shouldn't touch these
PATH=/sbin:$PATH
KERNEL=`uname -r`
ROOT_DIR=`pwd`
SOURCE_DIR="$ROOT_DIR/src"
BUILD_DIR="$ROOT_DIR/build"
INITRD_DIR="$BUILD_DIR/initrd"
ISO_DIR="$BUILD_DIR/iso"
MDKIMG_DIR="$BUILD_DIR/mdkimg"
ISOLNX_DIR="$ISO_DIR/isolinux"
MINICD_DIR="$BUILD_DIR/minicd"
MINICD_ROOT="/mnt/extra"
MODULES_BASE_DIR="$INITRD_DIR/lib/modules"
MODULES_DIR="$MODULES_BASE_DIR/$KERNEL"
MOUNT_DIR="$ROOT_DIR/mnt"
MDKIMG="mdkimg"
USER_ID=`id -u`
CURR_PKG=""
TMP="/tmp"

### these should be relatively constant
VERSION_DATE=`date --utc`
VERSION="1.0-rc2 ($VERSION_DATE)"
MDK_RELEASE=`cat /etc/mandrake-release`
CLOOPVER=`cat /etc/mandrake-release | awk '{ print $4" "$5 }'`
NAMESTR="MDK MiniCD"
CLOOPSTR=`cat /etc/mandrake-release | awk '{ print $1" "$2 }'`
NAME_VERSION_STR="$NAMESTR, Version $VERSION"
CLOOP_VERSION_STR="$CLOOPSTR $CLOOPVER"
URLSTR="http://www.puxedo.org/minicd/"
URLSTR="http://minicd.berlios.de/"
AUTHOR="Jaco Greeff <jaco@puxedo.org>"
COPYRIGHT="Copyright (C) 2002-2003, $AUTHOR"

### displays the help for the script
print_help() {
	exit
}

### print a header
print_header() {
	echo "* $*"
}

### print finished
print_done() {
	print_header "Done."
}

### display an error message and exit
print_error() {
	echo "ERROR: $*"
	exit
}

### do a shell command
docmd() {
	[ $QUIET -eq 0 ] && echo "+ $*"
	if [ $VERBOSE -eq 1 ]; then
		eval "$*" || exit
	else
		(eval "$*") 2>&1 >/dev/null || exit
	fi
}

### do a shell command
docd() {
	[ $QUIET -eq 0 ] && echo "+ cd $1"
	cd $1 || exit
}

### unpack a source archive
unpack_archive() {
	CURR_PKG="$1"
	ARCHIVE_PATH="$SOURCE_DIR/$CURR_PKG.tar.bz2"
	docmd "rm -rf $BUILD_DIR/$CURR_PKG"
	docd "$BUILD_DIR"
	docmd "bzip2 -dc $ARCHIVE_PATH | tar -xf -"
	docd "$CURR_PKG"
}

### clean up after ourselves
clean_pkg_build() {
	print_header "Cleaning $CURR_PKG ..."
	docd "$ROOT_DIR"
	docmd "rm -rf $BUILD_DIR/$CURR_PKG"
}

### clean everything
clean_all() {
	print_header "* Cleaning environment ..."
	docmd "rm -rf $BUILD_DIR && mkdir -p $BUILD_DIR"
}

### busybox build (small shell with all the required utilities)
build_static() {
	print_header "Building $BUSYBOX_SRC ..."
	unpack_archive "$BUSYBOX_SRC"
	docmd "make DOSTATIC=true USE_SYSTEM_PWD_GRP=false"
	docmd "mkdir -p $INITRD_DIR/initrd"
	docmd "make PREFIX=$INITRD_DIR/initrd install"
	clean_pkg_build
	print_done

	print_header "Building $MODUTILS_SRC ..."
	unpack_archive "$MODUTILS_SRC"
	docmd "./configure --enable-insmod-static"
	docmd "make"
	docmd "strip --strip-debug insmod/insmod.static"
	docmd "rm -rf $INITRD_DIR/initrd/sbin/insmod"
	#{insmod,kallsyms,ksyms,lsmod,modprobe,rmmod}"
	docmd "cp insmod/insmod.static $INITRD_DIR/initrd/sbin/insmod"
	#docmd "(cd $INITRD_DIR/initrd/sbin ; for i in kallsyms ksyms lsmod rmmod; do ln -s insmod \$i; done)"
	clean_pkg_build
	print_done

	print_header "Building $SPLASH_SRC ..."
	unpack_archive "$SPLASH_SRC"
	docmd "gcc -static -o bootsplash splash.c"
	docmd "strip --strip-debug bootsplash"
	docmd "cp bootsplash $INITRD_DIR/initrd/bin"
	clean_pkg_build
	print_done
}

### modules build
build_modules() {
	print_header "Installing base modules ..."
	docmd "rm -rf $MODULES_DIR"
	docmd "mkdir -p $MODULES_DIR"
	docmd "gzip -dc /lib/modules/$KERNEL/kernel/drivers/cdrom/cdrom.o.gz >$MODULES_DIR/cdrom.o"
	docmd "gzip -dc /lib/modules/$KERNEL/kernel/drivers/ide/ide-cd.o.gz >$MODULES_DIR/ide-cd.o"
	docmd "gzip -dc /lib/modules/$KERNEL/kernel/fs/ext3/ext3.o.gz >$MODULES_DIR/ext3.o"
	docmd "gzip -dc /lib/modules/$KERNEL/kernel/fs/isofs/isofs.o.gz >$MODULES_DIR/isofs.o"
	docmd "gzip -dc /lib/modules/$KERNEL/kernel/fs/jbd/jbd.o.gz >$MODULES_DIR/jbd.o"
	for i in nls_iso8859-13.o nls_iso8859-14.o nls_iso8859-15.o nls_iso8859-1.o nls_iso8859-2.o nls_iso8859-3.o nls_iso8859-4.o nls_iso8859-5.o nls_iso8859-6.o nls_iso8859-7.o nls_iso8859-8.o nls_iso8859-9.o nls_utf8.o; do
		docmd "gzip -dc /lib/modules/$KERNEL/kernel/fs/nls/$i.gz >$MODULES_DIR/$i"
	done
	docmd "gzip -dc /lib/modules/$KERNEL/kernel/lib/zlib_inflate/zlib_inflate.o.gz >$MODULES_DIR/zlib_inflate.o"
	print_done

	print_header "Building $CLOOP_SRC ..."
	unpack_archive "$CLOOP_SRC"
	docmd "make USEZLIB=zlib-1.1.4/libz.a"
	docmd "mkdir -p $MODULES_DIR"
	docmd "cp -a cloop.o $MODULES_DIR"
	clean_pkg_build
	print_done

	print_header "Creating modules.dep ..."
	docmd ":> $BUILD_DIR/modules.conf"
	docmd "depmod -a -n -C $BUILD_DIR/modules.conf -b $INITRD_DIR >$BUILD_DIR/modules.dep"
	docmd "cat $BUILD_DIR/modules.dep >$MODULES_DIR/modules.dep"
	docmd "rm -rf $BUILD_DIR/modules.*"
	print_done
}


### isolinux stuff
install_isolinux() {
	print_header "Installing/Generating ISOLinux ..."
	docmd "rm -rf $ISOLNX_DIR"
	docmd "mkdir -p $ISOLNX_DIR"
	docmd "cp /usr/lib/syslinux/isolinux.bin $ISOLNX_DIR/isolinux.bin"
	docmd "cp /boot/vmlinuz-$KERNEL $ISOLNX_DIR/vmlinuz"
	docmd "cat $ROOT_DIR/isolinux.cfg.in >$ISOLNX_DIR/isolinux.cfg"
	docmd "bmptoppm -verbose $SOURCE_DIR/isolinux.bmp >$BUILD_DIR/isolinux.ppm"
	docmd "ppmtolss16 \#3a3e6e=0 \#d0d0d0=7 <$BUILD_DIR/isolinux.ppm >$ISOLNX_DIR/isolinux.lss"
	docmd "echo -e '\\030isolinux.lss' >$ISOLNX_DIR/boot.msg"
	docmd "cat $ROOT_DIR/boot.msg.in | sed 's/\@NAME_VERSION_STR/$NAME_VERSION_STR/g' | sed 's/\@COPYRIGHT/$COPYRIGHT/g' | sed 's|\@URL|$URLSTR|g' >>$ISOLNX_DIR/boot.msg"
	docmd "dd if=/dev/zero of=$ISOLNX_DIR/boot.cat bs=1k count=2 2> /dev/null"
	docmd "rm -rf $ISO_DIR/weblogo.png"
	docmd "cp -a src/weblogo.png $ISO_DIR/weblogo.png"
	docmd "cat $ROOT_DIR/index.html.in | sed 's/\@MDK_RELEASE/$MDK_RELEASE/g' | sed 's/\@NAME_VERSION_STR/$NAME_VERSION_STR/g' | sed 's/\@COPYRIGHT/$COPYRIGHT/g' | sed 's/\@KERNELVER/$KERNEL/g' | sed 's|\@URL|$URLSTR|g' >$ISO_DIR/index.html"
	docmd "echo '[autorun]' >$ISO_DIR/autorun.inf"
	docmd "echo 'shellexecute=index.html' >>$ISO_DIR/autorun.inf"
	docmd "cat /mnt/extra/root/pkglist.txt >$ISO_DIR/pkglist.txt"
	docmd "cat $ROOT_DIR/CHANGELOG.in >$ISO_DIR/CHANGELOG.txt"
	print_done
}

### initrd stuff
build_initrd() {
	print_header "Creating linuxrc ..."
	docmd "rm -rf $INITRD_DIR/linuxrc"
	docmd "cat $ROOT_DIR/linuxrc.in | sed 's/\@KERNELVER/$KERNEL/g' | sed 's/\@NAME_VERSION_STR/$NAME_VERSION_STR/g' | sed 's/\@CLOOPVER/$CLOOPVER/g' >$INITRD_DIR/linuxrc"
	docmd "chmod +x $INITRD_DIR/linuxrc"
	print_done

	print_header "Building initrd ..."
	### create our directories
	docmd "(rm -rf $INITRD_DIR/dev ; cd $INITRD_DIR ; bzip2 -dc $SOURCE_DIR/$DEV_SRC.tar.bz2 | tar -xf -)"
	docmd "(cd $INITRD_DIR ; mkdir -p mnt/{mdkimg,minicd,ramdisk})"
	docmd "rm -rf $INITRD_DIR/initrd/etc"
	docmd "mkdir -p $INITRD_DIR/initrd/etc/{rc.d,hwdetect,bootsplash}"
	docmd "cat $ROOT_DIR/rc.in >$INITRD_DIR/initrd/etc/rc.d/rc"
	docmd "chmod +x $INITRD_DIR/initrd/etc/rc.d/rc"
	docmd "cat $ROOT_DIR/rc.sysinit.in >$INITRD_DIR/initrd/etc/rc.d/rc.sysinit"
	docmd "chmod +x $INITRD_DIR/initrd/etc/rc.d/rc.sysinit"
	docmd "cat $ROOT_DIR/rc.local.in >$INITRD_DIR/initrd/etc/rc.d/rc.local"
	docmd "chmod +x $INITRD_DIR/initrd/etc/rc.d/rc.local"
	docmd "cat $ROOT_DIR/hwdetect.pl.in >$INITRD_DIR/initrd/etc/rc.d/hwdetect.pl"
	docmd "chmod +x $INITRD_DIR/initrd/etc/rc.d/hwdetect.pl"
	docmd "cat $ROOT_DIR/issue.in | sed 's/\@MDK_RELEASE/$MDK_RELEASE/g' | sed 's/\@NAME_VERSION_STR/$NAME_VERSION_STR/g' | sed 's/\@COPYRIGHT/$COPYRIGHT/g' | sed 's/\@KERNELVER/$KERNEL/g' | sed 's|\@URL|$URLSTR|g' >$INITRD_DIR/initrd/etc/issue"
	docmd "cat $ROOT_DIR/issue-tux.in >$INITRD_DIR/initrd/etc/issue-tux"
	docmd "cat $INITRD_DIR/initrd/etc/issue >>$INITRD_DIR/initrd/etc/issue-tux"
	docmd "cat $ROOT_DIR/desktop.cdrom.in >$INITRD_DIR/initrd/etc/hwdetect/desktop.cdrom.in"
	docmd "cat $ROOT_DIR/desktop.fd.in >$INITRD_DIR/initrd/etc/hwdetect/desktop.fd.in"
	docmd "cat $ROOT_DIR/desktop.hd.in >$INITRD_DIR/initrd/etc/hwdetect/desktop.hd.in"
	docmd "cp -a $SOURCE_DIR/bootbg-*.* $INITRD_DIR/initrd/etc/bootsplash"

	### find the size of the bugger
	# parts taken from the /sbin/mkinitrd script in Mandrake
	# (modified to fit in here)
	IMAGESIZE=`du -kc $INITRD_DIR | grep total | sed 's/\t[^ ]*$//'`
	INODES=250
	for i in `find $INITRD_DIR`; do
		INODES=$[INODES + 1];
	done
	for i in `find /dev`; do
		INODES=$[INODES + 1];
	done
	IMAGESIZE=$[IMAGESIZE + INODES / 10]  # 10 inodes needs 1k
	#IMAGESIZE=$[IMAGESIZE + 1000]         # extra 1MB to play with
	if [ $VERBOSE -gt 1 ]; then
		echo "+ Creating filesystem with size ${IMAGESIZE}KB and $INODES inodes"
	fi

	### create fs
	IMAGE="$ISOLNX_DIR/initrd"
	docmd  "dd if=/dev/zero of=$IMAGE bs=1k count=$IMAGESIZE 2> /dev/null"
	docmd  "mke2fs -q -m 0 -F -N $INODES -s 1 $IMAGE"
	docmd  "mount -o loop -t ext2 $IMAGE $MOUNT_DIR"
	docmd  "rm -rf $MOUNT_DIR/lost+found"
	docmd  "(cd $INITRD_DIR; tar cf - .) | (cd $MOUNT_DIR; tar xf -)"
	docmd  "umount $MOUNT_DIR"

	### create gzip file
	docmd  "(cd $ISOLNX_DIR ; gzip -9 initrd)"
	print_done
}


### build the mdk cloop
build_cloop() {
	echo "* Creating environment ..."
	docmd "rm -rf $MINICD_DIR $BUILD_DIR/mdkimg.*"
	docmd "mkdir -p $MINICD_DIR $ISO_DIR"
	echo "* Copying mdk source ..."
	docmd "cp -a $MINICD_ROOT/{bin,etc,home,lib,sbin,usr,var} $MINICD_DIR"
	docmd "cat $MINICD_ROOT/root/pkglist.txt >$ISO_DIR/pkglist.txt"
	echo "* Cleaning mdk ..."
#usr/share/mdk/kde/*.desktop
	for i in etc/cron.daily/* etc/emacs/* \
                 "home/user/Desktop/Removable\\ media" home/user/Desktop/Trash/* home/user/Desktop/.mdk* \
                 home/user/.blackboxrc home/user/.drakfw home/user/.fonts* home/user/.mailcap \
                 home/user/.mc home/user/.mcop* home/user/.kde/*localhost.localdomain \
                 home/user/.kde/share/config/session/* home/user/.qt/.*lock \
                 home/user/.ICE* home/user/.bash_history home/user/.bash_logout \
                 home/user/.X* home/user/.x* \
                 usr/lib/syslinux/* usr/local/* usr/man/* \
                 usr/share/apps/LICENSES/* usr/share/apps/artsbuilder/examples/* \
                 usr/share/bootsplash/themes/Mandrake/images/*.jpg \
                 usr/share/common-licenses/* usr/share/apps/kdesktop/* usr/share/config/* usr/share/emacs/* usr/share/info/* usr/share/gnome/* usr/share/man/* usr/share/doc/* \
                 usr/share/wallpapers/*.jpg usr/share/wallpapers/kde* \
                 usr/share/mdk/backgrounds/* usr/share/mdk/faces/ic-* \
                 usr/share/themes/* \
                 var/cache/man/* var/cache/urpmi/headers/* var/cache/urpmi/partial/* var/cache/urpmi/rpms/* \
                 var/lib/rpm/__* var/lock/* var/log/* var/run/* var/spool/* var/tmp/*; do
		docmd "rm -rf $MINICD_DIR/$i"
	done
	for i in etc/urpmi/* var/lib/urpmi/* var/lib/rpm/*; do
		docmd "rm -rf $MINICD_DIR/$i"
	done
	docmd "cat $ROOT_DIR/directory.in >$MINICD_DIR/home/user/Desktop/.directory"
	docmd "cat $ROOT_DIR/halt.in >$MINICD_DIR/etc/init.d/halt"
	# Mandrake doesn't create our removed entries as of 9.1-rc2 (this might return)
	#docmd "cat $ROOT_DIR/kdesktop-links.in >$MINICD_DIR/usr/bin/kdesktop-links"
	docmd "mkdir -p $MINICD_DIR/usr/share/doc/HTML"
	docmd "cp -a src/weblogo.png $MINICD_DIR/usr/share/doc/HTML/weblogo.png"
	docmd "cat $ISO_DIR/pkglist.txt >$MINICD_DIR/usr/share/doc/HTML/pkglist.txt"
	docmd "cat $ISO_DIR/CHANGELOG.txt >$MINICD_DIR/usr/share/doc/HTML/CHANGELOG.txt"
	docmd "cat $ISO_DIR/index.html >$MINICD_DIR/usr/share/doc/HTML/index.html"
	docmd "cat $SOURCE_DIR/xbg.png >$MINICD_DIR/usr/share/wallpapers/xbg.png"
	for i in var/lock/subsys \
                 var/log/cron var/log/daemons var/log/kernel var/log/lpr var/log/mail var/log/news \
                 var/run/console var/run/netreport var/run/sudo var/run/usb \
                 var/spool/cron var/spool/lpd var/spool/mail \
         tmp; do
		docmd "mkdir -p $MINICD_DIR/$i"
	done
	for i in etc/modules.conf var/log/wtmp var/run/utmp; do
		docmd ":> $MINICD_DIR/$i"
	done
	echo "* Creating mdkimg.iso ..."
	DISTRO=`gawk -F' ' '{ print $1 }' $MINICD_DIR/etc/mandrake-release`
	VERSION=`gawk -F' ' '{ print $4 }' $MINICD_DIR/etc/mandrake-release`
	VERNAME=`gawk -F' ' '{ print $5 }' $MINICD_DIR/etc/mandrake-release`
	docmd "nice -5 mkisofs -R -U -V '$DISTRO' -P '$URLSTR' -p '$AUTHOR' -A '$VERSION $VERNAME $VERSION_DATE' -hide-rr-moved -cache-inodes -no-bak -pad -o mdkimg.iso $MINICD_DIR"
	echo "* Creating mdkimg.clp ..."
	docmd "nice -5 $BUILD_DIR/compressloop-1.9/compressloop -c 9 -v -b 65536 mdkimg.iso mdkimg.clp"
	docmd "rm -rf $MINICD_DIR"
	docmd "rm -rf $ISO_DIR/mdkimg.clp"
	docmd "cp mdkimg.clp $ISO_DIR/mdkimg.clp"
	echo "* Done."
}


### build the actual iso
build_iso() {
	print_header "Creating iso image ..."
	docmd "nice -5 mkisofs -pad -l -r -J -v -V '$NAMESTR' -P '$URLSTR' -p '$AUTHOR' -A '$NAME_VERSION_STR' -b isolinux/isolinux.bin -c isolinux/boot.cat -hide-rr-moved -no-emul-boot -boot-load-size 4 -boot-info-table -o $ROOT_DIR/bootcd.iso $ISO_DIR"
	print_done
	docmd "md5sum bootcd.iso >bootcd.md5"
}


[ "$USER_ID" == "0" ] || print_error "You have to be root to run this script"
while [ $# -gt 0 ]; do
	case $1 in
		--help)
			print_help
			;;
		--verbose)
			VERBOSE=1
			;;
		--quiet)
			QUIET=1
			;;
		--no-build)
			DO_BUILD=0
			DO_CLEAN=0
			;;
		--no-clean)
			DO_CLEAN=0
			;;
		--no-modules)
			DO_MODULES=0
			DO_CLEAN=0
			;;
		--no-iso)
			DO_ISO=0
			;;
		--no-isolinux)
			DO_ISOLINUX=0
			DO_CLEAN=0
			;;
		--no-initrd)
			DO_INITRD=0
			DO_CLEAN=0
			;;
		--no-cloop)
			DO_CLOOP=0
			DO_CLEAN=0
			;;
		*)
			print_error "Unknown command-line option '$1'"
			;;
	esac
	shift
done
[ $VERBOSE -eq 1 ] && [ $QUIET -eq 1 ] && print_error "--verbose and --quiet cannot be specified together."

[ $DO_CLEAN -eq 1 ]    && clean_all
[ $DO_BUILD -eq 1 ]    && build_static
[ $DO_MODULES -eq 1 ]  && build_modules
[ $DO_ISOLINUX -eq 1 ] && install_isolinux
[ $DO_INITRD -eq 1 ]   && build_initrd
[ $DO_CLOOP -eq 1 ]    && build_cloop
[ $DO_ISO -eq 1 ]      && build_iso
