"""Execute the Android ProxySelector adapter on the JVM, including stalled PAC."""
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--java-home', type=Path)
    args = parser.parse_args()
    def executable(name):
        return str(args.java_home/'bin'/name) if args.java_home else shutil.which(name)
    with tempfile.TemporaryDirectory(prefix='android-system-proxy-', dir=ROOT/'.build') as raw:
        subprocess.run([executable('javac'), '-encoding', 'UTF-8', '-d', raw,
                        str(ROOT/'tools/native/SystemProxy.java'),
                        str(ROOT/'tests/fixtures/SystemProxyProbe.java')], check=True, timeout=30)
        subprocess.run([executable('java'), '-cp', raw, 'org.xserver.android.SystemProxyProbe'],
                       check=True, timeout=10)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
