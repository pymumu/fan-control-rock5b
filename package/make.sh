#!/bin/sh
#
# Copyright (C) 2022 Ruilin Peng (Nick) <pymumu@gmail.com>.
#
# fan-control is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# fan-control is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
CURR_DIR=$(cd $(dirname $0);pwd)
VER="1.0.0"
FAN_CONTROL_DIR=$CURR_DIR/..
FAN_CONTROL_BIN=$FAN_CONTROL_DIR/src/fan-control

showhelp()
{
	echo "Usage: make [OPTION]"
	echo "Options:"
	echo " -o               output directory."
	echo " --arch           archtecture."
	echo " --ver            version."
	echo " -h               show this message."
}

build()
{
	ROOT=/tmp/fan-control-deiban
	rm -fr $ROOT
	mkdir -p $ROOT
	cd $ROOT/

	cp $CURR_DIR/DEBIAN $ROOT/ -af
	CONTROL=$ROOT/DEBIAN/control
	mkdir $ROOT/usr/sbin -p
	mkdir $ROOT/lib/systemd/system -p
    mkdir $ROOT/etc/init.d -p

	pkgver=$(echo ${VER}| sed 's/^1\.//g')
	sed -i "s/Version:.*/Version: ${pkgver}/" $ROOT/DEBIAN/control
	sed -i "s/Architecture:.*/Architecture: $ARCH/" $ROOT/DEBIAN/control
	chmod 0755 $ROOT/DEBIAN/prerm

	cp $FAN_CONTROL_DIR/systemd/fan-control.service $ROOT/lib/systemd/system/
	cp $FAN_CONTROL_DIR/etc/init.d/fan-control $ROOT/etc/init.d/
	cp $FAN_CONTROL_DIR/etc/fan-control.json $ROOT/etc/
	cp $FAN_CONTROL_DIR/src/fan-control $ROOT/usr/sbin
	if [ $? -ne 0 ]; then
		echo "copy fan-control file failed."
		return 1
	fi
	chmod +x $ROOT/usr/sbin/fan-control
    chmod +x $ROOT/etc/init.d/fan-control

	dpkg -b $ROOT $OUTPUTDIR/fan-control-rock5b.$VER.$FILEARCH.deb

	rm -fr $ROOT/
}

main()
{
	OPTS=`getopt -o o:h --long arch:,ver:,filearch: \
		-n  "" -- "$@"`

	if [ $? != 0 ] ; then echo "Terminating..." >&2 ; exit 1 ; fi

	# Note the quotes around `$TEMP': they are essential!
	eval set -- "$OPTS"

	while true; do
		case "$1" in
		--arch)
			ARCH="$2"
			shift 2;;
		--filearch)
			FILEARCH="$2"
			shift 2;;
		--ver)
			VER="$2"
			shift 2;;
		-o )
			OUTPUTDIR="$2"
			shift 2;;
		-h | --help )
			showhelp
			return 0
			shift ;;
		-- ) shift; break ;;
		* ) break ;;
		esac
	done

	if [ -z "$ARCH" ]; then
		ARCH="`dpkg --print-architecture`"
	fi

	if [ -z "$FILEARCH" ]; then
		FILEARCH=$ARCH
	fi

	if [ -z "$OUTPUTDIR" ]; then
		OUTPUTDIR=$CURR_DIR;
	fi

	build
}

main $@
exit $?
