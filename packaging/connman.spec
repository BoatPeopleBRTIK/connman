%global _hardened_build 1

Name: connman
Version: 1.33
Release: 20%{?dist}
Summary: An alternative daemon for managing internet connections on Linux

Group: System Environment/Base
License: GPLv2
URL: http://connman.net/
Source0: http://www.kernel.org/pub/linux/network/%{name}/%{name}-%{version}.tar.xz

BuildRequires: systemd-units
BuildRequires: glib2-devel >= 2.28
BuildRequires: dbus-devel >= 1.4
BuildRequires: iptables-devel
BuildRequires: gnutls-devel
BuildRequires: readline-devel
# connman searches for wpa_supplicant during build
BuildRequires: wpa_supplicant >= 1.0
Requires: wpa_supplicant >= 1.0
Requires(post): systemd-units
Requires(preun): systemd-units
Requires(postun): systemd-units

%description
The ConnMan project provides a daemon for managing internet connections within
embedded devices running the Linux operating system. The Connection Manager is
designed to be slim and to use as few resources as possible, so it can be
easily integrated.

%package devel
Summary: Libraries and headers for connman
Group: Development/Libraries
Requires: %{name}%{?_isa} = %{version}-%{release}

%description devel
This package contains development files for connman.

%prep
%setup -q

%build
./bootstrap
%configure
make %{?_smp_mflags}

%install
make install DESTDIR=%{buildroot}

%post
%systemd_post connman.service

%preun
%systemd_preun connman.service

%postun
%systemd_postun_with_restart connman.service 

%files
%doc README COPYING
%config(noreplace) /etc/dbus-1/system.d/connman.conf
%{_bindir}/connmanctl
%{_mandir}/man1/connmanctl.1.gz
%{_mandir}/man5/connman-service.config.5.gz
%{_mandir}/man5/connman-vpn-provider.config.5.gz
%{_mandir}/man5/connman-vpn.conf.5.gz
%{_mandir}/man5/connman.conf.5.gz
%{_mandir}/man8/connman-vpn.8.gz
%{_mandir}/man8/connman.8.gz
%{_sbindir}/connmand
%{_sbindir}/connmand-wait-online
%{_tmpfilesdir}/connman_resolvconf.conf
%{_unitdir}/connman-wait-online.service
%{_unitdir}/connman.service

%files devel
%{_includedir}/connman/
%{_libdir}/pkgconfig/connman.pc

%changelog
* Wed Aug 10 2016 Pavel Šimerda <psimerda@redhat.com> - 1.33-2
- Resolves: #1179280 – use system-wide crypto-policies

* Mon Aug 08 2016 Pavel Šimerda <psimerda@redhat.com> - 1.33-1
- New version 1.33

* Mon May 09 2016 Pavel Šimerda <psimerda@redhat.com> - 1.32-1
- New version 1.32

* Wed Feb 03 2016 Fedora Release Engineering <releng@fedoraproject.org> - 1.31-2
- Rebuilt for https://fedoraproject.org/wiki/Fedora_24_Mass_Rebuild

* Mon Jan 04 2016 Pavel Šimerda <psimerda@redhat.com> - 1.31-1
- new version 1.31

* Wed Sep 02 2015 Pavel Šimerda <psimerda@redhat.com> - 1.30-1
- new version 1.30

* Wed Jun 17 2015 Fedora Release Engineering <rel-eng@lists.fedoraproject.org> - 1.29-2
- Rebuilt for https://fedoraproject.org/wiki/Fedora_23_Mass_Rebuild

* Tue Jun 09 2015 Pavel Šimerda <psimerda@redhat.com> - 1.29-1
- new version 1.29

* Fri Mar 13 2015 Pavel Šimerda <psimerda@redhat.com> - 1.28-1
- new version 1.28

* Mon Sep 08 2014 Pavel Šimerda <psimerda@redhat.com> - 1.25-1
- new version 1.25

* Sat Aug 16 2014 Fedora Release Engineering <rel-eng@lists.fedoraproject.org> - 1.24-2
- Rebuilt for https://fedoraproject.org/wiki/Fedora_21_22_Mass_Rebuild

* Thu Jun 12 2014 Pavel Šimerda <psimerda@redhat.com> - 1.24-1
- new version 1.24

* Sat Jun 07 2014 Fedora Release Engineering <rel-eng@lists.fedoraproject.org> - 1.23-2
- Rebuilt for https://fedoraproject.org/wiki/Fedora_21_Mass_Rebuild

* Mon Apr 07 2014 Pavel Šimerda <psimerda@redhat.com> - 1.23-1
- new version 1.23

* Mon Mar 17 2014 Pavel Šimerda <psimerda@redhat.com> - 1.21-1
- new package version 1.21

* Mon Feb 17 2014 Pavel Šimerda <psimerda@redhat.com> - 1.13-4
- rebuilt

* Mon Feb 17 2014 Pavel Šimerda <psimerda@redhat.com> - 1.13-3
- #955321 - hardened build

* Sat Aug 03 2013 Fedora Release Engineering <rel-eng@lists.fedoraproject.org> - 1.13-2
- Rebuilt for https://fedoraproject.org/wiki/Fedora_20_Mass_Rebuild

* Tue Apr 23 2013 Tom Callaway <spot@fedoraproject.org> - 1.13-1
- update to 1.13

* Sun Mar 31 2013 Pavel Šimerda <psimerda@redhat.com> - 1.12-2
- added readline-devel to requires

* Sun Mar 24 2013 Peter Robinson <pbrobinson@fedoraproject.org> 1.12-1
- update to 1.12

* Wed Feb 13 2013 Fedora Release Engineering <rel-eng@lists.fedoraproject.org> - 1.6-2
- Rebuilt for https://fedoraproject.org/wiki/Fedora_19_Mass_Rebuild

* Sun Aug 26 2012 Pavel Šimerda <psimerda@redhat.com> - 1.6-1
- update to 1.6

* Sun Aug 26 2012 Pavel Šimerda <psimerda@redhat.com> - 1.5-4
- add wpa_supplicant build dependency

* Sat Aug 18 2012 Pavel Šimerda <psimerda@redhat.com> - 1.5-3
- include COPYING
- make -devel depend on main package

* Sat Aug 18 2012 Pavel Šimerda <psimerda@redhat.com> - 1.5-2
- fix scriptlets
- own /usr/include/connman

* Thu Aug 16 2012 Pavel Šimerda <psimerda@redhat.com> - 1.5-1
- initial build
