"""Static contracts for durable MDO session management."""

from __future__ import annotations

import json
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent


class SessionContractTests(unittest.TestCase):
    def test_session_manager_is_after_agent_runtime_in_bootstrap(self) -> None:
        manifest = json.loads((ROOT / "app/sources.json").read_text(encoding="utf-8"))
        sources = manifest["sources"]
        self.assertIn("src/sessions/manager.c", sources)
        self.assertLess(sources.index("src/agents/runtime.c"), sources.index("src/sessions/manager.c"))
        bootstrap = (ROOT / "app/src/bootstrap/bootstrap.c").read_text(encoding="utf-8")
        self.assertIn("MdoSessionManagerInit(g_MdoBootstrap.Runtime)", bootstrap)
        self.assertLess(bootstrap.index("MdoModuleManagerInit"), bootstrap.index("MdoSessionManagerInit"))

    def test_managed_layout_uses_one_readable_meta_and_xllm_ledger(self) -> None:
        source = (ROOT / "app/src/sessions/manager.c").read_text(encoding="utf-8")
        for name in ("meta.json", "snapshot.json", "journal.jsonl", "artifacts"):
            self.assertIn(name, source)
        self.assertIn("MdoHomeAtomicWrite(Path, Json, Size, true)", source)
        self.assertIn("xllmSessionRecover", (ROOT / "app/src/agents/runtime.c").read_text(encoding="utf-8"))
        self.assertNotIn("api_key", source.lower())

    def test_catalog_is_owned_and_trash_is_reversible(self) -> None:
        header = (ROOT / "app/include/mdo/sessions.h").read_text(encoding="utf-8")
        for symbol in (
            "MdoSessionCatalogSnapshot",
            "MdoSessionCatalogRelease",
            "MdoSessionLoad",
            "MdoSessionSetArchived",
            "MdoSessionMoveToTrash",
            "MdoSessionRestore",
            "MdoSessionDiagnostic",
        ):
            self.assertIn(symbol, header)
        self.assertIn("MDO_SESSION_TRASH", header)
        self.assertIn("PreviousStatus", header)

    def test_ui_event_replay_is_bounded_and_separate_from_model_ledger(self) -> None:
        header = (ROOT / "app/include/mdo/sessions.h").read_text(encoding="utf-8")
        events = (ROOT / "app/src/sessions/events.c").read_text(encoding="utf-8")
        manager = (ROOT / "app/src/sessions/manager.c").read_text(encoding="utf-8")
        for symbol in (
            "MdoSessionEventReplay",
            "MdoSessionEventSnapshotRelease",
            "MdoSessionEventSnapshotNextCursor",
            "MdoSessionEventSnapshotHistoryLost",
        ):
            self.assertIn(symbol, header)
        self.assertIn("ui-events.jsonl", events)
        self.assertIn("MDO_SESSION_EVENT_FILE_LIMIT", events)
        self.assertIn("MDO_SESSION_EVENT_REPLAY_MAX", events)
        self.assertIn("MdoSessionEventBridgeOnEvent", manager)
        self.assertIn("session already has an active runtime", manager)
        self.assertNotIn("journal.jsonl", events)

    def test_advanced_session_operations_keep_one_authoritative_ledger(self) -> None:
        header = (ROOT / "app/include/mdo/sessions.h").read_text(encoding="utf-8")
        agents = (ROOT / "app/src/agents/runtime.c").read_text(encoding="utf-8")
        manager = (ROOT / "app/src/sessions/manager.c").read_text(encoding="utf-8")
        for symbol in (
            "MdoSessionLastSequence",
            "MdoSessionClear",
            "MdoSessionTruncateAfter",
            "MdoSessionExportJson",
            "MdoSessionCatalogSearch",
        ):
            self.assertIn(symbol, header)
        self.assertIn("xllmSessionClear", agents)
        self.assertIn("xllmSessionTruncateAfter", agents)
        self.assertIn("xllmSessionSetSystemPrompt", agents)
        self.assertIn("MDO_SESSION_CATALOG_MAX", manager)
        self.assertIn("MDO_SESSION_SEARCH_MAX", manager)
        self.assertNotIn('"messages"', manager)


if __name__ == "__main__":
    unittest.main()
