#!/bin/sh
# Prepare an isolated release compiler. Never change the host's libc or
# register system services. Workspace mounts are managed by build_linux.py.
set -eu
if [ "$(id -u)" != 0 ]; then
    printf '%s\n' 'Run with sudo/root; debootstrap and chroot require it.' >&2
    exit 1
fi
root=${1:-}
if [ -z "$root" ]; then
    printf '%s\n' 'Usage: bootstrap_baseline.sh /absolute/path/to/build-root [debian-keyring]' >&2
    exit 1
fi
case "$root" in /*) ;; *) printf '%s\n' 'Build root must be absolute.' >&2; exit 1 ;; esac
if [ -L "$root" ]; then printf '%s\n' 'Build root must not be a symlink.' >&2; exit 1; fi
root=$(realpath -m "$root")
case "$root" in /|/usr|/usr/*|/etc|/etc/*|/bin|/lib|/lib64|/opt)
    printf '%s\n' 'Use a dedicated build cache directory, not a system directory.' >&2; exit 1 ;;
esac
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
keyring=${2:-$script_dir/debian-archive-keyring.gpg}
if [ ! -f "$keyring" ]; then
    printf '%s\n' 'Bundled Debian archive keyring is missing; restore it or pass an archive keyring path.' >&2
    exit 1
fi
lock="$script_dir/baseline.lock.json"
if [ "$#" -lt 2 ]; then
    key_digest=$(python3 -c 'import json,sys;print(json.load(open(sys.argv[1]))["keyring"]["sha256"])' "$lock")
    printf '%s  %s\n' "$key_digest" "$keyring" | sha256sum --check -
fi
version=$(python3 -c 'import json,sys;print(json.load(open(sys.argv[1]))["python"]["version"])' "$lock")
url=$(python3 -c 'import json,sys;print(json.load(open(sys.argv[1]))["python"]["url"])' "$lock")
digest=$(python3 -c 'import json,sys;print(json.load(open(sys.argv[1]))["python"]["sha256"])' "$lock")
if [ ! -f "$root/etc/debian_version" ]; then
    debootstrap --keyring="$keyring" --variant=minbase --arch=amd64 buster "$root" https://archive.debian.org/debian
fi
case "$(cat "$root/etc/debian_version")" in 10.*) ;; *) printf '%s\n' 'Existing root is not Debian 10.' >&2; exit 1 ;; esac
printf 'deb https://archive.debian.org/debian buster main\n' > "$root/etc/apt/sources.list"
# This is a signed, historical archive. Disable freshness expiration only;
# keep signature verification and package checksums enabled.
printf 'Acquire::Check-Valid-Until "false";\n' > "$root/etc/apt/apt.conf.d/90-mdo-archive"
chroot "$root" apt-get update
chroot "$root" env DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    gcc g++ binutils make git pkg-config musl-tools linux-libc-dev \
    libgtk-3-dev libwebkit2gtk-4.0-dev libx11-dev ca-certificates wget xz-utils \
    libssl-dev zlib1g-dev libffi-dev libbz2-dev liblzma-dev
if [ ! -x "$root/opt/mdo-build-python/bin/python3.12" ]; then
    mkdir -p "$root/tmp/mdo-python-build"
    archive="$root/tmp/mdo-python-build/Python-$version.tar.xz"
    chroot "$root" wget -q "$url" -O "/tmp/mdo-python-build/Python-$version.tar.xz"
    printf '%s  %s\n' "$digest" "$archive" | sha256sum --check -
    chroot "$root" sh -c 'cd /tmp/mdo-python-build && tar -xf "Python-$1.tar.xz" && cd "Python-$1" && ./configure --prefix=/opt/mdo-build-python --without-ensurepip --disable-test-modules && make -j2 && make install' sh "$version"
fi
test "$(chroot "$root" /opt/mdo-build-python/bin/python3.12 --version)" = "Python $version"
test "$(chroot "$root" getconf GNU_LIBC_VERSION)" = 'glibc 2.28'
printf '%s\n' 'Baseline ready: glibc 2.28, isolated GCC/GTK/WebKit headers, modern build Python.'
