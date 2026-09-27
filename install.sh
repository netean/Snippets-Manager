#!/usr/bin/env bash
#
# Build and install Snippet Manager on Linux.
#
# By default it installs for the current user into ~/.local (no sudo needed).
# Run ./install.sh --help for all options.

set -euo pipefail

APP_NAME="Snippet Manager"
BINARY="snippetmanager"
SOURCE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SOURCE_DIR/build-release"

PREFIX="$HOME/.local"
SYSTEM_INSTALL=false
INSTALL_DEPS=false
UNINSTALL=false

# Files installed by CMakeLists.txt, relative to the prefix. Used for uninstalling.
INSTALLED_FILES=(
    "bin/$BINARY"
    "share/applications/$BINARY.desktop"
    "share/icons/hicolor/128x128/apps/$BINARY.png"
    "share/icons/hicolor/64x64/apps/$BINARY.png"
    "share/icons/hicolor/32x32/apps/$BINARY.png"
    "share/icons/hicolor/scalable/apps/$BINARY.svg"
)

info()  { printf '\033[1;34m==>\033[0m %s\n' "$*"; }
warn()  { printf '\033[1;33mWarning:\033[0m %s\n' "$*" >&2; }
die()   { printf '\033[1;31mError:\033[0m %s\n' "$*" >&2; exit 1; }

usage() {
    cat <<EOF
Usage: ./install.sh [options]

Builds $APP_NAME from source and installs it, including the application
menu entry and icons.

Options:
  --system          Install for all users into /usr/local (uses sudo)
  --prefix DIR      Install into DIR instead (default: ~/.local)
  --install-deps    Install the build dependencies with your package manager
                    (apt, dnf, pacman or zypper; uses sudo)
  --uninstall       Remove a previous installation (use the same --system or
                    --prefix option you installed with). Your snippets
                    database is not touched.
  -h, --help        Show this help

Examples:
  ./install.sh                      # build and install for the current user
  ./install.sh --install-deps       # same, installing dependencies first
  ./install.sh --system             # install for all users
  ./install.sh --uninstall          # remove the per-user install
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --system)       SYSTEM_INSTALL=true; PREFIX="/usr/local" ;;
        --prefix)       [[ $# -ge 2 ]] || die "--prefix needs a directory"
                        PREFIX="$2"; shift ;;
        --prefix=*)     PREFIX="${1#*=}" ;;
        --install-deps) INSTALL_DEPS=true ;;
        --uninstall)    UNINSTALL=true ;;
        -h|--help)      usage; exit 0 ;;
        *)              usage >&2; echo >&2; die "Unknown option: $1" ;;
    esac
    shift
done

[[ "$(uname -s)" == "Linux" ]] || die "This script supports Linux only. See README.md for other platforms."

# Make the prefix absolute so the desktop file can point at the binary
mkdir -p "$PREFIX" 2>/dev/null || true
if [[ -d "$PREFIX" ]]; then
    PREFIX="$(cd "$PREFIX" && pwd)"
fi

# Use sudo only when the prefix isn't writable by the current user
SUDO=""
if [[ ! -w "$PREFIX" ]]; then
    if [[ $EUID -ne 0 ]]; then
        command -v sudo >/dev/null || die "$PREFIX is not writable and sudo is not available"
        SUDO="sudo"
    fi
fi

refresh_desktop_caches() {
    local apps_dir="$PREFIX/share/applications"
    local icons_dir="$PREFIX/share/icons/hicolor"

    if command -v update-desktop-database >/dev/null; then
        $SUDO update-desktop-database -q "$apps_dir" 2>/dev/null || true
    fi
    if command -v gtk-update-icon-cache >/dev/null && [[ -d "$icons_dir" ]]; then
        $SUDO gtk-update-icon-cache -q -t -f "$icons_dir" 2>/dev/null || true
    fi
    # Makes the KDE application menu pick up the change straight away
    for kbuild in kbuildsycoca6 kbuildsycoca5; do
        if command -v "$kbuild" >/dev/null; then
            "$kbuild" --noincremental >/dev/null 2>&1 || true
            break
        fi
    done
}

