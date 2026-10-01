import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import test from "node:test";

import { currentLocale, loadLocale, subscribeLocale, supportedLocales, t } from
  "../app/web/js/i18n.js";
import { errorMessage } from "../app/web/js/utils/dom.js";
import { sessionActionDialogCopy, sessionActionItems, sessionForkTitle } from
  "../app/web/js/features/sessions/session-actions.js";
import { resourceDescription } from
  "../app/web/js/features/settings/resource-panels.js";

const root = new URL("../app/web/", import.meta.url);
const packs = Object.fromEntries(supportedLocales.map((name) => [
  name, JSON.parse(readFileSync(new URL(`lang/${name}.json`, root), "utf8")),
]));
const localizedApiErrors = [
  ["session_capture_busy", "error.sessionCaptureBusy"],
  ["session_backup_busy", "error.sessionBackupBusy"],
  ["session_backup_limit", "error.sessionBackupLimit"],
  ["session_backup_invalid", "error.sessionBackupInvalid"],
  ["session_backup_unavailable", "error.sessionBackupUnavailable"],
  ["backup_upload_invalid", "error.backupUploadInvalid"],
  ["backup_upload_limit", "error.backupUploadLimit"],
  ["backup_upload_busy", "error.backupUploadBusy"],
  ["backup_upload_conflict", "error.backupUploadConflict"],
  ["backup_upload_incomplete", "error.backupUploadIncomplete"],
  ["backup_upload_checksum", "error.backupUploadChecksum"],
  ["backup_upload_not_found", "error.backupUploadNotFound"],
  ["backup_upload_unavailable", "error.backupUploadUnavailable"],
  ["session_message_changed", "error.sessionMessageChanged"],
  ["image_name_invalid", "error.imageNameInvalid"],
  ["write_token_required", "error.writeTokenRequired"],
  ["write_token_invalid", "error.writeTokenInvalid"],
  ["write_token_conflict", "error.writeTokenConflict"],
  ["write_admission_busy", "error.writeAdmissionBusy"],
  ["write_admission_unavailable", "error.writeAdmissionUnavailable"],
  ["purge_review_required", "error.purgeReviewRequired"],
  ["purge_client_busy", "error.purgeClientBusy"],
  ["purge_draft_unsaved", "error.purgeDraftUnsaved"],
  ["precondition_required", "error.preconditionRequired"],
  ["settings_unavailable", "error.settingsUnavailable"],
  ["home_import_busy", "error.homeImportBusy"],
  ["migration_storage_unsupported", "error.migrationStorageUnsupported"],
  ["home_restart_required", "error.homeRestartRequired"],
  ["configuration_persistence_failed", "error.configurationSaveFailed"],
  ["project_not_found", "error.projectNotFound"],
  ["project_busy", "error.projectBusy"],
  ["purge_preview_unavailable", "error.purgePreviewUnavailable"],
  ["purge_invalid", "error.purgeInvalid"],
  ["purge_request_invalid", "error.purgeRequestInvalid"],
  ["purge_request_not_found", "error.purgeRequestNotFound"],
  ["purge_unavailable", "error.purgeUnavailable"],
  ["purge_result_unavailable", "error.purgeResultUnavailable"],
  ["project_purge_aborted", "error.purgeAborted"],
  ["purge_restart_required", "error.purgeRestartRequired"],
  ["purge_request_conflict", "error.purgeRequestConflict"],
  ["purge_intent_unavailable", "error.purgeIntentUnavailable"],
  ["purge_intent_conflict", "error.purgeIntentConflict"],
  ["purge_intent_unsettled", "error.purgeIntentUnsettled"],
  ["session_create_invalid", "error.sessionCreateInvalid"],
  ["session_create_conflict", "error.sessionCreateConflict"],
  ["schedule_busy", "error.scheduleBusy"],
  ["schedule_state_conflict", "error.scheduleStateConflict"],
  ["approval_not_found", "error.approvalNotFound"],
  ["ask_not_found", "error.askNotFound"],
  ["run_not_found", "error.runNotFound"],
  ["image_cleanup_unavailable", "error.imageCleanupUnavailable"],
  ["image_cleanup_full", "error.imageCleanupFull"],
  ["queue_cleanup_full", "error.queueCleanupFull"],
  ["queue_item_invalid", "error.queueItemInvalid"],
  ["queue_item_not_found", "error.queueItemNotFound"],
  ["queue_receipt_not_found", "error.queueReceiptNotFound"],
  ["queue_run_conflict", "error.queueRunConflict"],
  ["queue_run_started", "error.queueRunStarted"],
  ["queue_state_invalid", "error.queueStateInvalid"],
  ["queue_unavailable", "error.queueUnavailable"],
  ["draft_full", "error.draftFull"],
  ["draft_invalid", "error.draftInvalid"],
  ["draft_state_conflict", "error.draftStateConflict"],
  ["draft_state_invalid", "error.draftStateInvalid"],
  ["draft_submission_conflict", "error.draftSubmissionConflict"],
  ["draft_submission_invalid", "error.draftSubmissionInvalid"],
  ["draft_submission_not_found", "error.draftSubmissionNotFound"],
  ["draft_unavailable", "error.draftUnavailable"],
  ["recovery_abandon_invalid", "error.recoveryAbandonInvalid"],
  ["recovery_result_unavailable", "error.recoveryResultUnavailable"],
  ["recovery_resume_invalid", "error.recoveryResumeInvalid"],
  ["recovery_service_unavailable", "error.recoveryServiceUnavailable"],
  ["recovery_view_too_large", "error.recoveryViewTooLarge"],
  ["session_create_failed", "error.sessionCreateFailed"],
  ["session_create_incomplete", "error.sessionCreateIncomplete"],
  ["session_result_unavailable", "error.sessionResultUnavailable"],
  ["session_service_unavailable", "error.sessionServiceUnavailable"],
  ["session_persistence_failed", "error.sessionPersistenceFailed"],
  ["session_read_failed", "error.sessionReadFailed"],
  ["session_history_unavailable", "error.sessionHistoryUnavailable"],
  ["session_events_unavailable", "error.sessionEventsUnavailable"],
  ["session_export_unavailable", "error.sessionExportUnavailable"],
  ["session_operation_failed", "error.sessionOperationFailed"],
  ["session_update_invalid", "error.sessionUpdateInvalid"],
  ["session_update_failed", "error.sessionUpdateFailed"],
  ["session_patch_invalid", "error.sessionPatchInvalid"],
  ["session_truncate_invalid", "error.sessionTruncateInvalid"],
  ["session_fork_invalid", "error.sessionForkInvalid"],
  ["session_feedback_unavailable", "error.sessionFeedbackUnavailable"],
  ["feedback_invalid", "error.feedbackInvalid"],
  ["feedback_event_invalid", "error.feedbackEventInvalid"],
  ["feedback_unavailable", "error.feedbackUnavailable"],
  ["feedback_cursor_stale", "error.feedbackCursorStale"],
  ["attachment_invalid", "error.attachmentInvalid"],
  ["attachment_not_found", "error.attachmentNotFound"],
  ["attachment_in_use", "error.attachmentInUse"],
  ["attachment_storage_full", "error.attachmentStorageFull"],
  ["attachment_response_failed", "error.attachmentResponseFailed"],
  ["image_body_invalid", "error.imageBodyInvalid"],
  ["image_type_invalid", "error.imageTypeInvalid"],
  ["unsupported_media_type", "error.unsupportedMediaType"],
  ["image_too_large", "error.imageTooLarge"],
  ["workspace_files_unavailable", "error.workspaceFilesUnavailable"],
  ["ask_answer_invalid", "error.askAnswerInvalid"],
  ["asks_unavailable", "error.asksUnavailable"],
  ["ask_result_unavailable", "error.askResultUnavailable"],
  ["approval_decision_invalid", "error.approvalDecisionInvalid"],
  ["approvals_unavailable", "error.approvalsUnavailable"],
  ["approval_result_unavailable", "error.approvalResultUnavailable"],
  ["run_start_invalid", "error.runStartInvalid"],
  ["run_result_unavailable", "error.runResultUnavailable"],
  ["todo_unavailable", "error.todoUnavailable"],
];

