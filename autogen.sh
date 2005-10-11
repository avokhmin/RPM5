#!/bin/sh

export CFLAGS
export LDFLAGS

LTV="libtoolize (GNU libtool) 1.5.18"
ACV="autoconf (GNU Autoconf) 2.59"
AMV="automake (GNU automake) 1.9.5"
USAGE="
This script documents the versions of the tools I'm using to build rpm:
	libtool-1.5.18
	autoconf-2.59
	automake-1.9.5
Simply edit this script to change the libtool/autoconf/automake versions
checked if you need to, as rpm should build (and has built) with all
recent versions of libtool/autoconf/automake.
"

libtoolize=`which glibtoolize 2>/dev/null`
case $libtoolize in
/*) ;;
*)  libtoolize=`which libtoolize 2>/dev/null`
    case $libtoolize in
    /*) ;;
    *)  libtoolize=libtoolize
    esac
esac

[ "`$libtoolize --version | head -1`" != "$LTV" ] && echo "$USAGE" # && exit 1
[ "`autoconf --version | head -1`" != "$ACV" ] && echo "$USAGE" # && exit 1
[ "`automake --version | head -1 | sed -e 's/1\.4[a-z]/1.4/'`" != "$AMV" ] && echo "$USAGE" # && exit 1

myopts=
if [ X"$@" = X  -a "X`uname -s`" = "XDarwin" -a -d /opt/local ]; then
    export myopts="--prefix=/usr --disable-nls"
    export CPPFLAGS="-I${myprefix}/include"
fi

if [ -d popt ]; then
    (echo "--- popt"; cd popt; sh ./autogen.sh --noconfigure "$@")
fi
if [ -d zlib ]; then
    (echo "--- zlib"; cd zlib; sh ./autogen.sh --noconfigure "$@")
fi
if [ -d beecrypt ]; then
    (echo "--- beecrypt"; cd beecrypt; sh ./autogen.sh --noconfigure "$@")
fi
if [ -d elfutils ]; then
    (echo "--- elfutils"; cd elfutils; sh ./autogen.sh --noconfigure "$@")
fi
if [ -d file ]; then
    (echo "--- file"; cd file; sh ./autogen.sh --noconfigure "$@")
fi
if [ -d neon ]; then
    (echo "--- neon"; cd neon; sh ./autogen.sh "$@")
fi
if [ -d sqlite ]; then
    (echo "--- sqlite"; cd sqlite; sh ./autogen.sh --disable-tcl "$@")
fi

echo "--- rpm"
$libtoolize --copy --force
aclocal
autoheader
automake -a -c
autoconf

if [ "$1" = "--noconfigure" ]; then 
    exit 0;
fi

if [ X"$@" = X  -a "X`uname -s`" = "XLinux" ]; then
    if [ -d /usr/share/man ]; then
	mandir=/usr/share/man
	infodir=/usr/share/info
    else
	mandir=/usr/man
	infodir=/usr/info
    fi
    if [ -d /usr/lib/nptl ]; then
	enable_posixmutexes="--enable-posixmutexes"
    else
	enable_posixmutexes=
    fi
    if [ -d /usr/include/selinux ]; then
	disable_selinux=
    else
	disable_selinux="--without-selinux"
    fi
    sh ./configure --prefix=/usr --sysconfdir=/etc --localstatedir=/var --infodir=${infodir} --mandir=${mandir} ${enable_posixmutexes} ${disable_selinux} "$@"
else
    ./configure ${myopts} "$@"
fi
