#!/bin/bash
# ServMark dependency installer
# Auto-detects the package manager by probing available commands.
# Supports: apt, dnf, yum, zypper, pacman, apk, and more.
#
# Works on any distribution without hardcoded OS lists:
#   DEB family:  Ubuntu, Debian, Deepin, Mint, UOS, etc.
#   RPM family:  CentOS, RHEL, Fedora, Rocky, Alma, Anolis,
#                openEuler, Kylin, Oracle, Amazon Linux, etc.
#   SUSE family: openSUSE, SLES
#   Arch family: Arch, Manjaro, EndeavourOS
#   Alpine:      Alpine Linux (containers)

set -e

if [ "$(id -u)" -ne 0 ]; then
    echo "This script must be run as root (or with sudo)."
    echo "Usage: sudo ./scripts/install-deps.sh"
    exit 1
fi

# Show what we're running on
if [ -f /etc/os-release ]; then
    . /etc/os-release
    echo "Detected: ${NAME:-unknown} (${ID:-unknown}), like: ${ID_LIKE:-none}"
fi

# === Package manager probe & package name tables ===
# Each entry: command=probe_cmd  update_cmd  install_cmd_prefix
# Package names are mapped per-manager below.

declare -A PKG_MAP_CC PKG_MAP_CMAKE PKG_MAP_PKGCONFIG
declare -A PKG_MAP_HWLOC PKG_MAP_NUMA PKG_MAP_SSL PKG_MAP_ZSTD
declare -A PKG_UPDATE PKG_INSTALL

# Map: manager_name => package_name
register_pkgs() {
    local mgr="$1" cc="$2" cm="$3" pk="$4" hw="$5" nu="$6" ssl="$7" zs="$8"
    PKG_MAP_CC[$mgr]="$cc"
    PKG_MAP_CMAKE[$mgr]="$cm"
    PKG_MAP_PKGCONFIG[$mgr]="$pk"
    PKG_MAP_HWLOC[$mgr]="$hw"
    PKG_MAP_NUMA[$mgr]="$nu"
    PKG_MAP_SSL[$mgr]="$ssl"
    PKG_MAP_ZSTD[$mgr]="$zs"
}

register_pkgs apt     "build-essential"     "cmake" "pkg-config" "libhwloc-dev"  "libnuma-dev"  "libssl-dev"     "libzstd-dev"
register_pkgs dnf     "gcc gcc-c++ make"    "cmake" "pkgconfig"  "hwloc-devel"    "numactl-devel" "openssl-devel"  "libzstd-devel"
register_pkgs yum     "gcc gcc-c++ make"    "cmake" "pkgconfig"  "hwloc-devel"    "numactl-devel" "openssl-devel"  "libzstd-devel"
register_pkgs zypper  "gcc gcc-c++ make"    "cmake" "pkg-config" "hwloc-devel"    "libnuma-devel" "libopenssl-devel" "libzstd-devel"
register_pkgs pacman  "gcc make"            "cmake" "pkg-config" "hwloc"          "numactl"      "openssl"         "zstd"
register_pkgs apk     "gcc g++ make"        "cmake" "pkgconfig"  "hwloc-dev"      "numactl-dev"  "openssl-dev"     "zstd-dev"

# Probe order: preferred first
PROBE_ORDER=("yum" "dnf" "apt-get" "zypper" "pacman" "apk")

detect_pkg_manager() {
    for probe in "${PROBE_ORDER[@]}"; do
        if command -v "$probe" &>/dev/null; then
            echo "$probe"
            return
        fi
    done
    echo ""
}

PKG_BIN=$(detect_pkg_manager)
if [ -z "$PKG_BIN" ]; then
    echo "ERROR: No supported package manager found."
    echo "Probed: ${PROBE_ORDER[*]}"
    echo ""
    echo "Please install these packages manually:"
    echo "  gcc, make, cmake, pkg-config, hwloc-devel, numactl-devel,"
    echo "  openssl-devel (libcrypto), libzstd-devel"
    exit 1
fi

# Map binary name to our internal key
case "$PKG_BIN" in
    apt-get)  MGR="apt" ;;
    dnf)      MGR="dnf" ;;
    yum)      MGR="yum" ;;
    zypper)   MGR="zypper" ;;
    pacman)   MGR="pacman" ;;
    apk)      MGR="apk" ;;
    *)        echo "Unknown package manager: $PKG_BIN"; exit 1 ;;
esac

echo "Using package manager: $PKG_BIN ($MGR)"

# === Install ===
# Per-package-group install: each entry from register_pkgs is installed
# individually so that a missing optional package (e.g. hwloc-devel on
# older distros) doesn't block everything else.
case "$MGR" in
    apt)
        apt-get update
        ;;
    apk)
        apk update
        ;;
esac

for pkg in ${PKG_MAP_CC[$MGR]} ${PKG_MAP_CMAKE[$MGR]} \
           ${PKG_MAP_PKGCONFIG[$MGR]} ${PKG_MAP_HWLOC[$MGR]} \
           ${PKG_MAP_NUMA[$MGR]} ${PKG_MAP_SSL[$MGR]} ${PKG_MAP_ZSTD[$MGR]}; do
    case "$MGR" in
        apt)     apt-get install -y "$pkg" ;;
        dnf|yum) $PKG_BIN install -y "$pkg" ;;
        zypper)  zypper --non-interactive install "$pkg" ;;
        pacman)  pacman -S --noconfirm "$pkg" ;;
        apk)     apk add "$pkg" ;;
    esac || echo "Note: $pkg not available, skipping"
done

echo ""
echo "All dependencies installed."
echo ""
echo "Build ServMark:"
echo "  mkdir build && cd build && cmake .. && make -j\$(nproc)"
