"""Compile both real client/server ZIP validators in place; tiny archives only."""
import argparse
import copy
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import time
import zipfile
from test_api_runtime import ROOT, free_port
from test_extension_settings_runtime import FORM, KINDS

SERVER = Path("D:/GIT/home/host/xywhsoft_ai/plugin/mdo/modules/ecosystem")


def expand(path):
    source = path.read_text(encoding="utf-8")
    return re.sub(r'^#include "([^"\n]+)"\s*$', lambda m: expand(path.parent / m[1]) if (path.parent / m[1]).is_file() else m[0], source, flags=re.M)


def run(host):
    app = ROOT / "app"
    with tempfile.TemporaryDirectory(prefix="settings-archives-", dir=ROOT / ".build") as raw:
        base = Path(raw); cases = []
        manifest = dict(kind="skills", id="skill:qa.form", name="Fixture", alias="qa-form", entry="entry.txt", platforms=["any"], settings=dict(form="settings.form.json", templates=["role.md"], environment={"SERVICE_API_KEY": "api_key"}))

        def case(name, change=lambda m, f, files: None, valid=False, kind=None):
            m = copy.deepcopy(manifest); f = copy.deepcopy(FORM); files = {"entry.txt": "Fixture", "role.md": "Name: {{$settings.character_name}}"}
            if kind: m.update(kind=kind, id=KINDS.get(kind, "local-model") + ":qa.form")
            change(m, f, files)
            files["extension.json"] = json.dumps(m)
            if f is not None: files["settings.form.json"] = json.dumps(f)
            path = base / (name + ".zip")
            with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as archive:
                for key, text in files.items(): archive.writestr(key, text)
            cases.append((name, valid))

        for kind in KINDS: case("valid-" + kind, valid=True, kind=kind)
        case("valid-local-models", valid=True, kind="local-models")
        case("missing-form", lambda m, f, files: m["settings"].update(form="missing.json"))
        case("outside-form", lambda m, f, files: m["settings"].update(form="../secret.json"))
        case("code-template", lambda m, f, files: m["settings"].update(templates=["program.c"]))
        case("missing-template", lambda m, f, files: files.pop("role.md"))
        case("template-oversize", lambda m, f, files: files.update({"role.md": "x" * 131073}))
        case("duplicate-template", lambda m, f, files: m["settings"].update(templates=["role.md", "role.md"]))
        case("unknown-env-field", lambda m, f, files: m["settings"].update(environment={"SERVICE_API_KEY": "missing"}))
        case("bad-env-name", lambda m, f, files: m["settings"].update(environment={"A=B": "api_key"}))
        case("secret-default", lambda m, f, files: f["groups"][0]["fields"][1].update(default="must-reject"))
        case("secret-prompt", lambda m, f, files: f["groups"][0]["fields"][1].update(expose_to_prompt=True))
        case("duplicate-field", lambda m, f, files: f["groups"][0]["fields"].append(f["groups"][0]["fields"][0]))
        case("invalid-default", lambda m, f, files: f["groups"][0]["fields"][0].update(default="https://"))
        case("invalid-lengths", lambda m, f, files: f["groups"][0]["fields"][2].update(min_length=5, max_length=2))
        for side, directory in [("client", ROOT / "app/include/mdo"), ("server", SERVER)]:
            probe = app / (".qa-" + base.name + "-" + side + ".c"); config = probe.with_suffix(".json")
            calls = []
            for name, valid in cases:
                expected = valid and (name != "valid-local-models" or side == "server")
                calls.append(f'Check({json.dumps(str(base / (name + ".zip")))},{str(expected).lower()},{json.dumps(name)});')
            source = expand(directory / "extension_manifest.h") + r'''
static unsigned Failed,Count;
static void Check(cstr Path,bool Expected,cstr Name){
 xfile F=xrtOpen(Path,XFILE_READ);MdoZip Z={0};bool Open=F&&MdoZipOpen(&Z,F);xvalue* M=Open?MdoExtensionZipManifest(&Z,NULL):NULL;
 if((M!=NULL)!=Expected){++Failed;printf("FAIL %s: %s\n",Name,Z.Error);}++Count;
 xrtValueRelease(M);MdoZipClose(&Z);xrtClose(F);
}
void ServiceInit(XS_HostInfo* Host){
''' + "\n".join(calls) + '\nprintf("archives_checked=%u\\narchives_failed=%u\\nprobe_done=1\\n",Count,Failed);fflush(stdout);}\n'
            probe.write_text(source, encoding="utf-8")
            config.write_text(json.dumps(dict(engine=dict(workers=1), services=[dict(enabled=True, name="settings-archive", **{"class": "http"}, ip="127.0.0.1", port=free_port(), host_default=dict(enabled=True, path="web", devlang="c", devfile=probe.name))])), encoding="utf-8")
            process = None
            try:
                log = base / (side + ".log")
                with log.open("wb") as output:
                    process = subprocess.Popen([str(host), str(config)], cwd=app, env={**os.environ, "USE_WEBVIEW": "0"}, stdout=output, stderr=output, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
                deadline = time.monotonic() + 60
                while process.poll() is None and time.monotonic() < deadline:
                    if "probe_done=1" in log.read_text(encoding="utf-8", errors="replace"): break
                    time.sleep(.1)
                text = log.read_text(encoding="utf-8", errors="replace")
                assert "archives_failed=0" in text and "probe_done=1" in text, text[-5000:]
                print(side + ": " + "; ".join(line for line in text.splitlines() if line.startswith("archives_")))
            finally:
                if process and process.poll() is None: process.terminate(); process.wait(timeout=10)
                probe.unlink(missing_ok=True); config.unlink(missing_ok=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__); parser.add_argument("--host", type=Path, default=ROOT / ".build/host/xs.exe")
    run(parser.parse_args().host.resolve())
