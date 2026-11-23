#!/usr/bin/env bash
#
# Builds the Linux packages for a release, each in a clean container:
#   - Arch (.pkg.tar.zst), from packaging/arch/PKGBUILD (the AUR package)
#   - Fedora (.rpm), from packaging/rpm/snippets-manager.spec
#   - Debian/Ubuntu (.deb), with packaging/deb/build-deb.sh on Ubuntu 24.04
# Each package is then installed in a fresh container to check it works.
#
# By default the version, source download and checksums come from the
# PKGBUILD. With --source, the packages are built from a local source archive
# instead (the release workflow uses this to build from the tagged commit
# before the release exists).
#
# Usage: packaging/build-packages.sh [--version X --source archive.tar.gz] [arch|rpm|deb ...]
#        (default: all three)
# Output: dist/ in the project folder

set -euo pipefail

PACKAGING="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DIST="$(dirname "$PACKAGING")/dist"
WORK="$DIST/work"

info() { printf '\033[1;34m==>\033[0m %s\n' "$*"; }
die()  { printf '\033[1;31mError:\033[0m %s\n' "$*" >&2; exit 1; }

if command -v podman >/dev/null; then ENGINE=podman
elif command -v docker >/dev/null; then ENGINE=docker
else die "This needs podman or docker."
fi
# Files written in the container should belong to you. With rootless podman the
# container's root already is you; with docker, hand them back explicitly.
if [[ "$ENGINE" == podman ]]; then OWNER="0:0"; else OWNER="$(id -u):$(id -g)"; fi

# Read the release details from the PKGBUILD (our own file)
eval "$(source "$PACKAGING/arch/PKGBUILD"; declare -p pkgname pkgver source sha256sums)"
TARBALL_URL="${source[0]#*::}"
LICENSE_URL="${source[1]#*::}"

LOCAL_SOURCE=""
targets=()
while [[ $# -gt 0 ]]; do
    case "$1" in
        --version) pkgver="$2"; shift ;;
        --source)  LOCAL_SOURCE="$(realpath "$2")"; shift ;;
        arch|rpm|deb) targets+=("$1") ;;
        *) die "Unknown option: $1" ;;
    esac
    shift
