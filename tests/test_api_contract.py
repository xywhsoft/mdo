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
            "projects", "runs", "artifacts", "approvals", "permissions", "diagnostics",
            "storage",
            "operations", "migrations/legacy",
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
        self.assertNotIn("xworkRuntimeEventSnapshot", events)
        self.assertNotIn('"/api/v1/events"', self.router)
        self.assertIn("MdoApiRouteMatch", self.router)

    def test_json_mutations_have_a_bounded_strict_body_reader(self) -> None:
        body = (ROOT / "app/src/api/body.c").read_text(encoding="utf-8")
        mutations = (ROOT / "app/src/api/mutations.c").read_text(encoding="utf-8")
        self.assertIn("xrtHttpFieldGetUnique", body)
        self.assertIn("application/json", body)
        self.assertIn("xrtHttp1BodyRead", body)
        self.assertIn("Data.Size > Limit - OutputSize", body)
        self.assertIn("MDO_API_REQUEST_MAX_BYTES,", body)
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
        self.assertIn("MdoApiConfigMergeDocument", mutations)
        self.assertIn("XHTTP_METHOD_PATCH", self.router)
        self.assertIn("MdoApiReplySuccessTakeRevision", state)
        self.assertIn('"runtime_consistent"', state)
        for field in ("appearance", "permission_profile", "web_search",
                      "workspace", "confirm_external_write"):
            self.assertIn(f'"{field}"', state)

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

    def test_advanced_session_routes_use_the_authoritative_ledger(self) -> None:
        sessions = (ROOT / "app/src/api/sessions.c").read_text(encoding="utf-8")
        for suffix in ("history", "fork", "truncate", "clear", "export"):
            self.assertIn(
                f'"/api/v1/projects/{{project}}/sessions/{{session}}/{suffix}"',
                self.router,
            )
        for call in (
            "MdoSessionLastSequence", "MdoSessionFork",
            "MdoSessionTruncateAfter", "MdoSessionClear",
            "MdoSessionExportJson",
        ):
            self.assertIn(call, sessions)
        self.assertIn("MdoApiSessionOpenActive", sessions)
        self.assertIn("MdoApiSessionExpectedRevision", sessions)
        self.assertIn('"session_state_conflict"', sessions)
        self.assertIn("MdoApiReplyDownload", sessions)
        self.assertIn("MDO_API_DOWNLOAD_MAX_BYTES", self.internal)
        self.assertIn('"application/octet-stream"', self.http)
        self.assertIn('XRT_STR_LITERAL("Content-Disposition")', self.http)

    def test_interactive_runs_have_owned_start_detail_and_cancel_routes(self) -> None:
        runs = (ROOT / "app/src/api/runs.c").read_text(encoding="utf-8")
        self.assertIn(
            '"/api/v1/projects/{project}/sessions/{session}/runs"',
            self.router,
        )
        self.assertIn('"/api/v1/runs/{run}"', self.router)
        self.assertIn("MdoRunStart", runs)
        self.assertIn("MdoRunCancel", runs)
        self.assertIn("MdoRunSnapshotCreate", runs)
        self.assertIn("MdoRunSnapshotResult", runs)
        self.assertIn("Present == xrtValueCount(Body.Value)", runs)
        self.assertIn('"final_text_truncated"', runs)
        self.assertIn('"run_limit_reached"', runs)

    def test_durable_recovery_is_bounded_token_checked_and_explicit(self) -> None:
        recovery = (ROOT / "app/src/api/recovery.c").read_text(
            encoding="utf-8")
        runs = (ROOT / "app/src/runs/manager.c").read_text(
            encoding="utf-8")
        agents = (ROOT / "app/src/agents/runtime.c").read_text(
            encoding="utf-8")
        self.assertIn(
            '"/api/v1/projects/{project}/sessions/{session}/recovery"',
            self.router,
        )
        self.assertIn(
            '"/api/v1/projects/{project}/sessions/{session}/resume"',
            self.router,
        )
        self.assertIn("MDO_API_RECOVERY_CALL_MAX 32u", recovery)
        self.assertIn("MDO_API_RECOVERY_ARGUMENT_MAX (16u * 1024u)", recovery)
        self.assertIn("MDO_API_RECOVERY_ARGUMENT_TOTAL_MAX", recovery)
        self.assertIn("XWORK_RECOVERY_RETRY", recovery)
        self.assertIn("XWORK_RECOVERY_RECORD_UNCERTAIN", recovery)
        self.assertIn('"recovery_view_too_large"', recovery)
        self.assertIn("MdoAgentSessionRecoverySnapshot", agents)
        self.assertIn("MdoRunsRecoveryTokenValid", runs)
        self.assertIn("MdoAgentRecoverySnapshotToken", runs)
        self.assertIn("recovery_token", recovery)
        self.assertIn("RunOptions.ResumeOptions = Options->ResumeOptions", runs)
        self.assertNotIn("xworkRuntimeEventSnapshot", runs)

    def test_schedule_resources_are_strict_and_revision_tagged(self) -> None:
        schedules = (ROOT / "app/src/api/schedules.c").read_text(
            encoding="utf-8")
        self.assertIn('"/api/v1/schedules/{schedule}"', self.router)
        self.assertIn('"/api/v1/schedules/{schedule}/enabled"', self.router)
        self.assertIn("MdoScheduleCreate", schedules)
        self.assertIn("MdoScheduleReplace", schedules)
        self.assertIn("MdoScheduleSetEnabled", schedules)
        self.assertIn("MdoScheduleRemove", schedules)
        self.assertIn("MdoApiScheduleExpectedRevision", schedules)
        self.assertIn('\\"mdo-schedule-%s-%llu\\"', schedules)
        self.assertIn("Present == xrtValueCount(Body.Value)", schedules)
        self.assertIn('"body_not_allowed"', schedules)
        self.assertIn('"revision_conflict"', schedules)
        self.assertNotIn("xworkSchedule", schedules)

    def test_unified_tasks_have_detail_output_and_cancel_routes(self) -> None:
        tasks = (ROOT / "app/src/api/tasks.c").read_text(encoding="utf-8")
        for path in (
            '"/api/v1/tasks/{task}"',
            '"/api/v1/tasks/{task}/output"',
        ):
            self.assertIn(path, self.router)
        self.assertIn("xworkRuntimeTaskSnapshot", tasks)
        self.assertIn("xworkRuntimeReadTaskOutput", tasks)
        self.assertNotIn("xworkRuntimeReadTaskEvents", tasks)
        self.assertNotIn('"/api/v1/tasks/{task}/events"', self.router)
        self.assertIn("xworkRuntimeCancelTask", tasks)
        self.assertIn("MDO_API_TASK_OUTPUT_MAX_BYTES (64u * 1024u)", tasks)
        self.assertIn("xrtBase64EncodeNew", tasks)
        self.assertIn('"encoding", "base64"', tasks)
        self.assertIn('"body_not_allowed"', tasks)
        self.assertNotIn("xworkRuntimeReleaseTask", tasks)

    def test_artifact_reads_are_bounded_and_offset_addressed(self) -> None:
        state = (ROOT / "app/src/api/state.c").read_text(encoding="utf-8")
        self.assertIn('"/api/v1/artifacts/{artifact}"', self.router)
        self.assertIn("MDO_API_ARTIFACT_MAX_BYTES (64u * 1024u)", state)
        self.assertIn("xworkRuntimeArtifactGetInfo", state)
        self.assertIn("xworkRuntimeReadArtifact", state)
        self.assertIn("xrtBase64EncodeNew", state)
        for field in ("offset", "next", "total_size", "bytes", "eof",
                      "sha256", "media_type", "data"):
            self.assertIn(f'"{field}"', state)
        self.assertIn('"artifact_offset_out_of_range"', state)

    def test_approvals_are_bounded_synchronous_and_fail_closed(self) -> None:
        approvals = (ROOT / "app/src/api/approvals.c").read_text(
            encoding="utf-8")
        manager = (ROOT / "app/src/approvals/manager.c").read_text(
            encoding="utf-8")
        bootstrap = (ROOT / "app/src/bootstrap/bootstrap.c").read_text(
            encoding="utf-8")
        self.assertIn('"/api/v1/approvals"', self.router)
        self.assertIn('"/api/v1/approvals/{approval}"', self.router)
        self.assertIn("MDO_APPROVAL_API_LIMIT", approvals)
        self.assertIn("xrtValueCount(Body.Value) != 1u", approvals)
        self.assertIn("MdoApprovalSnapshotCreate", approvals)
        self.assertIn("MdoApprovalDecide", approvals)
        self.assertIn("MDO_APPROVAL_PENDING_MAX", manager)
        self.assertIn("MDO_APPROVAL_MAX_WAIT_MICROSECONDS", manager)
        self.assertIn("xrtCancelRequested", manager)
        self.assertIn("xrtCondWaitUntil", manager)
        self.assertIn("XWORK_PERMISSION_DENY", manager)
        self.assertIn("MdoApprovalManagerInit", bootstrap)
        self.assertIn("RunOptions.OnPermission = MdoApprovalOnPermission", bootstrap)
        self.assertIn("ExecutorOptions.OnPermission = MdoApprovalOnPermission", bootstrap)
        self.assertLess(bootstrap.index("MdoRunManagerUnit();"),
                        bootstrap.index("MdoApprovalManagerUnit();"))

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
        schedules = (ROOT / "app/src/api/schedules.c").read_text(
            encoding="utf-8")
        self.assertNotIn('"secret_ref"', catalogs + state + schedules)
        self.assertNotIn('"authorization"', catalogs.lower())
        self.assertNotIn('"environment"', catalogs)
        self.assertNotIn('"http_headers"', catalogs)
        self.assertNotIn('Info.SystemPrompt)', catalogs)
        self.assertIn("MdoApiScheduleInfoValue(&Info, false", schedules)
        self.assertIn('"input_bytes"', schedules)

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
