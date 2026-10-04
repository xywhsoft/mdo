"""The real packed default Home publishes useful modules, not ABI test tools."""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import tempfile

from test_packed_home_lease import (
    ROOT, release_packed_copies, request, site, start, stop, wait_bootstrap,
)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--packed", type=Path,
                        default=ROOT / ("mdo.exe" if os.name == "nt" else "mdo"))
    args = parser.parse_args()
    packed = args.packed.resolve()
    with tempfile.TemporaryDirectory(prefix="module-packed-", dir=ROOT / ".build") as raw:
        base = Path(raw)
        directory, port = site(base, "site", packed)
        process = start(directory, packed, base / "home", dict(os.environ, USE_WEBVIEW="0"))
        try:
            bootstrap = wait_bootstrap(process, port, directory / "packed.log")[1]
            assert bootstrap["data"]["ready"], bootstrap
            status, envelope = request(port, "GET", "/api/v1/modules")
            assert status == 200, envelope
            data = envelope["data"]
            tools = {tool["id"] for tool in data["tools"]}
            assert "mdo.echo" not in tools, "test module leaked into the production catalog"
            assert "mdo.todo" in tools, data
            status, envelope = request(port, "GET", "/api/v1/agents")
            assert status == 200, envelope
            agents = {agent["id"] for agent in envelope["data"]["items"]}
            assert "mdo.default" in agents, envelope
            print("PASS packed default Agent/todo catalog; no Echo test tool")
        finally:
            stop(process)
            release_packed_copies(directory / packed.name)


if __name__ == "__main__":
    main()
