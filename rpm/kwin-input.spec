Name:           kwin-input
Version:        1.0.0
Release:        1%{?dist}
Summary:        Send input and take screenshots on KDE Plasma Wayland
License:        GPL-3.0-or-later AND LGPL-2.1-or-later
URL:            https://github.com/SamuelCGilbert/kwin-input
# A tarball of this source tree, made by .github/workflows/release.yml
Source0:        kwin-input.tar.gz

BuildRequires:  gcc make pkgconf-pkg-config
BuildRequires:  wayland-devel libxkbcommon-devel glib2-devel

%description
Sends keyboard and mouse input and takes screenshots on KDE Plasma (Wayland)
from the command line or another program, without root or uinput. KWin trusts
it through the installed .desktop file, so any program that can run kwin-input
can see the screen and type.

%prep
%setup -q

%build
%make_build CFLAGS="%{optflags}"

%install
%make_install PREFIX=%{_prefix} APPDIR=%{_datadir}/applications

%files
%license LICENSE
%doc README.md
%{_bindir}/kwin-input
%{_datadir}/applications/kwin-input.desktop

%changelog
* Tue Nov 17 2026 Sam Gilbert <86975269+SamuelCGilbert@users.noreply.github.com> - 1.0.0-1
- First package
