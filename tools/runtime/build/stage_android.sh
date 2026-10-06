#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
REPO=$(cd -- "$SCRIPT_DIR/../../.." && pwd)
ROOT=${MDO_TOOL_BUILD_ROOT:-$HOME/.cache/mdo-runtime-tools-20261006}
OUT="$REPO/tools/runtime/android-arm64-v8a"
for tool in busybox curl jq openssh python; do mkdir -p "$OUT/$tool"; done
for tool in busybox curl jq; do
  cp "$ROOT/out/$tool" "$OUT/$tool/$tool"
done
for program in ssh scp sftp ssh-keygen ssh-keyscan; do
  cp "$ROOT/out/$program" "$OUT/openssh/$program"
done
cp "$ROOT/out/python3" "$OUT/python/bin/python3"
cp "$ROOT/src/busybox-1.38.0/LICENSE" "$OUT/busybox/LICENSE"
cp "$ROOT/src/busybox-1.38.0/.config" "$OUT/busybox/build.config"
cp "$ROOT/src/curl-8.22.0/COPYING" "$OUT/curl/LICENSE"
cp "$ROOT/src/openssl-3.5.9/LICENSE.txt" "$OUT/curl/OpenSSL-LICENSE.txt"
cp "$ROOT/src/zlib-1.3.1/README" "$OUT/curl/zlib-README.txt"
cp "$ROOT/src/jq-1.8.2/COPYING" "$OUT/jq/LICENSE"
cp "$ROOT/src/jq-1.8.2/vendor/oniguruma/COPYING" "$OUT/jq/Oniguruma-LICENSE"
cp "$ROOT/src/openssh-10.3p1/LICENCE" "$OUT/openssh/LICENSE"
cp "$ROOT/src/openssl-3.5.9/LICENSE.txt" "$OUT/openssh/OpenSSL-LICENSE.txt"
cp "$ROOT/src/zlib-1.3.1/README" "$OUT/openssh/zlib-README.txt"
cp "${MDO_TOOL_DOWNLOADS:-$REPO/.build/runtime-tools/downloads}/cacert.pem" "$OUT/curl/cacert.pem"
cp "$ROOT/src/openssl-3.5.9/LICENSE.txt" "$OUT/python/OpenSSL-LICENSE.txt"
cp "$ROOT/src/zlib-1.3.1/README" "$OUT/python/zlib-README.txt"
printf 'Android runtime files staged\n'
