"""Confine unrelated background network traffic to disposable source probes."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def isolate_background_network(site: Path) -> None:
    if not site.resolve().is_relative_to((ROOT / ".build").resolve()):
        raise ValueError("offline injection requires a disposable .build source copy")
    unity = site / "generated/mdo_unity.c"
    prefix = ('#ifndef MDO_DISTRIBUTION_ORIGIN\n'
              '#define MDO_DISTRIBUTION_ORIGIN "http://127.0.0.1:1"\n#endif\n'
              '#ifndef MDO_UPDATE_ORIGIN\n'
              '#define MDO_UPDATE_ORIGIN "http://127.0.0.1:1"\n#endif\n')
    source = unity.read_text(encoding="utf-8")
    if not source.startswith(prefix):
        unity.write_text(prefix + source, encoding="utf-8", newline="\n")
    # Read-only/backup probes do not exercise the independent update checker.
    # Keep its cache writer from racing their byte-for-byte Home assertions;
    # dedicated update contracts still initialize the real manager directly.
    bootstrap = site / "src/bootstrap/bootstrap.c"
    text = bootstrap.read_text(encoding="utf-8")
    text = text.replace('if(!MdoExtensionUpdatesInit())', 'if(false /* offline fixture: independent update checker */)')
    bootstrap.write_text(text, encoding="utf-8", newline="\n")
