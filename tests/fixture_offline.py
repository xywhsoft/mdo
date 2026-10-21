"""Confine unrelated background network traffic to disposable source probes."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def isolate_background_network(site: Path) -> None:
    if not site.resolve().is_relative_to((ROOT / ".build").resolve()):
        raise ValueError("offline injection requires a disposable .build source copy")
    unity = site / "generated/mdo_unity.c"
    unity.write_text('#define MDO_DISTRIBUTION_ORIGIN "http://127.0.0.1:1"\n'
                     '#define MDO_UPDATE_ORIGIN "http://127.0.0.1:1"\n' +
                     unity.read_text(encoding="utf-8"), encoding="utf-8", newline="\n")
