Name:           snippets-manager
Version:        2.0
Release:        1%{?dist}
Summary:        Simple manager for text and code snippets

License:        GPL-3.0-or-later
URL:            https://github.com/netean/Snippets-Manager
Source0:        %{url}/archive/refs/tags/%{version}/Snippets-Manager-%{version}.tar.gz
Source1:        https://raw.githubusercontent.com/netean/Snippets-Manager/fd589bc60b3a66f1a62f5af43a40d92092d082c2/LICENSE

BuildRequires:  cmake
BuildRequires:  gcc-c++
BuildRequires:  qt6-qtbase-devel
BuildRequires:  kf6-syntax-highlighting-devel
BuildRequires:  desktop-file-utils
Requires:       hicolor-icon-theme

%description
Snippet Manager stores text snippets, code snippets and short phrases, and
copies them to the clipboard. It has folders and tags, plain text, rich text
and syntax-highlighted code, ranked search, a trash, import and export (JSON,
Markdown, CSV), placeholders such as {{date}}, and a quick search popup that
can be opened from the tray or a global keyboard shortcut.

%prep
%autosetup -n Snippets-Manager-%{version}
cp %{SOURCE1} LICENSE

%build
%cmake
%cmake_build

%check
%ctest

%install
%cmake_install
desktop-file-validate %{buildroot}%{_datadir}/applications/io.github.netean.SnippetManager.desktop

%files
%license LICENSE
%doc README.md CHANGELOG.md
%{_bindir}/snippetmanager
%{_datadir}/applications/io.github.netean.SnippetManager.desktop
%{_datadir}/icons/hicolor/*/apps/snippetmanager.*

%changelog
* Sun Nov 22 2026 Iain <850557+netean@users.noreply.github.com> - 2.0-1
- Snippet Manager 2.0
