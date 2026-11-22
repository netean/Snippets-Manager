#!/usr/bin/env bash
#
# Build a self-contained, ready-to-run AppImage of Snippet Manager.
#
# The AppImage bundles Qt and the SQLite driver, so it runs on other Linux
# machines without installing anything. It needs a glibc at least as new as
# the one it was built with, so by default it's built in an Ubuntu 24.04
# container (with podman or docker): the result runs on Ubuntu 24.04 and newer,
# Debian 13, Fedora 40+ and current rolling distros, whatever you build it on.
#
# Usage: ./package-appimage.sh            # build in a container (recommended)
#        ./package-appimage.sh --native   # build with this machine's Qt
# Output: SnippetManager-<version>-x86_64.AppImage in the project directory

set -euo pipefail

SOURCE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SOURCE_DIR/build-appimage"
APPDIR="$BUILD_DIR/AppDir"
TOOLS_DIR="$BUILD_DIR/tools"
ARCH="$(uname -m)"
BUILDER_IMAGE="snippetmanager-appimage-builder:ubuntu24.04"

# linuxdeploy is pinned to known releases and checked before it's run.
# The linuxdeploy checksum matches the one GitHub records for the release.
LINUXDEPLOY_URL="https://github.com/linuxdeploy/linuxdeploy/releases/download/1-alpha-20251107-1/linuxdeploy-x86_64.AppImage"
LINUXDEPLOY_SHA256="c20cd71e3a4e3b80c3483cef793cda3f4e990aca14014d23c544ca3ce1270b4d"
QT_PLUGIN_URL="https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/1-alpha-20250213-1/linuxdeploy-plugin-qt-x86_64.AppImage"
QT_PLUGIN_SHA256="15106be885c1c48a021198e7e1e9a48ce9d02a86dd0a1848f00bdbf3c1c92724"

info() { printf '\033[1;34m==>\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33mWarning:\033[0m %s\n' "$*" >&2; }
die()  { printf '\033[1;31mError:\033[0m %s\n' "$*" >&2; exit 1; }

NATIVE=false
case "${1:-}" in
    --native)  NATIVE=true ;;
    -h|--help) sed -n '2,15p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    "")        ;;
    *)         die "Unknown option: $1 (see --help)" ;;
esac

[[ "$(uname -s)" == "Linux" ]] || die "AppImages can only be built on Linux."
[[ "$ARCH" == "x86_64" ]] || die "This script currently supports x86_64 only."

# The version comes from project(... VERSION x.y) in CMakeLists.txt
VERSION="$(sed -n 's/^project(.*VERSION \([0-9][0-9.]*\).*/\1/p' "$SOURCE_DIR/CMakeLists.txt")"
[[ -n "$VERSION" ]] || die "Couldn't read the version from CMakeLists.txt"
OUTPUT="$SOURCE_DIR/SnippetManager-$VERSION-$ARCH.AppImage"

# ---------------------------------------------------------------------------
# Container build: runs this script with --native inside Ubuntu 24.04
# ---------------------------------------------------------------------------
if ! $NATIVE; then
    if command -v podman >/dev/null; then
        ENGINE=podman
        # Files created in the container belong to you, not root
        USER_ARGS=(--userns=keep-id)
    elif command -v docker >/dev/null; then
        ENGINE=docker
        USER_ARGS=(--user "$(id -u):$(id -g)")
    else
        die "Neither podman nor docker was found. Install one, or run with --native (the AppImage then only runs on distros at least as new as this one)."
    fi

    if ! $ENGINE image inspect "$BUILDER_IMAGE" >/dev/null 2>&1; then
        info "Creating the build container (first time only)"
        $ENGINE build -t "$BUILDER_IMAGE" -f - "$SOURCE_DIR/resources" <<'CONTAINERFILE'