function assertApiErrorsLocalized(locale) {
  for (const [code, key] of localizedApiErrors)
    assert.equal(errorMessage({ code, message: "English server detail" }),
      packs[locale][key], `${locale}:${code}`);
}

const builtinResources = [
  ["agent", { id: "mdo.default", description:
    "General coding and knowledge-work Agent with inherited model settings." },
  "resource.defaultAgentDescription"],
  ["skill", { id: "project-explorer", trust: "builtin", external: false,
    description: "Inspect a repository and report its structure before making changes." },
  "resource.projectExplorerDescription"],
  ["module", { id: "mdo.default-agent", external: false,
    description: "Built-in default Agent profile." },
  "resource.defaultAgentModuleDescription"],
  ["module", { id: "mdo.core.echo", external: false,
    description: "A minimal built-in module used to verify the complete module ABI path." },
  "resource.echoModuleDescription"],
  ["module", { id: "mdo.core.todo", external: false,
    description: "Publishes a compact plan snapshot for the session conversation dock." },
  "resource.todoModuleDescription"],
];

function assertBuiltinDescriptionsLocalized(locale) {
  for (const [kind, item, key] of builtinResources) {
    assert.equal(resourceDescription(kind, item), packs[locale][key],
      `${locale}:${item.id}`);
    assert.equal(resourceDescription(kind, { ...item, external: true }),
      item.description, `${locale}:${item.id}:external`);
    assert.equal(resourceDescription(kind, { ...item, description: "Author copy" }),
      "Author copy", `${locale}:${item.id}:edited`);
  }
}

