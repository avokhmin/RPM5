Summary:   Triggers on P functional tests.
Name:      triggers-P
Version:   1.0
Release:   1
License:   Public Domain
Group:     Development/Tools
URL:       http://rpm5.org/
Prefix:    /tmp

%description			# <== NUKE

%package a
BuildArch: noarch
Provides: A = 1.0
Provides: AA = 2.0

%package b
Provides: B = 1.0
Provides: BB = 2.0

%install
mkdir -p %{buildroot}/tmp/%{name}-a
echo "a" > %{buildroot}/tmp/%{name}-a/a
mkdir -p %{buildroot}/tmp/%{name}-b
echo "b" > %{buildroot}/tmp/%{name}-b/b

%triggerprein  a -- B <= 1.0

%triggerin     a -- B <= 1.0

%triggerun     a -- B <= 1.0

%triggerpostun a -- B <= 1.0

%triggerprein  b -- A <= 1.0

%triggerin     b -- A <= 1.0

%triggerun     b -- A <= 1.0

%triggerpostun b -- A <= 1.0

%triggerprein  a -- !B <= 1.0

%triggerin     a -- !B <= 1.0

%triggerun     a -- !B <= 1.0

%triggerpostun a -- !B <= 1.0

%triggerprein  b -- !A <= 1.0

%triggerin     b -- !A <= 1.0

%triggerun     b -- !A <= 1.0

%triggerpostun b -- !A <= 1.0

%files a
%dir /tmp/%{name}-a
/tmp/%{name}-a/a

%files b
%dir /tmp/%{name}-b
/tmp/%{name}-b/b

%changelog
* Sat Sep 20 2008 Jeff Johnson <jbj@rpm5.org> - 1.0-1
- Triggers on P functional tests.
