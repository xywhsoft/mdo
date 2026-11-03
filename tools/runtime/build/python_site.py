"""Portable pip defaults; imported by the bundled interpreter's .pth file."""
import os
from pathlib import Path
import re
import site
import sys
import sysconfig


def configure():
    prefix = Path(sys.prefix).resolve()
    home = None
    # Desktop extensions and APK data use different depths. Locate their
    # common Home anchor without persisting a machine-specific absolute path.
    for parent in Path(__file__).resolve().parents:
        if parent.name.lower() == "external-tools":
            home = parent.parent
            if home.name.lower() == "data":
                home = home.parent
            break
    abi = re.sub(r"[^A-Za-z0-9._-]", "_",
                 sys.implementation.cache_tag + "-" + sysconfig.get_platform())
    root = home / "data/external-tools/python-packages" if home else prefix / "mdo-packages"
    target = root / abi / "site-packages"
    # Honour an explicit pip target; the default survives tool updates and
    # never changes the installed extension's content/approval digest.
    target = Path(os.environ.setdefault("PIP_TARGET", str(target)))
    if sys.pycache_prefix is None:
        cache = home / "data/cache/python" if home else prefix / "mdo-cache"
        sys.pycache_prefix = str(cache / abi)
    if sys.platform.startswith("linux"):
        loader = prefix.parent / "lib/ld-musl-x86_64.so.1"
        launcher = prefix.parent.parent / "python/bin/python3"
        if loader.is_file() and launcher.is_file():
            # pip build helpers and user subprocesses must use the launcher,
            # rather than an ELF requiring a host-installed /lib musl loader.
            sys.executable = str(launcher)
            sys._mdo_musl_loader = str(loader)
    os.environ.setdefault("PIP_DISABLE_PIP_VERSION_CHECK", "1")
    os.environ.setdefault("PIP_NO_CACHE_DIR", "1")
    if target.is_dir():
        site.addsitedir(str(target))
