"""Check native boundary observations and ten exact production files in a pack."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
FILES = ("src/api/state.c", "src/api/mutations.c", "web/js/state/settings.js",
         "web/js/state/settings-write.js", "web/js/features/settings/settings-autosave.js",
         "web/js/features/settings/settings-view.js", "web/js/utils/dom.js",
         "web/lang/zh-CN.json", "web/lang/en-US.json", "web/lang/ru-RU.json")


def candidate(exe: Path) -> None:
    spec = importlib.util.spec_from_file_location("settings_pack_reader",
        HERE.parent / "decision-write-recovery/check_candidate.py")
    reader = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(reader)
    receipt = json.loads(exe.with_name("build.json").read_text(encoding="utf-8"))
    raw = exe.read_bytes()
    assert receipt["sha256"] == hashlib.sha256(raw).hexdigest()
    assert receipt["bytes"] == len(raw) and not receipt["mdo_worktree_dirty"]
    assert receipt["xs_commit"] == json.loads((ROOT / "deps.lock").read_text(encoding="utf-8"))["xserver"]["commit"]
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
    native = json.loads((HERE / "native.json").read_text(encoding="utf-8"))
    assert native == {"snapshot_boundary": 425, "preview_boundary": 425,
        "matching_patch_changes": False, "replacement_credential_changes": True,
        "cleared_credential_changes": True, "credential_reference_exposed": False,
        "reset_user_patch": False}
    ui = json.loads((HERE / "clean-ui.json").read_text(encoding="utf-8"))
    for start, end in (("before_patch", "after_patch"), ("before_reset", "after_reset")):
        assert ui[end]["revision"] == ui[start]["revision"] + 1
        assert ui[end]["transactions"] == ui[start]["transactions"] + 1
    assert ui["after_patch"]["theme"] == "dark" and ui["patch_feedback"] == "设置已保存。"
    assert ui["after_reset"]["user_patch"] is False and ui["reset_feedback"] == "设置已保存。"
    assert ui["after_reset"]["theme"] == "system" and ui["after_reset"]["locale"] == "auto"
    assert ui["model_calls"] == 0
    assert json.loads((HERE / "candidate.json").read_text(encoding="utf-8"))["passed"]
    print("PASS native boundaries, clean-pack lost PATCH/DELETE acknowledgements, and ten packed production files")


if __name__ == "__main__":
    main()
