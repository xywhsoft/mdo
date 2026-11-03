"""Copy isolated product sources with their local quoted include dependencies.

SDK headers remain the host's responsibility. This copies only real files
inside app/ and does not evaluate C conditions or expand compiler expressions.
"""
from pathlib import Path
import re
import shutil
import json


APP = (Path(__file__).resolve().parents[1] / "app").resolve()
QUOTED_INCLUDE = re.compile(r'^\s*#\s*include\s*"([^"\r\n]+)"', re.MULTILINE)


def install_todo_example(home: Path) -> None:
    """Seed the bundled tool for source-only fixtures that bypass bootstrap."""
    source = APP / "default-home/tools/todo"
    target = home / "tools/todo"
    if not target.exists():
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copytree(source, target)


def copy_echo_module(site: Path) -> None:
    """Inject the test tool into a disposable Home, never the product tree."""
    source = Path(__file__).resolve().parent / "fixtures/modules/echo.c"
    target = site / "default-home/tools/fixture_echo/tool.c"
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, target)
    (target.parent / "extension.json").write_text(json.dumps(dict(kind="tools",name="Echo fixture",
        alias="fixture_echo",id="tool:qa.echo",entry="tool.c",platforms=["any"])),encoding="utf-8")
    seed_personal_defaults(site)


def seed_personal_defaults(site: Path) -> None:
    """Opt-in metadata fixture for component probes which create Agent sessions."""
    from model_fixture import conversation_catalog
    path = site / "default-home/config/defaults.json"
    document = json.loads(path.read_text(encoding="utf-8"))
    if not document["models"]["items"]:
        document["models"] = conversation_catalog()
        path.write_text(json.dumps(document),encoding="utf-8")


def authored_extension(home: Path, kind: str, alias: str, entry: str, content: str) -> None:
    """Install an explicit, owned directory fixture without legacy flat files."""
    prefixes = {"tools":"tool", "agents":"agent", "subagents":"subagent", "commands":"command"}
    folder = home / kind / alias
    folder.mkdir(parents=True, exist_ok=True)
    (folder / "extension.json").write_text(json.dumps(dict(kind=kind,id=prefixes[kind]+":qa."+alias,
        name=alias,alias=alias,entry=entry,platforms=["any"])),encoding="utf-8")
    (folder / entry).write_text(content,encoding="utf-8")


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
        if source == APP / "src/storage/home.c":
            # Component fixtures do not load the Application SDK/plugin manager.
            # Full-product probes copy app/ directly and keep the real overlay.
            boundary = destination / "fixtures/model-plugin-free.h"
            boundary.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(Path(__file__).resolve().parent / "fixtures/model-plugin-free.h", boundary)
            target.write_text('#include "../../fixtures/model-plugin-free.h"\n' +
                              source.read_text(encoding="utf-8"),encoding="utf-8")
        if source.suffix in (".c", ".h"):
            for include in QUOTED_INCLUDE.findall(source.read_text(encoding="utf-8")):
                dependency = (source.parent / include).resolve()
                if dependency.is_relative_to(APP) and dependency.is_file():
                    copy(dependency)

    copy(APP / relative)
