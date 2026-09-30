"""MDO-0 source layout, dependency lock, and unity generation contracts."""

from __future__ import annotations

import importlib.util
import json
import re
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
SPEC = importlib.util.spec_from_file_location("mdo_build", ROOT / "tools" / "build_mdo.py")
assert SPEC is not None and SPEC.loader is not None
BUILD = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(BUILD)


class BuildContractTests(unittest.TestCase):
    def setUp(self) -> None:
        self.lock = json.loads((ROOT / "deps.lock").read_text(encoding="utf-8"))

    def test_dependency_lock_is_complete_and_exact(self) -> None:
        self.assertEqual(self.lock["schema_version"], 1)
        self.assertEqual(self.lock["hash_algorithm"],
                         "sha256-path-nul-lf-normalized-content-nul-v1")
        self.assertRegex(self.lock["xrt"]["commit"], r"^[0-9a-f]{40}$")
        self.assertRegex(self.lock["xserver"]["commit"], r"^[0-9a-f]{40}$")
        self.assertEqual(self.lock["xserver"]["required_extensions"],
                         ["xwork", "webview"])
        for name in ("xllm", "xllm-session", "xwork"):
            record = self.lock["libraries"][name]
            self.assertRegex(record["source_commit"], r"^[0-9a-f]{40}$")
            self.assertRegex(record["production_tree_sha256"], r"^[0-9a-f]{64}$")
        self.assertEqual(self.lock["pack"], {"format": "XRTPACK", "version": 1})
        self.assertEqual(self.lock["mdo"], {
            "module_abi_version": 1,
            "session_schema_version": 3,
            "config_schema_version": 1,
        })

    def test_dependency_source_hash_is_checkout_line_ending_independent(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            lf = root / "lf.h"
            crlf = root / "crlf.h"
            lf.write_bytes(b"one\ntwo\n")
            crlf.write_bytes(b"one\r\ntwo\r\n")
            self.assertEqual(BUILD.source_file_sha256(lf),
                             BUILD.source_file_sha256(crlf))

    def test_new_source_root_is_independent_from_legacy_app(self) -> None:
        sources = BUILD.source_list()
        self.assertEqual(sources, [
            "src/storage/home.c",
            "src/projects/lifecycle.c",
            "src/config/config.c",
            "src/config/service.c",
            "src/security/secrets.c",
            "src/models/catalog.c",
            "src/skills/manager.c",
            "src/memory/manager.c",
            "src/memory/transfer.c",
            "src/migration/manager.c",
            "src/migration/common.c",
            "src/migration/config_projects.c",
            "src/migration/sessions.c",
            "src/migration/memory.c",
            "src/migration/schedules.c",
            "src/migration/report.c",
            "src/migration/apply.c",
            "src/web/manager.c",
            "src/mcp/manager.c",
            "src/modules/manager.c",
            "src/operations/manager.c",
            "src/agents/runtime.c",
            "src/asks/manager.c",
            "src/projects/manager.c",
            "src/projects/purge_inventory.c",
            "src/projects/purge.c",
            "src/schedules/manager.c",
            "src/schedules/executor.c",
            "src/sessions/events.c",
            "src/sessions/attachments.c",
            "src/sessions/todo.c",
            "src/sessions/manager.c",
            "src/approvals/manager.c",
            "src/power/inhibitor.c",
            "src/power/manager.c",
            "src/runs/manager.c",
            "src/bootstrap/bootstrap.c",
            "src/api/http.c",
            "src/api/body.c",
            "src/api/attachments.c",
            "src/api/value.c",
            "src/api/resources.c",
            "src/api/catalogs.c",
            "src/api/state.c",
            "src/api/workspace_state.c",
            "src/api/pane_layout.c",
            "src/api/inventory.c",
            "src/api/project_purge.c",
            "src/api/memory.c",
            "src/api/tasks.c",
            "src/api/approvals.c",
            "src/api/asks.c",
            "src/api/workspace_files.c",
            "src/api/runs.c",
            "src/api/recovery.c",
            "src/api/schedules.c",
            "src/api/diagnostics.c",
            "src/api/migration.c",
            "src/api/events.c",
            "src/api/feedback.c",
            "src/api/profile.c",
            "src/api/draft.c",
            "src/api/queue.c",
            "src/api/todo.c",
            "src/api/operations.c",
            "src/api/sessions.c",
            "src/api/mutations.c",
            "src/api/router.c",
            "src/bootstrap/service.c",
        ])
        for path in (ROOT / "app").rglob("*"):
            if path.is_file() and path.suffix.lower() in {".c", ".h", ".json", ".js"}:
                self.assertNotIn("app_bak", path.read_text(encoding="utf-8"))
        self.assertTrue((ROOT / "app_bak" / "main.c").is_file())

    def test_unity_generation_is_manifest_ordered_and_deterministic(self) -> None:
        first = BUILD.generated_unity(self.lock, BUILD.source_list())
        second = BUILD.generated_unity(self.lock, BUILD.source_list())
        self.assertEqual(first, second)
        self.assertEqual(first.count('#include "../src/storage/home.c"'), 1)
        self.assertEqual(first.count('#include "../src/security/secrets.c"'), 1)
        self.assertEqual(first.count('#include "../src/models/catalog.c"'), 1)
        self.assertEqual(first.count('#include "../src/bootstrap/bootstrap.c"'), 1)
        self.assertEqual(first.count('#include "../src/modules/manager.c"'), 1)
        self.assertEqual(first.count('#include "../src/skills/manager.c"'), 1)
        self.assertEqual(first.count('#include "../src/schedules/manager.c"'), 1)
        self.assertEqual(first.count('#include "../src/schedules/executor.c"'), 1)
        self.assertEqual(first.count('#include "../src/runs/manager.c"'), 1)
        self.assertEqual(first.count('#include "../src/approvals/manager.c"'), 1)
        self.assertEqual(first.count('#include "../src/asks/manager.c"'), 1)
        self.assertEqual(first.count('#include "../src/api/approvals.c"'), 1)
        self.assertEqual(first.count('#include "../src/api/asks.c"'), 1)
        self.assertEqual(first.count('#include "../src/api/workspace_files.c"'), 1)
        self.assertEqual(first.count('#include "../src/api/router.c"'), 1)
        self.assertEqual(first.count('#include "../src/bootstrap/service.c"'), 1)
        self.assertIn(self.lock["xrt"]["commit"], first)
        self.assertIn(self.lock["xserver"]["commit"], first)

    def test_module_sdk_header_is_generated_byte_for_byte(self) -> None:
        BUILD.prepare(self.lock)
        self.assertEqual(BUILD.GENERATED_MODULE_HEADER_PATH.read_bytes(),
                         BUILD.MODULE_HEADER_PATH.read_bytes())

    def test_dev_and_pack_configs_use_the_same_generated_entry(self) -> None:
        dev = json.loads((ROOT / "dev.json").read_text(encoding="utf-8"))
        packed = json.loads((ROOT / "app" / "xs.json").read_text(encoding="utf-8"))
        dev_host = dev["services"][0]["host_default"]
        packed_host = packed["services"][0]["host_default"]
        self.assertEqual(dev_host["devfile"], "app/" + packed_host["devfile"])
        self.assertEqual(dev_host["path"], "app/" + packed_host["path"])
        self.assertEqual(packed_host["devfile"], "generated/mdo_unity.c")

    def test_built_in_home_and_web_roots_are_explicit(self) -> None:
        home = ROOT / "app" / "default-home"
        for relative in ("config/defaults.json", "modules/README.md",
                         "skills/README.md", "mcp/README.md"):
            self.assertTrue((home / relative).is_file(), relative)
        defaults = json.loads((home / "config" / "defaults.json").read_text(encoding="utf-8"))
        self.assertEqual(defaults["schema_version"], 1)
        self.assertEqual(defaults["models"]["default_model"], "ling-3.0-tiny")
        ling = defaults["models"]["items"][0]
        self.assertEqual(ling["id"], "ling-3.0-tiny")
        self.assertFalse(ling["editable"])
        self.assertTrue((ROOT / "app" / "web" / "index.html").is_file())
        self.assertIn("mdo-home/", (ROOT / ".gitignore").read_text(encoding="utf-8"))
        self.assertIn("app/generated/", (ROOT / ".gitignore").read_text(encoding="utf-8"))

    def test_version_header_matches_locked_mdo_schemas(self) -> None:
        header = (ROOT / "app" / "include" / "mdo" / "version.h").read_text(
            encoding="utf-8")
        names = {
            "MDO_CONFIG_SCHEMA_VERSION": "config_schema_version",
            "MDO_SESSION_SCHEMA_VERSION": "session_schema_version",
            "MDO_MODULE_ABI_VERSION": "module_abi_version",
        }
        for macro_name, lock_name in names.items():
            match = re.search(rf"#define\s+{macro_name}\s+(\d+)u", header)
            self.assertIsNotNone(match, macro_name)
            self.assertEqual(int(match.group(1)), self.lock["mdo"][lock_name])


if __name__ == "__main__":
    unittest.main(verbosity=2)
