%define	with_python_subpackage	1 %{nil}
%define with_perl_subpackage	1
%define	with_bzip2		1 %{nil}
%define	with_apidocs		1 %{nil}
%define with_internal_db	1 %{nil}
%define strip_binaries		1

# XXX enable at your own risk, CDB access to rpmdb isn't cooked yet.
%define	enable_cdb		create cdb

# XXX legacy requires './' payload prefix to be omitted from rpm packages.
%define	_noPayloadPrefix	1

%define	__prefix	/usr
%{expand: %%define __share %(if [ -d %{__prefix}/share/man ]; then echo /share ; else echo %%{nil} ; fi)}

Summary: The Red Hat package management system.
Name: rpm
%define version 4.0.3
Version: %{version}
%{expand: %%define rpm_version %{version}}
Release: 0.95
Group: System Environment/Base
Source: ftp://ftp.rpm.org/pub/rpm/dist/rpm-4.0.x/rpm-%{rpm_version}.tar.gz
Copyright: GPL
Conflicts: patch < 2.5
%ifos linux
Prereq: gawk fileutils textutils mktemp shadow-utils
%endif
Requires: popt = 1.6.3

%if !%{with_internal_db}
BuildRequires: db3-devel

# XXX glibc-2.1.92 has incompatible locale changes that affect statically
# XXX linked binaries like /bin/rpm.
%ifnarch ia64
Requires: glibc >= 2.1.92
%endif
%endif

BuildRequires: zlib-devel
# XXX Red Hat 5.2 has not bzip2 or python
%if %{with_bzip2}
BuildRequires: bzip2 >= 0.9.0c-2
%endif
%if %{with_python_subpackage}
BuildRequires: python-devel >= 1.5.2
%endif
%if %{with_perl_subpackage}
BuildRequires: perl >= 0:5.00503
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
Requires: rpm = %{rpm_version}, popt = 1.6.3

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
Requires: rpm = %{rpm_version}

%description build
This package contains scripts and executable programs that are used to
build packages using RPM.

%if %{with_python_subpackage}
%package python
Summary: Python bindings for apps which will manipulate RPM packages.
Group: Development/Libraries
Requires: rpm = %{rpm_version}
Requires: python >= 1.5.2
Requires: popt = 1.6.3

%description python
The rpm-python package contains a module which permits applications
written in the Python programming language to use the interface
supplied by RPM (RPM Package Manager) libraries.

This package should be installed if you want to develop Python
programs that will manipulate RPM packages and databases.
%endif

%if %{with_perl_subpackage}
%package perl
Summary: Native bindings to the RPM API for Perl.
Group: Development/Languages
URL: http://www.cpan.org
Provides: perl(RPM::Database) = %{rpm_version}
Provides: perl(RPM::Header) = %{rpm_version}
Requires: rpm = %{rpm_version}
Requires: perl >= 0:5.00503
Requires: popt = 1.6.3
Obsoletes: perl-Perl-RPM

%description perl
The Perl-RPM module is an attempt to provide Perl-level access to the
complete application programming interface that is a part of the Red
Hat Package Manager (RPM). Rather than have scripts rely on executing
RPM commands and parse the resulting output, this module aims to give
Perl programmers the ability to do anything that would otherwise have
been done in C or C++.

The interface is being designed and laid out as a collection of
classes, at least some of which are also available as tied-hash
implementations.

At this time, the interface only provides access to the database of
installed packages, and header data retrieval for RPM and SRPM files
is not yet installed.  Error management and the export of most defined
constants, through RPM::Error and RPM::Constants, respectively, are
also available.

%endif

%package -n popt
Summary: A C library for parsing command line parameters.
Group: Development/Libraries
Version: 1.6.3

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

# XXX workaround alpha sha1 digest miscompilation
%ifarch alpha alphaev5 alphaev56 alphapca56 alphaev6 alphaev67
make CFLAGS="-g -O0" digest.o digest.lo -C rpmio
%endif

make

%if %{with_perl_subpackage}
{ cd Perl-RPM
  CFLAGS="$RPM_OPT_FLAGS" perl Makefile.PL
  export SUBDIR="%{_builddir}/%{buildsubdir}"
  make INC="-I. -I$SUBDIR/lib -I$SUBDIR/rpmio -I$SUBDIR/popt" %{?_smp_mflags}
}
%endif

%install
rm -rf $RPM_BUILD_ROOT

make DESTDIR="$RPM_BUILD_ROOT" install

%ifos linux

# Save list of packages through cron
mkdir -p ${RPM_BUILD_ROOT}/etc/cron.daily
install -m 755 scripts/rpm.daily ${RPM_BUILD_ROOT}/etc/cron.daily/rpm

mkdir -p ${RPM_BUILD_ROOT}/etc/logrotate.d
install -m 755 scripts/rpm.log ${RPM_BUILD_ROOT}/etc/logrotate.d/rpm

