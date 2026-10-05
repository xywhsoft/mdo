"""Copy isolated product sources with their local quoted include dependencies.

SDK headers remain the host's responsibility. This copies only real files
inside app/ and does not evaluate C conditions or expand compiler expressions.
"""
from pathlib import Path
import re
import shutil


APP = (Path(__file__).resolve().parents[1] / "app").resolve()
QUOTED_INCLUDE = re.compile(r'^\s*#\s*include\s*"([^"\r\n]+)"', re.MULTILINE)


def copy_echo_module(site: Path) -> None:
    """Inject the test tool into a disposable Home, never the product tree."""
    source = Path(__file__).resolve().parent / "fixtures/modules/echo.c"
    target = site / "default-home/modules/tools/fixture_echo.c"
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, target)


def copy_app_source(relative: str, site: Path) -> None:
    destination = site.resolve()
    seen: set[Path] = set()

    def copy(source: Path) -> None:
        source = source.resolve()
        if not source.is_relative_to(APP):
            raise ValueError(f"source escapes app: {source}")
        if source in seen:
            return
        seen.add(source)
        target = destination / source.relative_to(APP)
        if not target.resolve().is_relative_to(destination):
            raise ValueError(f"fixture target escapes site: {target}")
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
        if source.suffix in (".c", ".h"):
            for include in QUOTED_INCLUDE.findall(source.read_text(encoding="utf-8")):
                dependency = (source.parent / include).resolve()
                if dependency.is_relative_to(APP) and dependency.is_file():
                    copy(dependency)

    copy(APP / relative)
