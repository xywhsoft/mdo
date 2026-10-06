#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
REPO=$(cd -- "$SCRIPT_DIR/../../.." && pwd)
ROOT=${MDO_TOOL_BUILD_ROOT:-$HOME/.cache/mdo-runtime-tools-20261006}
NDK=${ANDROID_NDK_ROOT:?Set ANDROID_NDK_ROOT to Android NDK 30.0.16248370}
TC="$NDK/toolchains/llvm/prebuilt/linux-x86_64"
mkdir -p "$ROOT/python-sdk" "$ROOT/out"
if [[ ! -f "$ROOT/python-sdk/prefix/lib/libpython3.14.so" ]]; then
  tar -xf "${MDO_TOOL_DOWNLOADS:-$REPO/.build/runtime-tools/downloads}/python-3.14.8-aarch64-linux-android.tar.gz" -C "$ROOT/python-sdk"
fi
"$TC/bin/aarch64-linux-android26-clang" -Os -fPIE -pie \
  -I"$ROOT/python-sdk/prefix/include/python3.14" \
  "$SCRIPT_DIR/python_runner.c" \
  -L"$ROOT/python-sdk/prefix/lib" -lpython3.14 \
  -Wl,-rpath,'$ORIGIN/../lib' -Wl,-z,max-page-size=16384 \
  -o "$ROOT/out/python3"
"$TC/bin/llvm-strip" "$ROOT/out/python3"
file "$ROOT/out/python3"
"$TC/bin/llvm-readelf" -d "$ROOT/out/python3" | grep -E 'NEEDED|RUNPATH'
