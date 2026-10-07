"""MDO-0 source layout, dependency lock, and unity generation contracts."""

from __future__ import annotations

import importlib.util
import hashlib
import json
import re
import tempfile
import unittest
from unittest.mock import patch
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
SPEC = importlib.util.spec_from_file_location("mdo_build", ROOT / "tools" / "build_mdo.py")
assert SPEC is not None and SPEC.loader is not None
BUILD = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(BUILD)


class BuildContractTests(unittest.TestCase):
    def setUp(self) -> None:
        self.lock = json.loads((ROOT / "deps.lock").read_text(encoding="utf-8"))

    def test_reused_host_requires_matching_profile_revision_icon_and_bytes(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            base = Path(raw)
            host = base / "xs.exe"
            host.write_bytes(b"fixture-host")
            app = base / "app"
            app.mkdir()
            (app / "entry.c").write_text("void f(void) { xrtFree(0); }", encoding="utf-8")
            receipt = {"schema_version": 1, "profile_sha256": BUILD.host_profile_digest(False),
                       "revision": self.lock["xserver"]["commit"],
                       "extensions": self.lock["xserver"]["required_extensions"],
                       "icon_sha256": hashlib.sha256(BUILD.ICON_PATH.read_bytes()).hexdigest(),
                       "binary_sha256": hashlib.sha256(host.read_bytes()).hexdigest(),
                       "xrt_symbols": ["xrtFree"]}
            path = host.with_name("xs.exe.build.json")
            with patch.object(BUILD, "APP", app):
                path.write_text(json.dumps(receipt))
                BUILD.verify_host_receipt(host, self.lock, False, icon=True)
                for key, value in (("profile_sha256", "full"), ("revision", "0000000"),
                                   ("icon_sha256", "0" * 64), ("binary_sha256", "0" * 64),
                                   ("extensions", []), ("xrt_symbols", [])):
                    with self.subTest(key=key):
                        path.write_text(json.dumps({**receipt, key: value}))
                        with self.assertRaises(BUILD.BuildError):
                            BUILD.verify_host_receipt(host, self.lock, False, icon=True)
                path.unlink()
                with self.assertRaises(BUILD.BuildError):
                    BUILD.verify_host_receipt(host, self.lock, False)

    def test_default_host_is_compact_and_full_mode_is_explicit(self) -> None:
        self.assertEqual(BUILD.host_profile_arguments(False), ["--profile", str(BUILD.HOST_PROFILE_PATH)])
        self.assertEqual(BUILD.host_profile_arguments(True), [])
        self.assertEqual(BUILD.host_profile_digest(True), "full")
        self.assertEqual(len(BUILD.host_profile_digest(False)), 64)

    def test_online_models_never_bundle_a_provider_credential(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            source, target = Path(raw) / "connection.json", Path(raw) / "bundled.key"
            with patch.object(BUILD, "BUILTIN_CONNECTION_PATH", source), patch.object(BUILD, "BUILTIN_KEY_PATH", target):
                BUILD.prepare_builtin_credential()
                self.assertFalse(target.exists())
                source.write_text('{"api_key":"fixture-only-key"}', encoding="ascii")
                target.write_bytes(b"stale-provider-key")
                BUILD.prepare_builtin_credential()
                self.assertFalse(target.exists())
                source.write_text('{"api_key":"bad\\nkey"}', encoding="ascii")
                BUILD.prepare_builtin_credential()
                self.assertFalse(target.exists())
                source.unlink()
                BUILD.prepare_builtin_credential()
                self.assertFalse(target.exists())
                BUILD.prepare_builtin_credential(source)
                self.assertFalse(target.exists())

    def test_dependency_lock_is_complete_and_exact(self) -> None:
        self.assertEqual(self.lock["schema_version"], 1)
        self.assertEqual(self.lock["hash_algorithm"],
                         "sha256-path-nul-lf-normalized-content-nul-v1")
        self.assertRegex(self.lock["xrt"]["commit"], r"^[0-9a-f]{40}$")
        self.assertRegex(self.lock["xserver"]["commit"], r"^[0-9a-f]{40}$")
        self.assertEqual(self.lock["xserver"]["required_extensions"],
                         ["xwork", "webview", "image"])
        image = self.lock["xserver"]["native_extensions"]["image"]
        self.assertEqual(image["abi_version"], 1)
        self.assertRegex(image["tree_sha256"], r"^[0-9a-f]{64}$")
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

    def test_native_image_pin_covers_decoder_and_license_bytes(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            (root / "vendor").mkdir(); (root / "src").mkdir()
            (root / "xs-image.c").write_bytes(b"wrapper\n")
            (root / "src/config.h").write_bytes(b"scalar\n")
            (root / "UPSTREAM.json").write_bytes(b"{}\n")
            notice = root / "vendor/LICENSE"
            notice.write_bytes(b"license\n")
            first = BUILD.image_extension_sha256(root)
            notice.write_bytes(b"license\r\n")
            self.assertEqual(first, BUILD.image_extension_sha256(root))
            notice.write_bytes(b"different license\n")
            self.assertNotEqual(first, BUILD.image_extension_sha256(root))
            notice.write_bytes(b"license\n")
            (root / "vendor/decoder.c").write_bytes(b"added decoder\n")
            self.assertNotEqual(first, BUILD.image_extension_sha256(root))

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
            "src/skills/tools.c",
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
            "src/account/client.c",
            "src/account/credential.c",
            "src/account/authorization.c",
            "src/account/session.c",
            "src/remote/websocket.c",
            "src/remote/storage.c",
            "src/remote/service_client.c",
            "src/remote/loopback.c",
            "src/remote/receipts.c",
            "src/remote/bridge.c",
            "src/remote/manager.c",
            "src/web/manager.c",
            "src/mcp/manager.c",
            "src/modules/manager.c",
            "src/operations/manager.c",
            "src/distribution/manager.c",
            "src/agents/runtime.c",
            "src/asks/manager.c",
            "src/projects/manager.c",
            "src/projects/binding.c",
            "src/projects/purge_inventory.c",
            "src/projects/purge.c",
            "src/schedules/manager.c",
            "src/schedules/executor.c",
            "src/sessions/data_gate.c",
            "src/sessions/events.c",
            "src/sessions/sidecars/profile.c",
            "src/sessions/sidecars/binding.c",
            "src/sessions/sidecars/draft.c",
            "src/sessions/sidecars/queue.c",
            "src/sessions/attachments.c",
            "src/sessions/todo.c",
            "src/sessions/manager.c",
            "src/sessions/backup.c",
            "src/sessions/backup_model.c",
            "src/sessions/backup_relations.c",
            "src/sessions/backup_submissions.c",
            "src/sessions/backup_input_payload.c",
            "src/sessions/backup_input_archive.c",
            "src/sessions/backup_origin.c",
            "src/sessions/backup_restore.c",
            "src/sessions/backup_decode.c",
            "src/sessions/backup_replay.c",
            "src/sessions/backup_model_history.c",
            "src/sessions/backup_images.c",
            "src/sessions/backup_stage.c",
            "src/sessions/restore.c",
            "src/approvals/manager.c",
            "src/power/inhibitor.c",
            "src/power/manager.c",
            "src/runs/manager.c",
            "src/update/manager.c",
            "src/update/windows.c",
            "src/bootstrap/bootstrap.c",
            "src/api/http.c",
            "src/api/distribution.c",
            "src/api/update.c",
            "src/api/downloads.c",
            "src/api/image_downloads.c",
            "src/api/backup_upload.c",
            "src/api/backup_preview.c",
            "src/api/backup_restore.c",
            "src/api/write_admission.c",
            "src/api/body.c",
            "src/api/attachments.c",
            "src/api/value.c",
            "src/api/resources.c",
            "src/api/catalogs.c",
            "src/api/extensions.c",
            "src/api/ecosystem.c",
            "src/api/state.c",
            "src/api/workspace_state.c",
            "src/api/pane_layout.c",
            "src/api/inventory.c",
            "src/api/project_purge.c",
            "src/api/project_purge_intent.c",
            "src/api/memory.c",
            "src/api/tasks.c",
            "src/api/approvals.c",
            "src/api/asks.c",
            "src/api/workspace_files.c",
            "src/api/directories.c",
            "src/api/runs.c",
            "src/api/recovery.c",
            "src/api/schedules.c",
            "src/api/diagnostics.c",
            "src/api/migration.c",
            "src/api/events.c",
            "src/api/live.c",
            "src/api/profile.c",
            "src/api/draft.c",
            "src/api/queue.c",
            "src/api/todo.c",
            "src/api/operations.c",
            "src/api/session_capture.c",
            "src/api/sessions.c",
            "src/api/mutations.c",
            "src/api/account.c",
            "src/api/remote.c",
            "src/api/router.c",
            "src/bootstrap/service.c",
        ])
        for path in (ROOT / "app").rglob("*"):
            if path.is_file() and path.suffix.lower() in {".c", ".h", ".json", ".js"}:
                self.assertNotIn("app_bak", path.read_text(encoding="utf-8"))

    def test_source_graph_rejects_orphan_sources_and_private_headers(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            app = Path(raw)
            (app / "src").mkdir()
            (app / "include").mkdir()
            (app / "src/main.c").write_text('#include "helper.inc.c"\n')
            (app / "src/helper.inc.c").write_text('#include "../include/helper.h"\n')
            (app / "include/helper.h").write_text("/* reachable private header */\n")
            with patch.object(BUILD, "APP", app):
                BUILD.validate_source_graph(["src/main.c"])
                for name in ("src/orphan.c", "include/orphan.h"):
                    orphan = app / name
                    orphan.write_text("/* no entry */\n")
                    with self.assertRaisesRegex(BUILD.BuildError, "not reachable"):
                        BUILD.validate_source_graph(["src/main.c"])
                    orphan.unlink()

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
        self.assertEqual(defaults["models"]["default_model"], "ornith-1.5-35b")
        ling = defaults["models"]["items"][0]
        self.assertEqual(ling["id"], "ornith-1.5-35b")
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
