"""Run the production Java path resolver on a filesystem with real symlinks.

Run with a JDK on Linux/WSL (matching Android's symlink semantics); no device,
Gradle, network or Android mock implementation is required.
"""
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
        return str(args.java_home / 'bin' / name) if args.java_home else shutil.which(name)
    javac, java = executable('javac'), executable('java')
    if not javac or not java: parser.error('JDK required; specify --java-home')
    with tempfile.TemporaryDirectory(prefix='mdo-runtime-paths-') as temporary:
        base = Path(temporary); classes = base / 'classes'; classes.mkdir()
        data = base / 'data'; data.mkdir()
        subprocess.run([javac, '-encoding', 'UTF-8', '-d', str(classes),
            str(ROOT / 'tools/android/MdoRuntimePaths.java'),
            str(ROOT / 'tests/fixtures/android-runtime-paths/MdoRuntimePathsProbe.java')], check=True, timeout=30)
        subprocess.run([java, '-cp', str(classes), 'org.xserver.android.MdoRuntimePathsProbe', str(data)],
            check=True, timeout=30)


if __name__ == '__main__': main()