mkdir -p $RPM_BUILD_ROOT/etc/rpm
cat << E_O_F > $RPM_BUILD_ROOT/etc/rpm/macros.db1
%%_dbapi		1
E_O_F
cat << E_O_F > $RPM_BUILD_ROOT/etc/rpm/macros.cdb
%{?enable_cdb:#%%__dbi_cdb	%{enable_cdb}}
E_O_F

mkdir -p $RPM_BUILD_ROOT/var/lib/rpm
for dbi in \
	Basenames Conflictname Dirnames Group Installtid Name Providename \
	Provideversion Removetid Requirename Requireversion Triggername \
	Packages __db.001 __db.002 __db.003 __db.004 __db.005 __db.006 __db.007 \
	__db.008 __db.009
do
    touch $RPM_BUILD_ROOT/var/lib/rpm/$dbi
done

%endif

%if %{with_apidocs}
gzip -9n apidocs/man/man*/* || :
%endif

%if %{with_perl_subpackage}
{ cd Perl-RPM
  eval `perl '-V:installsitearch'`
  eval `perl '-V:installarchlib'`
  mkdir -p $RPM_BUILD_ROOT/$installarchlib
  make PREFIX=$RPM_BUILD_ROOT/usr install
  rm -f $RPM_BUILD_ROOT/$installarchlib/perllocal.pod
  rm -f $RPM_BUILD_ROOT/$installsitearch/auto/RPM/.packlist
  cd ..
}
%endif

%if %{strip_binaries}
{ cd $RPM_BUILD_ROOT
  %{__strip} ./bin/rpm
  %{__strip} .%{__prefix}/bin/rpm2cpio
}
%endif

%clean
rm -rf $RPM_BUILD_ROOT

%pre
%ifos linux
if [ -f /var/lib/rpm/Packages -a -f /var/lib/rpm/packages.rpm ]; then
    echo "
You have both
	/var/lib/rpm/packages.rpm	db1 format installed package headers
	/var/lib/rpm/Packages		db3 format installed package headers
Please remove (or at least rename) one of those files, and re-install.
"
    exit 1
fi
/usr/sbin/groupadd -g 37 rpm				> /dev/null 2>&1
/usr/sbin/useradd  -r -d /var/lib/rpm -u 37 -g 37 rpm	> /dev/null 2>&1
%endif
exit 0

%post
%ifos linux
/sbin/ldconfig
if [ -f /var/lib/rpm/packages.rpm ]; then
    /bin/chown rpm.rpm /var/lib/rpm/*.rpm
elif [ -f /var/lib/rpm/Packages ]; then
    # undo db1 configuration
    rm -f /etc/rpm/macros.db1
    /bin/chown rpm.rpm /var/lib/rpm/[A-Z]*
else
    # initialize db3 database
    rm -f /etc/rpm/macros.db1
    /bin/rpm --initdb
fi
%endif
exit 0

%ifos linux
%postun
/sbin/ldconfig
if [ $1 = 0 ]; then
    /usr/sbin/userdel rpm
    /usr/sbin/groupdel rpm
fi


%post devel -p /sbin/ldconfig
%postun devel -p /sbin/ldconfig

%post -n popt -p /sbin/ldconfig
%postun -n popt -p /sbin/ldconfig
%endif

%if %{with_python_subpackage}
%post python -p /sbin/ldconfig
%postun python -p /sbin/ldconfig
%endif

%define	rpmattr		%attr(0755, rpm, rpm)

%files
%defattr(-,root,root)
%doc RPM-PGP-KEY RPM-GPG-KEY CHANGES GROUPS doc/manual/[a-z]*
%attr(0755, rpm, rpm)	/bin/rpm

%ifos linux
%config(noreplace,missingok)	/etc/cron.daily/rpm
%config(noreplace,missingok)	/etc/logrotate.d/rpm
%dir				/etc/rpm
%config(noreplace,missingok)	/etc/rpm/macros.*
%attr(0755, rpm, rpm)	%dir /var/lib/rpm

%define	rpmdbattr %attr(0644, rpm, rpm) %verify(not md5 size mtime) %ghost %config(missingok,noreplace)
%rpmdbattr	/var/lib/rpm/Basenames
%rpmdbattr	/var/lib/rpm/Conflictname
%rpmdbattr	/var/lib/rpm/__db.0*
%rpmdbattr	/var/lib/rpm/Dirnames
%rpmdbattr	/var/lib/rpm/Group
%rpmdbattr	/var/lib/rpm/Installtid
%rpmdbattr	/var/lib/rpm/Name
%rpmdbattr	/var/lib/rpm/Packages
%rpmdbattr	/var/lib/rpm/Providename
%rpmdbattr	/var/lib/rpm/Provideversion
%rpmdbattr	/var/lib/rpm/Removetid
%rpmdbattr	/var/lib/rpm/Requirename
%rpmdbattr	/var/lib/rpm/Requireversion
%rpmdbattr	/var/lib/rpm/Triggername

%endif

%rpmattr	%{__prefix}/bin/rpm2cpio
%rpmattr	%{__prefix}/bin/gendiff
%rpmattr	%{__prefix}/bin/rpmdb
#%rpmattr	%{__prefix}/bin/rpm[eiu]
%rpmattr	%{__prefix}/bin/rpmsign
%rpmattr	%{__prefix}/bin/rpmquery
%rpmattr	%{__prefix}/bin/rpmverify

%{__prefix}/lib/librpm-4.0.3.so
%{__prefix}/lib/librpmdb-4.0.3.so
%{__prefix}/lib/librpmio-4.0.3.so
%{__prefix}/lib/librpmbuild-4.0.3.so

%attr(0755, rpm, rpm)	%dir %{__prefix}/lib/rpm
%rpmattr	%{__prefix}/lib/rpm/config.guess
%rpmattr	%{__prefix}/lib/rpm/config.sub
%rpmattr	%{__prefix}/lib/rpm/convertrpmrc.sh
%attr(0644, rpm, rpm)	%{__prefix}/lib/rpm/macros
%rpmattr	%{__prefix}/lib/rpm/mkinstalldirs
%rpmattr	%{__prefix}/lib/rpm/rpm.*
%rpmattr	%{__prefix}/lib/rpm/rpm[deiukqv]
%attr(0644, rpm, rpm)	%{__prefix}/lib/rpm/rpmpopt*
%attr(0644, rpm, rpm)	%{__prefix}/lib/rpm/rpmrc

%ifarch i386 i486 i586 i686 athlon
%attr(-, rpm, rpm)		%{__prefix}/lib/rpm/i[3456]86*
%attr(-, rpm, rpm)		%{__prefix}/lib/rpm/athlon*
%endif
%ifarch alpha alphaev5 alphaev56 alphapca56 alphaev6 alphaev67
%attr(-, rpm, rpm)		%{__prefix}/lib/rpm/alpha*
%endif
%ifarch sparc sparcv9 sparc64
%attr(-, rpm, rpm)		%{__prefix}/lib/rpm/sparc*
%endif
%ifarch ia64
%attr(-, rpm, rpm)		%{__prefix}/lib/rpm/ia64*
%endif
%ifarch powerpc ppc ppciseries ppcpseries ppcmac
%attr(-, rpm, rpm)		%{__prefix}/lib/rpm/ppc*
%endif
%ifarch s390 s390x
%attr(-, rpm, rpm)		%{__prefix}/lib/rpm/s390*
%endif
%ifarch armv3l armv4l
%attr(-, rpm, rpm)		%{__prefix}/lib/rpm/armv[34][lb]*
%endif
%ifarch mips mipsel mipseb
%attr(-, rpm, rpm)		%{__prefix}/lib/rpm/mips*
%endif
%attr(-, rpm, rpm)		%{__prefix}/lib/rpm/noarch*

%lang(cs)	%{__prefix}/*/locale/cs/LC_MESSAGES/rpm.mo
%lang(da)	%{__prefix}/*/locale/da/LC_MESSAGES/rpm.mo
%lang(de)	%{__prefix}/*/locale/de/LC_MESSAGES/rpm.mo
%lang(fi)	%{__prefix}/*/locale/fi/LC_MESSAGES/rpm.mo
%lang(fr)	%{__prefix}/*/locale/fr/LC_MESSAGES/rpm.mo
%lang(is)	%{__prefix}/*/locale/is/LC_MESSAGES/rpm.mo
%lang(ja)	%{__prefix}/*/locale/ja/LC_MESSAGES/rpm.mo
%lang(no)	%{__prefix}/*/locale/no/LC_MESSAGES/rpm.mo
%lang(pl)	%{__prefix}/*/locale/pl/LC_MESSAGES/rpm.mo
%lang(pt)	%{__prefix}/*/locale/pt/LC_MESSAGES/rpm.mo
%lang(pt_BR)	%{__prefix}/*/locale/pt_BR/LC_MESSAGES/rpm.mo
%lang(ro)	%{__prefix}/*/locale/ro/LC_MESSAGES/rpm.mo
%lang(ru)	%{__prefix}/*/locale/ru/LC_MESSAGES/rpm.mo
%lang(sk)	%{__prefix}/*/locale/sk/LC_MESSAGES/rpm.mo
%lang(sl)	%{__prefix}/*/locale/sl/LC_MESSAGES/rpm.mo
%lang(sr)	%{__prefix}/*/locale/sr/LC_MESSAGES/rpm.mo
%lang(sv)	%{__prefix}/*/locale/sv/LC_MESSAGES/rpm.mo
%lang(tr)	%{__prefix}/*/locale/tr/LC_MESSAGES/rpm.mo

%{__prefix}%{__share}/man/man1/gendiff.1*
%{__prefix}%{__share}/man/man8/rpm.8*
%{__prefix}%{__share}/man/man8/rpm2cpio.8*
%lang(pl)	%{__prefix}%{__share}/man/pl/man[18]/*.[18]*
%lang(ru)	%{__prefix}%{__share}/man/ru/man[18]/*.[18]*
%lang(sk)	%{__prefix}%{__share}/man/sk/man[18]/*.[18]*

%files build
%defattr(-,root,root)
%dir %{__prefix}/src/redhat
%dir %{__prefix}/src/redhat/BUILD
%dir %{__prefix}/src/redhat/SPECS
%dir %{__prefix}/src/redhat/SOURCES
%dir %{__prefix}/src/redhat/SRPMS
%dir %{__prefix}/src/redhat/RPMS
%{__prefix}/src/redhat/RPMS/*
%rpmattr	%{__prefix}/bin/rpmbuild
%rpmattr	%{__prefix}/lib/rpm/brp-*
%rpmattr	%{__prefix}/lib/rpm/check-prereqs
%rpmattr	%{__prefix}/lib/rpm/config.site
%rpmattr	%{__prefix}/lib/rpm/cpanflute
%rpmattr	%{__prefix}/lib/rpm/cross-build
%rpmattr	%{__prefix}/lib/rpm/find-lang.sh
%rpmattr	%{__prefix}/lib/rpm/find-prov.pl
%rpmattr	%{__prefix}/lib/rpm/find-provides
%rpmattr	%{__prefix}/lib/rpm/find-provides.perl
%rpmattr	%{__prefix}/lib/rpm/find-req.pl
%rpmattr	%{__prefix}/lib/rpm/find-requires
%rpmattr	%{__prefix}/lib/rpm/find-requires.perl
%rpmattr	%{__prefix}/lib/rpm/get_magic.pl
%rpmattr	%{__prefix}/lib/rpm/getpo.sh
%rpmattr	%{__prefix}/lib/rpm/http.req
%rpmattr	%{__prefix}/lib/rpm/javadeps
%rpmattr	%{__prefix}/lib/rpm/magic.prov
%rpmattr	%{__prefix}/lib/rpm/magic.req
%rpmattr	%{__prefix}/lib/rpm/perl.prov

# XXX remove executable bit to disable autogenerated perl requires for now.
#%rpmattr	%{__prefix}/lib/rpm/perl.req
%attr(0644, rpm, rpm) %{__prefix}/lib/rpm/perl.req

%rpmattr	%{__prefix}/lib/rpm/rpm[bt]
%rpmattr	%{__prefix}/lib/rpm/rpmdiff
%rpmattr	%{__prefix}/lib/rpm/rpmdiff.cgi
%rpmattr	%{__prefix}/lib/rpm/u_pkg.sh
%rpmattr	%{__prefix}/lib/rpm/vpkg-provides.sh
%rpmattr	%{__prefix}/lib/rpm/vpkg-provides2.sh

%{__prefix}%{__share}/man/man8/rpmbuild.8*

%if %{with_python_subpackage}
%files python
%defattr(-,root,root)
%{__prefix}/lib/python1.5/site-packages/rpmmodule.so
%{__prefix}/lib/python1.5/site-packages/poptmodule.so
%endif

%if %{with_perl_subpackage}
%files perl
%defattr(-,root,root)
%rpmattr	%{__prefix}/bin/rpmprune
%{perl_sitearch}/auto/*
%{perl_sitearch}/RPM
%{perl_sitearch}/RPM.pm
%{__prefix}%{__share}/man/man1/rpmprune.1*
%{__prefix}%{__share}/man/man3/RPM*
%endif

%files devel
%defattr(-,root,root)
%if %{with_apidocs}
%doc apidocs
%endif
%{__prefix}/include/rpm
%{__prefix}/lib/librpm.a
%{__prefix}/lib/librpm.la
%{__prefix}/lib/librpm.so
%{__prefix}/lib/librpmdb.a
%{__prefix}/lib/librpmdb.la
%{__prefix}/lib/librpmdb.so
%{__prefix}/lib/librpmio.a
%{__prefix}/lib/librpmio.la
%{__prefix}/lib/librpmio.so
%{__prefix}/lib/librpmbuild.a
%{__prefix}/lib/librpmbuild.la
%{__prefix}/lib/librpmbuild.so

%files -n popt
%defattr(-,root,root)
%{__prefix}/lib/libpopt.so.*
%{__prefix}%{__share}/man/man3/popt.3*
%lang(cs)	%{__prefix}/*/locale/cs/LC_MESSAGES/popt.mo
%lang(da)	%{__prefix}/*/locale/da/LC_MESSAGES/popt.mo
%lang(gl)	%{__prefix}/*/locale/gl/LC_MESSAGES/popt.mo
%lang(hu)	%{__prefix}/*/locale/hu/LC_MESSAGES/popt.mo
%lang(is)	%{__prefix}/*/locale/is/LC_MESSAGES/popt.mo
%lang(no)	%{__prefix}/*/locale/no/LC_MESSAGES/popt.mo
%lang(pt)	%{__prefix}/*/locale/pt/LC_MESSAGES/popt.mo
%lang(ro)	%{__prefix}/*/locale/ro/LC_MESSAGES/popt.mo
%lang(ru)	%{__prefix}/*/locale/ru/LC_MESSAGES/popt.mo
%lang(sk)	%{__prefix}/*/locale/sk/LC_MESSAGES/popt.mo
%lang(sl)	%{__prefix}/*/locale/sl/LC_MESSAGES/popt.mo
%lang(sv)	%{__prefix}/*/locale/sv/LC_MESSAGES/popt.mo
%lang(tr)	%{__prefix}/*/locale/tr/LC_MESSAGES/popt.mo
%lang(uk)	%{__prefix}/*/locale/uk/LC_MESSAGES/popt.mo
%lang(wa)	%{__prefix}/*/locale/wa/LC_MESSAGES/popt.mo
%lang(zh_CN)	%{__prefix}/*/locale/zh_CN.GB2312/LC_MESSAGES/popt.mo

# XXX These may end up in popt-devel but it hardly seems worth the effort now.
%{__prefix}/lib/libpopt.a
%{__prefix}/lib/libpopt.la
%{__prefix}/lib/libpopt.so
%{__prefix}/include/popt.h

%changelog
* Thu Aug 23 2001 Jeff Johnson <jbj@redhat.com>
- workaround alpha sha1 digest miscompilation.

* Fri Aug 17 2001 Jeff Johnson <jbj@redhat.com>
- verify perms (but not mode) on %ghost files.

* Thu Aug 16 2001 Jeff Johnson <jbj@redhat.com>
- python: add exception to detect bad data in hdrUnload.
- change dir creation message from warning to debug for now.

* Wed Aug 15 2001 Jeff Johnson <jbj@redhat.com>
- always use dl size in regionSwab() return.
- ppc: revert ppcmac to ppc.
- ppc: autoconf test for va_copy.

* Mon Aug 13 2001 Jeff Johnson <jbj@redhat.com>
- fix: segfault on headerFree given malicious data.
- fix: don't verify hash page nelem.
- better error messages for verification failures.
- include directory /usr/lib/rpm in rpm package.

* Wed Aug  8 2001 Jeff Johnson <jbj@redhat.com>
- add legacy (compile only) wrappers for fdFileno et al.
- add -D_REENTRANT (note rpmlib is still not thread safe).

* Mon Aug  6 2001 Jeff Johnson <jbj@redhat.com>
- python: add hiesenbug patch.

* Sun Aug  5 2001 Jeff Johnson <jbj@redhat.com>
- portability: some compilers squawk at return ((void) foo()) (#50419).
- remove fdFileno() from librpmio, use inline version instead (#50420).
- fix: linux find-requires needs quotes around [:blank:].
- remove /var/lib/rpm/__db* cache files if %%__dbi_cdb is not configured.

* Sat Aug  4 2001 Jeff Johnson <jbj@redhat.com>
- fix: i18n tags not terminated correctly with NUL (#50304).
- add explicit casts to work around a s390 compiler problem.
- fix: autoconf glob tests (#50845).

* Tue Jul 31 2001 Jeff Johnson <jbj@redhat.com>
- detailed build package error messages.

* Mon Jul 30 2001 Tim Powers <timp@redhat.com>
- added all of the perl modules to the files list for the rpm-perl package.

* Sat Jul 28 2001 Jeff Johnson <jbj@redhat.com>
- add support for mips (#49283).
- add __as, _build_arch, and __cxx macros (#36662, #36663, #49280).

* Fri Jul 27 2001 Jeff Johnson <jbj@redhat.com>
- fix: --noscripts is another multimode option.
- add tmpdir to configure db3 tmpdir into chroot tree.
- permit lazy db opens within chroot.
- fix: diddle dbenv path to accomodate backing store reopen in chroot.

* Tue Jul 24 2001 Jeff Johnson <jbj@redhat.com>
- fix: don't segfault when presented with rpm-2.4.10 packaging (#49688).

* Mon Jul 23 2001 Jeff Johnson <jbj@redhat.com>
- add pmac/ppciseries/ppcpseries varieties to ppc arch family.
- include tdigest.c tkey.c and trpmio.c to "make dist".
- re-enable dependency resolution source from package NVR.
- rename pmac to ppcmac.
- ia64: revert -O0 compilation.
- upgrade to db-3.3.11 final.

* Sun Jul 22 2001 Jeff Johnson <jbj@redhat.com>
- use %%{rpm_version} to avoid other package versions.

* Sat Jul 21 2001 Jeff Johnson <jbj@redhat.com>
- add sha1 test vectors, verify on ix86/alpha/sparc.
- add rpm-perl subpackage from Perl-RPM.
- python: parameterize with PYVER to handle 1.5 and/or 2.1 builds.
- add build dependency on zlib-devel (#49575).

* Fri Jul 20 2001 Jeff Johnson <jbj@redhat.com>
- fix: yet another segfault from bad metadata prevented.

* Thu Jul 19 2001 Jeff Johnson <jbj@redhat.com>
- fix: 4 memory leaks eliminated.

* Wed Jul 18 2001 Jeff Johnson <jbj@redhat.com>
- fix: %%dev(...) needs to map rdev and mtime from metadata.
- resurrect --specedit for i18n.

* Tue Jul 17 2001 Jeff Johnson <jbj@redhat.com>
- python: "seal" immutable region for legacy headers in rhnUnload().
- python: add poptmodule.so bindings.

* Mon Jul 16 2001 Jeff Johnson <jbj@redhat.com>
- fix: don't total hard linked file size multiple times (#46286).
- add %dev(type,major,minor) directive to permit non-root dev build.
- fix: _smp_flags macro broken.
- python: bind rhnUnload differently.
- fix: rescusitate --querytags.
- fix: short aliases broken (#49213).

* Fri Jul 13 2001 Jeff Johnson <jbj@redhat.com>
- isolate cdb access configuration (experimental, use at your own risk).
- fix: hard fail on locked dbopen if CDB locking not in use.
- fix: dbconfig with mp_mmapsize=16Mb/mp_size=1Mb for
  "everything ENOSPC" failure check.

* Thu Jul 12 2001 Jeff Johnson <jbj@redhat.com>
- fix: scope multi-mode options like --nodeps correctly (#48825).

* Wed Jul 11 2001 Jeff Johnson <jbj@redhat.com>
- fix: adjust arg count for --POPTdesc/--POPTargs deletion.
- add linux per-platform macro %_smp_mflags <sopwith@redhat.com>.
- document more popt aliases for --help usage.
- remove --tarbuild from man page(s), use -t[abpcils] instead (#48666).
- add explicit version to Requires: popt

* Tue Jul 10 2001 Jeff Johnson <jbj@redhat.com>
- fix: -i CLI context broken Yet Again.
- fix: --rebuild broken.
- unlink all _db.nnn files before 1st db open.
- python bindings should not segfault when fed bad data.

* Mon Jul  9 2001 Jeff Johnson <jbj@redhat.com>
- package version now configureable, default v3.
- rename rpm libraries to have version with libtool -release.
- revert rpmqv.c change for now.

* Sun Jul  8 2001 Jeff Johnson <jbj@redhat.com>
- python: rhnLoad/rhnUnload to check header digest.

* Sat Jul  7 2001 Jeff Johnson <jbj@redhat.com>
- expose rpmShowProgress() and rpmVerifyDigest() in rpmcli.h.
- portability: avoid st_mtime, gendiff uses basename, etc (#47497).
- glibc-2.0.x has not __va_copy().
- popthelp.c: don't use stpcpy to avoid portability grief (#47500).
- permit alias/exec description/arg text to be set from popt config.
- use rpmqv.c, not rpm.c, as rpm's main() routine.

* Wed Jul  4 2001 Jeff Johnson <jbj@redhat.com>
- add removetid to header during --repackage.

* Tue Jul  3 2001 Jeff Johnson <jbj@redhat.com>
- fix: redundant entries in file manifests handled correctly (#46914).
- map uid/gid from metadata into payload headers.

* Sat Jun 30 2001 Jeff Johnson <jbj@redhat.com>
- update intl dirs to gettext-0.10.38.
- fix: sanity check for header size added in headerCopyLoad() (#46469).

* Thu Jun 28 2001 Jeff Johnson <jbj@redhat.com>
- fix: sanity checks on #tags (<65K) and offset (<16Mb) in header.
- fix: add -r to useradd to prevent /etc/skel glop (#46215).
- fix: disambiguate typedef and struct name(s) for kpackage.

* Mon Jun 25 2001 Jeff Johnson <jbj@redhat.com>
- fix: remove executable bit on perl.req, not find-req.pl.
- fix: permit partially enumerated hardlink file sets during build.
- fix: resurrect rpm signature modes.

* Fri Jun 22 2001 Jeff Johnson <jbj@redhat.com>
- remove executable bit to disable autogenerated perl requires until
  perl provides can be vetted.
- disable per-platform %%configure use of %%_gnu until libtool package
  stabilizes.

* Thu Jun 21 2001 Jeff Johnson <jbj@redhat.com>
- propagate %%{_gnu} to per-platform configuration.
- fix: parameterized macros with massive mumber of options need
  "optind = 1" "Dmitry V. Levin" <ldv@alt-linux.org>.
- add athlon per-platform configuration.

* Wed Jun 20 2001 Jeff Johnson <jbj@redhat.com>
- fix: partial sets of hardlinked files permitted in payload.
- fix: mark rpmdb files with %config to prevent erasure on downgrade.
- work around a (possible) compiler problem on ia64.
- fix: rpm -qlv link count for directories dinna include '..'.
- fix: rpm -qlv size for directories should be zero.
- add --noghost to filter non-payload files from rpm -qlv output.
- add %%{_gnu} macro to append "-gnu" to %%{_target_platform} to
  support --target/--host flavored %%configure. Legacy behavior
  available by undefining %%{_gnu}.

* Tue Jun 19 2001 Jeff Johnson <jbj@redhat.com>
- finalize per-header methods, accessing headerFoo through vector.
- make package ordering loop messages debug, not warning.

* Mon Jun 18 2001 Jeff Johnson <jbj@redhat.com>
- preliminary abstraction to support per-header methods.

* Sun Jun 17 2001 Jeff Johnson <jbj@redhat.com>
- alpha: don't add "()(64bit)" dependency markers.
- ia64/sparc: <arch>.{req,prov} identical to linux.{req,prov}.
- add "rpmlib(ScriptletInterpreterArgs)" to track
	%%post -p "/sbin/ldconfig -n /usr/lib"
  incompatibilities.
- linux.req needs exit 0
- popt: add POPT_ARGFLAG_SHOW_DEFAULT to display initial values (#32558).
- popt: add POPT_CONTEXT_ARG_OPTS for all opts to return 1 (#30912).
- fix: fsm reads/writes now return error on partial I/O.
- fix: Ferror returned spurious error for gzdio/bzdio.
- check for API/ABI creep, diddle up some compatibility.

* Thu Jun 14 2001 Jeff Johnson <jbj@redhat.com>
- fix: db1 end-of-file not detected in legacy compatible way.
- fix: remove (harmless) chown error message from %post.
- add --target/--host to %%configure, add example cross-build/config.site
  scripts to /usr/lib/rpm <arjanv@redhat.com> (#44581).
- rpmdb iterator selectors permit default/strcmp/regex/glob matching.
- rpmdb iterator selectors permit negative matches.

* Wed Jun 13 2001 Jeff Johnson <jbj@redhat.com>
- add rpmdbSetIteratorRE() for regex matching in database iterators.
- permit rpm -qa to take RE args applied to name tag.
- permit dbiFindMatches() to use version/release patterns.
- eliminate all uses of rpmdbSetIterator{Version,Release}.

* Tue Jun 12 2001 Jeff Johnson <jbj@redhat.com>
- remove rpmrc Provides: Yet Again, use virtual packages.
- dump cursor debugging wrappers.
- rpm --verify can disable rpmFileAttr checks.

* Mon Jun 11 2001 Jeff Johnson <jbj@redhat.com>
- remove dead code frpm popt table reorg.
- more CLI typedefs/prototypes moved from rpmlib.h to rpmcli.h.
- rpm --verify skips files in non-installed states.
- rpm --verify skips content checks for %ghost files.
- rpm --verify displays config/doc/gnost/license/readme atrrs for files.
- rpm --verify checks immutable header region digest if available.
- rpmbuild adds header region digest (SHA1 as string).
- use rpmTag* typedefs in new hge/hae/hme/hre header vectors.

* Fri Jun  8 2001 Jeff Johnson <jbj@redhat.com>
- fix: QUERY_FOR_LIST file count clobbered.
- create top level rpmcli API, factor top level modes into popt tables.
- popt: add POPT_BIT_SET/POPT_BIT_CLR to API.
- autogen.sh checks for latest libtool-1.4 and automake-1.4-p2.
- rpm --verify reports failure(s) if corresponding tag is not in header.
- rpm --verify honors %config(missingok), add -v for legacy behavior.

* Wed Jun  6 2001 Jeff Johnson <jbj@redhat.com>
- fix typos in linux.{req,prov}.
- always use db cursors.
- permit duplicates for btree indices.
- document build modes in rpmbuild.8, rpmbuild is born.
- default to dbenv with mpool, --rebuilddb with nofsync is much faster.

* Fri Jun  1 2001 Jeff Johnson <jbj@redhat.com>
- merge sparc64/ia64 fiddles back into linux.{req,prov}.
- automagically generate perl module dependencies always.
- fix: lclint fiddles broke uCache initialization (#43139).

* Thu May 31 2001 Jeff Johnson <jbj@redhat.com>
- return multiple suggested packages (Pawel Kolodziej <pawelk@pld.org.pl>).
- fix: return suggested packages when using Depends cache.

* Wed May 30 2001 Jeff Johnson <jbj@redhat.com>
- fix: for busted db1, attempt chain reconnection to following record.

* Tue May 29 2001 Jeff Johnson <jbj@redhat.com>
- eliminate db-1.85 and db-2.x configuration.
- fix: popt arg sanity checks broken, optarg != optArg.
- fix: popt range checks on floats/doubles broken.
- popt: return POPT_ERROR_ERRNO on config open/read/close failure.
- fix: popt exec doesn't add '--', --target et al no longer need '='.
- fix: popt consume-next-arg "!#:+" w/o side effect (#41956).

* Fri May 25 2001 Jeff Johnson <jbj@redhat.com>
- perform db->verify when closing db files.

* Wed May 23 2001 Jeff Johnson <jbj@redhat.com>
- headerFree() returns NULL, _free is C++ safe.
- remove all header region assertion failures, return NULL instead.

* Mon May 21 2001 Jeff Johnson <jbj@redhat.com>
- fix: skip %ghost files when building packages (#38218).
- refuse to install on systems using db1.

* Sun May 20 2001 Jeff Johnson <jbj@redhat.com>
- fix: i18n strings need 1 on sucess return code (#41313).

* Wed May 16 2001 Jeff Johnson <jbj@redhat.com>
- fix: filter duplicate package removals (#35828).
- add armv3l arch.

* Mon May 14 2001 Jeff Johnson <jbj@redhat.com>
- upgrade to db-3.3.4.

* Sun May 13 2001 Jeff Johnson <jbj@redhat.com>
- add cron/logrotate scripts to save installed package filenames.

* Thu May 10 2001 Jeff Johnson <jbj@redhat.com>
- rpm database has rpm.rpm g+w permissions to share db3 mutexes.
- expose more db3 macro configuration tokens.
- move fprint.[ch] and hash.[ch] to rpmdb directory.
- detect and fiddle incompatible mixtures of db3 env/open flags.
- add DBI_WRITECURSOR to map to db3 flags with CDB database model.
- add rpmdbSetIteratorRewrite to warn of pending lazy (re-)writes.
- harden rpmdb iterators from damaged header instance segfaults.

* Mon May  7 2001 Jeff Johnson <jbj@redhat.com>
- use internal db-3.2.9 sources to build by default.
- don't build db1 support by default.
- create rpmdb.la so that linkage against rpm's db-3.2.9 is possible.

* Sun May  6 2001 Jeff Johnson <jbj@redhat.com>
- fix: specfile queries with BuildArch: (#27589).

* Sat May  5 2001 Jeff Johnson <jbj@redhat.com>
- enough lclint annotations and fiddles already.

* Thu May  3 2001 Jeff Johnson <jbj@redhat.com>
- still more boring lclint annotations and fiddles.

* Sun Apr 29 2001 Jeff Johnson <jbj@redhat.com>
- transaction iterator(s) need to run in reverse order on pure erasures.
- erasures not yet strict, warn & chug on unlink(2)/rmdir(2) failure.
- more boring lclint annotations and fiddles.

* Sat Apr 28 2001 Jeff Johnson <jbj@redhat.com>
- globalize _free(3) wrapper in rpmlib.h, consistent usage throughout.
- internalize locale insensitive ctype(3) in rpmio.h
- boring lclint annotations and fiddles.

* Thu Apr 26 2001 Jeff Johnson <jbj@redhat.com>
- fix: ineeded count wrong for overlapped, created files.

* Wed Apr 25 2001 Jeff Johnson <jbj@redhat.com>
- fix: readlink return value clobbered by header write.

* Mon Apr 23 2001 Jeff Johnson <jbj@redhat.com>
- regenerate rpm.8 man page from docbook glop (in max-rpm).
- lib/depends.c: diddle debugging messages.

* Sat Apr 21 2001 Jeff Johnson <jbj@redhat.com>
- fix: s390 (and ppc?) could return CPIOERR_BAD_HEADER (#28645).
- fix: Fwrite's are optimized out by aggressive compiler(irix) (#34711).
- portability: vsnprintf/snprintf wrappers for those without (#34657).
- more info provided by rpmdepOrder() debugging messages.
- merge (compatible) changes from top-of-stack into rpmlib.h.
- cpio mappings carry dirname/basename, not absolute path.
- fix: check waitpid return code.
- remove support for v1 src rpm's.
- re-position callbacks with ts/fi in cpio payload layer.
- state machines for packages (psm.c) and payloads (fsm.c)
- add --repackage option to put erased bits back into a package.

* Tue Apr 17 2001 Jeff Johnson <jbj@redhat.com>
- fix: s390 (and ppc?) could return CPIOERR_BAD_HEADER (#28645).
- fix: Fwrite's are optimized out by aggressive compiler(irix) (#34711).
- portability: vsnprintf/snprintf wrappers for those without (#34657).
- don't build with db1 support, don't install with packages.rpm present.

* Wed Apr  4 2001 Jeff Johnson <jbj@redhat.com>
- fix: parameterized macro segfault (Jakub Bogusz <qboosh@pld.org.pl>)
- fix: i18n tags in rpm-2.5.x had wrong offset/length (#33478).
- fix: AIX has sizeof(uint_16) != sizeof(mode_t) verify cast needed.
- fix: zero length hard links unpacked incorrectly (#34211).
- fix: --relocate missing trailing slash (#28874,#25876).
- fix: --excludedoc shouldn't create empty doc dir (#14531).
- fix: %_netsharedpath needs to look at basenames (#26561).
- fix: --excludepath was broken (#24434).

* Thu Mar 22 2001 Jeff Johnson <jbj@redhat.com>
- update per-interpreter dependency scripts, add sql/tcl (#20295).
- fix: rpmvercmp("1.a", "1.") returned -1, not +1 (#21392).
- add %exclude support (i.e. "everything but") to %files.
	(Michael (Micksa) Slade" <micksa@knobbits.org>)
- add --with/--without popt glue for conditional builds(Tomasz Kloczko).
- python: strip header regions during unload.
- add -g to optflags in per-platform config.
- permit confgure/compile with db3-3.2.9.
- permit manifest files as args to query/verify modes.

* Thu Mar 15 2001 Jeff Johnson <jbj@redhat.com>
- start rpm-4.0.3.
- add cpuid asm voodoo to detect athlon processors.