# ---------------------------------------------------------------------------
# Uninstall
# ---------------------------------------------------------------------------
if $UNINSTALL; then
    info "Removing $APP_NAME from $PREFIX"
    removed=0
    for file in "${INSTALLED_FILES[@]}"; do
        if [[ -e "$PREFIX/$file" ]]; then
            $SUDO rm -f "$PREFIX/$file"
            echo "    removed $PREFIX/$file"
            removed=$((removed + 1))
        fi
    done

    if [[ $removed -eq 0 ]]; then
        warn "Nothing found to remove in $PREFIX. If you installed with --system or --prefix, pass the same option."
        exit 1
    fi

    refresh_desktop_caches
    info "$APP_NAME has been uninstalled."
    echo "    Your snippets database was left in place (~/.local/share/SnippetManager by default)."
    exit 0
fi

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------
install_dependencies() {
    local sudo_cmd=""
    [[ $EUID -ne 0 ]] && sudo_cmd="sudo"

    if command -v apt-get >/dev/null; then
        info "Installing dependencies with apt"
        $sudo_cmd apt-get update
        $sudo_cmd apt-get install -y cmake make g++ qt6-base-dev libqt6sql6-sqlite
    elif command -v dnf >/dev/null; then
        info "Installing dependencies with dnf"
        $sudo_cmd dnf install -y cmake make gcc-c++ qt6-qtbase-devel
    elif command -v pacman >/dev/null; then
        info "Installing dependencies with pacman"
        $sudo_cmd pacman -S --needed --noconfirm cmake make gcc qt6-base
    elif command -v zypper >/dev/null; then
        info "Installing dependencies with zypper"
        $sudo_cmd zypper install -y cmake make gcc-c++ qt6-base-devel qt6-sql-sqlite
    else
        die "Couldn't find a supported package manager. Install CMake, a C++17 compiler and Qt 6 (Core, Widgets, Sql with the SQLite driver) manually."
    fi
}

if $INSTALL_DEPS; then
    install_dependencies
fi

missing=()
command -v cmake >/dev/null || missing+=("cmake")
command -v make >/dev/null || command -v ninja >/dev/null || missing+=("make")
command -v c++ >/dev/null || command -v g++ >/dev/null || command -v clang++ >/dev/null || missing+=("a C++ compiler")
if [[ ${#missing[@]} -gt 0 ]]; then
    die "Missing build tools: ${missing[*]}. Run ./install.sh --install-deps to install them."
fi

# ---------------------------------------------------------------------------
# Build
# ---------------------------------------------------------------------------
info "Configuring build"
if ! cmake -S "$SOURCE_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PREFIX" >"$BUILD_DIR.log" 2>&1; then
    cat "$BUILD_DIR.log" >&2
    if grep -q "Qt6" "$BUILD_DIR.log"; then
        die "Qt 6 development files were not found. Run ./install.sh --install-deps to install them."
    fi
    die "CMake configuration failed (see above)."
fi

info "Building $APP_NAME"
jobs="$(nproc 2>/dev/null || echo 2)"
cmake --build "$BUILD_DIR" --parallel "$jobs" || die "Build failed."

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------
info "Installing to $PREFIX"
$SUDO cmake --install "$BUILD_DIR" --prefix "$PREFIX" >/dev/null || die "Install failed."

# Point the menu entry at the installed binary, so it launches even when the
# prefix's bin directory isn't on the desktop session's PATH (common for ~/.local/bin)
desktop_file="$PREFIX/share/applications/$BINARY.desktop"
if [[ -f "$desktop_file" ]]; then
    $SUDO sed -i "s|^Exec=.*|Exec=$PREFIX/bin/$BINARY|" "$desktop_file"
fi

refresh_desktop_caches

# The Qt SQLite driver is a runtime plugin; warn if it looks absent
if ! find /usr/lib* /usr/local/lib* -path '*sqldrivers*' -name 'libqsqlite*' 2>/dev/null | grep -q .; then
    warn "The Qt 6 SQLite driver wasn't found. If the app can't open its database, run ./install.sh --install-deps."
fi

info "$APP_NAME has been installed."
echo "    Launch it from your application menu, or run: $PREFIX/bin/$BINARY"
case ":$PATH:" in
    *":$PREFIX/bin:"*) ;;
    *) echo "    Tip: add $PREFIX/bin to your PATH to run '$BINARY' from a terminal." ;;
esac
if $SYSTEM_INSTALL; then
    uninstall_args=" --system"
elif [[ "$PREFIX" != "$HOME/.local" ]]; then
    uninstall_args=" --prefix \"$PREFIX\""
else
    uninstall_args=""
fi
echo "    To uninstall: ./install.sh --uninstall$uninstall_args"
