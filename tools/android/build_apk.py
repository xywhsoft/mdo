# Derived from the pinned xs Android APK packager; mdo owns this customization.
#!/usr/bin/env python3
"""Package a local xs application as a signed ARM64 APK, without Gradle."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import zipfile
from xml.sax.saxutils import escape

ROOT = Path(os.environ["MDO_ANDROID_XSERVER_ROOT"])
from runtime_bundle import add_runtime
MDO_ROOT = Path(__file__).resolve().parents[2]


def run(command: list[str]) -> None:
    print("[apk] " + Path(command[0]).name + " " + " ".join(command[1:3]), flush=True)
    subprocess.run(command, check=True)


def build(sdk: Path, java: Path, library: Path, pack: Path, output: Path,
          package: str, label: str, version: str, code: int, keystore: Path,
          debuggable: bool = False, home_name: str = "app-home",
          resources: Path | None = None, app_links: list[str] | None = None,
          package_install: bool = False) -> None:
    if not re.fullmatch(r"[a-z][a-z0-9_]*(?:\.[a-z][a-z0-9_]*)+", package):
        raise ValueError("invalid Android application ID")
    if code < 1 or code > 2100000000:
        raise ValueError("invalid Android version code")
    if not re.fullmatch(r"[a-z][a-z0-9_-]{0,63}", home_name):
        raise ValueError("home must be a simple directory name")
    if not pack.is_file() or not library.is_file():
        raise ValueError("missing application pack or native library")
    if resources is not None and not resources.is_dir():
        raise ValueError("Android resources directory does not exist")
    app_links = app_links or []
    if len(app_links) > 8 or len(set(app_links)) != len(app_links):
        raise ValueError("at most eight distinct App Links")
    from urllib.parse import urlsplit
    filters = []
    for link in app_links:
        uri = urlsplit(link)
        if uri.scheme != 'https' or not re.fullmatch(r'[a-z0-9]+(?:[.-][a-z0-9]+)*', uri.netloc) or not re.fullmatch(r'/[A-Za-z0-9/_-]+', uri.path) or uri.query or uri.fragment:
            raise ValueError("App Link must be an exact HTTPS host/path without query or fragment")
        filters.append('<intent-filter android:autoVerify="true"><action android:name="android.intent.action.VIEW" />'
            '<category android:name="android.intent.category.DEFAULT" /><category android:name="android.intent.category.BROWSABLE" />'
            f'<data android:scheme="https" android:host="{uri.netloc}" android:path="{uri.path}" /></intent-filter>')
    links_metadata = escape(json.dumps(app_links), {chr(34): '&quot;'})
    icon_attributes = 'android:icon="@mipmap/ic_launcher" android:roundIcon="@mipmap/ic_launcher"' if resources is not None else ""
    lock = json.loads((ROOT / "tools/android/toolchain.lock.json").read_text())
    tools = sdk / "build-tools" / lock["build_tools"]
    suffix = ".exe" if os.name == "nt" else ""
    android = sdk / "platforms" / f"android-{lock['compile_sdk']}" / "android.jar"
    jvm = str(java / "bin" / ("java" + suffix))
    d8 = [jvm, "-cp", str(tools / "lib/d8.jar"), "com.android.tools.r8.D8"]
    signer = [jvm, "-jar", str(tools / "lib/apksigner.jar")]
    run([str(java / "bin" / ("javac" + suffix)), "-version"])
    output.parent.mkdir(parents=True, exist_ok=True)
    keystore.parent.mkdir(parents=True, exist_ok=True)
    if not keystore.exists():
        # Development signer is persistent but private and untracked. A public
        # distribution must use its own release keystore and signing process.
        run([str(java / "bin" / ("keytool" + suffix)), "-genkeypair", "-keystore", str(keystore),
             "-storepass", "android", "-keypass", "android", "-alias", "xs-development",
             "-keyalg", "RSA", "-keysize", "3072", "-validity", "10000",
             "-dname", "CN=xs Development,O=Local Development,C=CN"])
    manifest = f'''<manifest xmlns:android="http://schemas.android.com/apk/res/android"
 package="{package}" android:versionCode="{code}" android:versionName="{escape(version, {chr(34): '&quot;'})}">
 <uses-sdk android:minSdkVersion="{lock['api']}" android:targetSdkVersion="{lock['compile_sdk']}" />
 <uses-permission android:name="android.permission.INTERNET" />
 <uses-permission android:name="android.permission.WAKE_LOCK" />
 <uses-permission android:name="android.permission.FOREGROUND_SERVICE" />
 <uses-permission android:name="android.permission.FOREGROUND_SERVICE_SPECIAL_USE" />
 <uses-permission android:name="android.permission.POST_NOTIFICATIONS" />
 {'<uses-permission android:name="android.permission.REQUEST_INSTALL_PACKAGES" />' if package_install else ''}
 <application android:label="{escape(label, {chr(34): '&quot;'})}" android:theme="@android:style/Theme.Material.NoActionBar"
  android:allowBackup="false" android:usesCleartextTraffic="true" android:extractNativeLibs="true"
  android:debuggable="{'true' if debuggable else 'false'}" android:supportsRtl="true" {icon_attributes}>
  <meta-data android:name="org.xserver.android.HOME" android:value="{home_name}" />
  <meta-data android:name="org.xserver.android.APP_LINKS" android:value="{links_metadata}" />
  <activity android:name="org.xserver.android.XsActivity" android:exported="true" android:launchMode="singleTask" android:windowSoftInputMode="adjustResize">
   <intent-filter><action android:name="android.intent.action.MAIN" /><category android:name="android.intent.category.LAUNCHER" /></intent-filter>
   {''.join(filters)}
  </activity>
  {'<activity android:name="org.xserver.android.PackageInstall" android:exported="false" android:configChanges="orientation|screenSize" /><receiver android:name="org.xserver.android.PackageInstall$ResultReceiver" android:exported="false" />' if package_install else ''}
  <service android:name="org.xserver.android.XsService" android:exported="false" android:foregroundServiceType="specialUse">
   <property android:name="android.app.PROPERTY_SPECIAL_USE_FGS_SUBTYPE" android:value="User-started local C agent host with asynchronous tools and an on-device HTTP frontend; stopped explicitly from its notification." />
  </service>
 </application>
</manifest>'''
    with tempfile.TemporaryDirectory(prefix="xs-apk-", dir=output.parent) as raw:
        directory = Path(raw)
        xml = directory / "AndroidManifest.xml"
        xml.write_text(manifest, encoding="utf-8")
        classes = directory / "classes"; classes.mkdir()
        overlay = directory / "java"
        import shutil
        shutil.copytree(ROOT / "android/src", overlay)
        service = overlay / "org/xserver/android/XsService.java"
        text = service.read_text().replace("PackageInstall.initialize(getApplicationContext());", "MdoRuntime.initialize(getApplicationContext());\n            PackageInstall.initialize(getApplicationContext());")
        service.write_text(text)
        shutil.copy2(MDO_ROOT / "tools/android/MdoRuntime.java", service.with_name("MdoRuntime.java"))
        sources = sorted(overlay.rglob("*.java"))
        run([str(java / "bin" / ("javac" + suffix)), "-encoding", "UTF-8", "-source", "8", "-target", "8",
             "-classpath", str(android), "-d", str(classes), *map(str, sources)])
        jar = directory / "classes.jar"
        with zipfile.ZipFile(jar, "w") as archive:
            for file in sorted(classes.rglob("*.class")):
                archive.write(file, file.relative_to(classes).as_posix())
        dex = directory / "dex"; dex.mkdir()
        run([*d8, "--release", "--min-api", str(lock["api"]), "--lib", str(android), "--output", str(dex), str(jar)])
        unsigned = directory / "unsigned.apk"
        compiled_resources = []
        if resources is not None:
            resource_archive = directory / "resources.zip"
            run([str(tools / ("aapt2" + suffix)), "compile", "--dir", str(resources), "-o", str(resource_archive)])
            compiled_resources = [str(resource_archive)]
        run([str(tools / ("aapt2" + suffix)), "link", "-I", str(android), "--manifest", str(xml), "-o", str(unsigned), *compiled_resources])
        with zipfile.ZipFile(unsigned, "a") as archive:
            archive.write(library, "lib/arm64-v8a/libxs.so", compress_type=zipfile.ZIP_STORED)
            archive.write(pack, "assets/app.xrtpack", compress_type=zipfile.ZIP_STORED)
            release = json.loads((MDO_ROOT / "app/release.json").read_text())
            add_runtime(archive, MDO_ROOT / "tools/runtime/android-arm64-v8a", os.environ["MDO_ANDROID_EDITION"], code, release["toolpack_revision"])
            for file in dex.glob("*.dex"):
                archive.write(file, file.name, compress_type=zipfile.ZIP_DEFLATED)
            archive.write(ROOT / "tcc/COPYING", "assets/licenses/tcc-LGPL.txt", compress_type=zipfile.ZIP_DEFLATED)
        aligned = directory / "aligned.apk"
        run([str(tools / ("zipalign" + suffix)), "-P", "16", "-f", "4", str(unsigned), str(aligned)])
        signed = directory / "signed.apk"
        run([*signer, "sign", "--ks", str(keystore), "--ks-key-alias", "xs-development", "--ks-pass", "pass:android",
             "--key-pass", "pass:android", "--out", str(signed), str(aligned)])
        run([*signer, "verify", "--verbose", str(signed)])
        run([str(tools / ("zipalign" + suffix)), "-c", "-P", "16", "4", str(signed)])
        signed.replace(output)
    print("[apk] Built " + str(output), flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", type=Path, required=True)
    parser.add_argument("--java-home", type=Path, required=True)
    parser.add_argument("--library", type=Path, required=True)
    parser.add_argument("--pack", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--package", required=True)
    parser.add_argument("--label", required=True)
    parser.add_argument("--version", default="0.1.0")
    parser.add_argument("--version-code", type=int, default=1)
    parser.add_argument("--keystore", type=Path, required=True)
    parser.add_argument("--debuggable", action="store_true")
    parser.add_argument("--home-name", default="app-home")
    parser.add_argument("--resources", type=Path,
                        help="optional Android res tree containing mipmap/ic_launcher")
    parser.add_argument("--app-link", action="append", default=[], help="verified HTTPS callback, repeatable")
    parser.add_argument("--package-install", action="store_true", help="enable native system APK updates")
    args = parser.parse_args()
    build(args.sdk.resolve(), args.java_home.resolve(), args.library.resolve(), args.pack.resolve(),
          args.output.resolve(), args.package, args.label, args.version, args.version_code,
          args.keystore.resolve(), args.debuggable, args.home_name,
          args.resources.resolve() if args.resources is not None else None, args.app_link, args.package_install)
