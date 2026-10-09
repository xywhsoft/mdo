"""Check the four local API cases and the exact production sources in a pack."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
FILES = ("include/mdo/memory.h", "src/api/inventory.c",
         "src/distribution/manager.c", "src/memory/file_memory.inc.c")


def candidate(exe: Path) -> None:
    spec = importlib.util.spec_from_file_location("passive_home_pack",
        HERE.parent / "decision-write-recovery/check_candidate.py")
    reader = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(reader)
    receipt = json.loads(exe.with_name("build.json").read_text(encoding="utf-8"))
    raw = exe.read_bytes()
    assert receipt["sha256"] == hashlib.sha256(raw).hexdigest()
    assert receipt["bytes"] == len(raw) and not receipt["mdo_worktree_dirty"]
    assert receipt["xs_commit"] == json.loads((ROOT / "deps.lock").read_text())["xserver"]["commit"]
    with tempfile.TemporaryDirectory() as directory:
        archive = Path(directory) / "app.xrtpack"
        reader.extract_pack(exe, archive)
        files = reader.files_from_pack(archive.read_bytes())
    sources = {}
    for path in FILES:
        expected = (ROOT / "app" / path).read_bytes()
        assert files[path] == expected, path
        sources[path] = {"sha256": hashlib.sha256(expected).hexdigest(), "matches": True}
    data = {key: receipt[key] for key in ("mdo_commit", "xs_commit", "sha256",
        "bytes", "mdo_worktree_dirty", "packaged_app_sha256")}
    data.update(sources=sources, passed=True)
    (HERE / "candidate.json").write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path)
    args = parser.parse_args()
    if args.exe:
        candidate(args.exe.resolve())
    native = json.loads((HERE / "native.json").read_text())
    assert [item["mode"] for item in native] == ["fresh", "browser-cache", "inside-source", "user-data"]
    for item in native:
        assert item["catalog_revision"] == 2 and item["source_file_count"] == 1
        assert item["source_valid"] and item["source_bytes_unchanged"]
        assert not item["memory_directory_created"]
        assert item["catalog_persisted"] == (item["mode"] == "user-data")
        assert item["importable"] == (item["mode"] in ("fresh", "browser-cache"))
    assert json.loads((HERE / "candidate.json").read_text())["passed"]
    print("PASS four real local publication/memory/migration API cases and four packed production files")


if __name__ == "__main__":
    main()
