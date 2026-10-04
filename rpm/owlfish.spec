# SPDX-FileCopyrightText: 2026 ccontino84
# SPDX-License-Identifier: LGPL-2.1-only

Name:       owlfish
Version:    1.3.1
Release:    1
Summary:    Warm tint and extra dimming for the display
License:    LGPL-2.1-only
URL:        https://github.com/ccontino84/owlfish
Source0:    %{name}-%{version}.tar.bz2
BuildRequires:  pkgconfig(Qt5Core)
BuildRequires:  pkgconfig(Qt5DBus)
BuildRequires:  pkgconfig(Qt5Gui)
BuildRequires:  pkgconfig(Qt5Quick)
BuildRequires:  pkgconfig(Qt5Sensors)
BuildRequires:  pkgconfig(mlite5)
Requires:   lipstick-qt5
Requires:   qt5-qtsensors-plugin-sensorfw
# Settings page
Requires:   jolla-settings
Requires:   nemo-qml-plugin-dbus-qt5
Requires:   nemo-qml-plugin-configuration-qt5
Requires:   qt5-qtdeclarative-import-sensors

%description
Adds a colour filter on top of the home screen compositor (lipstick) that
tints the display warmer and can dim it further than the backlight can.
Loaded into lipstick as a Qt generic plugin; no system files are modified.

Controlled from Settings > Owlfish and a top menu shortcut, or dconf
under /apps/owlfish/ (see README.md).

%if 0%{?_chum}
Title: Owlfish
Type: addon
DeveloperName: ccontino84
Categories:
 - Utility
Custom:
  Repo: https://github.com/ccontino84/owlfish
PackageIcon: https://raw.githubusercontent.com/ccontino84/owlfish/main/icons/icon-m-owlfish.svg
Links:
  Homepage: https://github.com/ccontino84/owlfish
  Bugtracker: https://github.com/ccontino84/owlfish/issues
%endif

%prep
%autosetup -n %{name}-%{version}

%build
%qmake5 CONFIG+=notests OWLFISH_VERSION=%{version}
make %{?_smp_mflags}

%install
%qmake5_install
# Written by %post, unless the device already sets QT_QPA_GENERIC_PLUGINS
mkdir -p %{buildroot}%{_sharedstatedir}/environment/compositor
touch %{buildroot}%{_sharedstatedir}/environment/compositor/00-owlfish.conf

%post
# On every install and upgrade; never fails the installation
if /usr/libexec/owlfish/update-env; then
    echo "owlfish: restart the home screen to load it: systemctl --user restart lipstick"
fi
exit 0

%postun
if [ $1 -eq 0 ]; then
    echo "owlfish: restart the home screen to unload it: systemctl --user restart lipstick"
fi

%files
%license LICENSE
%{_libdir}/qt5/plugins/generic/libowlfish.so
%dir /usr/libexec/owlfish
%attr(755,root,root) /usr/libexec/owlfish/update-env
%ghost %attr(644,root,root) %{_sharedstatedir}/environment/compositor/00-owlfish.conf
%{_datadir}/jolla-settings/entries/owlfish.json
%{_datadir}/jolla-settings/pages/owlfish
%{_datadir}/themes/sailfish-default/silica/*/icons-monochrome/icon-m-owlfish.png
