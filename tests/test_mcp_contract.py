"""Static contracts for the MDO-5 MCP manager and configuration."""

from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
HEADER = ROOT / "app/include/mdo/mcp.h"
FORMAT = ROOT / "docs/mcp-format.md"
SOURCE = ROOT / "app/src/mcp/manager.c"
SOURCES = ROOT / "app/sources.json"
BOOTSTRAP = ROOT / "app/src/bootstrap/bootstrap.c"


class McpContractTests(unittest.TestCase):
    def test_api_separates_immutable_catalog_from_live_status(self) -> None:
        text = HEADER.read_text(encoding="utf-8")
        self.assertIn("typedef struct MdoMcpCatalog MdoMcpCatalog", text)
        self.assertIn("MdoMcpCatalogSnapshot", text)
        self.assertIn("MdoMcpCatalogRef", text)
        self.assertIn("MdoMcpCatalogRelease", text)
        self.assertIn("MdoMcpManagerGetStatus", text)
        self.assertIn("MdoMcpManagerDisconnect", text)
        self.assertIn("MdoMcpManagerRegisterDiscoveryTools", text)
        self.assertIn("MdoMcpManagerLoadTool", text)

    def test_format_is_cold_lazy_and_secret_reference_only(self) -> None:
        text = FORMAT.read_text(encoding="utf-8")
        self.assertIn("do not start a process or make a network request", text)
        self.assertIn("secret_ref", text)
        self.assertIn("never returns their", text)
        self.assertIn("values through catalog or status APIs", text)
        self.assertIn("tool_search", text)
        self.assertIn("tool_load", text)
        self.assertIn("never falls back", text)

    def test_protocol_and_transport_are_explicit(self) -> None:
        text = FORMAT.read_text(encoding="utf-8")
        self.assertIn("2026-07-28", text)
        self.assertIn("2025-11-25", text)
        self.assertIn("streamable-http", text)
        self.assertIn("never silently treated as stdio", text)
        self.assertIn("does not\nsilently downgrade", text)

    def test_manager_is_in_the_runtime_bootstrap(self) -> None:
        source = SOURCE.read_text(encoding="utf-8")
        manifest = SOURCES.read_text(encoding="utf-8")
        bootstrap = BOOTSTRAP.read_text(encoding="utf-8")
        self.assertIn('"src/mcp/manager.c"', manifest)
        self.assertIn("MdoMcpManagerInit(g_MdoBootstrap.Runtime)", bootstrap)
        self.assertIn("xworkRuntimeReplaceMcpServersBySource", source)
        self.assertIn("xworkRuntimeDisconnectMcpServer", source)
        self.assertIn("MdoMcpResolveSecret", source)
        self.assertIn("XWORK_MCP_TRANSPORT_STREAMABLE_HTTP", source)
        self.assertIn("pHttpHeaders", source)
        self.assertIn("MdoMcpCatalogForgetSecrets", source)


if __name__ == "__main__":
    unittest.main()
