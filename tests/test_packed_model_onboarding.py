"""Empty startup, retired product entries, personal configuration and removal."""
import argparse
import copy
import json
import os
from pathlib import Path
import tempfile

from model_fixture import model_catalog
from test_api_runtime import request
from test_packed_home_lease import ROOT, site, start, stop, wait_bootstrap

def check(packed):
    with tempfile.TemporaryDirectory(prefix="model-onboarding-", dir=ROOT / ".build") as raw:
        base = Path(raw)
        for name in ("fresh", "retired", "personal"):
            native, port = site(base, name, packed); home = base / (name + "-home")
            prior = None
            if name != "fresh":
                old = model_catalog("ornith-1.5-35b")
                old["providers"][0].update(id="ornith", builtin=True, editable=False, removable=False)
                old["providers"][0]["endpoints"] = {"chat_completions": "builtin:ornith/openai/chat-completions"}
                old["items"][0].update(provider="ornith", builtin=True, editable=False, removable=False)
                if name == "personal":
                    personal = model_catalog("personal-model")
                    old["providers"] += personal["providers"]; old["items"] += personal["items"]
                    old["default_model"] = "personal-model"
                path = home / "config/models.json"; path.parent.mkdir(parents=True)
                prior = json.dumps({"schema_version": 1, "patch": old}).encode(); path.write_bytes(prior)
            process = start(native, packed, home, dict(os.environ, USE_WEBVIEW="0"))
            def call(method, path, body=None, headers=None):
                code, head, data = request(port, method, "/api/v1" + path,
                    body=None if body is None else json.dumps(body).encode(),
                    headers={"Content-Type": "application/json", **(headers or {})})
                assert code == 200, (path, code, data[:500])
                return json.loads(data)["data"], head
            try:
                assert wait_bootstrap(process, port, native / "packed.log")[1]["data"]["ready"]
                catalog, _ = call("GET", "/models")
                assert not any(m["id"] == "ornith-1.5-35b" for m in catalog["models"]), catalog
                assert len(catalog["models"]) == (1 if name == "personal" else 0), catalog
                if prior: assert (home / "config/models.json").read_bytes() == prior
                config, header = call("GET", "/models/config")
                if prior:
                    code, _, data = request(port, "PUT", "/api/v1/settings/models", body=prior,
                        headers={"Content-Type": "application/json", "If-Match": header["etag"]})
                    assert code == 422, (code, data)
                fixture = model_catalog("user-model")
                call("PUT", "/settings/models", {"schema_version": 1, "patch": fixture}, {"If-Match": header["etag"]})
                assert call("GET", "/models")[0]["default_model_id"] == "user-model"
                config, header = call("GET", "/models/config")
                call("PUT", "/settings/models", {"schema_version": 1, "patch": {
                    "default_model": "", "items": [], "providers": []}}, {"If-Match": header["etag"]})
                assert call("GET", "/models")[0]["models"] == []
            finally: stop(process)
    return {"fresh_start": True, "retired_builtin_removed": True, "personal_connection_preserved": True,
            "load_does_not_rewrite_user_file": True, "first_connection_and_last_removal": True, "real_model_calls": 0}

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__); parser.add_argument("--packed", type=Path, required=True)
    args = parser.parse_args(); print(json.dumps(check(args.packed.resolve()), indent=2))
