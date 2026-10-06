#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
REPO=$(cd -- "$SCRIPT_DIR/../../.." && pwd)
ROOT=${MDO_TOOL_BUILD_ROOT:-$HOME/.cache/mdo-runtime-tools-20261006}
DOWNLOADS=${MDO_TOOL_DOWNLOADS:-$REPO/.build/runtime-tools/downloads}
NDK=${ANDROID_NDK_ROOT:?Set ANDROID_NDK_ROOT to Android NDK 30.0.16248370}
TC="$NDK/toolchains/llvm/prebuilt/linux-x86_64"
export PATH="$TC/bin:$PATH"
export ANDROID_NDK_ROOT="$NDK"
export CC=aarch64-linux-android26-clang
export AR=llvm-ar RANLIB=llvm-ranlib STRIP=llvm-strip
export CFLAGS='-Os -fPIC -ffunction-sections -fdata-sections'
export LDFLAGS="-static -Wl,--gc-sections -Wl,-z,max-page-size=16384 -L$ROOT/prefix/lib"
DYNAMIC_LDFLAGS="-Wl,--gc-sections -Wl,-z,max-page-size=16384 -L$ROOT/prefix/lib"
mkdir -p "$ROOT/src" "$ROOT/prefix" "$ROOT/out" "$ROOT/logs"
extract() {
  local archive="$1" directory="$2"
  if [[ ! -d "$ROOT/src/$directory" ]]; then tar -xf "$DOWNLOADS/$archive" -C "$ROOT/src"; fi
}
python3 "$SCRIPT_DIR/fetch_sources.py" --verify-only --downloads "$DOWNLOADS"
extract zlib-1.3.1.tar.gz zlib-1.3.1
extract openssl-3.5.9.tar.gz openssl-3.5.9
extract jq-1.8.2.tar.gz jq-1.8.2
extract busybox-1.38.0.tar.bz2 busybox-1.38.0
extract openssh-10.3p1.tar.gz openssh-10.3p1
extract curl-8.22.0.tar.xz curl-8.22.0
if [[ ! -f "$ROOT/prefix/lib/libz.a" ]]; then
  cd "$ROOT/src/zlib-1.3.1"
  ./configure --static --prefix="$ROOT/prefix" > "$ROOT/logs/zlib.log" 2>&1
  make -j2 >> "$ROOT/logs/zlib.log" 2>&1
  make install >> "$ROOT/logs/zlib.log" 2>&1
fi
printf 'zlib ready\n'
if [[ ! -f "$ROOT/prefix/lib/libcrypto.a" ]]; then
  cd "$ROOT/src/openssl-3.5.9"
  unset CC
  ./Configure android-arm64 -D__ANDROID_API__=26 no-shared no-tests no-apps no-engine \
    --prefix="$ROOT/prefix" --libdir=lib > "$ROOT/logs/openssl.log" 2>&1
  make -j2 >> "$ROOT/logs/openssl.log" 2>&1
  make install_sw >> "$ROOT/logs/openssl.log" 2>&1
  export CC=aarch64-linux-android26-clang
fi
printf 'OpenSSL ready\n'
if [[ ! -f "$ROOT/out/jq" ]]; then
  cd "$ROOT/src/jq-1.8.2"
  ./configure --host=aarch64-linux-android --disable-maintainer-mode --disable-docs \
    --disable-shared --enable-static --with-oniguruma=builtin \
    > "$ROOT/logs/jq.log" 2>&1
  make -j2 LDFLAGS="$LDFLAGS -all-static" >> "$ROOT/logs/jq.log" 2>&1
  cp jq "$ROOT/out/jq"
  "$STRIP" "$ROOT/out/jq"
fi
printf 'jq ready\n'
if [[ ! -f "$ROOT/out/busybox" ]]; then
  cd "$ROOT/src/busybox-1.38.0"
  make allnoconfig > "$ROOT/logs/busybox.log" 2>&1
  python3 - <<'PY'