done
[[ ${#targets[@]} -gt 0 ]] || targets=(arch rpm deb)

# ---------------------------------------------------------------------------
# Sources, checked against the PKGBUILD's checksums
# ---------------------------------------------------------------------------
mkdir -p "$WORK/sources"
fetch() {
    local url="$1" file="$2" sha256="$3"
    if [[ ! -f "$file" ]] || ! echo "$sha256  $file" | sha256sum --check --status; then
        info "Downloading $(basename "$file")"
        curl -fsSL -o "$file" "$url" || die "Couldn't download $url"
    fi
    echo "$sha256  $file" | sha256sum --check --status || die "$(basename "$file") doesn't match the PKGBUILD's checksum"
}
TARBALL="$WORK/sources/$pkgname-$pkgver.tar.gz"
if [[ -n "$LOCAL_SOURCE" ]]; then
    cp "$LOCAL_SOURCE" "$TARBALL"
else
    fetch "$TARBALL_URL" "$TARBALL" "${sha256sums[0]}"
fi
fetch "$LICENSE_URL" "$WORK/sources/LICENSE" "${sha256sums[1]}"

# The recipes as built: the PKGBUILD and .spec, with the version (and, for a
# local source archive, its checksum) filled in
RECIPES="$WORK/recipes"
rm -rf "$RECIPES" && mkdir -p "$RECIPES"
tarball_sha256="$(sha256sum "$TARBALL" | cut -d' ' -f1)"
sed -e "s/^pkgver=.*/pkgver=$pkgver/" \
    -e "s/'${sha256sums[0]}'/'$tarball_sha256'/" \
    "$PACKAGING/arch/PKGBUILD" > "$RECIPES/PKGBUILD"
[[ "$pkgver" == "$(source "$PACKAGING/arch/PKGBUILD"; echo "$pkgver")" ]] || sed -i 's/^pkgrel=.*/pkgrel=1/' "$RECIPES/PKGBUILD"
sed -e "s/^Version:.*/Version:        $pkgver/" "$PACKAGING/rpm/snippets-manager.spec" > "$RECIPES/snippets-manager.spec"

run() {
    # run <image> <script>: runs a script as root in a fresh container, with the
    # packaging folder and work folder mounted
    local image="$1" script="$2"
    $ENGINE run --rm -v "$PACKAGING:/packaging:ro,Z" -v "$WORK:/work:Z" -e OWNER="$OWNER" \
        -e VERSION="$pkgver" -e PKGNAME="$pkgname" \
        -e APT_OPTS="-o Acquire::http::Timeout=30 -o Acquire::Retries=5" \
        "$image" bash -euo pipefail -c "$script"
}

# ---------------------------------------------------------------------------
# Arch
# ---------------------------------------------------------------------------
build_arch() {
    info "Building the Arch package"
    rm -rf "$WORK/arch" && mkdir -p "$WORK/arch"
    run docker.io/library/archlinux:latest '
        pacman -Syu --noconfirm --needed base-devel cmake qt6-base syntax-highlighting namcap >/dev/null
        useradd -m builder
        mkdir /build && cp /work/recipes/PKGBUILD /build/
        cp /work/sources/$PKGNAME-$VERSION.tar.gz /work/sources/LICENSE /build/
        chown -R builder /build
        su builder -c "cd /build && makepkg --noconfirm --cleanbuild"
        echo "--- namcap (package checks)"; namcap /build/$PKGNAME-$VERSION-*.pkg.tar.zst || true
        su builder -c "cd /build && makepkg --printsrcinfo" > /work/arch/.SRCINFO
        # (Not the separate -debug package makepkg may also build)
        cp /build/$PKGNAME-$VERSION-*.pkg.tar.zst /work/arch/
        chown -R "$OWNER" /work/arch'
    info "Installing it in a fresh Arch container"
    run docker.io/library/archlinux:latest '
        pacman -Syu --noconfirm >/dev/null
        pacman -U --noconfirm /work/arch/$PKGNAME-$VERSION-*.pkg.tar.zst >/dev/null
        QT_QPA_PLATFORM=offscreen snippetmanager --version'
    cp "$WORK"/arch/*.pkg.tar.zst "$DIST/"
    # The .SRCINFO only matches the repository's PKGBUILD when building its own release
    [[ -n "$LOCAL_SOURCE" ]] || cp "$WORK/arch/.SRCINFO" "$PACKAGING/arch/.SRCINFO"
}

# ---------------------------------------------------------------------------
# Fedora
# ---------------------------------------------------------------------------
build_rpm() {
    info "Building the Fedora package"
    rm -rf "$WORK/rpm" && mkdir -p "$WORK/rpm"
    run registry.fedoraproject.org/fedora:44 '
        dnf install -y -q rpm-build rpmdevtools dnf-plugins-core >/dev/null
        dnf builddep -y -q /work/recipes/snippets-manager.spec >/dev/null
        mkdir -p /root/rpmbuild/SOURCES
        cp /work/sources/$PKGNAME-$VERSION.tar.gz /root/rpmbuild/SOURCES/Snippets-Manager-$VERSION.tar.gz
        cp /work/sources/LICENSE /root/rpmbuild/SOURCES/LICENSE
        rpmbuild -bb /work/recipes/snippets-manager.spec
        cp /root/rpmbuild/RPMS/*/*.rpm /work/rpm/
        chown -R "$OWNER" /work/rpm'
    info "Installing it in a fresh Fedora container"
    run registry.fedoraproject.org/fedora:44 '
        dnf install -y -q /work/rpm/'"$pkgname"'-'"$pkgver"'-*.rpm >/dev/null
        QT_QPA_PLATFORM=offscreen snippetmanager --version'
    cp "$WORK"/rpm/"$pkgname-$pkgver"-*.x86_64.rpm "$DIST/"
}

# ---------------------------------------------------------------------------
# Debian / Ubuntu
# ---------------------------------------------------------------------------
build_deb() {
    info "Building the Debian/Ubuntu package"
    rm -rf "$WORK/deb" && mkdir -p "$WORK/deb"
    run docker.io/library/ubuntu:24.04 '
        export DEBIAN_FRONTEND=noninteractive
        apt-get $APT_OPTS update -qq && apt-get $APT_OPTS install -y -qq cmake g++ qt6-base-dev libqt6sql6-sqlite dpkg-dev file >/dev/null
        /packaging/deb/build-deb.sh /work/sources/$PKGNAME-$VERSION.tar.gz /work/sources/LICENSE $VERSION /work/deb
        chown -R "$OWNER" /work/deb'
    info "Installing it in a fresh Ubuntu container"
    run docker.io/library/ubuntu:24.04 '
        export DEBIAN_FRONTEND=noninteractive
        apt-get $APT_OPTS update -qq && apt-get $APT_OPTS install -y -qq /work/deb/*.deb >/dev/null
        QT_QPA_PLATFORM=offscreen snippetmanager --version'
    cp "$WORK"/deb/*.deb "$DIST/"
}

for target in "${targets[@]}"; do
    case "$target" in
        arch) build_arch ;;
        rpm)  build_rpm ;;
        deb)  build_deb ;;
        *)    die "Unknown package type: $target (use arch, rpm or deb)" ;;
    esac
done

info "Packages in $DIST:"
find "$DIST" -maxdepth 1 -type f \( -name '*.pkg.tar.zst' -o -name '*.rpm' -o -name '*.deb' \) -printf '    %f\n' | sort
