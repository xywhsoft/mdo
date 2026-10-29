"""Compile the actual native resolver; Windows settings are mocked, never changed."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default=shutil.which('gcc') or r'E:\software\w64devkit\bin\gcc.exe')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='system-proxy-native-', dir=ROOT/'.build') as raw:
        output = Path(raw)/('probe.exe' if os.name == 'nt' else 'probe')
        subprocess.run([args.cc, '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                        '-I'+str(ROOT/'tools/native'), str(ROOT/'tests/fixtures/system_proxy_probe.c'),
                        '-o', str(output)], check=True)
        subprocess.run([str(output)], check=True, timeout=15)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