FROM docker.io/library/ubuntu:24.04
RUN apt-get update -qq \
 && DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --no-install-recommends \
        cmake make g++ qt6-base-dev qt6-base-dev-tools qmake6 libqt6sql6-sqlite \
        qt6-wayland libgl-dev curl file ca-certificates \
 && rm -rf /var/lib/apt/lists/*
CONTAINERFILE
    fi

    info "Building in the container ($ENGINE)"
    # :Z relabels the folder so SELinux (e.g. on Fedora) lets the container use it
    $ENGINE run --rm "${USER_ARGS[@]}" -e HOME=/tmp \
        -v "$SOURCE_DIR:/src:Z" -w /src "$BUILDER_IMAGE" ./package-appimage.sh --native
    exit $?
fi

# ---------------------------------------------------------------------------
# Native build
# ---------------------------------------------------------------------------
command -v cmake >/dev/null || die "cmake not found. Run ./install.sh --install-deps first."
command -v curl >/dev/null || die "curl not found."

glibc="$(ldd --version 2>/dev/null | head -n1 | grep -oE '[0-9]+\.[0-9]+$' || true)"
if [[ -n "$glibc" ]] && [[ "$(printf '%s\n2.39\n' "$glibc" | sort -V | tail -n1)" != "2.39" ]]; then
    warn "This system has glibc $glibc, so the AppImage will only run on distros at least this new. Build without --native for wider compatibility."
fi

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

# ---------------------------------------------------------------------------
# Tools
# ---------------------------------------------------------------------------
mkdir -p "$TOOLS_DIR"
fetch_tool() {
    local file="$1" url="$2" sha256="$3"
    if [[ ! -f "$TOOLS_DIR/$file" ]] || ! echo "$sha256  $TOOLS_DIR/$file" | sha256sum --check --status; then
        info "Downloading $file"
        curl -fsSL -o "$TOOLS_DIR/$file.part" "$url" || die "Failed to download $url"
        mv "$TOOLS_DIR/$file.part" "$TOOLS_DIR/$file"
    fi
    # Checked every time, so a changed file is never run
    echo "$sha256  $TOOLS_DIR/$file" | sha256sum --check --status \
        || die "$file doesn't match its expected checksum; not running it. Delete $TOOLS_DIR and try again."
    chmod +x "$TOOLS_DIR/$file"
}
fetch_tool "linuxdeploy-$ARCH.AppImage" "$LINUXDEPLOY_URL" "$LINUXDEPLOY_SHA256"
fetch_tool "linuxdeploy-plugin-qt-$ARCH.AppImage" "$QT_PLUGIN_URL" "$QT_PLUGIN_SHA256"

# The tools are AppImages themselves; this lets them run without FUSE (e.g. in containers)
export APPIMAGE_EXTRACT_AND_RUN=1

# ---------------------------------------------------------------------------
# Build and stage into the AppDir
# ---------------------------------------------------------------------------
info "Building Snippet Manager $VERSION"
# Start clean: a previous build may have been made elsewhere (e.g. outside the container)
rm -rf "$BUILD_DIR/build"
cmake -S "$SOURCE_DIR" -B "$BUILD_DIR/build" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr -DBUILD_TESTING=OFF >/dev/null
cmake --build "$BUILD_DIR/build" --parallel "$(nproc 2>/dev/null || echo 2)"

rm -rf "$APPDIR"
DESTDIR="$APPDIR" cmake --install "$BUILD_DIR/build" >/dev/null

# Include native Wayland support when the Qt Wayland plugin is available
# (otherwise the app runs through XWayland on Wayland desktops)
plugin_dir="$("$QMAKE" -query QT_INSTALL_PLUGINS)"
deps_only=()
if [[ -e "$plugin_dir/platforms/libqwayland-generic.so" ]]; then
    export EXTRA_PLATFORM_PLUGINS="libqwayland-generic.so"
    # Wayland windows also need these plugins, which the pinned Qt plugin
    # for linuxdeploy doesn't bundle by itself
    for dir in wayland-shell-integration wayland-decoration-client wayland-graphics-integration-client; do
        if [[ -d "$plugin_dir/$dir" ]]; then
            mkdir -p "$APPDIR/usr/plugins"
            cp -r "$plugin_dir/$dir" "$APPDIR/usr/plugins/"
            deps_only+=(--deploy-deps-only "$APPDIR/usr/plugins/$dir")
        fi
    done
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
        --desktop-file "$APPDIR/usr/share/applications/io.github.netean.SnippetManager.desktop" \
        --icon-file "$SOURCE_DIR/resources/icon.png" \
        --icon-filename snippetmanager \
        "${deps_only[@]}" \
        --plugin qt \
        --output appimage
)

[[ -f "$OUTPUT" ]] || die "linuxdeploy didn't produce $OUTPUT"
[[ -n "$(find "$APPDIR" -path '*sqldrivers*' -name 'libqsqlite*')" ]] \
    || die "The Qt SQLite driver wasn't bundled; the app wouldn't be able to open its database."
if [[ -n "${EXTRA_PLATFORM_PLUGINS:-}" ]]; then
    [[ -n "$(find "$APPDIR" -path '*wayland-shell-integration*' -name '*xdg-shell*')" ]] \
        || die "Qt's Wayland shell integration wasn't bundled; windows wouldn't open on Wayland."
fi

info "Created Snippet Manager $VERSION: $(basename "$OUTPUT") ($(du -h "$OUTPUT" | cut -f1))"
echo "    Run it with: chmod +x $(basename "$OUTPUT") && ./$(basename "$OUTPUT")"
