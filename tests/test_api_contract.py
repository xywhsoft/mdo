"""MDO-8 versioned HTTP API source and protocol contracts."""

from __future__ import annotations

import json
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent


class ApiContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.public = (ROOT / "app/include/mdo/api.h").read_text(encoding="utf-8")
        cls.internal = (ROOT / "app/src/api/internal.h").read_text(encoding="utf-8")
        cls.http = (ROOT / "app/src/api/http.c").read_text(encoding="utf-8")
        cls.router = (ROOT / "app/src/api/router.c").read_text(encoding="utf-8")
        cls.resources = (ROOT / "app/src/api/resources.c").read_text(encoding="utf-8")
        cls.service = (ROOT / "app/src/bootstrap/service.c").read_text(encoding="utf-8")

    def test_api_is_versioned_and_has_a_bounded_response(self) -> None:
        self.assertIn("MDO_API_SCHEMA_VERSION 1u", self.public)
        self.assertIn('"/api/v1/bootstrap"', self.router)
        self.assertIn("MDO_API_RESPONSE_MAX_BYTES", self.internal)
        self.assertIn("MDO_API_REQUEST_MAX_BYTES", self.internal)
        self.assertIn("JsonSize > MDO_API_RESPONSE_MAX_BYTES", self.http)
        for resource in (
            "bootstrap", "settings", "models", "agents", "modules",
            "skills", "mcp", "sessions", "schedules", "tasks",
            "projects", "runs", "artifacts", "permissions", "diagnostics",
            "storage", "events",
            "operations",
        ):
            self.assertIn(f'"/api/v1/{resource}"', self.router)
        self.assertIn('"/api/v1/projects/{project}/sessions/{session}/events"',
                      self.router)

    def test_request_entry_is_owned_by_the_api_layer(self) -> None:
        self.assertIn("MdoApiInit", self.service)
        self.assertIn("MdoApiUnit", self.service)
        self.assertIn("XS_RequestResult RequestProc", self.service)
        self.assertIn("return MdoApiRequest(pRequest);", self.service)

    def test_non_api_requests_fall_through_to_static_files(self) -> None:
        self.assertIn("!MdoApiPath(Context.Target.Path)", self.router)
        self.assertIn("return XS_FALLBACK;", self.router)
        self.assertIn('"route_not_found"', self.router)

    def test_method_contract_covers_head_options_and_405(self) -> None:
        self.assertIn("XHTTP_METHOD_GET | XHTTP_METHOD_HEAD", self.router)
        self.assertIn("XHTTP_METHOD_OPTIONS", self.router)
        self.assertIn('"GET, HEAD, OPTIONS"', self.router)
        self.assertIn('405u, "method_not_allowed"', self.router)
        self.assertIn("MethodCode != XHTTP_METHOD_HEAD", self.http)

    def test_event_replay_is_cursor_based_and_bounded(self) -> None:
        events = (ROOT / "app/src/api/events.c").read_text(encoding="utf-8")
        self.assertIn("MDO_API_EVENT_MAX_LIMIT 32u", events)
        self.assertIn("MDO_API_EVENT_TEXT_BYTES 4096u", events)
        self.assertIn('"next_cursor"', events)
        self.assertIn('"latest_event_id"', events)
        self.assertIn('"history_lost"', events)
        self.assertIn('"terminal"', events)
        self.assertIn("MdoSessionEventReplay", events)
        self.assertIn("xworkRuntimeEventSnapshot", events)
        self.assertIn("MdoApiRouteMatch", self.router)

    def test_json_mutations_have_a_bounded_strict_body_reader(self) -> None:
        body = (ROOT / "app/src/api/body.c").read_text(encoding="utf-8")
        mutations = (ROOT / "app/src/api/mutations.c").read_text(encoding="utf-8")
        self.assertIn("xrtHttpFieldGetUnique", body)
        self.assertIn("application/json", body)
        self.assertIn("xrtHttp1BodyRead", body)
        self.assertIn("MDO_API_REQUEST_MAX_BYTES - OutputSize", body)
        self.assertIn("xrtJsonParse", body)
        self.assertIn('"/api/v1/settings/{domain}/preview"', self.router)
        self.assertIn("MdoConfigPreviewImport", mutations)
        self.assertNotIn("MdoConfigImport", mutations)

    def test_settings_writes_require_a_revision_etag(self) -> None:
        mutations = (ROOT / "app/src/api/mutations.c").read_text(encoding="utf-8")
        state = (ROOT / "app/src/api/state.c").read_text(encoding="utf-8")
        self.assertIn('"/api/v1/settings/{domain}"', self.router)
        self.assertIn('XRT_STR_LITERAL("If-Match")', mutations)
        self.assertIn('"mdo-config-', mutations)
        self.assertIn("Number == UINT64_MAX", mutations)
        self.assertIn("MdoSettingsApply", mutations)
        self.assertIn("MdoSettingsRestore", mutations)
        self.assertIn("MdoApiReplySuccessTakeRevision", state)
        self.assertIn('"runtime_consistent"', state)

    def test_catalog_reload_routes_use_candidate_publish_managers(self) -> None:
        mutations = (ROOT / "app/src/api/mutations.c").read_text(encoding="utf-8")
        operations = (ROOT / "app/src/api/operations.c").read_text(encoding="utf-8")
        for resource, manager in (
            ("models", "MdoModelManagerReload"),
            ("skills", "MdoSkillManagerReload"),
            ("mcp", "MdoMcpManagerReload"),
        ):
            self.assertIn(f'"/api/v1/{resource}/reload"', self.router)
            self.assertIn(manager, mutations)
        self.assertIn("previous generation remains active", mutations)
        self.assertIn('"/api/v1/modules/reload"', self.router)
        self.assertIn("MdoOperationStartModuleReload", operations)
        self.assertIn("202u", operations)

    def test_long_operations_have_stable_pollable_ids(self) -> None:
        operations = (ROOT / "app/src/api/operations.c").read_text(encoding="utf-8")
        manager = (ROOT / "app/src/operations/manager.c").read_text(
            encoding="utf-8")
        self.assertIn('"/api/v1/operations/{operation}"', self.router)
        self.assertIn('MdoApiValueSetString(Item, "id"', operations)
        self.assertIn('MdoApiValueSetBool(Item, "terminal"', operations)
        self.assertIn("xrtTaskPoolCreate", manager)
        self.assertIn("MDO_OPERATION_LIMIT 64u", manager)
        self.assertIn("xrtTaskPoolCancel", manager)
        self.assertIn("MdoOperationStartMcpRefresh", manager)
        self.assertIn("MdoMcpManagerRefresh", manager)
        for suffix in ("enabled", "disconnect", "refresh"):
            self.assertIn(f'"/api/v1/mcp/{{server}}/{suffix}"', self.router)
        self.assertIn('"mcp_refresh"', operations)
        self.assertIn('"tools_discovered"', operations)

    def test_session_resources_are_strict_and_revision_tagged(self) -> None:
        sessions = (ROOT / "app/src/api/sessions.c").read_text(encoding="utf-8")
        self.assertIn('"/api/v1/projects/{project}/sessions/{session}"',
                      self.router)
        self.assertIn(
            '"/api/v1/projects/{project}/sessions/{session}/restore"',
            self.router)
        self.assertIn("MdoApiSessionCreateRoute", sessions)
        self.assertIn("Present == xrtValueCount(Body.Value)", sessions)
        self.assertIn("MdoSessionCreate", sessions)
        self.assertIn("MdoSessionLoad", sessions)
        self.assertIn("mdo-session-%s-%llu", sessions)
        self.assertIn("MdoApiReplySuccessTakeEntityTag", sessions)
        self.assertIn("session_persistence_failed", sessions)
        self.assertIn("session_service_unavailable", sessions)
        self.assertIn("session_read_failed", sessions)
        self.assertIn("MdoApiSessionExpectedRevision", sessions)
        self.assertIn("MdoSessionRename", sessions)
        self.assertIn("MdoSessionSetPinned", sessions)
        self.assertIn("MdoSessionSetArchived", sessions)
        self.assertIn("MdoSessionMoveToTrash", sessions)
        self.assertIn("MdoSessionRestore", sessions)
        self.assertIn("revision_conflict", sessions)
        self.assertNotIn("credential", sessions.lower())

    def test_every_json_response_has_identity_and_hardening_headers(self) -> None:
        for text in (
            'XRT_STR_LITERAL("schema_version")',
            'XRT_STR_LITERAL("ok")',
            'XRT_STR_LITERAL("request_id")',
            'XRT_STR_LITERAL("X-Request-Id")',
            'XRT_STR_LITERAL("Cache-Control")',
            'XRT_STR_LITERAL("X-Content-Type-Options")',
            'XRT_STR_LITERAL("Referrer-Policy")',
        ):
            self.assertIn(text, self.http)

    def test_bootstrap_resource_uses_public_snapshots(self) -> None:
        self.assertIn("MdoBootstrapGetSnapshot", self.resources)
        self.assertNotIn("g_MdoBootstrap", self.resources)
        self.assertNotIn("g_MdoConfig", self.resources)
        self.assertNotIn("g_MdoHome", self.resources)

    def test_catalog_handlers_do_not_expose_secret_values_or_large_bodies(self) -> None:
        catalogs = (ROOT / "app/src/api/catalogs.c").read_text(encoding="utf-8")
        state = (ROOT / "app/src/api/state.c").read_text(encoding="utf-8")
        self.assertNotIn('"secret_ref"', catalogs + state)
        self.assertNotIn('"authorization"', catalogs.lower())
        self.assertNotIn('"environment"', catalogs)
        self.assertNotIn('"http_headers"', catalogs)
        self.assertNotIn('Info.SystemPrompt)', catalogs)
        self.assertNotIn('"input", Info.Input', state)
        self.assertIn('"input_bytes"', state)

    def test_bootstrap_resource_contains_ui_startup_domains(self) -> None:
        for name in (
            "version", "ready", "stage", "home", "config", "models",
            "skills", "memory", "web", "mcp", "modules", "schedules",
            "sessions",
        ):
            self.assertIn(f'"{name}"', self.resources)

    def test_api_sources_precede_the_service_entry(self) -> None:
        manifest = json.loads((ROOT / "app/sources.json").read_text(encoding="utf-8"))
        sources = manifest["sources"]
        self.assertLess(sources.index("src/api/http.c"),
                        sources.index("src/api/resources.c"))
        self.assertLess(sources.index("src/api/resources.c"),
                        sources.index("src/api/router.c"))
        self.assertLess(sources.index("src/api/router.c"),
                        sources.index("src/bootstrap/service.c"))


if __name__ == "__main__":
    unittest.main(verbosity=2)
