"""Static contracts for bounded, auditable MDO memory."""

from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent


class MemoryContractTests(unittest.TestCase):
    def test_memory_store_is_bounded_audited_and_scope_separated(self) -> None:
        header = (ROOT / "app/include/mdo/memory.h").read_text(encoding="utf-8")
        source = (ROOT / "app/src/memory/manager.c").read_text(encoding="utf-8")
        for symbol in (
            "MdoMemorySnapshotCreate",
            "MdoMemoryUpsert",
            "MdoMemoryRemove",
            "MdoMemorySnapshotRelease",
        ):
            self.assertIn(symbol, header)
        self.assertIn("memory/global.json", source)
        self.assertIn("memory/projects/%s.json", source)
        self.assertIn("memory/audit.jsonl", source)
        self.assertIn("MDO_MEMORY_MAX_ENTRIES", source)
        self.assertIn("MDO_MEMORY_STORE_LIMIT", source)
        self.assertIn("MDO_MEMORY_AUDIT_LIMIT", source)
        self.assertIn('"phase", "prepared"', source)
        self.assertNotIn('MdoMemoryObjectString(Root, "content"', source)

    def test_memory_manager_is_lazy_and_bootstrapped_before_agents(self) -> None:
        bootstrap = (ROOT / "app/src/bootstrap/bootstrap.c").read_text(encoding="utf-8")
        manager = (ROOT / "app/src/memory/manager.c").read_text(encoding="utf-8")
        self.assertIn("MdoMemoryManagerInit(g_MdoBootstrap.Runtime)", bootstrap)
        self.assertLess(bootstrap.index("MdoMemoryManagerInit"),
                        bootstrap.index("MdoModuleManagerInit"))
        init_body = manager.split("bool MdoMemoryManagerInit", 1)[1].split(
            "void MdoMemoryManagerUnit", 1)[0]
        self.assertNotIn("MdoHomeOpenWrite", init_body)
        self.assertNotIn("MdoHomeAtomicWrite", init_body)

    def test_agent_memory_is_configurable_scoped_and_permissioned(self) -> None:
        config_header = (ROOT / "app/include/mdo/config.h").read_text(encoding="utf-8")
        agent_header = (ROOT / "app/include/mdo/agents.h").read_text(encoding="utf-8")
        agent_source = (ROOT / "app/src/agents/runtime.c").read_text(encoding="utf-8")
        memory_source = (ROOT / "app/src/memory/manager.c").read_text(encoding="utf-8")
        self.assertIn("bool MemoryEnabled;", config_header)
        self.assertIn("const char* ProjectId;", agent_header)
        self.assertIn("uint64 MemoryGeneration;", agent_header)
        self.assertIn("MdoMemoryBuildPrompt(Options->ProjectId", agent_source)
        self.assertIn("MdoMemoryConfigureFileTools(Session->Agent", agent_source)
        for name in ("memory_search", "memory_write", "memory_delete"):
            self.assertNotIn(name, memory_source)
        files = (ROOT / "app/src/memory/file_memory.inc.c").read_text(encoding="utf-8")
        self.assertIn("xworkAgentMountDirectory", files)
        self.assertIn("untrusted reference data", files)
        self.assertIn("MEMORY.md", files)
        api = (ROOT / "app/src/api/memory.c").read_text(encoding="utf-8")
        self.assertIn("MdoMemoryFileSnapshotCreate", api)
        self.assertIn("MdoMemoryFileWrite", api)

    def test_directory_transfer_is_manifested_validated_and_non_destructive(self) -> None:
        header = (ROOT / "app/include/mdo/memory.h").read_text(encoding="utf-8")
        source = (ROOT / "app/src/memory/transfer.c").read_text(encoding="utf-8")
        manifest = (ROOT / "app/sources.json").read_text(encoding="utf-8")
        for symbol in (
            "MdoMemoryExportDirectory",
            "MdoMemoryPreviewImportDirectory",
            "MdoMemoryImportDirectory",
            "MdoMemoryTransferSummary",
        ):
            self.assertIn(symbol, header)
        self.assertIn('"src/memory/transfer.c"', manifest)
        self.assertIn('"mdo-memory-directory"', source)
        self.assertIn("manifest.json", source)
        self.assertIn("sha256", source)
        self.assertIn("XFILE_NOFOLLOW", source)
        self.assertIn("destination already exists", source)
        self.assertIn("MdoMemoryInternalImportEmpty", source)


if __name__ == "__main__":
    unittest.main()
