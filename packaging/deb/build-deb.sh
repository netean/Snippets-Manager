#!/usr/bin/env bash
#
# Builds the .deb inside a Debian or Ubuntu system (build-packages.sh runs it
# in an Ubuntu 24.04 container, so the package installs on 24.04 and newer).
#
# Usage: build-deb.sh <source tarball> <LICENSE file> <version> <output folder>

set -euo pipefail
tarball="$1"; license="$2"; version="$3"; out="$4"
package=snippets-manager
work="$(mktemp -d)"

tar -xzf "$tarball" -C "$work"
src="$(find "$work" -mindepth 1 -maxdepth 1 -type d | head -n1)"
cmake -S "$src" -B "$work/build" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build "$work/build" --parallel "$(nproc)"
ctest --test-dir "$work/build" --output-on-failure

root="$work/root"
DESTDIR="$root" cmake --install "$work/build"
strip --strip-unneeded "$root/usr/bin/snippetmanager"

# Licence, in the form Debian expects
doc="$root/usr/share/doc/$package"
mkdir -p "$doc"
cat > "$doc/copyright" <<COPYRIGHT
Format: https://www.debian.org/doc/packaging-manuals/copyright-format/1.0/
Upstream-Name: Snippet Manager
Source: https://github.com/netean/Snippets-Manager

Files: *
Copyright: Iain (https://github.com/netean)
License: GPL-3.0+
 This program is free software: you can redistribute it and/or modify it
 under the terms of the GNU General Public License as published by the Free
 Software Foundation, either version 3 of the License, or (at your option)
 any later version.
 .
 On Debian systems, the full text of the GNU General Public License version 3
 can be found in /usr/share/common-licenses/GPL-3.
COPYRIGHT
# (Debian's copy only differs in using https:// for two web addresses)
cmp -s <(sed 's|https://|http://|g' "$license") <(sed 's|https://|http://|g' /usr/share/common-licenses/GPL-3) \
    || echo "Note: LICENSE differs from Debian's GPL-3 text" >&2

# Library dependencies, worked out by Debian's own tool. It needs a control file to run.
mkdir -p "$work/debian"
printf 'Source: %s\n\nPackage: %s\nArchitecture: any\n' "$package" "$package" > "$work/debian/control"
shlibs="$(cd "$work" && dpkg-shlibdeps -O "$root/usr/bin/snippetmanager" 2>/dev/null | sed -n 's/^shlibs:Depends=//p')"

mkdir -p "$root/DEBIAN"
cat > "$root/DEBIAN/control" <<CONTROL
Package: $package
Version: $version-1
Architecture: $(dpkg --print-architecture)
Maintainer: Iain <850557+netean@users.noreply.github.com>
Installed-Size: $(du -sk "$root/usr" | cut -f1)
Depends: $shlibs, libqt6sql6-sqlite, qt6-qpa-plugins, hicolor-icon-theme
Recommends: qt6-wayland
Section: utils
Priority: optional
Homepage: https://github.com/netean/Snippets-Manager
Description: Simple manager for text and code snippets
 Snippet Manager stores text snippets, code snippets and short phrases, and
 copies them to the clipboard. It has folders and tags, plain text, rich text
 and syntax-highlighted code, ranked search, a trash, import and export
 (JSON, Markdown, CSV), placeholders such as {{date}}, and a quick search
 popup that can be opened from the tray or a global keyboard shortcut.
CONTROL

mkdir -p "$out"
dpkg-deb --root-owner-group --build "$root" "$out/${package}_${version}-1_$(dpkg --print-architecture).deb"
rm -rf "$work"
