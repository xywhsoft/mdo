"""Exercise production inbox IO on a real JVM, on Linux/WSL for Android paths."""
import argparse
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--java-home', type=Path)
    parser.add_argument('--xserver-root', type=Path)
    parser.add_argument('--android-jar', type=Path)
    args = parser.parse_args()
    def java(name):
        return str(args.java_home/'bin'/name) if args.java_home else shutil.which(name)
    with tempfile.TemporaryDirectory(prefix='mdo-share-io-') as raw:
        base = Path(raw)
        subprocess.run([java('javac'), '-encoding', 'UTF-8', '-d', str(base),
            str(ROOT/'tools/android/MdoShareFiles.java'),
            str(ROOT/'tests/fixtures/android-sharing/MdoShareFilesProbe.java')], check=True, timeout=30)
        subprocess.run([java('java'), '-cp', str(base), 'org.xserver.android.MdoShareFilesProbe', str(base/'inbox')],
            check=True, timeout=30)
        if args.xserver_root:
            if not args.android_jar:
                parser.error('--android-jar required for Activity integration check')
            sys.path.insert(0, str(ROOT/'tools/android'))
            import share_overlay
            overlay = base/'java'; shutil.copytree(args.xserver_root/'android/src',overlay)
            share_overlay.apply(overlay)
            subprocess.run([java('javac'), '-encoding', 'UTF-8', '-source', '8', '-target', '8',
                '-classpath', str(args.android_jar), '-d', str(base/'classes'),
                *map(str, sorted(overlay.rglob('*.java')))],check=True,timeout=60)
            intent = ET.fromstring('<manifest xmlns:android="http://schemas.android.com/apk/res/android">'+share_overlay.MANIFEST+'</manifest>')[0]
            name = '{http://schemas.android.com/apk/res/android}name'
            assert {item.attrib[name] for item in intent.findall('action')} == {
                'android.intent.action.SEND', 'android.intent.action.SEND_MULTIPLE'}
            try:
                share_overlay.apply(overlay)
            except ValueError:
                pass
            else:
                raise AssertionError('Already-patched Activity must fail the overlay build')
            activity = overlay/'org/xserver/android/XsActivity.java'
            original = (args.xserver_root/'android/src/org/xserver/android/XsActivity.java').read_text(encoding='utf-8')
            activity.write_text(original.replace('                deliverAppLink();', '                // Changed SDK callback'),encoding='utf-8')
            try:
                share_overlay.apply(overlay)
            except ValueError:
                pass
            else:
                raise AssertionError('Changed SDK must fail the Activity overlay build')
            print('Android sharing pinned Activity compile and drift guard PASS')
