"""Execute the shipped APK file validator with real Linux paths and sizes.

Run under Linux/WSL with a JDK; --xserver-root additionally validates and compiles
the actual pinned installer overlay against android.jar, without any device.
"""
import argparse
import importlib.util
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--java-home', type=Path)
    parser.add_argument('--xserver-root', type=Path)
    parser.add_argument('--android-jar', type=Path)
    args = parser.parse_args()
    def executable(name):
        return str(args.java_home / 'bin' / name) if args.java_home else shutil.which(name)
    javac, java = executable('javac'), executable('java')
    if not javac or not java:
        parser.error('JDK required; specify --java-home')
    policy = ROOT / 'tools/android/MdoPackageInstallPolicy.java'
    c_limit = re.search(r'#define MDO_UPDATE_LIMIT \((\d+)u\*1024u\*1024u\)',
                        (ROOT / 'app/src/update/manager.c').read_text(encoding='utf-8'))
    java_limit = re.search(r'MAX_BYTES = (\d+)L \* 1024 \* 1024', policy.read_text(encoding='utf-8'))
    if not c_limit or not java_limit or c_limit[1] != java_limit[1]:
        raise AssertionError('Download and native install size limits differ')
    with tempfile.TemporaryDirectory(prefix='mdo-apk-policy-') as temporary:
        base = Path(temporary); classes = base / 'classes'; classes.mkdir()
        data = base / 'data'; data.mkdir()
        subprocess.run([javac, '-encoding', 'UTF-8', '-d', str(classes), str(policy),
            str(ROOT / 'tests/fixtures/android-package-install/MdoPackageInstallPolicyProbe.java')],
            check=True, timeout=30)
        subprocess.run([java, '-cp', str(classes), 'org.xserver.android.MdoPackageInstallPolicyProbe', str(data)],
            check=True, timeout=30)
        if args.xserver_root:
            if not args.android_jar:
                parser.error('--android-jar required for pinned installer integration check')
            os.environ['MDO_ANDROID_XSERVER_ROOT'] = str(args.xserver_root)
            sys.path.insert(0, str(ROOT / 'tools/android'))
            spec = importlib.util.spec_from_file_location('mdo_apk_packager', ROOT / 'tools/android/build_apk.py')
            packager = importlib.util.module_from_spec(spec); spec.loader.exec_module(packager)
            source = base / 'PackageInstall.java'
            shutil.copy2(args.xserver_root / 'android/src/org/xserver/android/PackageInstall.java', source)
            packager.overlay_package_installer(source)
            subprocess.run([javac, '-encoding', 'UTF-8', '-classpath', str(args.android_jar),
                            '-d', str(classes), str(source), str(source.with_name(policy.name))],
                           check=True, timeout=30)
            try:
                packager.overlay_package_installer(source)
            except ValueError:
                pass
            else:
                raise AssertionError('Changed SDK validator must fail the overlay build')
            print('PASS actual pinned PackageInstall integration and SDK drift guard')


if __name__ == '__main__': main()
