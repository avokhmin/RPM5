%define	_noVersionedDependencies	1

%define	__prefix	/usr
%{expand:%%define __share %(if [ -d %{__prefix}/share/man ]; then echo /share ; else echo %%{nil} ; fi)}

%define	__mandir	%{__prefix}%{__share}/man

Summary: The Red Hat package management system.
Name: rpm
%define version 3.0.7
Version: %{version}
Release: 0.3
Group: System Environment/Base
Source: ftp://ftp.rpm.org/pub/rpm/dist/rpm-3.0.x/rpm-%{version}.tar.gz
Copyright: GPL
Conflicts: patch < 2.5
%ifos linux
# XXX the libio interface is incompatible in glibc-2.2
Conflicts: glibc > 2.1.3
Prereq: gawk fileutils textutils sh-utils mktemp
BuildRequires: bzip2 >= 0.9.0c-2
Requires: popt, bzip2 >= 0.9.0c-2
BuildRequires: python-devel >= 1.5.2
%endif
BuildRoot: %{_tmppath}/%{name}-root

%description
The RPM Package Manager (RPM) is a powerful command line driven
package management system capable of installing, uninstalling,
verifying, querying, and updating software packages.  Each software
package consists of an archive of files along with information about
the package like its version, a description, etc.

%package devel
Summary: Development files for applications which will manipulate RPM packages.
Group: Development/Libraries
Requires: rpm = %{version}, popt

%description devel
This package contains the RPM C library and header files.  These
development files will simplify the process of writing programs which
manipulate RPM packages and databases. These files are intended to
simplify the process of creating graphical package managers or any
other tools that need an intimate knowledge of RPM packages in order
to function.

This package should be installed if you want to develop programs that
will manipulate RPM packages and databases.

%package build
Summary: Scripts and executable programs used to build packages.
Group: Development/Tools
Requires: rpm = %{version}

%description build
This package contains scripts and executable programs that are used to
build packages using RPM.

%ifos linux
%package python
Summary: Python bindings for apps which will manipulate RPM packages.
Group: Development/Libraries
BuildRequires: popt >= 1.5
Requires: popt >= 1.5
Requires: python >= 1.5.2

%description python
The rpm-python package contains a module which permits applications
written in the Python programming language to use the interface
supplied by RPM (RPM Package Manager) libraries.

This package should be installed if you want to develop Python
programs that will manipulate RPM packages and databases.
%endif

%package -n popt
Summary: A C library for parsing command line parameters.
Group: Development/Libraries
Version: 1.5.1

%description -n popt
Popt is a C library for parsing command line parameters.  Popt was
heavily influenced by the getopt() and getopt_long() functions, but it
improves on them by allowing more powerful argument expansion.  Popt
can parse arbitrary argv[] style arrays and automatically set
variables based on command line arguments.  Popt allows command line
arguments to be aliased via configuration files and includes utility
functions for parsing arbitrary strings into argv[] arrays using
shell-like rules.

Install popt if you're a C programmer and you'd like to use its
capabilities.

%prep
%setup -q

%build
%ifos linux
CFLAGS="$RPM_OPT_FLAGS" ./configure --prefix=%{__prefix} --sysconfdir=/etc --localstatedir=/var --infodir='${prefix}%{__share}/info' --mandir='${prefix}%{__share}/man'
%else
CFLAGS="$RPM_OPT_FLAGS" ./configure --prefix=%{__prefix}
%endif

make

%ifos linux
make -C python
%endif

%install
rm -rf $RPM_BUILD_ROOT

make DESTDIR="$RPM_BUILD_ROOT" install
%ifos linux
make DESTDIR="$RPM_BUILD_ROOT" install -C python
mkdir -p $RPM_BUILD_ROOT/etc/rpm
%endif

{ cd $RPM_BUILD_ROOT
  strip ./bin/rpm
  strip .%{__prefix}/bin/rpm2cpio
  strip .%{__prefix}/lib/rpm/rpmputtext .%{__prefix}/lib/rpm/rpmgettext
}

%clean
rm -rf $RPM_BUILD_ROOT

%post
/bin/rpm --initdb
%ifos linux
if [ ! -e /etc/rpm/macros -a -e /etc/rpmrc -a -f %{__prefix}/lib/rpm/convertrpmrc.sh ] 
then
	sh %{__prefix}/lib/rpm/convertrpmrc.sh > /dev/null 2>&1
fi
%endif

%ifos linux
%post devel -p /sbin/ldconfig
%postun devel -p /sbin/ldconfig

%post python -p /sbin/ldconfig
%postun python -p /sbin/ldconfig

%post -n popt -p /sbin/ldconfig
%postun -n popt -p /sbin/ldconfig
%endif

