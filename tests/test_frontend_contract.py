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

    def test_shell_uses_navigation_and_chat_with_on_demand_tasks(self) -> None:
        self.assertIn('<aside class="sidebar"', self.index)
        self.assertIn('<main class="workspace"', self.index)
        self.assertNotIn('id="inspector"', self.index)
        self.assertIn('<dialog class="app-dialog tasks-dialog"', self.index)
        self.assertIn('id="timeline" aria-live="polite"', self.index)
        self.assertIn('id="composer"', self.index)
        self.assertIn('import("/js/main.js")', self.index)
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

    def test_memory_settings_has_no_project_management_controls(self) -> None:
        self.assertIn('data-settings-section="memory"', self.index)
        self.assertNotIn('data-settings-panel="projects"', self.index)
        panel = self.index.split('data-settings-panel="memory"', 1)[1].split('</section>', 1)[0]
        for control in ('memory-scope', 'memory-page-files', 'memory-file-content', 'memory-page-edit'):
            self.assertIn(f'id="{control}"', panel)
        for retired in ('projects-add', 'settings-projects-list', 'project-purge-recovery'):
            self.assertNotIn(retired, panel)
        self.assertNotIn('navigation.openSettings("projects")', self.scripts['js/app.js'])

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
        self.assertNotIn("RETAINED_EVENTS", replay)
        self.assertIn("loadOlderTimeline", replay)
        self.assertIn("readHistoryRange", replay)
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
        self.assertIn("await newTaskController.submit", app)
        self.assertIn("await submissionController.submit", app)
        self.assertNotIn("submissionLanes", app)
        self.assertIn("composerImages.isUploading()) return", app)
        self.assertIn("await dispatchQueued()", app)
        self.assertIn("await promptQueue.select", app)
        self.assertIn("if (terminalState(run))", app)
        self.assertIn("/runs/${run}", runs)

    def test_composer_profile_has_idle_update_and_queued_snapshot_paths(self) -> None:
        app = self.scripts["js/app.js"]
        profile = self.scripts["js/features/chat/composer-profile.js"]
        sessions = self.scripts["js/state/sessions.js"]
        self.assertIn("const profile = composerProfile.selection()", app)
        self.assertIn("await submissionController.submit(key, rawInput, attachments, interrupt,", app)
        self.assertIn("newTaskController.createForAttachment", app)
        self.assertIn("updateSessionProfile(session, profile)", profile)
        self.assertIn("isRunActive()", profile)
        self.assertIn("draftStore.setComposerProfile(key,", profile)
        self.assertIn("/profile", sessions)
        self.assertIn("ifMatch: etag(session)", sessions)

    def test_settings_autosave_uses_etag_and_server_side_merge(self) -> None:
        state = self.scripts["js/state/settings.js"]
        view = self.scripts["js/features/settings/settings-view.js"]
        navigation = self.scripts["js/state/navigation.js"]
        self.assertIn('api.patch("/settings/settings/preview"', state)
        self.assertIn('api.patch("/settings/settings"', state)
        self.assertIn("{ ifMatch: etag }", state)
        self.assertIn("createSettingsAutosave", view)
        self.assertIn("changedSettingsValues", view)
        self.assertIn("invalid.reportValidity()", view)
        self.assertNotIn("snapshot.web.secret_ref", view)
        self.assertNotIn("snapshot.transport.proxy.secret_ref", view)
        self.assertIn("form.elements.proxy_secret_ref.value.trim()", view)
        self.assertIn('#/settings/${resourceId(section', navigation)
        for marker in (
            'id="settings-workspace"',
            'id="apply-settings"', 'id="restore-confirm"',
        ):
            self.assertIn(marker, self.index)
        self.assertNotIn('id="preview-settings"', self.index)

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
        backup = (ROOT / "app/web/js/api/backup-download.js").read_text(encoding="utf-8")
        self.assertIn('response.headers.get("Content-Disposition")', backup)
        self.assertIn('backup_download_checksum', backup)
        self.assertIn('id="session-sequence-field"', self.index)
        self.assertIn('if (action === "rename")', self.scripts["js/app.js"])
        self.assertIn("max-height: min(320px, 70vh)", self.css)

    def test_task_dialog_incrementally_replays_output_and_artifacts(self) -> None:
        tasks = self.scripts["js/state/tasks.js"]
        panel = self.scripts["js/features/tasks/task-panel.js"]
        for marker in (
            "taskDetailStore", "artifactPreviewStore", "selectTask",
            "refreshSelectedTask", "readArtifactPreview",
        ):
            self.assertIn(marker, tasks)
        self.assertIn("OUTPUT_PAGE_BYTES = 32 * 1024", tasks)
        self.assertIn("OUTPUT_RETAINED_BYTES = 256 * 1024", tasks)
        self.assertIn("stdout=${stdout}&stderr=${stderr}&result=${result}", tasks)
        self.assertIn("window.atob", tasks)
        self.assertIn("new TextDecoder()", tasks)
        self.assertIn("/artifacts/${id}?offset=0&limit=", tasks)
        self.assertIn('id="task-detail"', self.index)
        for label in ("标准输出", "错误输出", "任务结果", "产物"):
            self.assertIn(label, panel)
        self.assertNotIn("/events?", tasks)
        self.assertNotIn("task-event-list", panel)
        self.assertNotIn('id="task-summary"', self.index)
        self.assertNotIn("innerHTML", panel)

    def test_permission_decisions_are_visible_bounded_and_one_shot(self) -> None:
        state = self.scripts["js/state/approvals.js"]
        panel = self.scripts["js/features/chat/conversation-docks.js"]
        self.assertNotIn('id="decisions-tab"', self.index)
        self.assertNotIn('id="decisions-panel"', self.index)
        self.assertNotIn('id="trace-panel"', self.index)
        self.assertNotIn('id="context-panel"', self.index)
        self.assertIn('api.get("/approvals")', state)
        self.assertIn('api.put(`/approvals/${id}`, { decision })', state)
        self.assertIn('new Set(["allow", "allow_run", "deny"])', state)
        self.assertIn("pending = new Set()", state)
        self.assertIn("approvalDecisionStatus(key)", panel)
        self.assertIn("approvalDecisionStore.subscribe(render)", panel)
        self.assertIn("approvalDecisionStore.subscribe(render)",
                      self.scripts["js/features/chat/conversation-docks.js"])
        self.assertIn("text: item.arguments_json", panel)
        self.assertIn('t("dock.approval.allowRun")',
                      self.scripts["js/features/chat/conversation-docks.js"])
        self.assertNotIn("innerHTML", panel)

    def test_interrupted_calls_require_explicit_recovery_decisions(self) -> None:
        state = self.scripts["js/state/recovery.js"]
        panel = self.scripts["js/features/chat/recovery-dock.js"]
        app = self.scripts["js/app.js"]
        docks = self.scripts["js/features/chat/conversation-docks.js"]
        self.assertIn('id: "recovery-summary"', docks)
        self.assertIn('id: "recovery-list"', docks)
        self.assertIn("!needsRecovery", docks)
        self.assertIn("revealRecovery()", docks)
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
        self.assertIn("grid-template-columns: var(--sidebar-column, var(--sidebar-width)) minmax(0, 1fr)", self.css)
        self.assertIn('id="sidebar-resize" role="separator"', self.index)
        self.assertNotIn('id="inspector-resize"', self.index)
        self.assertNotIn("inspector", self.scripts["js/app.js"])
        self.assertIn("@media (max-width: 760px)", self.css)
        self.assertIn("@media (prefers-reduced-motion: reduce)", self.css)
        self.assertIn("[hidden] { display: none !important; }", self.css)
        self.assertIn(":focus-visible", self.css)
        self.assertIn("env(safe-area-inset-bottom)", self.css)

    def test_agent_default_permissions_share_the_chat_profiles(self) -> None:
        settings_select = self.index.split('id="setting-default-permission"', 1)[1].split('</select>', 1)[0]
        chat_select = self.index.split('id="composer-permission"', 1)[1].split('</select>', 1)[0]
        for profile in ("read-only", "balanced", "full-access"):
            self.assertIn(f'value="{profile}"', settings_select)
            self.assertIn(f'value="{profile}"', chat_select)
        self.assertNotIn('name="interaction_mode"', self.index)
        settings = self.scripts["js/features/settings/settings-view.js"]
        self.assertIn("form.elements.permission_profile.value", settings)
        self.assertNotIn("interaction_mode", settings)


if __name__ == "__main__":
    unittest.main()
