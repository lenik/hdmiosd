# Version is injected by packaging/rpm/Makefile via `zfr version`.
# RPM Version cannot contain '-'; use `zfr version -r` (hyphens → '_').
# srcversion is the unsanitized Meson/git version and names the tarball.
%{!?version:%global version 0.0.0}
%{!?srcversion:%global srcversion %{version}}

Name:           hdmiosd
Version:        %{version}
Release:        1%{?dist}
Summary:        On-screen DDC/CI controller for HDMI/DP displays

License:        AGPL-3.0-or-later
URL:            https://github.com/lenik/hdmiosd
Packager:       Lenik <hdmiosd@bodz.net>
Source0:        %{name}-%{srcversion}.tar.xz

BuildRequires:  meson
BuildRequires:  ninja-build
BuildRequires:  pkgconf
BuildRequires:  asciidoctor
BuildRequires:  zip
BuildRequires:  binutils
BuildRequires:  pkgconfig(bas-cpp)
BuildRequires:  pkgconfig(bas-ui)
BuildRequires:  wxGTK3-devel
BuildRequires:  json-devel
BuildRequires:  gettext

Requires:       wxGTK3

%description
hdmiosd discovers DDC/CI-capable monitors over Linux I2C and provides a
wxWidgets canvas OSD (Device OSD + App OSD). Settings are stored in
~/.config/hdmiosd/settings.ini. Tabler icons are embedded via bas-cpp
assets.

%prep
%setup -q -n %{name}-%{srcversion}

%build
meson setup build \
    --prefix=%{_prefix} \
    --bindir=%{_bindir} \
    --datadir=%{_datadir} \
    --mandir=%{_mandir} \
    --sysconfdir=%{_sysconfdir} \
    --localstatedir=%{_localstatedir} \
    --buildtype=plain
meson compile -C build

%install
meson install -C build --destdir=%{buildroot}

%files
%{_bindir}/hdmiosd
%{_datadir}/bash-completion/completions/hdmiosd
%{_mandir}/man1/hdmiosd.1*
%{_mandir}/*/man1/hdmiosd.1*
%{_datadir}/doc/%{name}/
%{_datadir}/locale/*/LC_MESSAGES/hdmiosd.mo

%changelog
* Thu Aug 20 2026 Lenik <hdmiosd@bodz.net>
- Align spec with debian/control (Meson, AGPL-3.0-or-later).
- Version comes from `zfr version`, the same method meson.build uses.
