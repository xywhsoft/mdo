#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
REPO=$(cd -- "$SCRIPT_DIR/../../.." && pwd)
ROOT=${MDO_TOOL_BUILD_ROOT:?Set MDO_TOOL_BUILD_ROOT to the existing OpenSSL/zlib build cache}
DOWNLOADS=${MDO_TOOL_DOWNLOADS:-$REPO/.build/runtime-tools/downloads}
NDK=${ANDROID_NDK_ROOT:?Set ANDROID_NDK_ROOT}
TC="$NDK/toolchains/llvm/prebuilt/linux-x86_64"
export PATH="$TC/bin:${CARGO_HOME:-$HOME/.cargo}/bin:$PATH"
export PKG_CONFIG_PATH="$ROOT/prefix/lib/pkgconfig"
CC=aarch64-linux-android26-clang
CXX=aarch64-linux-android26-clang++
OUT="$REPO/tools/runtime/android-arm64-v8a"
mkdir -p "$ROOT/src" "$ROOT/logs" "$ROOT/out" "$OUT/aria2" "$OUT/ripgrep" "$OUT/7zip"
python3 "$SCRIPT_DIR/fetch_sources.py" --verify-only --downloads "$DOWNLOADS" > "$ROOT/logs/extra-sources.log"
if [[ ! -d "$ROOT/src/aria2-1.37.0" ]]; then tar -xf "$DOWNLOADS/aria2-1.37.0.tar.xz" -C "$ROOT/src"; fi
if [[ ! -f "$ROOT/out/aria2c" ]]; then
  cd "$ROOT/src/aria2-1.37.0"
  CC="$CC" CXX="$CXX" AR=llvm-ar RANLIB=llvm-ranlib \
    CFLAGS='-Os -ffunction-sections -fdata-sections' CXXFLAGS='-Os -ffunction-sections -fdata-sections' \
    CPPFLAGS="-I$ROOT/prefix/include" LDFLAGS="-Wl,--gc-sections -Wl,-z,max-page-size=16384 -static-libstdc++ -L$ROOT/prefix/lib" LIBS='-ldl' \
    ./configure --host=aarch64-linux-android --with-openssl --without-gnutls --without-libssh2 \
    --without-libcares --without-libxml2 --without-libexpat --without-sqlite3 \
    --with-libz --disable-metalink --disable-nls --disable-libaria2 --with-ca-bundle=/system/etc/security/cacerts \
    > "$ROOT/logs/aria2.log" 2>&1
  make -j4 >> "$ROOT/logs/aria2.log" 2>&1
  cp src/aria2c "$ROOT/out/aria2c"
  llvm-strip "$ROOT/out/aria2c"
fi
cp "$ROOT/out/aria2c" "$OUT/aria2/aria2c"
cp "$ROOT/src/aria2-1.37.0/COPYING" "$OUT/aria2/LICENSE"
cp "$ROOT/src/openssl-3.5.9/LICENSE.txt" "$OUT/aria2/OpenSSL-LICENSE.txt"
cp "$ROOT/src/zlib-1.3.1/README" "$OUT/aria2/zlib-README.txt"
cp "$DOWNLOADS/cacert.pem" "$OUT/aria2/cacert.pem"
printf 'aria2 ready\n'
if [[ ! -d "$ROOT/src/7zip-26.04" ]]; then
  cp -a "$REPO/.build/runtime-tools/extra-staging/7zip-source" "$ROOT/src/7zip-26.04"
fi
if [[ ! -f "$ROOT/out/7zz" ]]; then
  cd "$ROOT/src/7zip-26.04/CPP/7zip/Bundles/Alone2"
  make -j4 -f makefile.gcc CC="$CC" CXX="$CXX" USE_CLANG=1 \
    LDFLAGS='-static-libstdc++ -Wl,--gc-sections -Wl,-z,max-page-size=16384' LIB2='-ldl' \
    CFLAGS_WARN_WALL='-Wall -Wextra' > "$ROOT/logs/7zip.log" 2>&1
  cp _o/7zz "$ROOT/out/7zz"
  llvm-strip "$ROOT/out/7zz"
fi
cp "$ROOT/out/7zz" "$OUT/7zip/7zz"
cp "$ROOT/src/7zip-26.04/DOC/License.txt" "$OUT/7zip/LICENSE"
printf '7-Zip ready\n'
if [[ ! -d "$ROOT/src/ripgrep-15.2.0" ]]; then tar -xf "$DOWNLOADS/ripgrep-15.2.0.tar.gz" -C "$ROOT/src"; fi
if [[ ! -f "$ROOT/out/rg" ]]; then
  cd "$ROOT/src/ripgrep-15.2.0"
  CARGO_TARGET_AARCH64_LINUX_ANDROID_LINKER="$CC" \
    RUSTFLAGS='-C opt-level=z -C strip=symbols -C link-arg=-Wl,-z,max-page-size=16384' \
    cargo build --locked --release --target aarch64-linux-android > "$ROOT/logs/ripgrep.log" 2>&1
  cp target/aarch64-linux-android/release/rg "$ROOT/out/rg"
  llvm-strip "$ROOT/out/rg"
fi
cp "$ROOT/out/rg" "$OUT/ripgrep/rg"
for file in COPYING LICENSE-MIT UNLICENSE; do cp "$ROOT/src/ripgrep-15.2.0/$file" "$OUT/ripgrep/$file"; done
printf 'ripgrep ready\n'
llvm-readelf -d "$OUT/aria2/aria2c" "$OUT/7zip/7zz" "$OUT/ripgrep/rg" | grep NEEDED
