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


if __name__ == "__main__":
    unittest.main()