%files
%defattr(-,root,root)
%doc RPM-PGP-KEY RPM-GPG-KEY CHANGES GROUPS doc/manual/*
/bin/rpm
%ifos linux
%dir /etc/rpm
%endif
%{__prefix}/bin/rpm2cpio
%{__prefix}/bin/gendiff
%{__prefix}/lib/librpm.so.*
%{__prefix}/lib/librpmbuild.so.*

%{__prefix}/lib/rpm/brp-*
%{__prefix}/lib/rpm/config.guess
%{__prefix}/lib/rpm/config.sub
%{__prefix}/lib/rpm/convertrpmrc.sh
%{__prefix}/lib/rpm/find-prov.pl
%{__prefix}/lib/rpm/find-provides
%{__prefix}/lib/rpm/find-req.pl
%{__prefix}/lib/rpm/find-requires
%{__prefix}/lib/rpm/macros
%{__prefix}/lib/rpm/mkinstalldirs
%{__prefix}/lib/rpm/rpmpopt
%{__prefix}/lib/rpm/rpmrc
%{__prefix}/lib/rpm/vpkg-provides.sh
%{__prefix}/lib/rpm/vpkg-provides2.sh

%ifarch i386 i486 i586 i686
%{__prefix}/lib/rpm/i[3456]86*
%endif
%ifarch alpha
%{__prefix}/lib/rpm/alpha*
%endif
%ifarch sparc sparc64
%{__prefix}/lib/rpm/sparc*
%endif
%ifarch ia64
%{__prefix}/lib/rpm/ia64*
%endif
%ifarch powerpc ppc
%{__prefix}/lib/rpm/ppc*
%endif

%dir %{__prefix}/src/redhat
%dir %{__prefix}/src/redhat/BUILD
%dir %{__prefix}/src/redhat/SPECS
%dir %{__prefix}/src/redhat/SOURCES
%dir %{__prefix}/src/redhat/SRPMS
%dir %{__prefix}/src/redhat/RPMS
%{__prefix}/src/redhat/RPMS/*
%{__prefix}/*/locale/*/LC_MESSAGES/rpm.mo
%{__mandir}/man[18]/*.[18]*
%lang(pl) %{__mandir}/pl/man[18]/*.[18]*
%lang(ru) %{__mandir}/ru/man[18]/*.[18]*
%lang(sk) %{__mandir}/sk/man[18]/*.[18]*

%files build
%defattr(-,root,root)
%{__prefix}/lib/rpm/check-prereqs
%{__prefix}/lib/rpm/cpanflute
%{__prefix}/lib/rpm/find-lang.sh
%{__prefix}/lib/rpm/find-provides.perl
%{__prefix}/lib/rpm/find-requires.perl
%{__prefix}/lib/rpm/get_magic.pl
%{__prefix}/lib/rpm/getpo.sh
%{__prefix}/lib/rpm/http.req
%{__prefix}/lib/rpm/magic.prov
%{__prefix}/lib/rpm/magic.req
%{__prefix}/lib/rpm/perl.prov
%{__prefix}/lib/rpm/perl.req
%{__prefix}/lib/rpm/rpmdiff
%{__prefix}/lib/rpm/rpmdiff.cgi
%{__prefix}/lib/rpm/rpmgettext
%{__prefix}/lib/rpm/rpmputtext
%{__prefix}/lib/rpm/u_pkg.sh

%ifos linux
%files python
%defattr(-,root,root)
%{__prefix}/lib/python1.5/site-packages/rpmmodule.so
%endif

%files devel
%defattr(-,root,root)
%{__prefix}/include/rpm
%{__prefix}/lib/librpm.a
%{__prefix}/lib/librpm.la
%{__prefix}/lib/librpm.so
%{__prefix}/lib/librpmbuild.a
%{__prefix}/lib/librpmbuild.la
%{__prefix}/lib/librpmbuild.so

%files -n popt
%defattr(-,root,root)
%{__prefix}/lib/libpopt.so.*
%{__prefix}/*/locale/*/LC_MESSAGES/popt.mo
%{__mandir}/man3/popt.3*

# XXX These may end up in popt-devel but it hardly seems worth the effort now.
%{__prefix}/lib/libpopt.a
%{__prefix}/lib/libpopt.la
%{__prefix}/lib/libpopt.so
%{__prefix}/include/popt.h

%changelog
* Mon Oct 16 2000 Jeff Johnson <jbj@redhat.com>
- sync python bindings with rpm4.

* Mon Sep 18 2000 Jeff Johnson <jbj@redhat.com>
- conflicts should permit installation with glibc-2.1.3.

* Thu Sep 14 2000 Jeff Johnson <jbj@redhat.com>
- start rpm-3.0.7.
