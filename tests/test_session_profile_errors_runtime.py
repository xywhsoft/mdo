"""Bounded real packed profile rejection and recovery, using a loopback model.

--interactive retains the seeded page for ordinary browser actions. A restrict
file changes the alternative's supported reasoning after the page is loaded;
healthy restores it. Neither mode patches the pack or the native API.
"""
from __future__ import annotations

import argparse
from collections import Counter
import hashlib
from http.server import ThreadingHTTPServer
import json
import os
from pathlib import Path
import tempfile
import threading
import time

from manual_session_history_qa import Model, Proxy
from test_api_runtime import request, session_events
from test_interrupt_runtime import until
from test_packed_home_lease import site, start, stop, wait_bootstrap
from test_packed_queue_start_recovery import configure_model


ROOT = Path(__file__).resolve().parents[1]


def check(packed: Path, base: Path, interactive: bool = False) -> dict:
    native, port = site(base, "native", packed)
    home = base / "home"
    model = ThreadingHTTPServer(("127.0.0.1", 0), Model)
    Model.calls, Model.requests = 0, []
    model_worker = threading.Thread(target=model.serve_forever, daemon=True)
    model_worker.start()
    process = start(native, packed, home, dict(os.environ, USE_WEBVIEW="0",
        USERPROFILE=str(base), MDO_QUEUE_FIXTURE_KEY="fixture-only"))
    proxy = worker = None
    rejections = []

    def call(method, path, body=None, expected=200, tag=None):
        status, headers, raw = request(port, method, "/api/v1" + path,
            body=json.dumps(body).encode() if body is not None else None,
            headers={"Content-Type": "application/json", **({"If-Match": tag} if tag else {})})
        document = json.loads(raw)
        assert status == expected, (status, document)
        return document.get("data", document), headers

    def reject(method, path, body, code, tag=None):
        document, _ = call(method, path, body, 422, tag)
        assert document["error"]["code"] == code, document
        rejections.append({"method": method, "path": path, "body": body,
            "status": 422, "error": document["error"]})

    def reasoning_options(efforts, model_id="profile-alternative"):
        config, headers = call("GET", "/models/config")
        config.pop("runtime_override", None)
        alternative = next(item for item in config["items"] if item["id"] == model_id)
        alternative.update(reasoning_efforts=efforts, default_reasoning_effort=efforts[0])
        call("PUT", "/settings/models", {"schema_version": 1, "patch": config}, tag=headers["etag"])

    try:
        status, bootstrap = wait_bootstrap(process, port, native / "packed.log")
        assert status == 200 and bootstrap["data"]["ready"], bootstrap
        configure_model(port, f"http://127.0.0.1:{model.server_port}/v1")
        config, headers = call("GET", "/models/config")
        config.pop("runtime_override", None)
        alternative = dict(next(item for item in config["items"] if item["id"] == "queue-fixture"))
        alternative.update(id="profile-alternative", name="Profile alternative", wire_model="profile-alternative")
        config["items"].append(alternative)
        call("PUT", "/settings/models", {"schema_version": 1, "patch": config}, tag=headers["etag"])
        initial_list, _ = call("GET", "/sessions")
        valid = {"project_id": "default", "model_id": "queue-fixture",
            "reasoning_effort": "none", "permission_profile": "balanced", "workspace_root": str(base)}
        for patch, kind in [({"model_id": "missing-profile-model"}, "model_missing"),
            ({"protocol": "openai-responses"}, "protocol_unsupported"),
            ({"reasoning_effort": "unsupported"}, "reasoning_unsupported"),
            ({"max_output_tokens": 4294967295}, "output_limit")]:
            reject("POST", "/sessions", {**valid, **patch}, "model_profile_" + kind)
        after_list, _ = call("GET", "/sessions")
        assert after_list["items"] == initial_list["items"], after_list
        source, _ = call("POST", "/sessions", {**valid, "title": "Profile errors source"}, 201)
        path = "/projects/default/sessions/" + source["id"]
        run, _ = call("POST", path + "/runs", {"prompt": "Seed the profile error check."}, 202)
        result = until(lambda: (value if (value := call("GET", "/runs/" + run["id"])[0])["terminal"] else None), seconds=15)
        assert result["state"] == "succeeded", result
        original, tag = call("GET", path)
        snapshot = home / "sessions/default" / source["id"] / "snapshot.json"
        before = hashlib.sha256(snapshot.read_bytes()).hexdigest()
        profile = {key: original[key] for key in ("model_id", "reasoning_effort", "permission_profile")}
        for patch, kind in [({"model_id": "missing-profile-model"}, "model_missing"),
            ({"reasoning_effort": "unsupported"}, "reasoning_unsupported")]:
            reject("PUT", path + "/profile", {**profile, **patch}, "model_profile_" + kind, tag["etag"])
            current, current_tag = call("GET", path)
            assert current == original and current_tag["etag"] == tag["etag"], current
            assert hashlib.sha256(snapshot.read_bytes()).hexdigest() == before
        # Queue-bound choices must fail before receipt/history admission.
        queue = path + "/queue"
        queue_id = "a" * 32
        call("POST", queue, {"id": queue_id, "text": "Missing model must not start",
            "stage": True, "first": False, "profile": {**profile, "model_id": "missing-profile-model"}}, 201)
        for state in ("pending", "sending"):
            call("PUT", queue + "/" + queue_id, {"state": state})
        reject("POST", path + "/runs", {"prompt": "Missing model must not start", "queue_item_id": queue_id},
            "model_profile_model_missing")
        assert not (snapshot.parent / "queue-receipts" / (queue_id + ".json")).exists()
        assert hashlib.sha256(snapshot.read_bytes()).hexdigest() == before
        current, _ = call("GET", path)
        assert current == original and Model.calls == 1, (current, Model.calls)
        call("DELETE", queue + "/" + queue_id)
        rejected_snapshot_unchanged = True

        if interactive:
            def before_profile(raw):
                if not (base / "reject-next").exists() or (base / "rejection-prepared").exists():
                    return
                desired = json.loads(raw)
                reasoning_options(["low" if desired["reasoning_effort"] != "low" else "high"], desired["model_id"])
                (base / "rejection-prepared").touch()
            Proxy.before_profile = staticmethod(before_profile)
            Proxy.base, Proxy.native_port, Proxy.mode = base, port, "profile_reject"
            Proxy.session_path = "/api/v1" + path
            Proxy.started, Proxy.fork_id = time.monotonic(), ""
            Proxy.proof, Proxy.attempts = [], Counter()
            Proxy.stopping.clear()
            proxy = ThreadingHTTPServer(("127.0.0.1", 0), Proxy)
            worker = threading.Thread(target=proxy.serve_forever, daemon=True)
            worker.start()
            print(json.dumps({"url": f"http://127.0.0.1:{proxy.server_port}/#/projects/default/sessions/{source['id']}",
                "native_port": port, "directory": str(base), "source_id": source["id"]}), flush=True)
            restricted = restored = False
            end = time.monotonic() + 540
            while not (base / "stop").exists() and time.monotonic() < end:
                assert process.poll() is None, "Native candidate exited"
                if (base / "restrict").exists() and not restricted:
                    reasoning_options(["low"])
                    restricted = True
                    (base / "restricted").touch()
                if (base / "rejection-prepared").exists():
                    restricted = True
                if (base / "healthy").exists() and restricted and not restored:
                    reasoning_options(["none", "low", "medium", "high"])
                    restored = True
                    (base / "restored").touch()
                time.sleep(.1)
        else:
            confirmed, _ = call("PUT", path + "/profile", {**profile, "model_id": "profile-alternative",
                "reasoning_effort": "medium"}, tag=tag["etag"])
            assert confirmed["revision"] == original["revision"] + 1
            assert hashlib.sha256(snapshot.read_bytes()).hexdigest() == before
            run, _ = call("POST", path + "/runs", {"prompt": "Continue after rejected configuration."}, 202)
            result = until(lambda: (value if (value := call("GET", "/runs/" + run["id"])[0])["terminal"] else None), seconds=15)
            assert result["state"] == "succeeded", result
            assert Model.calls == 2 and Model.requests == ["ornith-1.5-35b", "profile-alternative"]
        events = session_events(port, "/api/v1" + path)
        proof = {"source_id": source["id"], "rejections": rejections,
            "rejected_snapshot_unchanged": rejected_snapshot_unchanged,
            "metadata_before": original, "metadata_after": call("GET", path)[0],
            "model_calls": Model.calls, "model_requests": Model.requests,
            "final_errors": sum(e["kind"] == "error" for e in events),
            "event_kinds": dict(Counter(e["kind"] for e in events)),
            "transport": list(Proxy.proof) if interactive else []}
        (base / "proof.json").write_text(json.dumps(proof, indent=2) + "\n", encoding="utf-8")
        return proof
    finally:
        Proxy.stopping.set()
        if proxy:
            proxy.shutdown(); proxy.server_close(); worker.join(timeout=3)
        stop(process)
        model.shutdown(); model.server_close(); model_worker.join(timeout=3)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--packed", required=True, type=Path)
    parser.add_argument("--directory", type=Path)
    parser.add_argument("--interactive", action="store_true")
    args = parser.parse_args()
    if args.directory:
        base = args.directory.resolve()
        assert not base.exists(), "Choose a new isolated directory"
        base.mkdir(parents=True)
        proof = check(args.packed.resolve(), base, args.interactive)
    else:
        assert not args.interactive, "Interactive evidence needs --directory"
        with tempfile.TemporaryDirectory(prefix="profile-errors-", dir=ROOT / ".build") as raw:
            proof = check(args.packed.resolve(), Path(raw))
    print(json.dumps({"profile_rejections": len(proof["rejections"]), "model_calls": proof["model_calls"],
        "final_errors": proof["final_errors"], "result": "PASS"}), flush=True)


if __name__ == "__main__":
    main()
