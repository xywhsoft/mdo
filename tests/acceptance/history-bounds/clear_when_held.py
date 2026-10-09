"""Clear only the disposable fixture after its old native history body is held."""
import argparse
import json
from pathlib import Path
import sys
import time

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tests"))
from test_api_runtime import request


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    args = parser.parse_args()
    base = args.directory.resolve()
    assert base.is_relative_to(ROOT / ".build/conversation-acceptance/history-bounds")
    state = json.loads((base / "state.json").read_text(encoding="utf-8"))
    path = "/api/v1/projects/default/sessions/" + state["session"]
    evidence = Path(__file__).parent
    assert not (base / "held-body.json").exists(), "use a fresh fixture"
    (base / "hold-before").touch()
    deadline = time.monotonic() + 30
    while not (base / "held-body.json").exists():
        assert time.monotonic() < deadline, "no native history response was held"
        time.sleep(.02)
    status, headers, body = request(state["upstream_port"], "GET", path)
    assert status == 200
    (evidence / "session-before.json").write_bytes(body)
    status, _, body = request(state["upstream_port"], "POST", path + "/clear",
                             headers={"If-Match": headers["etag"]})
    assert status == 200, (status, body)
    (evidence / "clear-response.json").write_bytes(body)
    (evidence / "held-body.json").write_bytes((base / "held-body.json").read_bytes())
    print("Cleared disposable history while its prior native response was held", flush=True)


if __name__ == "__main__":
    main()
