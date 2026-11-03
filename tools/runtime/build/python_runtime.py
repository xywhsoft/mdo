"""Prepare staged portable interpreters for normal CLI and pip use."""
from pathlib import Path, PurePosixPath
import hashlib
import json
import shutil
import urllib.request
import zipfile


def prepare_windows_python(directory: Path) -> None:
    """Only transform a disposable/staged runtime, before computing its manifest.

    CPython finds its bundled stdlib ZIP and extension modules beside python.exe.
    The upstream ._pth instead forces isolation even without -I, hiding project
    imports and PYTHONPATH. Removing it restores native CLI path initialization;
    explicit -I, -E and -P retain their standard behavior. No launcher is needed.
    """
    if not (directory / "python.exe").is_file():
        raise FileNotFoundError(directory / "python.exe")
    if not list(directory.glob("python3*.zip")):
        raise FileNotFoundError("Bundled Python standard library ZIP is missing")
    for path in directory.glob("python*._pth"):
        path.unlink()


def pip_wheel(cache: Path | None = None) -> Path:
    """Fetch only the pinned upstream wheel, verifying cache hits too."""
    lock = json.loads(Path(__file__).with_name("pip.lock.json").read_text(encoding="utf-8"))
    if cache is None:
        cache = Path(__file__).resolve().parents[3] / ".build/runtime-tools/downloads"
    cache.mkdir(parents=True, exist_ok=True)
    target = cache / lock["file"]
    if not target.exists():
        with urllib.request.urlopen(lock["url"], timeout=60) as response:
            data = response.read(lock["bytes"] + 1)
        if len(data) != lock["bytes"] or hashlib.sha256(data).hexdigest() != lock["sha256"]:
            raise ValueError("Pinned pip download checksum mismatch")
        target.write_bytes(data)
    data = target.read_bytes()
    if len(data) != lock["bytes"] or hashlib.sha256(data).hexdigest() != lock["sha256"]:
        raise ValueError("Pinned pip cache checksum mismatch")
    return target


def prepare_python_pip(prefix: Path, *, windows: bool = False, wheel: Path | None = None, portable_linux: bool = False) -> str:
    """Bundle pip as ordinary pure Python files; never run a downloaded installer."""
    if windows:
        destination = prefix / "Lib/site-packages"
    else:
        libraries = sorted((prefix / "lib").glob("python3.*"))
        libraries = [p for p in libraries if p.is_dir()]
        if len(libraries) != 1:
            raise ValueError("Expected one Python standard library directory")
        destination = libraries[0] / "site-packages"
    destination.mkdir(parents=True, exist_ok=True)
    wheel = wheel or pip_wheel()
    with zipfile.ZipFile(wheel) as archive:
        for info in archive.infolist():
            name = PurePosixPath(info.filename)
            if name.is_absolute() or ".." in name.parts or "\\" in info.filename or ":" in info.filename:
                raise ValueError("Unsafe pip wheel path")
            if (info.external_attr >> 16) & 0o170000 == 0o120000:
                raise ValueError("Pip wheel contains a symbolic link")
        archive.extractall(destination)
    if portable_linux:
        # The interpreter is started with our private musl loader. The host's
        # /lib/ld-musl-* may be absent or a different version. Keep pip's wheel
        # tags tied to the loader actually supplied with this interpreter.
        detector = destination / "pip/_vendor/packaging/_musllinux.py"
        text = detector.read_text(encoding="utf-8")
        anchor = '    try:\n        with open(executable, "rb") as f:\n'
        if text.count(anchor) != 1:
            raise ValueError("Pinned pip musl detector changed; review the integration")
        text = text.replace(anchor,
            '    loader = getattr(sys, "_mdo_musl_loader", None)\n'
            '    if loader:\n'
            '        proc = subprocess.run([loader], check=False, stderr=subprocess.PIPE, text=True)\n'
            '        return _parse_musl_version(proc.stderr)\n' + anchor)
        detector.write_text(text, encoding="utf-8", newline="\n")
    shutil.copy2(Path(__file__).with_name("python_site.py"), destination / "_mdo_python_runtime.py")
    # The bootstrap import must not write its own bytecode into immutable tool
    # files before configure() redirects all later caches into Home data.
    (destination / "mdo-python.pth").write_text(
        "import sys; sys.dont_write_bytecode=True; import _mdo_python_runtime; "
        "_mdo_python_runtime.configure(); sys.dont_write_bytecode=bool(sys.flags.dont_write_bytecode)\n",
        encoding="utf-8")
    metadata = next(destination.glob("pip-*.dist-info/METADATA"))
    return next(line.split(": ", 1)[1] for line in metadata.read_text(encoding="utf-8").splitlines() if line.startswith("Version: "))
