Summary: The Red Hat package management system.
Name: rpm
%define version 3.0.3
Version: %{version}
Release: 0.25
Group: System Environment/Base
Source: ftp://ftp.rpm.org/pub/rpm/dist/rpm-3.0.x/rpm-%{version}.tar.gz
Copyright: GPL
Conflicts: patch < 2.5
%ifos linux
Prereq: gawk fileutils textutils sh-utils mktemp
BuildRequires: bzip2 >= 0.9.0c-2
#BuildRequires: python-devel = 1.5.1
%endif
BuildRoot: /var/tmp/%{name}-root

%description
The Red Hat Package Manager (RPM) is a powerful command line driven
package management system capable of installing, uninstalling,
verifying, querying, and updating software packages.  Each software
package consists of an archive of files along with information about
the package like its version, a description, etc.

%package devel
Summary: Development files for applications which will manipulate RPM packages.
Group: Development/Libraries
Requires: popt
%ifos linux
Requires: python >= 1.5.1
%endif

%description devel
This package contains the RPM C library and header files.  These
development files will simplify the process of writing programs
which manipulate RPM packages and databases and are intended to make
it easier to create graphical package managers or any other tools
that need an intimate knowledge of RPM packages in order to function.

This package should be installed if you want to develop programs that
will manipulate RPM packages and databases.

%prep
%setup -q

%build
CFLAGS="$RPM_OPT_FLAGS" ./configure --prefix=/usr
make
%ifos linux
make -C python
%endif

%install
rm -rf $RPM_BUILD_ROOT

make DESTDIR="$RPM_BUILD_ROOT" install
%ifos linux
make DESTDIR="$RPM_BUILD_ROOT" install -C python
%endif

{ cd $RPM_BUILD_ROOT
  strip ./bin/rpm
  strip ./usr/bin/rpm2cpio
  strip ./usr/lib/rpm/rpmputtext ./usr/lib/rpm/rpmgettext
}

%clean
rm -rf $RPM_BUILD_ROOT

%post
/bin/rpm --initdb
%ifos linux
if [ ! -e /etc/rpm/macros -a -e /etc/rpmrc -a -f /usr/lib/rpm/convertrpmrc.sh ] 
then
	sh /usr/lib/rpm/convertrpmrc.sh 2>&1 > /dev/null
fi
%endif

%ifos linux
%post devel -p /sbin/ldconfig
%postun devel -p /sbin/ldconfig
%endif

%files
%defattr(-,root,root)
%doc RPM-PGP-KEY CHANGES GROUPS doc/manual/*
/bin/rpm
/usr/bin/rpm2cpio
/usr/bin/gendiff
/usr/lib/librpm.so.*
/usr/lib/librpmbuild.so.*
/usr/lib/rpm
%dir /usr/src/redhat
%dir /usr/src/redhat/BUILD
%dir /usr/src/redhat/SPECS
%dir /usr/src/redhat/SOURCES
%dir /usr/src/redhat/SRPMS
%dir /usr/src/redhat/RPMS
/usr/src/redhat/RPMS/*
/usr/share/locale/*/LC_MESSAGES/rpm.mo
/usr/man/man8/*.8
%lang(pl) /usr/man/pl/man8/*
%lang(ru) /usr/man/ru/man8/*

%files devel
%defattr(-,root,root)
/usr/include/rpm
/usr/lib/librpm.a
/usr/lib/librpm.la
/usr/lib/librpm.so
/usr/lib/librpmbuild.a
/usr/lib/librpmbuild.la
/usr/lib/librpmbuild.so
%ifos linux
/usr/lib/python1.5/site-packages/rpmmodule.so
%endif