test("bundled language packs cover the annotated shell and switch without stale responses", async () => {
  const referenceKeys = Object.keys(packs["zh-CN"]).sort();
  for (const name of supportedLocales) {
    assert.deepEqual(Object.keys(packs[name]).sort(), referenceKeys, name);
    for (const key of referenceKeys) {
      const parameters = (value) => [...value.matchAll(/\{([a-zA-Z_][\w]*)\}/g)]
        .map((match) => match[1]).sort();
      assert.deepEqual(parameters(packs[name][key]), parameters(packs["zh-CN"][key]),
        `${name}:${key} parameters`);
    }
  }
  const html = readFileSync(new URL("index.html", root), "utf8");
  for (const [, key] of html.matchAll(/data-i18n(?:-title|-placeholder|-aria-label)?="([^"]+)"/g))
    assert.ok(referenceKeys.includes(key), `missing static key: ${key}`);
  for (const [tag, key] of html.matchAll(/<button class="starter"[^>]*data-prompt-key="([^"]+)"[^>]*>/g)) {
    assert.ok(referenceKeys.includes(key), `missing starter prompt: ${key}`);
    assert.match(tag, /data-prompt="[^"]+"/, `missing bundled starter fallback: ${key}`);
  }
  for (const path of [
    "js/features/sessions/session-actions.js",
    "js/features/sessions/session-export.js",
    "js/features/sessions/session-list.js",
    "js/features/chat/composer-profile.js",
    "js/features/chat/composer-project.js",
    "js/features/chat/token-meter.js",
    "js/features/chat/timeline.js",
    "js/features/chat/markdown.js",
    "js/features/chat/prompt-queue.js",
    "js/features/chat/conversation-docks.js",
    "js/features/chat/conversation-search.js",
    "js/features/chat/composer-images.js",
    "js/features/chat/image-preview.js",
    "js/features/chat/draft-store.js",
    "js/features/chat/feedback-store.js",
    "js/features/chat/message-edit-dialog.js",
    "js/features/chat/message-replacement.js",
    "js/features/chat/slash-commands.js",
    "js/features/chat/file-mentions.js",
    "js/features/settings/settings-view.js",
    "js/features/settings/resource-panels.js",
    "js/features/settings/schedule-panel.js",
    "js/features/settings/feedback-panel.js",
    "js/features/settings/project-panel.js",
    "js/features/settings/memory-panel.js",
    "js/features/settings/model-config-panel.js",
    "js/features/shell/pane-layout.js",
    "js/features/tasks/task-panel.js",
    "js/features/approvals/decision-panel.js",
    "js/features/approvals/recovery-panel.js",
    "js/features/approvals/labels.js",
    "js/features/sessions/project-dialog.js",
    "js/features/shell/workspace-startup.js",
    "js/state/asks.js",
    "js/state/todo.js",
    "js/state/resources.js",
    "js/state/sessions.js",
    "js/utils/dom.js",
    "js/app.js",
  ]) {
    const source = readFileSync(new URL(path, root), "utf8");
    for (const [, key] of source.matchAll(/\bt\("([^"]+)"/g))
      assert.ok(referenceKeys.includes(key), `missing dynamic key: ${key}`);
    if (path.endsWith("conversation-docks.js"))
      for (const [, key] of source.matchAll(/"(dock\.[^"]+)"/g))
        assert.ok(referenceKeys.includes(key), `missing dock key: ${key}`);
    if (path.endsWith("slash-commands.js"))
      for (const [, key] of source.matchAll(/descriptionKey: "([^"]+)"/g))
        assert.ok(referenceKeys.includes(key), `missing command key: ${key}`);
    if (path.endsWith("session-actions.js"))
      for (const [, key] of source.matchAll(/\bitem\("[^"]+",\s*"([^"]+)"/g))
        assert.ok(referenceKeys.includes(key), `missing action key: ${key}`);
    if (path.endsWith("session-actions.js"))
      for (const [, key] of source.matchAll(/"(sessionAction\.[^"]+)"/g))
        assert.ok(referenceKeys.includes(key), `missing session action key: ${key}`);
    if (path.endsWith("resource-panels.js"))
      for (const [, key] of source.matchAll(/:\s*"(resource\.[^"]+)"/g))
        assert.ok(referenceKeys.includes(key), `missing resource code key: ${key}`);
    if (path.endsWith("schedule-panel.js"))
      for (const [, key] of source.matchAll(/"(schedule\.[^"]+)"/g))
        assert.ok(referenceKeys.includes(key), `missing schedule key: ${key}`);
    if (path.endsWith("feedback-panel.js"))
      for (const [, key] of source.matchAll(/"(feedback\.[^"]+)"/g))
        assert.ok(referenceKeys.includes(key), `missing feedback key: ${key}`);
    if (path.endsWith("project-panel.js") || path.endsWith("project-dialog.js"))
      for (const [, key] of source.matchAll(/"(project\.[^"]+)"/g))
        assert.ok(referenceKeys.includes(key), `missing project key: ${key}`);
    if (path.endsWith("memory-panel.js"))
      for (const [, key] of source.matchAll(/"(memory\.[^"]+)"/g))
        assert.ok(referenceKeys.includes(key), `missing memory key: ${key}`);
    if (path.endsWith("model-config-panel.js"))
      for (const [, key] of source.matchAll(/"(modelConfig\.[^"]+)"/g))
        assert.ok(referenceKeys.includes(key), `missing model configuration key: ${key}`);
  }
  for (const state of ["pending", "running", "succeeded", "failed",
    "cancelled", "timed_out", "lost"])
    assert.ok(referenceKeys.includes(`task.state.${state}`));
  for (const kind of ["process", "agent", "scheduled"])
    assert.ok(referenceKeys.includes(`task.kind.${kind}`));
  for (const kind of ["created", "state_changed", "cancel_requested",
    "restored", "notice_taken"])
    assert.ok(referenceKeys.includes(`task.event.${kind}`));

  const node = {
    dataset: { i18n: "shell.newTask", i18nAriaLabel: "shell.newTask.configure" },
    textContent: "新建任务",
    attributes: new Map([["aria-label", "配置后创建任务"]]),
    getAttribute(name) { return this.attributes.get(name); },
    setAttribute(name, value) { this.attributes.set(name, value); },
  };
  const originalDocument = globalThis.document;
  const originalFetch = globalThis.fetch;
  let releaseEnglish;
  const englishGate = new Promise((resolve) => { releaseEnglish = resolve; });
  globalThis.document = {
    documentElement: { lang: "zh-CN" },
    title: "墨斗",
    querySelectorAll() { return [node]; },
  };
  globalThis.fetch = async (path) => {
    const name = path.slice("/lang/".length, -".json".length);
    if (name === "en-US") await englishGate;
    return Response.json(packs[name]);
  };
  const changes = [];
  assert.equal(t("welcome.project.prompt", {}, "bundled fallback"), "bundled fallback");
  const unsubscribe = subscribeLocale((name) => changes.push(name));
  try {
    assert.equal(await loadLocale("zh-CN"), true);
    assert.equal(node.textContent, "新建任务");
    assertApiErrorsLocalized("zh-CN");
    assertBuiltinDescriptionsLocalized("zh-CN");
    const stale = loadLocale("en-US");
    assert.equal(await loadLocale("ru-RU"), true);
    releaseEnglish();
    assert.equal(await stale, false);
    assert.equal(currentLocale(), "ru-RU");
    assert.equal(globalThis.document.documentElement.lang, "ru-RU");
    assert.equal(node.textContent, "Новая задача");
    assertApiErrorsLocalized("ru-RU");
    assertBuiltinDescriptionsLocalized("ru-RU");
    assert.equal(node.getAttribute("aria-label"), "Настроить и создать задачу");
    assert.equal(t("nav.actionsFor", { title: "Тест" }), "Действия с сеансом Тест");
    assert.equal(t("composer.backgroundQueueFailed", { title: "Тест", error: "сбой" }),
      "Не удалось отправить сообщение из очереди фонового сеанса «Тест»: сбой");
    assert.equal(t("ask.answerRequired"), "Введите ответ");
    assert.equal(sessionActionItems({ status: "active", pinned: true })[1].label, "Открепить");
    assert.equal(sessionForkTitle({ title: "Тест" }), "Тест (ветка)");
    assert.equal(errorMessage({ code: "network_error" }),
      "Нет соединения с локальной службой. Проверьте, что mdo работает.");
    assert.equal(errorMessage({ code: "session_profile_invalid",
      message: "The requested session profile is invalid" }),
      "Профиль сеанса недействителен. Проверьте модель, уровень рассуждения и разрешения.");
    assert.equal(errorMessage({ code: "invalid_response", message: "服务返回了无效响应" }),
      "Служба вернула недопустимый ответ. Повторите попытку.");
    assert.equal(errorMessage({ code: "approval_not_found",
      message: "The requested approval does not exist" }),
      packs["ru-RU"]["error.approvalNotFound"]);
    assert.equal(sessionActionDialogCopy("trash", { title: "Тест" })[1],
      "«Тест» можно восстановить из корзины.");
    assert.equal(t("missing.key", {}, "中文回退"), "中文回退");
    assert.equal(await loadLocale("en-US"), true);
    assert.equal(node.textContent, "New task");
    assertApiErrorsLocalized("en-US");
    assertBuiltinDescriptionsLocalized("en-US");
    assert.equal(errorMessage({ code: "queue_full", message: "队列已满" }),
      "The pending queue is full. Handle existing messages first.");
    assert.equal(errorMessage({ code: "schedule_busy",
      message: "The schedule cannot be deleted" }),
      packs["en-US"]["error.scheduleBusy"]);
    assert.equal(errorMessage({ code: "toString", message: "Unknown server detail" }),
      "Unknown server detail");
    assert.equal(t("draft.tooLarge"), "Draft exceeds the 64 KiB save limit");
    assert.equal(errorMessage({ code: "draft_too_large", message: "草稿过长" }),
      "Draft exceeds the 64 KiB save limit");
    assert.equal(t("draft.saveFailed", { error: "full" }), "Draft not saved: full");
    assert.equal(t("pane.loadFailed", { error: "offline" }),
      "Could not load panel layout: offline");
    assert.equal(t("startup.untitled"), "Untitled task");
    assert.equal(await loadLocale("zh-CN"), true);
    assert.equal(node.textContent, "新建任务");
    assert.equal(errorMessage({ code: "session_profile_invalid",
      message: "The requested session profile is invalid" }),
      "会话配置无效，请检查模型、思考强度和权限。");
    assertApiErrorsLocalized("zh-CN");
    assert.deepEqual(changes, ["zh-CN", "ru-RU", "en-US", "zh-CN"]);
    await assert.rejects(loadLocale("fr"), /Unsupported locale/);
  } finally {
    unsubscribe();
    releaseEnglish();
    globalThis.document = originalDocument;
    globalThis.fetch = originalFetch;
  }
});
