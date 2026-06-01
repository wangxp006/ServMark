#!/bin/bash
# ServMark dependency installer
# Supports: CentOS/RHEL/Fedora, Ubuntu/Debian, openSUSE

set -e

if [ "$(id -u)" -ne 0 ]; then
    echo "This script must be run as root (or with sudo)."
    echo "Usage: sudo ./scripts/install-deps.sh"
    exit 1
fi

detect_distro() {
    if [ -f /etc/os-release ]; then
        . /etc/os-release
        echo "$ID"
    elif [ -f /etc/redhat-release ]; then
        echo "centos"
    else
        echo "unknown"
    fi
}

DISTRO=$(detect_distro)
echo "Detected distribution: $DISTRO"

case "$DISTRO" in
    ubuntu|debian)
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

    centos|rhel|fedora|rocky|almalinux)
        echo "Installing dependencies via dnf/yum..."
        if command -v dnf &>/dev/null; then
            PKG_MGR="dnf"
        else
            PKG_MGR="yum"
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

    opensuse*|sles)
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
        echo "Unknown distribution: $DISTRO"
        echo ""
        echo "Please install these packages manually:"
        echo "  - C11 compiler (gcc or clang)"
        echo "  - cmake >= 3.16"
        echo "  - hwloc development library"
        echo "  - libnuma development library"
        echo "  - OpenSSL development library (libcrypto)"
        echo "  - libzstd development library"
        echo ""
        echo "Ubuntu/Debian:"
        echo "  apt install build-essential cmake pkg-config libhwloc-dev libnuma-dev libssl-dev libzstd-dev"
        echo ""
        echo "CentOS/RHEL/Fedora:"
        echo "  dnf install gcc make cmake pkgconfig hwloc-devel numactl-devel openssl-devel libzstd-devel"
        echo ""
        echo "openSUSE:"
        echo "  zypper install gcc make cmake pkg-config hwloc-devel libnuma-devel libopenssl-devel libzstd-devel"
        exit 1
        ;;
esac

echo ""
echo "All dependencies installed."
echo "Now build ServMark:"
echo "  mkdir build && cd build && cmake .. && make -j\$(nproc)"