from pathlib import Path
keys = '''BUSYBOX STATIC DESKTOP LONG_OPTS SHOW_USAGE FEATURE_VERBOSE_USAGE
UNICODE_SUPPORT UNICODE_COMBINING_WCHARS UNICODE_WIDE_WCHARS UNICODE_PRESERVE_BROKEN FEATURE_ASSUME_UNICODE LFS
ASH ASH_INTERNAL_GLOB ASH_JOB_CONTROL ASH_ALIAS ASH_ECHO ASH_PRINTF ASH_TEST ASH_GETOPTS ASH_CMDCMD
FEATURE_SH_IS_ASH SH_IS_ASH FEATURE_SH_STANDALONE FEATURE_SH_NOFORK
AWK FEATURE_AWK_LIBM SED GREP EGREP FGREP FEATURE_GREP_CONTEXT
FIND FEATURE_FIND_PRINT0 FEATURE_FIND_TYPE FEATURE_FIND_NAME FEATURE_FIND_PATH FEATURE_FIND_REGEX FEATURE_FIND_EXEC
XARGS FEATURE_XARGS_SUPPORT_ZERO_TERM FEATURE_XARGS_SUPPORT_QUOTES
TAR FEATURE_TAR_CREATE FEATURE_TAR_GNU_EXTENSIONS FEATURE_TAR_LONG_OPTIONS FEATURE_TAR_AUTODETECT
FEATURE_SEAMLESS_GZ FEATURE_SEAMLESS_BZ2 FEATURE_SEAMLESS_XZ FEATURE_SEAMLESS_LZMA FEATURE_UNZIP_CDF
GZIP GUNZIP UNZIP BZIP2 BUNZIP2 XZ UNXZ
BASE64 CAT CP FEATURE_CP_LONG_OPTIONS MV RM MKDIR RMDIR TOUCH
HEAD FEATURE_FANCY_HEAD TAIL FEATURE_FANCY_TAIL CUT SORT FEATURE_SORT_BIG UNIQ TR WC
DIFF FEATURE_DIFF_LONG_OPTIONS PATCH
BASENAME DIRNAME REALPATH READLINK FEATURE_READLINK_FOLLOW
ENV PRINTENV ECHO PRINTF TEST TEST1 TEST2 TRUE FALSE
LS FEATURE_LS_FILETYPES FEATURE_LS_TIMESTAMPS FEATURE_LS_SORTFILES FEATURE_LS_RECURSIVE FEATURE_LS_WIDTH
STAT FEATURE_STAT_FORMAT DATE FEATURE_DATE_ISOFMT SLEEP FEATURE_FANCY_SLEEP
CHMOD LN DU FEATURE_DU_DEFAULT_BLOCKSIZE_1K DF MD5SUM SHA256SUM FEATURE_MD5_SHA1_SUM_CHECK
TIMEOUT WHICH WHOAMI ID MKTemp MKTEMP CMP TEE OD XXD'''.upper().split()
p = Path('.config')
data = p.read_text()
for key in keys:
    data = data.replace(f'# CONFIG_{key} is not set', f'CONFIG_{key}=y')
data = data.replace('CONFIG_SUBST_WCHAR=0', 'CONFIG_SUBST_WCHAR=63')
data = data.replace('CONFIG_LAST_SUPPORTED_WCHAR=0', 'CONFIG_LAST_SUPPORTED_WCHAR=1114111')
p.write_text(data)
PY
  yes '' | make oldconfig >> "$ROOT/logs/busybox.log" 2>&1 || [[ ${PIPESTATUS[1]} == 0 ]]
  make -j2 CC="$CC" AR="$AR" STRIP="$STRIP" \
    EXTRA_CFLAGS="$CFLAGS -DANDROID -D__ANDROID__ -Dstrchrnul=bb_android_strchrnul" EXTRA_LDFLAGS="$LDFLAGS" \
    >> "$ROOT/logs/busybox.log" 2>&1
  cp busybox "$ROOT/out/busybox"
  "$STRIP" "$ROOT/out/busybox"
