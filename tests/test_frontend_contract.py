#!/usr/bin/env python3
"""Static contracts for the dependency-free MDO-9 workspace UI."""

from __future__ import annotations

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
WEB = ROOT / "app" / "web"


class FrontendContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.index = (WEB / "index.html").read_text(encoding="utf-8")
        cls.css = (WEB / "css" / "app.css").read_text(encoding="utf-8")
        cls.scripts = {
            path.relative_to(WEB).as_posix(): path.read_text(encoding="utf-8")
            for path in (WEB / "js").rglob("*.js")
        }

    def test_shell_uses_semantic_three_region_layout(self) -> None:
        self.assertIn('<aside class="sidebar"', self.index)
        self.assertIn('<main class="workspace"', self.index)
        self.assertIn('<aside class="inspector"', self.index)
        self.assertIn('id="timeline" aria-live="polite"', self.index)
        self.assertIn('id="composer"', self.index)
        self.assertIn('type="module" src="/js/main.js"', self.index)
        self.assertNotRegex(self.index, r"\son[a-z]+=")

    def test_local_module_graph_is_closed(self) -> None:
        self.assertGreaterEqual(len(self.scripts), 12)
        imports = re.compile(r'\bfrom\s+["\'](\.[^"\']+)["\']')
        for relative, source in self.scripts.items():
            parent = (WEB / relative).parent
            for specifier in imports.findall(source):
                target = (parent / specifier).resolve()
                self.assertTrue(target.is_relative_to(WEB.resolve()),
                                (relative, specifier))
                self.assertTrue(target.is_file(), (relative, specifier, target))

    def test_frontend_has_no_package_manager_or_generated_bundle(self) -> None:
        self.assertFalse((ROOT / "package.json").exists())
        self.assertFalse((WEB / "package.json").exists())
        combined = "\n".join(self.scripts.values())
        self.assertNotIn("node_modules", combined)
        self.assertNotIn("webpack", combined.lower())
        self.assertNotIn("vite", combined.lower())

    def test_api_client_enforces_envelope_and_no_store(self) -> None:
        client = self.scripts["js/api/client.js"]
        self.assertIn('const API_ROOT = "/api/v1"', client)
        self.assertIn('envelope?.ok !== true', client)
        self.assertIn('cache: "no-store"', client)
        self.assertIn('credentials: "same-origin"', client)
        self.assertIn("class ApiError", client)
        self.assertIn("resourceId", client)

    def test_state_is_partitioned_by_server_resource(self) -> None:
        expected = {
            "js/state/bootstrap.js",
            "js/state/sessions.js",
            "js/state/catalogs.js",
            "js/state/tasks.js",
            "js/state/runs.js",
            "js/state/navigation.js",
        }
        self.assertTrue(expected.issubset(self.scripts))
        store = self.scripts["js/state/store.js"]
        for status in ("idle", "loading", "refreshing", "ready", "error"):
            self.assertIn(f'"{status}"', store)
        self.assertIn("requestGeneration", store)

    def test_timeline_replays_bounded_cursor_pages_and_batches_paint(self) -> None:
        replay = self.scripts["js/features/chat/timeline-store.js"]
        view = self.scripts["js/features/chat/timeline.js"]
        self.assertIn("after=${cursor}&limit=32", replay)
        self.assertIn("MAX_PAGES_PER_REFRESH = 4", replay)
        self.assertIn("RETAINED_EVENTS = 640", replay)
        self.assertIn("history_lost", replay)
        self.assertIn("requestAnimationFrame", view)
        for event_kind in (
            "model_text_delta", "model_reasoning_delta", "tool_start",
            "task_updated", "artifact_created", "recovery_required",
        ):
            self.assertIn(f'"{event_kind}"', view)

    def test_untrusted_api_text_uses_dom_text_nodes(self) -> None:
        for path in (
            "js/features/chat/timeline.js",
            "js/features/sessions/session-list.js",
            "js/features/tasks/task-panel.js",
        ):
            self.assertNotIn("innerHTML", self.scripts[path], path)
        self.assertIn("node.textContent", self.scripts["js/utils/dom.js"])
        self.assertNotIn("innerHTML", self.scripts["js/features/chat/markdown.js"])
        self.assertIn("renderMarkdown(item.text)", self.scripts["js/features/chat/timeline.js"])
        for source in self.scripts.values():
            self.assertNotIn("eval(", source)
            self.assertNotIn("new Function", source)

    def test_run_controls_wait_for_server_authority(self) -> None:
        app = self.scripts["js/app.js"]
        runs = self.scripts["js/state/runs.js"]
        self.assertIn("await startRun", app)
        self.assertIn("await cancelRun", app)
        self.assertIn("await readRun", app)
        self.assertIn("if ((!text && !attachments.length) || composerImages.isUploading())", app)
        self.assertIn("const existingLane = activeSubmissionLane(origin)", app)
        self.assertIn("await drainPendingSubmissions(lane, selected)", app)
        self.assertIn("composerImages.isUploading()) return", app)
        self.assertIn("await dispatchQueued()", app)
        self.assertIn("promptQueue.enqueue", app)
        self.assertIn("if (terminalState(run))", app)
        self.assertIn("/runs/${run}", runs)

    def test_composer_profile_is_applied_to_session_before_run(self) -> None:
        app = self.scripts["js/app.js"]
        profile = self.scripts["js/features/chat/composer-profile.js"]
        sessions = self.scripts["js/state/sessions.js"]
        self.assertIn("...composerProfile.selection()", app)
        self.assertIn("updateSessionProfile(session, profile)", profile)
        self.assertIn("isRunActive()", profile)
        self.assertIn("/profile", sessions)
        self.assertIn("ifMatch: etag(session)", sessions)

    def test_settings_use_preview_etag_and_server_side_merge(self) -> None:
        state = self.scripts["js/state/settings.js"]
        view = self.scripts["js/features/settings/settings-view.js"]
        navigation = self.scripts["js/state/navigation.js"]
        self.assertIn('api.patch("/settings/settings/preview"', state)
        self.assertIn('api.patch("/settings/settings"', state)
        self.assertIn("{ ifMatch: etag }", state)
        self.assertIn("previewFingerprint", view)
        self.assertIn("form.reportValidity()", view)
        self.assertNotIn("secret_ref", view)
        self.assertIn('#/settings/${resourceId(section', navigation)
        for marker in (
            'id="settings-workspace"', 'id="preview-settings"',
            'id="apply-settings"', 'id="restore-confirm"',
        ):
            self.assertIn(marker, self.index)

    def test_resource_management_is_split_into_bounded_stores(self) -> None:
        resources = self.scripts["js/state/resources.js"]
        panels = self.scripts["js/features/settings/resource-panels.js"]
        for name in ("modules", "skills", "mcp", "permissions", "storage",
                     "diagnostics", "migrations"):
            self.assertIn(f"{name}Store", resources)
        self.assertIn('api.post(`/${name}/reload`)', resources)
        self.assertIn("attempt < 100", resources)
        self.assertNotIn("innerHTML", panels)
        self.assertIn("setMcpEnabled", panels)
        self.assertIn("refreshMcp", panels)

    def test_legacy_migration_requires_preview_and_second_confirmation(self) -> None:
        resources = self.scripts["js/state/resources.js"]
        panels = self.scripts["js/features/settings/resource-panels.js"]
        self.assertIn('"/migrations/legacy"', resources)
        self.assertIn("applyLegacyMigration", resources)
        self.assertIn("preview_token", resources)
        self.assertIn("requires_confirmation", panels)
        self.assertIn("confirmingSource", panels)
        self.assertIn("旧目录会原样保留", panels)
        self.assertIn("restart_required", panels)

    def test_session_lifecycle_uses_revisioned_server_mutations(self) -> None:
        sessions = self.scripts["js/state/sessions.js"]
        listing = self.scripts["js/features/sessions/session-list.js"]
        actions = self.scripts["js/features/sessions/session-actions.js"]
        for marker in ("patchSession", "trashSession", "restoreSession"):
            self.assertIn(marker, sessions)
        self.assertIn("ifMatch: etag(session)", sessions)
        self.assertIn('role: "menu"', listing)
        self.assertIn('sessionActionItems(session)', listing)
        self.assertIn('session.status === "trash"', actions)
        self.assertNotIn("innerHTML", listing)
        for marker in (
            'id="session-status-filter"', 'id="session-action-dialog"',
            'id="confirm-session-action"',
        ):
            self.assertIn(marker, self.index)

    def test_advanced_session_actions_are_bounded_and_explicit(self) -> None:
        sessions = self.scripts["js/state/sessions.js"]
        listing = self.scripts["js/features/sessions/session-list.js"]
        actions = self.scripts["js/features/sessions/session-actions.js"]
        client = self.scripts["js/api/client.js"]
        for marker in (
            "loadSessionHistory", "forkSession", "truncateSession",
            "clearSession", "exportSession",
        ):
            self.assertIn(marker, sessions)
        for action in ("fork", "truncate", "clear", "export"):
            self.assertIn(f'item("{action}"', actions)
        self.assertIn('sessionActionItems(session)', listing)
        self.assertIn('sessionActionItems(session)',
                      self.scripts["js/features/sessions/session-action-menu.js"])
        self.assertIn("Number.isSafeInteger", sessions)
        self.assertIn('credentials: "same-origin"', client)
        self.assertIn('response.headers.get("Content-Disposition")', client)
        self.assertIn('id="session-sequence-field"', self.index)
        self.assertIn('if (action === "rename")', self.scripts["js/app.js"])
        self.assertIn("max-height: min(320px, 70vh)", self.css)

    def test_task_inspector_incrementally_replays_output_and_artifacts(self) -> None:
        tasks = self.scripts["js/state/tasks.js"]
        panel = self.scripts["js/features/tasks/task-panel.js"]
        for marker in (
            "taskDetailStore", "artifactPreviewStore", "selectTask",
            "refreshSelectedTask", "readArtifactPreview",
        ):
            self.assertIn(marker, tasks)
        self.assertIn("OUTPUT_PAGE_BYTES = 32 * 1024", tasks)
        self.assertIn("OUTPUT_RETAINED_BYTES = 256 * 1024", tasks)
        self.assertIn("EVENT_RETAINED_ITEMS = 128", tasks)
        self.assertIn("stdout=${stdout}&stderr=${stderr}&result=${result}", tasks)
        self.assertIn("window.atob", tasks)
        self.assertIn("new TextDecoder()", tasks)
        self.assertIn("/artifacts/${id}?offset=0&limit=", tasks)
        self.assertIn('id="task-detail"', self.index)
        for label in ("标准输出", "错误输出", "任务结果", "事件", "产物"):
            self.assertIn(label, panel)
        self.assertNotIn("innerHTML", panel)

    def test_permission_decisions_are_visible_bounded_and_one_shot(self) -> None:
        state = self.scripts["js/state/approvals.js"]
        panel = self.scripts["js/features/approvals/decision-panel.js"]
        self.assertIn('id="decisions-tab"', self.index)
        self.assertIn('id="decisions-panel"', self.index)
        self.assertIn('api.get("/approvals")', state)
        self.assertIn('api.put(`/approvals/${id}`, { decision })', state)
        self.assertIn('new Set(["allow", "deny"])', state)
        self.assertIn("pending = new Set()", state)
        self.assertIn("approvalDecisionStatus(key)", panel)
        self.assertIn("approvalDecisionStore.subscribe(render)", panel)
        self.assertIn("approvalDecisionStore.subscribe(render)",
                      self.scripts["js/features/chat/conversation-docks.js"])
        self.assertIn("text: formatArguments(item.arguments_json)", panel)
        self.assertIn('text: t("decision.allowOnce"', panel)
        self.assertIn('text: t("decision.deny"', panel)
        self.assertIn(".approval-card", self.css)
        self.assertNotIn("innerHTML", panel)

    def test_interrupted_calls_require_explicit_recovery_decisions(self) -> None:
        state = self.scripts["js/state/recovery.js"]
        panel = self.scripts["js/features/approvals/recovery-panel.js"]
        app = self.scripts["js/app.js"]
        self.assertIn('id="recovery-summary"', self.index)
        self.assertIn('id="recovery-list"', self.index)
        self.assertIn("recoveryStore", state)
        self.assertIn("recovery_token", state)
        self.assertIn('new Set(["retry", "record_uncertain"])', state)
        self.assertIn("every pending call needs a recovery decision", state)
        self.assertIn('"记录为不确定"', panel)
        self.assertIn('"重新执行"', panel)
        self.assertIn("!item.tool_available", panel)
        self.assertIn("至少一次语义", panel)
        self.assertIn("loadRecovery()", app)
        self.assertIn("monitorRun(run)", app)
        self.assertIn(".recovery-option.selected", self.css)
        self.assertNotIn("innerHTML", panel)

    def test_responsive_and_accessibility_modes_are_explicit(self) -> None:
        self.assertIn("grid-template-columns: var(--sidebar-column, var(--sidebar-width)) minmax(0, 1fr) var(--inspector-column, var(--inspector-width))", self.css)
        self.assertIn('id="sidebar-resize" role="separator"', self.index)
        self.assertIn('id="inspector-resize" role="separator"', self.index)
        self.assertIn("@media (max-width: 1203px)", self.css)
        self.assertIn("@media (max-width: 760px)", self.css)
        self.assertIn("@media (prefers-reduced-motion: reduce)", self.css)
        self.assertIn("[hidden] { display: none !important; }", self.css)
        self.assertIn(":focus-visible", self.css)
        self.assertIn("env(safe-area-inset-bottom)", self.css)


if __name__ == "__main__":
    unittest.main()
