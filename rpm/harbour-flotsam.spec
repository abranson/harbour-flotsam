Name:       harbour-flotsam
Summary:    Flotsam connectivity and synchronization service
Version:    0.1.0
Release:    1
Group:      Qt/Qt
License:    BSD-3-Clause
URL:        https://github.com/harbour-flotsam/harbour-flotsam
Source0:    %{name}-%{version}.tar.bz2

Requires:   sailfishsilica-qt5
Requires:   qt5-qtdeclarative-qtquick
Requires:   connman-qt5
Requires:   sailfish-components-accounts-qt5
Requires:   qr-filter-qml-plugin
Requires:   sailjail
Requires:   sailjail-permissions
BuildRequires: pkgconfig(Qt5Core)
BuildRequires: pkgconfig(Qt5Qml)
BuildRequires: pkgconfig(Qt5Quick)
BuildRequires: pkgconfig(Qt5DBus)
BuildRequires: pkgconfig(Qt5Network)
BuildRequires: pkgconfig(sailfishapp)
BuildRequires: pkgconfig(accounts-qt5)
BuildRequires: pkgconfig(connman-qt5)
BuildRequires: pkgconfig(sailfishaccounts)
BuildRequires: desktop-file-utils

%description
Flotsam is a Sailfish OS application with a user-session synchronization
daemon and a privileged ConnMan integration helper.

%prep
%setup -q

%build
%qmake5
%make_build

%install
rm -rf %{buildroot}
%qmake5_install

chmod 0755 %{buildroot}%{_datadir}/harbour-flotsam/qml
chmod 0755 %{buildroot}%{_datadir}/harbour-flotsam/qml/pages
install -d -m 0755 %{buildroot}%{_datadir}/licenses/%{name}
install -m 0644 LICENSE %{buildroot}%{_datadir}/licenses/%{name}/LICENSE

install -D -m 0644 data/harbour-flotsam.desktop \
    %{buildroot}%{_datadir}/applications/harbour-flotsam.desktop
install -D -m 0644 data/Flotsam.permission \
    %{buildroot}%{_sysconfdir}/sailjail/permissions/Flotsam.permission
install -D -m 0644 data/icons/harbour-flotsam.svg \
    %{buildroot}%{_datadir}/icons/hicolor/scalable/apps/harbour-flotsam.svg

install -D -m 0644 data/systemd/user/harbour-flotsam-syncd.service \
    %{buildroot}%{_userunitdir}/harbour-flotsam-syncd.service
install -d %{buildroot}%{_userunitdir}/user-session.target.wants
ln -sf ../harbour-flotsam-syncd.service \
    %{buildroot}%{_userunitdir}/user-session.target.wants/harbour-flotsam-syncd.service

install -D -m 0644 data/systemd/system/harbour-flotsam-connman-helper.service \
    %{buildroot}%{_unitdir}/harbour-flotsam-connman-helper.service
install -D -m 0644 data/dbus-1/services/org.harbour.flotsam.Sync.service \
    %{buildroot}%{_datadir}/dbus-1/services/org.harbour.flotsam.Sync.service
install -D -m 0644 data/dbus-1/system-services/org.harbour.flotsam.Connman.service \
    %{buildroot}%{_datadir}/dbus-1/system-services/org.harbour.flotsam.Connman.service
install -D -m 0644 data/dbus-1/system.d/org.harbour.flotsam.Connman.conf \
    %{buildroot}%{_sysconfdir}/dbus-1/system.d/org.harbour.flotsam.Connman.conf
install -D -m 0644 data/dbus-1/interfaces/org.harbour.flotsam.Sync.xml \
    %{buildroot}%{_datadir}/dbus-1/interfaces/org.harbour.flotsam.Sync.xml
install -D -m 0644 data/dbus-1/interfaces/org.harbour.flotsam.Connman.xml \
    %{buildroot}%{_datadir}/dbus-1/interfaces/org.harbour.flotsam.Connman.xml

%post
systemctl-user daemon-reload >/dev/null 2>&1 || :
systemctl-user restart harbour-flotsam-syncd.service >/dev/null 2>&1 || :
systemctl daemon-reload >/dev/null 2>&1 || :

%postun
if [ "$1" -eq 0 ]; then
    systemctl-user stop harbour-flotsam-syncd.service >/dev/null 2>&1 || :
    systemctl stop harbour-flotsam-connman-helper.service >/dev/null 2>&1 || :
fi
systemctl-user daemon-reload >/dev/null 2>&1 || :
systemctl daemon-reload >/dev/null 2>&1 || :

%files
%defattr(-,root,root,-)
%dir %attr(0755,root,root) %{_datadir}/licenses/%{name}
%license %{_datadir}/licenses/%{name}/LICENSE
%{_bindir}/harbour-flotsam
%attr(2755,root,privileged) %{_libexecdir}/harbour-flotsam-syncd
%{_libexecdir}/harbour-flotsam-connman-helper
%{_datadir}/harbour-flotsam/qml
%{_datadir}/applications/harbour-flotsam.desktop
%config %{_sysconfdir}/sailjail/permissions/Flotsam.permission
%{_datadir}/icons/hicolor/scalable/apps/harbour-flotsam.svg
%{_userunitdir}/harbour-flotsam-syncd.service
%{_userunitdir}/user-session.target.wants/harbour-flotsam-syncd.service
%{_unitdir}/harbour-flotsam-connman-helper.service
%{_datadir}/dbus-1/services/org.harbour.flotsam.Sync.service
%{_datadir}/dbus-1/system-services/org.harbour.flotsam.Connman.service
%config %{_sysconfdir}/dbus-1/system.d/org.harbour.flotsam.Connman.conf
%{_datadir}/dbus-1/interfaces/org.harbour.flotsam.Sync.xml
%{_datadir}/dbus-1/interfaces/org.harbour.flotsam.Connman.xml

%changelog
* Thu Sep 03 2026 Jolla Mobile Ltd <info@jolla.com> - 0.1.0-1
- Initial Flotsam system package.
