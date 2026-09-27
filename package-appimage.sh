#!/usr/bin/env bash
#
# Build a self-contained, ready-to-run AppImage of Snippet Manager.
#
# The AppImage bundles Qt and the SQLite driver, so it runs on other Linux
# machines without installing anything. It needs a glibc at least as new as
# the one on the machine that built it, so build on the oldest distro you
# want to support.
#
# Usage: ./package-appimage.sh
# Output: SnippetManager-<version>-x86_64.AppImage in the project directory

set -euo pipefail

SOURCE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SOURCE_DIR/build-appimage"
APPDIR="$BUILD_DIR/AppDir"
TOOLS_DIR="$BUILD_DIR/tools"
ARCH="$(uname -m)"

info() { printf '\033[1;34m==>\033[0m %s\n' "$*"; }
die()  { printf '\033[1;31mError:\033[0m %s\n' "$*" >&2; exit 1; }

[[ "$(uname -s)" == "Linux" ]] || die "AppImages can only be built on Linux."
[[ "$ARCH" == "x86_64" ]] || die "This script currently supports x86_64 only."
command -v cmake >/dev/null || die "cmake not found. Run ./install.sh --install-deps first."
command -v curl >/dev/null || die "curl not found."

# linuxdeploy's Qt plugin finds Qt through qmake
if [[ -z "${QMAKE:-}" ]]; then
    for candidate in qmake6 /usr/lib/qt6/bin/qmake /usr/lib64/qt6/bin/qmake qmake; do
        if command -v "$candidate" >/dev/null && "$candidate" -query QT_VERSION 2>/dev/null | grep -q '^6\.'; then
            QMAKE="$(command -v "$candidate")"
            break
        fi
    done
fi
[[ -n "${QMAKE:-}" ]] || die "Couldn't find qmake for Qt 6. Install the Qt 6 development tools (e.g. qt6-base-dev-tools) or set QMAKE."
export QMAKE

# The version comes from project(... VERSION x.y) in CMakeLists.txt
VERSION="$(sed -n 's/^project(.*VERSION \([0-9][0-9.]*\).*/\1/p' "$SOURCE_DIR/CMakeLists.txt")"
[[ -n "$VERSION" ]] || die "Couldn't read the version from CMakeLists.txt"
OUTPUT="$SOURCE_DIR/SnippetManager-$VERSION-$ARCH.AppImage"

# ---------------------------------------------------------------------------
# Tools
# ---------------------------------------------------------------------------
mkdir -p "$TOOLS_DIR"
download_tool() {
    local file="$1" url="$2"
    if [[ ! -x "$TOOLS_DIR/$file" ]]; then
        info "Downloading $file"
        curl -fsSL -o "$TOOLS_DIR/$file" "$url" || die "Failed to download $url"
        chmod +x "$TOOLS_DIR/$file"
    fi
}
download_tool "linuxdeploy-$ARCH.AppImage" \
    "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-$ARCH.AppImage"
download_tool "linuxdeploy-plugin-qt-$ARCH.AppImage" \
    "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-$ARCH.AppImage"

# The tools are AppImages themselves; this lets them run without FUSE (e.g. in containers)
export APPIMAGE_EXTRACT_AND_RUN=1

# ---------------------------------------------------------------------------
# Build and stage into the AppDir
# ---------------------------------------------------------------------------
info "Building Snippet Manager $VERSION"
cmake -S "$SOURCE_DIR" -B "$BUILD_DIR/build" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr >/dev/null
cmake --build "$BUILD_DIR/build" --parallel "$(nproc 2>/dev/null || echo 2)"

rm -rf "$APPDIR"
DESTDIR="$APPDIR" cmake --install "$BUILD_DIR/build" >/dev/null

# Include native Wayland support when the Qt Wayland plugin is available
# (otherwise the app runs through XWayland on Wayland desktops)
plugin_dir="$("$QMAKE" -query QT_INSTALL_PLUGINS)"
if [[ -e "$plugin_dir/platforms/libqwayland-generic.so" ]]; then
    export EXTRA_PLATFORM_PLUGINS="libqwayland-generic.so"
fi
# Qt only loads the SQLite driver at runtime, so ask for it explicitly
export EXTRA_QT_MODULES="sql"

# ---------------------------------------------------------------------------
# Bundle Qt and create the AppImage
# ---------------------------------------------------------------------------
info "Bundling Qt and creating the AppImage"
rm -f "$SOURCE_DIR"/SnippetManager-*-"$ARCH".AppImage
(
    cd "$BUILD_DIR"
    # LINUXDEPLOY_OUTPUT_VERSION is embedded in the AppImage's desktop file (X-AppImage-Version)
    LDAI_OUTPUT="$OUTPUT" LINUXDEPLOY_OUTPUT_VERSION="$VERSION" "$TOOLS_DIR/linuxdeploy-$ARCH.AppImage" \
        --appdir "$APPDIR" \
        --desktop-file "$APPDIR/usr/share/applications/snippetmanager.desktop" \
        --icon-file "$SOURCE_DIR/resources/icon.png" \
        --icon-filename snippetmanager \
        --plugin qt \
        --output appimage
)

[[ -f "$OUTPUT" ]] || die "linuxdeploy didn't produce $OUTPUT"
[[ -n "$(find "$APPDIR" -path '*sqldrivers*' -name 'libqsqlite*')" ]] \
    || die "The Qt SQLite driver wasn't bundled; the app wouldn't be able to open its database."

info "Created Snippet Manager $VERSION: $(basename "$OUTPUT") ($(du -h "$OUTPUT" | cut -f1))"
echo "    Run it with: chmod +x $(basename "$OUTPUT") && ./$(basename "$OUTPUT")"
