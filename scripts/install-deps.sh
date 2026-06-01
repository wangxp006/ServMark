#!/bin/bash
# ServMark dependency installer
# Detects OS family via /etc/os-release ID_LIKE field.
# RPM family:  CentOS, RHEL, Fedora, Rocky, Alma, Anolis, openEuler, Kylin,
#              UOS server, Oracle Linux, Scientific Linux, Amazon Linux, etc.
# DEB family:  Ubuntu, Debian, Deepin, UOS desktop, Linux Mint, etc.
# SUSE family: openSUSE, SLES

set -e

if [ "$(id -u)" -ne 0 ]; then
    echo "This script must be run as root (or with sudo)."
    echo "Usage: sudo ./scripts/install-deps.sh"
    exit 1
fi

# Source /etc/os-release for ID, ID_LIKE, VERSION_ID
if [ -f /etc/os-release ]; then
    . /etc/os-release
else
    echo "Cannot detect OS: /etc/os-release not found"
    exit 1
fi

echo "Detected: $NAME ($ID), like: ${ID_LIKE:-none}"

# Determine package family from ID_LIKE (most reliable for derivatives)
# Fall back to ID if ID_LIKE is empty
FAMILY=""
if echo "$ID_LIKE $ID" | grep -qE 'fedora|rhel|centos'; then
    FAMILY="rpm"
elif echo "$ID_LIKE $ID" | grep -qE 'debian|ubuntu'; then
    FAMILY="deb"
elif echo "$ID_LIKE $ID" | grep -qE 'suse'; then
    FAMILY="suse"
fi

case "$FAMILY" in
    deb)
        echo "Installing dependencies via apt..."
        apt-get update
        apt-get install -y \
            build-essential \
            cmake \
            pkg-config \
            libhwloc-dev \
            libnuma-dev \
            libssl-dev \
            libzstd-dev
        ;;

    rpm)
        echo "Installing dependencies via dnf/yum..."
        if command -v dnf &>/dev/null; then
            PKG_MGR="dnf"
        elif command -v yum &>/dev/null; then
            PKG_MGR="yum"
        else
            echo "Neither dnf nor yum found. Please install packages manually."
            exit 1
        fi
        $PKG_MGR install -y \
            gcc \
            gcc-c++ \
            make \
            cmake \
            pkgconfig \
            hwloc-devel \
            numactl-devel \
            openssl-devel \
            libzstd-devel
        ;;

    suse)
        echo "Installing dependencies via zypper..."
        zypper --non-interactive install \
            gcc \
            gcc-c++ \
            make \
            cmake \
            pkg-config \
            hwloc-devel \
            libnuma-devel \
            libopenssl-devel \
            libzstd-devel
        ;;

    *)
        echo "Could not determine package family from ID='$ID' ID_LIKE='$ID_LIKE'"
        echo ""
        echo "Please install these packages manually and re-run:"
        echo ""
        echo "  DEB (Ubuntu/Debian/Deepin/Mint):"
        echo "    apt install build-essential cmake pkg-config libhwloc-dev libnuma-dev libssl-dev libzstd-dev"
        echo ""
        echo "  RPM (CentOS/RHEL/Fedora/Rocky/Alma/Anolis/openEuler/Kylin):"
        echo "    dnf install gcc gcc-c++ make cmake pkgconfig hwloc-devel numactl-devel openssl-devel libzstd-devel"
        echo ""
        echo "  SUSE (openSUSE/SLES):"
        echo "    zypper install gcc gcc-c++ make cmake pkg-config hwloc-devel libnuma-devel libopenssl-devel libzstd-devel"
        exit 1
        ;;
esac

echo ""
echo "All dependencies installed."
echo ""
echo "Build ServMark:"
echo "  mkdir build && cd build && cmake .. && make -j\$(nproc)"
