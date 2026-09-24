"""Static contracts for the MDO Agent session product boundary."""

from __future__ import annotations

import json
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent


class AgentContractTests(unittest.TestCase):
    def test_agent_runtime_is_in_the_deterministic_unity_build(self) -> None:
        manifest = json.loads((ROOT / "app/sources.json").read_text(encoding="utf-8"))
        self.assertIn("src/agents/runtime.c", manifest["sources"])
        self.assertLess(
            manifest["sources"].index("src/modules/manager.c"),
            manifest["sources"].index("src/agents/runtime.c"),
        )

    def test_public_api_exposes_versioned_session_and_run_options(self) -> None:
        header = (ROOT / "app/include/mdo/agents.h").read_text(encoding="utf-8")
        for symbol in (
            "MdoAgentSessionOptions",
            "MdoAgentSessionInfo",
            "MdoAgentRunOptions",
            "MdoAgentRunInfo",
            "MdoAgentSessionCreateWithRuntime",
            "Recover",
            "MdoAgentSessionRef",
            "MdoAgentSessionRelease",
            "MdoAgentRunCreate",
            "MdoAgentRunStart",
            "MdoAgentRunWait",
            "MdoAgentRunCancel",
            "MdoAgentRunDestroy",
        ):
            self.assertIn(symbol, header)
        self.assertGreaterEqual(header.count("uint32 Size;"), 4)

    def test_runtime_pins_every_catalog_and_callback_owner(self) -> None:
        source = (ROOT / "app/src/agents/runtime.c").read_text(encoding="utf-8")
        for fragment in (
            "MdoModelCatalogSnapshot()",
            "MdoModuleCatalogSnapshot()",
            "MdoSkillCatalogSnapshot()",
            "MdoModuleCatalogAgentAcquire",
            "MdoModuleCatalogAgentRelease",
            "AgentOptions.OnOwnerRetain = MdoAgentsOwnerRetainCallback",
            "AgentOptions.OnOwnerRelease = MdoAgentsOwnerReleaseCallback",
            "MdoAgentSessionRef(Session)",
        ):
            self.assertIn(fragment, source)

    def test_effective_selection_is_reported_and_limits_fail_closed(self) -> None:
        source = (ROOT / "app/src/agents/runtime.c").read_text(encoding="utf-8")
        self.assertIn("Agent output limit exceeds the selected model profile", source)
        self.assertIn("Agent context override exceeds the selected model profile", source)
        self.assertIn("subagent model or context override exceeds its parent ceiling", source)
        self.assertIn("selected Skill requires a tool outside the Agent allowlist", source)
        self.assertIn("selected Skill prompt exceeds the Agent context injection limit", source)
        self.assertIn("Info->ModelGeneration = Session->ModelGeneration", source)
        self.assertIn("Info->ModuleGeneration = Session->ModuleGeneration", source)
        self.assertIn("Info->SkillGeneration = Session->SkillGeneration", source)

    def test_dependency_lock_supports_independent_library_provenance(self) -> None:
        lock = json.loads((ROOT / "deps.lock").read_text(encoding="utf-8"))
        libraries = lock["libraries"]
        self.assertEqual(libraries["xwork"]["version"], "3.7.0")
        self.assertEqual(libraries["xwork"]["abi_version"], 6)
        self.assertNotEqual(
            libraries["xllm"]["source_commit"],
            libraries["xwork"]["source_commit"],
        )


if __name__ == "__main__":
    unittest.main()