fi
printf 'BusyBox ready\n'
if [[ ! -f "$ROOT/out/curl" ]]; then
  cd "$ROOT/src/curl-8.22.0"
  export LDFLAGS="$DYNAMIC_LDFLAGS"
  CPPFLAGS="-I$ROOT/prefix/include" LIBS='-ldl' ac_cv_func_memset_explicit=no \
    ./configure --host=aarch64-linux-android --disable-shared --enable-static \
    --with-openssl="$ROOT/prefix" --with-zlib="$ROOT/prefix" \
    --without-libpsl --without-libidn2 --without-brotli --without-zstd \
    --without-nghttp2 --without-nghttp3 --without-ngtcp2 --without-libssh2 \
    --disable-ldap --disable-ldaps --disable-rtsp --disable-telnet --disable-tftp \
    --disable-pop3 --disable-imap --disable-smtp --disable-gopher --disable-mqtt \
    --disable-manual --disable-docs --with-ca-path=/system/etc/security/cacerts \
    > "$ROOT/logs/curl.log" 2>&1
  # Relink when a cached tree was configured with different system linkage.
  rm -f src/curl src/.libs/curl
  make -j2 LDFLAGS="$LDFLAGS -static-libtool-libs" >> "$ROOT/logs/curl.log" 2>&1
  cp src/curl "$ROOT/out/curl"
  "$STRIP" "$ROOT/out/curl"
fi
printf 'curl ready\n'
if [[ ! -f "$ROOT/out/ssh" ]]; then
  cd "$ROOT/src/openssh-10.3p1"
  export LDFLAGS="$DYNAMIC_LDFLAGS"
  python3 - <<'PY'
from pathlib import Path
p = Path('openbsd-compat/explicit_bzero.c')
data = p.read_text()
data = data.replace('static void (* volatile ssh_bzero)(void *, size_t) = bzero;',
'''static void ssh_android_bzero(void *p, size_t n) { memset(p, 0, n); }
static void (* volatile ssh_bzero)(void *, size_t) = ssh_android_bzero;''')
p.write_text(data)
p = Path('openbsd-compat/getrrsetbyname.c')
data = p.read_text()
if '#endif /* !__ANDROID__:' not in data:
    data = data.replace('#ifndef HAVE__RES_EXTERN', '#if !defined(HAVE__RES_EXTERN) && !defined(__ANDROID__)')
    data = data.replace('\tstruct __res_state *_resp = _THREAD_PRIVATE(_res, _res, &_res);',
'''#ifndef __ANDROID__
\tstruct __res_state *_resp = _THREAD_PRIVATE(_res, _res, &_res);
#endif''')
    data = data.replace('\t/* initialize resolver */', '#ifndef __ANDROID__\n\t/* initialize resolver */')
    data = data.replace('#endif /* RES_USE_DNSEC */', '#endif /* RES_USE_DNSEC */\n#endif /* !__ANDROID__: Bionic res_query initializes its resolver internally. */')
    p.write_text(data)
PY
  CPPFLAGS="-I$ROOT/prefix/include -DHAVE_ATTRIBUTE__SENTINEL__=1 -Drecallocarray=ssh_android_recallocarray -Dreallocarray=ssh_android_reallocarray -Dgetentropy=ssh_android_getentropy" \
    ac_cv_func_close_range=no ac_cv_func_bzero=yes ac_cv_func_recallocarray=no \
    ac_cv_func_reallocarray=no ac_cv_func_getentropy=no \
    ./configure --host=aarch64-linux-android --with-ssl-dir="$ROOT/prefix" \
    --with-zlib="$ROOT/prefix" --without-pam --without-selinux --without-kerberos5 \
    --without-libedit --without-security-key-builtin --without-ldns \
    --without-systemd --without-xauth --with-privsep-user=nobody \
    --with-default-path=/system/bin --prefix=/mdo-tools --sysconfdir=/system/etc/ssh \
    > "$ROOT/logs/openssh.log" 2>&1
  make -j2 ssh scp sftp ssh-keygen ssh-keyscan >> "$ROOT/logs/openssh.log" 2>&1
  for file in ssh scp sftp ssh-keygen ssh-keyscan; do
    cp "$file" "$ROOT/out/$file"
    "$STRIP" "$ROOT/out/$file"
  done
fi
printf 'OpenSSH ready\n'
file "$ROOT/out/"*
