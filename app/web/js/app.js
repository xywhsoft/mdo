import { mountIcons } from "./components/icons.js";
import { bootstrapStore, loadBootstrap } from "./state/bootstrap.js";
import {
  sessionsStore, sessionDetailStore, loadSessions, loadSession, readSession, createSession,
  patchSession, trashSession, restoreSession, loadSessionHistory, forkSession,
  truncateSession, clearSession, exportSession, loadSessionTranscript,
} from "./state/sessions.js";
import { modelsStore, agentsStore, projectsStore, loadCatalogs, loadModels, loadAgents, loadProjects, createProject } from "./state/catalogs.js";
import {
  settingsStore, loadSettings, previewSettings, applySettings,
} from "./state/settings.js";
import {
  modulesStore, skillsStore, mcpStore, permissionsStore, storageStore,
  diagnosticsStore, migrationsStore, loadResource, loadManagementResources,
} from "./state/resources.js";
import {
  tasksStore, taskDetailStore, artifactPreviewStore, loadTasks,
  refreshSelectedTask,
} from "./state/tasks.js";
import { runsStore, loadRuns, startRun, readRun, cancelRun } from "./state/runs.js";
import { approvalsStore, loadApprovals } from "./state/approvals.js";
import { asksStore, selectAsks, clearAsks, refreshSelectedAsks } from "./state/asks.js";
import { recoveryStore, selectRecovery, loadRecovery, readRecovery, abandonRecovery } from "./state/recovery.js";
import { navigation } from "./state/navigation.js";
import { createSessionList } from "./features/sessions/session-list.js";
import { createSessionActionMenu } from "./features/sessions/session-action-menu.js";
import { sessionActionDialogCopy, sessionActionToast, sessionForkTitle } from "./features/sessions/session-actions.js";
import { SESSION_TITLE_UTF8_LIMIT, sessionTitleUtf8Bytes } from "./features/sessions/session-title.js";
import { formatSessionMarkdown, sessionMarkdownFilename } from "./features/sessions/session-export.js";
import { createProjectDialog } from "./features/sessions/project-dialog.js";
import { projectDefaultsFromWorkspace } from "./features/sessions/project-identity.js";
import { timelineStore, selectTimeline, clearTimeline, refreshSelectedTimeline, reloadSelectedTimeline } from "./features/chat/timeline-store.js";
import { todoStore, selectTodo, clearTodo } from "./state/todo.js";
import { createTimelineView } from "./features/chat/timeline.js";
import { createTracePanel } from "./features/chat/trace-panel.js";
import { createQueueGate } from "./features/chat/queue-gate.js";
import { createMessageEditDialog } from "./features/chat/message-edit-dialog.js";
import { runMessageReplacement } from "./features/chat/message-replacement.js";
import { createConversationSearch } from "./features/chat/conversation-search.js";
import { feedbackStore, selectFeedback, clearFeedback, setFeedback } from "./features/chat/feedback-store.js";
import { createConversationDocks } from "./features/chat/conversation-docks.js";
import { createPromptQueue } from "./features/chat/prompt-queue.js";
import { createDraftStore } from "./features/chat/draft-store.js";
import { createProjectDraftSelection } from "./features/chat/project-draft-selection.js";
import { createSubmissionController } from "./features/chat/submission-controller.js";
import { createNewTaskController, taskTitle } from "./features/chat/new-task-controller.js";
import { createNewTaskComposerFocus } from "./features/chat/new-task-composer-focus.js";
import { createComposerImages, unsupportedModelError } from "./features/chat/composer-images.js";
import { createComposerProject } from "./features/chat/composer-project.js";
import { createImagePreview } from "./features/chat/image-preview.js";
import { createSlashCommands } from "./features/chat/slash-commands.js";
import { createFileMentions } from "./features/chat/file-mentions.js";
import { trackComposerMenuRoom } from "./features/chat/composer-menu-room.js";
import { createComposerProfile } from "./features/chat/composer-profile.js";
import { createNewSessionProfile } from "./features/chat/new-session-profile.js";
import { createTokenMeter } from "./features/chat/token-meter.js";
import { createTaskPanel } from "./features/tasks/task-panel.js";
import { createDecisionPanel } from "./features/approvals/decision-panel.js";
import { createRecoveryPanel } from "./features/approvals/recovery-panel.js";
import { recoveryMatchesWorkspace } from "./features/approvals/recovery-decisions.js";
import { createSettingsView } from "./features/settings/settings-view.js";
import { createSchedulePanel } from "./features/settings/schedule-panel.js";
import { createProjectPanel } from "./features/settings/project-panel.js";
import { createProjectPurgeConfirmation } from "./features/settings/project-purge-confirmation.js";
import { createProjectPurgeRecovery } from "./features/settings/project-purge-recovery.js";
import { createProjectPurgeRecoveryPanel } from "./features/settings/project-purge-recovery-panel.js";
import { createResourcePanels } from "./features/settings/resource-panels.js";
import { createFeedbackPanel } from "./features/settings/feedback-panel.js";
import { createKeyboardShortcuts } from "./features/shell/keyboard-shortcuts.js";
import { createRunNotifications } from "./features/shell/run-notifications.js";
import { startWorkspaceNavigation } from "./features/shell/workspace-startup.js";
import { focusSessionComposerAfterNavigation } from "./features/shell/session-composer-focus.js";
import { createProjectSwitcher } from "./features/shell/project-switcher.js";
import { createSessionMetadataSync } from "./features/shell/session-metadata-sync.js";
import { createSessionLoadNotice } from "./features/shell/session-load-notice.js";
import { waitForSelectedDetail } from "./features/shell/session-detail-wait.js";
import { createPaneLayout } from "./features/shell/pane-layout.js";
import { trackMobileViewport } from "./features/shell/mobile-viewport.js";
import { api, ApiError, setApiWriteGuard, currentPageWriteToken, setApiWriteConflictHandler, hasPendingApiWrites } from "./api/client.js";
import { clear, element, errorMessage, isImeKey, refreshRelativeTimes, toast } from "./utils/dom.js";
import { subscribeLocale, t } from "./i18n.js";

const $ = (selector) => {
  const node = document.querySelector(selector);
  if (!node) throw new Error(`Missing UI element: ${selector}`);
  return node;
};

function terminalState(run) {
  return Boolean(run?.terminal);
}

const RUN_STATE_LABEL = Object.freeze({
  created: ["run.created", "准备中"],
  running: ["run.running", "运行中"],
  succeeded: ["run.succeeded", "已完成"],
  failed: ["run.failed", "运行失败"],
  cancelled: ["run.cancelled", "已停止"],
  timed_out: ["run.timedOut", "已超时"],
  archived: ["run.archived", "已归档"],
  trash: ["run.trash", "回收站"],
  loading: ["run.loading", "载入中"],
});

function runStateText(state) {
  const [key, fallback] = RUN_STATE_LABEL[state] ?? ["run.ready", "就绪"];
  return t(key, {}, fallback);
}

export async function boot() {
  mountIcons();
  const entryHash = location.hash;
  const purgeRecovery = createProjectPurgeRecovery({ transport: api,
    getWriteToken: currentPageWriteToken,
    onReload(projectId) {
      const saved = history.state?.mdoWorkspace;
      const route = navigation.get();
      const url = route.view === "workspace" && (!projectId || route.projectId === projectId)
        ? "#/settings/projects" : location.href;
      history.replaceState({ ...history.state,
        ...(!projectId || saved?.projectId === projectId ? { mdoWorkspace: null } : {}) }, "", url);
      window.location.reload();
    },
    async beforePrepare() {
      if (localPurgeBusy()) throw new ApiError("Wait for local operations", { code: "purge_client_busy" });
      if (!await draftStore.flushAll()) throw new ApiError("Save or copy local drafts", { code: "purge_draft_unsaved" });
      if (localPurgeBusy()) throw new ApiError("Wait for local operations", { code: "purge_client_busy" });
    },
    canComplete: () => !localPurgeBusy() && !draftStore.hasUnsaved(),
  });
  setApiWriteConflictHandler((error) => purgeRecovery.markWriteConflict(error));
  setApiWriteGuard((request) => purgeRecovery.allowsWrite(request));
  // Check portable recovery before restored drafts migrate or dispatch.
  // A failed read leaves a visible retry gate; read-only views still load.
  await purgeRecovery.refresh();

  const shell = $("#app-shell");
  const wideLayout = window.matchMedia("(min-width: 1204px)");
  const mobileLayout = window.matchMedia("(max-width: 760px)");
  trackMobileViewport(shell, mobileLayout);
  shell.dataset.inspector = "closed";
  const prompt = $("#prompt");
  const newTaskComposerFocus = createNewTaskComposerFocus({ prompt, navigation });
  const composer = $("#composer");
  trackComposerMenuRoom(composer);
  const send = $("#send");
  const stop = $("#stop");
  const composerError = $("#composer-error");
  let composerErrorState = null;
  const composerHint = $("#composer-hint");
  const composerProfileStatus = $("#composer-profile-status");
  const composerProfileReset = $("#composer-profile-reset");
  const draftStatus = $("#draft-status");
  let draftError = null;
  function renderDraftStatus() {
    draftStatus.hidden = !draftError || Boolean(bootstrapFailure());
    const reason = draftError ? errorMessage(draftError) : "";
    draftStatus.textContent = draftError ? t("draft.saveFailed",
      { error: reason }, `草稿未保存：${reason}`) : "";
  }
  const runStatus = $("#run-status");
  const runtimeState = $("#runtime-state");
  const runtimeLabel = $("#runtime-label");
  const sessionTitle = $("#session-title");
  const sessionSubtitle = $("#session-subtitle");
  const mobileTitle = $("#mobile-session-title");
  const mobileMeta = $("#mobile-session-meta");
  const exportButtons = [$("#export-session"), $("#export-session-mobile")];
  const openTrace = $("#open-trace");
  const contextList = $("#context-list");
  const workspaceChip = $("#workspace-chip");
  const workspaceLabel = $("#workspace-label");
  const mobileActivity = $("#mobile-activity-dot");
  const settingsWorkspace = $("#settings-workspace");
  const skipLink = $(".skip-link");
  const agentWorkspaceRegions = [$(".workspace-header"), $(".conversation"), $(".composer-region")];
  let settingsActive = false;
  let inspectorBeforeSettings = shell.dataset.inspector;
  let paneLayout = null;
  let sessionWritable = true;
  let selectedSessionStatus = "active";
  let activeRun = null;
  const stoppingRunIds = new Set();
  let runMonitor = 0;
  let selectedKey = "";
  let selectedDraftKey = "";
  let lastWorkspaceProjectId = "";
  let creatingSessionKey = "";
  let migratedDraftTarget = "";
  let skipLegacyDraftCapture = false;
  let tasksTimer = 0;
  let runsTimer = 0;
  let sessionTimer = 0;
  let approvalsTimer = 0;
  let routeVersion = 0;
  let routeSignature = "";
  let messageActionBusy = false;
  let pendingForkComposerFocus = "";
  let interruptRequested = false;
  let themeToggleBusy = false;
  let composerAttachments = [];
  let promptComposing = false;
  let composerImages = null;
  let newTaskController = null;
  let projectDraftSelection = null;
  let shortcuts;
  const queueBlocked = createQueueGate();
  // A cancelled run can remain nonterminal through several polls. Avoid
  // repeating DELETE while its persisted priority queue item is still waiting.
  const priorityCancelAttempts = new Set();

  function syncSettingsTitle(section = navigation.get().settingsSection) {
    const titles = {
      projects: ["settings.projects", "项目"],
      schedules: ["settings.schedules", "计划任务"],
      feedback: ["settings.feedback", "反馈"],
    };
    const [key, fallback] = titles[section] ?? ["shell.settings.title", "设置"];
    $("#settings-title").textContent = t(key, {}, fallback);
  }

  function focusForkComposerWhenReady() {
    if (!pendingForkComposerFocus) return;
    const route = navigation.get();
    if (route.view !== "workspace" ||
        `${route.projectId}/${route.sessionId}` !== pendingForkComposerFocus) {
      pendingForkComposerFocus = "";
      return;
    }
    const session = sessionDetailStore.get().data;
    if (!session || `${session.project_id}/${session.id}` !== pendingForkComposerFocus ||
        prompt.disabled) return;
    // Navigation removes the original action. Keep a user's newer focus choice.
    if (document.activeElement === document.body ||
        !document.activeElement?.isConnected) prompt.focus();
    pendingForkComposerFocus = "";
  }

  function submittingCurrent() {
    return Boolean(!navigation.get().sessionId && newTaskController?.isBusy());
  }

  function localPurgeBusy() {
    return hasPendingApiWrites() || messageActionBusy ||
      composerImages?.hasInFlight() || composerProfile.hasInFlight() ||
      submissionController?.hasInFlight() || promptQueue.hasInFlight() ||
      newTaskController?.isBusy() || newTaskController?.isPreparing() || newTaskController?.isMigrating();
  }

  const projectDialog = createProjectDialog({
    dialog: $("#project-dialog"), form: $("#project-form"),
    error: $("#project-error"), submit: $("#create-project"), modelsStore,
    onCreated(project) {
      showActiveSessions();
      navigation.newTask(project.id);
      closeDrawers();
      prompt.focus();
    },
    onUpdated(project) { projectPanel.focusProject(project.id); },
  });
  $("#close-project-dialog").addEventListener("click", () => $("#project-dialog").close());
  $("#cancel-project").addEventListener("click", () => $("#project-dialog").close());

  let cancelSessionComposerFocus = () => {};
  const sessionList = createSessionList({
    container: $("#session-list"),
    count: $("#session-count"),
    searchInput: $("#session-search"),
    store: sessionsStore,
    projectsStore,
    filter: $("#session-status-filter"),
    navigation,
    onSelect(session, event) {
      const mobileKeyboardSelection = mobileLayout.matches && event.detail === 0;
      cancelSessionComposerFocus();
      navigation.select(session.project_id, session.id);
      closeDrawers();
      // Desktop keyboard navigation stays in the list. A mobile keyboard
      // selection closes the drawer, so hand focus to the composer instead.
      if (mobileKeyboardSelection || (!mobileLayout.matches && event.detail > 0))
        cancelSessionComposerFocus = focusSessionComposerAfterNavigation({
          navigation, sessionDetailStore, prompt,
          projectId: session.project_id, sessionId: session.id,
          origin: mobileKeyboardSelection ? $("#open-sidebar") : event.currentTarget,
        });
    },
    onAction: handleSessionAction,
    onAddProject: (workspaceRoot) => createProject(projectDefaultsFromWorkspace(workspaceRoot)),
    onManageProject(projectId) {
      navigation.openSettings("projects");
      closeDrawers();
      projectPanel.focusProject(projectId);
    },
    onNewInProject: switchToProject,
  });
  const actionMenus = [
    ["#session-action-control", "#session-actions", "#session-header-menu"],
    ["#session-action-control-mobile", "#session-actions-mobile", "#session-header-menu-mobile"],
  ].map(([control, button, menu]) => createSessionActionMenu({
    control: $(control), button: $(button), menu: $(menu), navigation,
    store: sessionDetailStore, onAction: handleSessionAction,
  }));
  function switchToProject(projectId) {
    showActiveSessions();
    navigation.newTask(projectId);
    closeDrawers();
    prompt.focus();
  }
  const headerProjectSwitcher = createProjectSwitcher({
    control: $("#header-project-control"), button: $("#header-project-switch"),
    name: $("#header-project-name"), separator: $("#session-title-separator"),
    menu: $("#header-project-menu"), navigation, projectsStore,
    onSelectProject: switchToProject,
    onManageProjects() { navigation.openSettings("projects"); },
  });
  const mobileProjectSwitcher = createProjectSwitcher({
    control: $("#mobile-project-control"), button: $("#mobile-project-switch"),
    name: $("#mobile-project-name"), menu: $("#mobile-project-menu"),
    navigation, projectsStore, includeNewTask: true,
    onSelectProject: switchToProject,
    onManageProjects() { navigation.openSettings("projects"); },
  });
  mobileLayout.addEventListener("change", () => {
    for (const menu of actionMenus) menu.close();
    headerProjectSwitcher.close();
    mobileProjectSwitcher.close();
  });
  function showActiveSessions() {
    $("#session-search").value = "";
    sessionList.showActive();
  }
  $("#session-search").addEventListener("input", (event) => sessionList.setQuery(event.target.value));
  createComposerProject({ select: $("#composer-project"), row: $("#composer-project-row"),
    prompt, navigation, projectsStore, sessionsStore });

  const messageEditDialog = createMessageEditDialog({
    dialog: $("#message-edit-dialog"), form: $("#message-edit-form"),
    input: $("#message-edit-input"), cancel: $("#cancel-message-edit"),
  });
  createRunNotifications({ runsStore, navigation, settingsStore,
    onUnreadChange: (keys) => sessionList.setUnread(keys) });

  function assertReplacementProfile(attachments) {
    if (composerProfile.isBusy())
      throw new Error(t("composer.profileBusy", {}, "请等待会话配置更新完成"));
    if (attachments.length && !composerImages.supportsCurrentModel())
      throw unsupportedModelError();
  }

  function assertReplacementIdle(selected) {
    const key = `${selected.projectId}/${selected.sessionId}`;
    if (activeRun || submittingCurrent() || composerImages?.isUploading())
      throw new Error(t("messageAction.waitForRun", {}, "请在当前运行结束后操作消息"));
    if (!draftStore.isLoaded(key))
      throw new Error(t("composer.hintLoadingDraft", {}, "正在读取草稿，请稍候"));
    if (draftStore.isRunUncertain(key)) throw uncertainRunError();
    if (draftStore.submissions(key).length ||
        promptQueue.peek(selected.projectId, selected.sessionId) ||
        prompt.value.trim() || composerAttachments.length)
      throw new Error(t("messageAction.resolveDraft", {}, "请先处理草稿和待发送队列，再编辑历史消息"));
  }

  function assertMessageReplacementReady(sequence, text, attachments) {
    if (messageActionBusy) throw new Error(t("messageAction.busy", {}, "请等待当前消息操作完成"));
    if (!Number.isSafeInteger(sequence) || sequence < 1 ||
        (!text.trim() && !attachments.length))
      throw new Error(t("messageAction.invalidBoundary", {}, "这条消息没有可用的编辑边界或完整文本"));
    const session = sessionDetailStore.get().data;
    const selected = navigation.get();
    if (!session || session.project_id !== selected.projectId ||
        session.id !== selected.sessionId)
      throw new Error(t("messageAction.selectSession", {}, "请先选择会话"));
    if (session.status !== "active")
      throw new Error(t("composer.readOnly", {}, "该会话不可运行；请先恢复到进行中"));
    assertReplacementProfile(attachments);
    assertReplacementIdle(selected);
    return { session, selected };
  }

  async function replaceAndRunMessage(sequence, text, attachments, action) {
    const { session, selected } = assertMessageReplacementReady(sequence, text, attachments);
    const targetKey = `${selected.projectId}/${selected.sessionId}`;
    const originVersion = routeVersion;
    const stillSelected = () => routeVersion === originVersion &&
      selectedKey === targetKey &&
      navigation.get().projectId === selected.projectId &&
      navigation.get().sessionId === selected.sessionId;
    messageActionBusy = true;
    setRun(activeRun);
    try {
      const result = await runMessageReplacement({ session, sequence, text,
        attachments, isCurrent: stillSelected,
        validateBeforeTruncate: async () => {
          if (!await draftStore.refreshSessionSubmissions(targetKey))
            throw new Error(t("composer.hintLoadingDraft", {}, "正在读取草稿，请稍候"));
          await promptQueue.select(selected.projectId, selected.sessionId);
          if (!stillSelected()) return;
          assertReplacementProfile(attachments);
          assertReplacementIdle(selected);
        },
        loadHistory: loadSessionHistory, truncate: truncateSession, startRun,
        onTruncated(updated) {
          // A committed edit/retry starts a new turn. An old history filter
          // must not hide its messages; cancelled or rejected edits keep it.
          conversationSearch.close();
          timelineView.follow();
          sessionDetailStore.setData(updated);
          void Promise.allSettled([reloadSelectedTimeline(),
            selectTodo(updated.project_id, updated.id)]);
        },
        onStartFailure(updated, error, current) {
          const key = `${updated.project_id}/${updated.id}`;
          draftStore.edit(key, text, attachments, true);
          if (error.runAdmissionUncertain) {
            draftStore.setRunUncertain(key, true);
            void loadRuns();
          }
          if (!current) return;
          prompt.value = text;
          composerAttachments = [...attachments];
          composerImages.set(attachments);
          prompt.dispatchEvent(new Event("input", { bubbles: true }));
          showComposerError(error, error.runAdmissionUncertain
            ? () => t("composer.runAdmissionUncertain") : "");
          setRun(activeRun);
          prompt.focus();
        },
        onStarted(run) { monitorRun(run); },
      });
      void Promise.allSettled([...(stillSelected() ? [refreshSelectedTimeline()] : []),
        loadTasks(), loadRuns(), loadRecovery()]);
      if (stillSelected()) {
        toast(action === "retry"
          ? t("messageAction.retried", {}, "已在当前会话重试")
          : t("messageAction.edited", {}, "已在当前会话发送编辑后的消息"));
      } else if (!result.current)
        toast(t("messageAction.backgroundRun", {}, "原会话已在后台重新运行"));
    } finally {
      messageActionBusy = false;
      setRun(activeRun);
      // setRun releases the editor's disabled state. Restore focus only
      // after that release, and respect any later control/route selection.
      if (stillSelected() && (document.activeElement === document.body ||
          !document.activeElement?.isConnected)) prompt.focus({ preventScroll: true });
    }
  }

  let conversationSearch;
  function isCurrentMessageOwner(owner, version = routeVersion) {
    const route = navigation.get();
    return routeVersion === version && route.projectId === owner.projectId &&
      route.sessionId === owner.sessionId &&
      selectedKey === `${owner.projectId}/${owner.sessionId}`;
  }
  const timelineView = createTimelineView({
    container: $("#timeline"), welcome: $("#welcome"),
    toBottom: $("#to-bottom"), store: timelineStore,
    sessionStore: sessionDetailStore,
    feedbackStore,
    onSearchCount: (count, historyLost) => conversationSearch?.setCount(count, historyLost),
    onFeedback: async (eventId, value, owner) => {
      if (!isCurrentMessageOwner(owner))
        throw new Error(t("messageAction.ownerChanged", {}, "会话已切换，请重新选择消息"));
      await setFeedback(owner.projectId, owner.sessionId, eventId, value);
    },
    onFork: async (throughSequence, owner) => {
      const version = routeVersion;
      if (!isCurrentMessageOwner(owner, version))
        throw new Error(t("messageAction.ownerChanged", {}, "会话已切换，请重新选择消息"));
      const session = sessionDetailStore.get().data;
      if (!session || `${session.project_id}/${session.id}` !== selectedKey || activeRun)
        throw new Error(t("sessionAction.forkWaitForRun", {}, "请在当前运行结束后分叉会话"));
      if (session.status !== "active")
        throw new Error(t("composer.readOnly", {}, "该会话不可运行；请先恢复到进行中"));
      const history = await loadSessionHistory(session);
      if (!isCurrentMessageOwner(owner, version))
        throw new Error(t("messageAction.ownerChanged", {}, "会话已切换，请重新选择消息"));
      if (sessionDetailStore.get().data?.status !== "active")
        throw new Error(t("composer.readOnly", {}, "该会话不可运行；请先恢复到进行中"));
      const boundary = throughSequence ?? history.last_sequence;
      if (!Number.isSafeInteger(boundary) || boundary < 0 ||
          boundary > history.last_sequence)
        throw new Error(t("sessionAction.forkHistoryChanged", {}, "此回复已不在当前会话历史中，请刷新会话"));
      const fork = await forkSession({ ...session, etag: history.etag, revision: history.revision }, {
        title: sessionForkTitle(session),
        through_sequence: boundary,
      });
      if (isCurrentMessageOwner(owner, version)) {
        pendingForkComposerFocus = `${fork.project_id}/${fork.id}`;
        navigation.select(fork.project_id, fork.id);
        focusForkComposerWhenReady();
        toast(t("sessionAction.forked", {}, "已创建会话分支"));
      } else toast(t("sessionAction.backgroundFork", {}, "原会话的分支已在后台创建"));
    },
    onEdit: async (sequence, text, attachments, owner, opener,
      resolveText = () => text) => {
      const version = routeVersion;
      if (!isCurrentMessageOwner(owner, version))
        throw new Error(t("messageAction.ownerChanged", {}, "会话已切换，请重新选择消息"));
      assertMessageReplacementReady(sequence, text, attachments);
      const completeText = await resolveText();
      if (!isCurrentMessageOwner(owner, version))
        throw new Error(t("messageAction.ownerChanged", {}, "会话已切换，请重新选择消息"));
      assertMessageReplacementReady(sequence, completeText, attachments);
      const edited = await messageEditDialog.open(completeText, attachments, opener);
      if (edited !== null) {
        if (!isCurrentMessageOwner(owner, version))
          throw new Error(t("messageAction.ownerChanged", {}, "会话已切换，请重新选择消息"));
        await replaceAndRunMessage(sequence, edited, attachments, "edit");
      }
    },
    onRetry: async (sequence, text, attachments, owner,
      resolveText = () => text) => {
      const version = routeVersion;
      if (!isCurrentMessageOwner(owner, version))
        throw new Error(t("messageAction.ownerChanged", {}, "会话已切换，请重新选择消息"));
      assertMessageReplacementReady(sequence, text, attachments);
      const completeText = await resolveText();
      if (!isCurrentMessageOwner(owner, version))
        throw new Error(t("messageAction.ownerChanged", {}, "会话已切换，请重新选择消息"));
      return replaceAndRunMessage(sequence, completeText, attachments, "retry");
    },
  });
  conversationSearch = createConversationSearch({
    bar: $("#conversation-find"), input: $("#conversation-find-input"),
    count: $("#conversation-find-count"), closeButton: $("#close-find"),
    openButtons: [$("#open-find"), $("#open-find-mobile")],
    navigation, prompt, onQuery: (query) => timelineView.search(query),
    onOpen() {
      closeDrawers();
      conversationDocks.setSearchActive(true);
    },
    onClose: () => conversationDocks.setSearchActive(false),
  });
  createImagePreview({
    dialog: $("#image-preview"), image: $("#image-preview-content"),
    caption: $("#image-preview-name"),
    closeButton: $("#close-image-preview"), navigation,
  });
  const tracePanel = createTracePanel({
    panel: $("#trace-panel"), list: $("#trace-list"),
    summary: $("#trace-summary"), store: timelineStore, navigation,
  });
  createTaskPanel({
    container: $("#task-list"),
    detailContainer: $("#task-detail"),
    summary: $("#task-summary"),
    store: tasksStore,
    detailStore: taskDetailStore,
    previewStore: artifactPreviewStore,
    onChanged: () => void loadRuns(),
  });
  createDecisionPanel({
    container: $("#approval-list"),
    summary: $("#approval-summary"),
    store: approvalsStore,
    onChanged: () => Promise.all([loadTasks(), loadRuns()]),
    onDrained: () => {
      if (!mobileLayout.matches || shell.dataset.inspector !== "open" ||
          $("#decisions-tab").getAttribute("aria-selected") !== "true" ||
          document.activeElement !== $("#decisions-tab")) return;
      setDrawer("inspector", false, { persist: false });
      if (!prompt.disabled) prompt.focus({ preventScroll: true });
    },
  });
  const conversationDocks = createConversationDocks({
    container: $("#conversation-docks"), navigation, tasksStore, approvalsStore,
    asksStore,
    todoStore,
    runsStore,
    onOpenTasks: () => { selectInspectorTab("tasks"); setDrawer("inspector", true); },
    onChanged: () => Promise.all([loadTasks(), loadRuns(), refreshSelectedAsks()]),
    onDecisionArrived: (card, kind) => {
      if (!mobileLayout.matches) return;
      if (kind === "approval" && shell.dataset.inspector === "open" &&
          $("#decisions-tab").getAttribute("aria-selected") === "true") {
        const target = [...$("#approval-list").children]
          .find((item) => item.dataset.approvalId === card.dataset.approvalId);
        if (target) {
          const panel = $("#decisions-panel");
          panel.scrollTop += target.getBoundingClientRect().top -
            panel.getBoundingClientRect().top;
          target.querySelector("h3")?.focus({ preventScroll: true });
        }
        return;
      }
      const sidebarOpen = shell.dataset.sidebar === "open";
      const inspectorOpen = shell.dataset.inspector === "open";
      if (!sidebarOpen && !inspectorOpen &&
          (promptComposing || prompt.value.length || composerAttachments.length ||
           (document.activeElement !== prompt &&
            document.activeElement !== document.body))) return;
      if (sidebarOpen) setDrawer("sidebar", false, { persist: false });
      if (inspectorOpen) setDrawer("inspector", false, { persist: false });
      const heading = card.querySelector("h3") ?? card;
      heading.tabIndex = -1;
      heading.focus({ preventScroll: true });
    },
  });
  let draftStore;
  let submissionController;
  const promptQueue = createPromptQueue({
    container: $("#prompt-queue"), navigation, modelsStore,
    isRunActive: () => Boolean(activeRun),
    isSessionRunActive: (key) => {
      const [projectId, sessionId] = key.split("/");
      return Boolean(activeRun && activeRun.project_id === projectId &&
        activeRun.session_id === sessionId) ||
        (runsStore.get().data?.items ?? []).some((run) =>
          run.project_id === projectId && run.session_id === sessionId &&
          !terminalState(run));
    },
    isSessionWritable: () => sessionWritable,
    isRunReviewPending: (key) => draftStore?.isRunUncertain(key) ?? false,
    stagedEntries: () => {
      if (!navigation.get().sessionId)
        return (draftStore?.submissions("") ?? []).map((item) => ({
          id: item.id, text: item.text, attachments: item.attachments, staged: true,
          rejected: item.state === "rejected", profile: item.profile,
        }));
      const current = navigation.get();
      const staged = current.sessionId ?
        (draftStore?.submissions(selectedKey) ?? []).filter((item) =>
          !promptQueue.find(current.projectId, current.sessionId, item.id))
          .map((item) => ({ id: item.id, text: item.text,
            attachments: item.attachments, staged: true,
            rejected: item.state === "rejected", profile: item.profile })) : [];
      return staged;
    },
    onRetry: async () => {
      if (!sessionWritable) return;
      const selected = navigation.get();
      const key = `${selected.projectId}/${selected.sessionId}`;
      if (!await draftStore.ensureLoaded(key)) return;
      if (`${navigation.get().projectId}/${navigation.get().sessionId}` !== key) return;
      if (draftStore.isRunUncertain(key)) {
        showComposerError(uncertainRunError());
        return;
      }
      await promptQueue.select(selected.projectId, selected.sessionId);
      if (!sessionWritable ||
          `${navigation.get().projectId}/${navigation.get().sessionId}` !== key) return;
      const first = promptQueue.peek(selected.projectId, selected.sessionId);
      if (draftStore.submission(key) &&
          !await submissionController.reconcile(key)) return;
      if (!sessionWritable) return;
      if (first?.state === "sending")
        await promptQueue.retry(selected.projectId, selected.sessionId, first.id);
      if (first?.state === "staged")
        await promptQueue.promote(selected.projectId, selected.sessionId, first.id);
      queueBlocked.unblock(`${selected.projectId}/${selected.sessionId}`);
      await maybeCancelPriorityRun();
      await dispatchQueued();
      void submissionController.pump(key);
    },
    onRemoved: async (key, removed) => {
      // Removing an accepted item is an explicit decision after review.
      // The persisted uncertainty guard still blocks until acknowledged.
      if (removed?.run_id) queueBlocked.unblock(key);
      await maybeCancelPriorityRun();
      await dispatchQueued();
    },
  });
  const slashCommands = createSlashCommands({
    composer, input: prompt,
    onExecute: async (command) => {
      const session = sessionDetailStore.get().data;
      if (command === "/new") openNewTask();
      else if (command === "/model") {
        const modelSelect = $("#composer-model");
        if (modelSelect.disabled)
          throw new Error(t("slash.modelBusy", {}, "当前无法切换模型"));
        const choices = [...modelSelect.options].filter((option) =>
          option.value && !option.disabled);
        if (choices.length < 2)
          throw new Error(t("slash.noOtherModel", {}, "没有其他可切换的模型"));
        const index = choices.findIndex((option) =>
          option.value === modelSelect.value);
        modelSelect.value = choices[(index + 1) % choices.length].value;
        modelSelect.dispatchEvent(new Event("change", { bubbles: true }));
      }
      else if (command === "/settings") navigation.openSettings("general");
      else if (command === "/theme") await toggleTheme();
      else if (command === "/help") {
        shortcuts.openHelp();
      } else if (command === "/stop") {
        if (!activeRun)
          throw new Error(t("slash.noActiveRun", {}, "当前没有运行中的任务"));
        stop.click();
      } else {
        if (!session)
          throw new Error(t("slash.selectSession", {}, "请先选择会话"));
        if (command === "/export") await handleSessionAction("export", session);
        else {
          if (activeRun)
            throw new Error(t("slash.waitForHistory", {}, "请在当前运行结束后修改会话历史"));
          if (command === "/fork") await handleSessionAction("fork", session);
          else if (command === "/clear") await handleSessionAction("clear", session);
        }
      }
    },
  });
  const fileMentions = createFileMentions({ composer, input: prompt, navigation });
  const tokenMeter = createTokenMeter({
    root: $("#context-meter"), trigger: $("#context-meter-trigger"),
    ring: $("#context-meter-ring"), panel: $("#context-meter-panel"),
    estimate: $("#composer-input-estimate"), prompt,
    modelSelect: $("#composer-model"),
    attachments: () => composerAttachments,
    sessionStore: sessionDetailStore, timelineStore, modelsStore, runsStore,
  });
  draftStore = createDraftStore({
    isWritePaused: purgeRecovery.isPaused,
    onRestore(text, attachments, uncertainRun, submission) {
      prompt.value = text;
      composerAttachments = attachments;
      composerImages?.set(attachments);
      resizePrompt();
      tokenMeter.refresh();
      if (uncertainRun && selectedKey) {
        showComposerError(uncertainRunError());
        setRun(activeRun);
      } else if (["posting", "rejected"].includes(submission?.state) && selectedKey &&
          !submissionController?.isBusy(selectedKey)) {
        showComposerError(submission.state === "rejected"
          ? submissionRejectedError() : submissionUncertainError());
        setRun(activeRun);
      }
      promptQueue.render();
      const pendingProject = draftStore.newTask()?.project_id;
      if (!selectedKey && pendingProject &&
          navigation.get().view === "workspace" &&
          !navigation.get().sessionId &&
          navigation.get().projectId !== pendingProject)
        navigation.revalidate();
    },
    onError(error) {
      draftError = error;
      renderDraftStatus();
    },
    onSaved() {
      draftError = null;
      renderDraftStatus();
    },
    onLoaded() { setRun(activeRun); promptQueue.render(); },
  });
  projectDraftSelection = createProjectDraftSelection({ draftStore, navigation,
    onFailure: (error) => showComposerError(error),
    onChange: () => setRun(activeRun),
    onMigrated: () => { skipLegacyDraftCapture = selectedDraftKey === ""; },
  });
  navigation.setNewTaskGuard(
    () => projectDraftSelection.owner(),
    () => toast(t("composer.newTaskOtherProject"), "error"));
  submissionController = createSubmissionController({
    isWritePaused: purgeRecovery.isPaused,
    draftStore, promptQueue,
    onPersisted(key, submission) {
      clearSubmittedComposer(key, submission);
      if (selectedOwnsDraft(key)) setRun(activeRun);
    },
    onPromoted(key, submission) {
      queueBlocked.unblock(key);
      if (selectedOwnsDraft(key)) {
        if (submission.interrupt) void maybeCancelPriorityRun();
        void dispatchQueued();
      }
    },
    onConsumed(key) {
      if (selectedOwnsDraft(key)) void refreshSelectedTimeline();
      void loadRuns();
    },
    onReview(key, submission, error) {
      if (selectedOwnsDraft(key))
        showComposerError(submission.state === "rejected"
          ? submissionRejectedError() : submissionUncertainError(),
          error || "");
      else toast(t("composer.backgroundQueueFailed", {
        title: key, error: error ? errorMessage(error) :
          t("composer.submissionUncertain"),
      }), "error");
    },
    onRestored(key, restored) {
      if (!selectedOwnsDraft(key)) return;
      prompt.value = restored.text;
      composerAttachments = restored.attachments;
      composerImages.set(restored.attachments);
      resizePrompt();
      tokenMeter.refresh();
      hideComposerError();
      prompt.focus();
    },
    onChange(key) {
      if (selectedOwnsDraft(key)) {
        if (["submission_unconfirmed", "submission_rejected"].includes(
              composerError.dataset.code) &&
            !draftStore.submissions(key).some((item) =>
              ["posting", "rejected"].includes(item.state))) hideComposerError();
        setRun(activeRun);
      }
      promptQueue.render();
    },
  });
  draftStore.select("");
  if (!purgeRecovery.isPaused()) void projectDraftSelection.restoreLegacy();
  const composerProfile = createComposerProfile({
    modelSelect: $("#composer-model"), reasoningSelect: $("#composer-reasoning"),
    permissionSelect: $("#composer-permission"), navigation,
    sessionStore: sessionDetailStore, modelsStore, agentsStore, projectsStore,
    draftStore, status: composerProfileStatus, resetButton: composerProfileReset,
    isRunActive: () => Boolean(activeRun),
    hasPendingSubmission(session) {
      const key = `${session.project_id}/${session.id}`;
      return Boolean(draftStore.submissions(key).length ||
        promptQueue.hasUnsettled(session.project_id, session.id) ||
        (runsStore.get().data?.items ?? []).some((run) =>
          run.project_id === session.project_id && run.session_id === session.id &&
          !terminalState(run)));
    },
    onBusyChange: () => setRun(activeRun),
    onSelectionChange() {
      tokenMeter.refresh();
      composerImages?.refresh();
      if (composerError.dataset.code === "image_model_unsupported" &&
          composerImages?.supportsCurrentModel()) hideComposerError();
    },
  });
  newTaskController = createNewTaskController({
    isWritePaused: purgeRecovery.isPaused,
    draftStore, newId: () => promptQueue.newId(), createSession,
    async findSession(projectId, sessionId) {
      return (await api.get(`/projects/${projectId}/sessions/${sessionId}`)).data;
    },
    onPersisted(item, projectId) {
      const before = navigation.get();
      if (before.view === "workspace" && !before.sessionId &&
          (before.projectId || "default") !== projectId)
        navigation.revalidate();
      const route = navigation.get();
      if (route.view !== "workspace" || route.sessionId ||
          (route.projectId || "default") !== projectId) return;
      clearSubmittedComposer("", item);
      if (selectedDraftKey !== "") {
        selectedDraftKey = "";
        draftStore.select("");
      }
      setRun(activeRun);
    },
    onMigrated(key) {
      showActiveSessions();
      const [projectId, sessionId] = key.split("/");
      const current = navigation.get();
      if (current.view === "workspace" && !current.sessionId &&
          current.projectId === projectId) {
        const returnFocus = composerError.contains(document.activeElement) ||
          document.activeElement === $("#composer-project");
        newTaskComposerFocus.migrate(projectId, sessionId);
        creatingSessionKey = key;
        migratedDraftTarget = key;
        navigation.select(projectId, sessionId);
        selectTimeline(projectId, sessionId);
        if (returnFocus && !prompt.disabled) prompt.focus({ preventScroll: true });
      }
    },
    onReview(error) {
      newTaskComposerFocus.cancel();
      const current = navigation.get();
      if (current.view === "workspace" && !current.sessionId) {
        showComposerError(newTaskReviewError(), error);
      } else toast(errorMessage(error), "error");
    },
    onChange() {
      if (!navigation.get().sessionId && draftStore.newTask() &&
          selectedDraftKey !== "") {
        selectedDraftKey = "";
        draftStore.select("");
      }
      if (!navigation.get().sessionId) setRun(activeRun);
      promptQueue.render();
    },
  });
  composerImages = createComposerImages({
    composer, prompt, button: $("#composer-attach"), input: $("#composer-file"),
    strip: $("#composer-images"), modelSelect: $("#composer-model"),
    navigation, modelsStore,
    sessionStore: sessionDetailStore,
    ensureSession: (text) => ensureSession(text, composerProfile.selection()),
    onChange(attachments) {
      composerAttachments = attachments;
      draftStore.edit(selectedDraftKey, prompt.value, attachments);
      tokenMeter.refresh();
    },
    async onBeforeRemove(owner, id) {
      const key = `${owner.projectId}/${owner.sessionId}`;
      // A queue poll may see the durable marker immediately. Persist the
      // current reference first, so it cannot delete an unsaved image before
      // the removal either commits or rolls back.
      if (!await draftStore.flush(key))
        throw new Error(t("image.removeRollback", {},
          "草稿未保存，图片已恢复"));
      try { await api.markImageDiscard(owner.projectId, owner.sessionId, id); }
      catch (error) {
        // The image still belongs to the saved draft. Report that state,
        // rather than showing an untranslated queue/storage error.
        const retained = new Error(error?.message ?? "");
        retained.code = error?.code === "queue_cleanup_full"
          ? "image_cleanup_full" : "image_cleanup_unavailable";
        retained.cause = error;
        throw retained;
      }
    },
    async onRemove(owner, id, previous) {
      const key = `${owner.projectId}/${owner.sessionId}`;
      let saved = false;
      try { saved = await draftStore.flush(key); }
      finally {
        if (!saved) draftStore.editAttachments(key, previous, true);
      }
      if (!saved) return false;
      try { await api.deleteImage(owner.projectId, owner.sessionId, id); }
      catch (error) {
        // The draft no longer references this image. A transient DELETE
        // failure must use the same retry path as discarded uploads and
        // removed queue items, even if the user has left this session.
        if (error?.code !== "attachment_not_found")
          promptQueue.rememberUnusedImages(owner.projectId, owner.sessionId, [id]);
      }
      if (selectedOwnsDraft(key) && ["image_cleanup_unavailable",
        "image_cleanup_full"].includes(composerError.dataset.code))
        hideComposerError();
      return true;
    },
    onUploading(uploading) {
      if (uploading && ["image_model_unsupported", "image_selection_invalid"]
        .includes(composerError.dataset.code)) hideComposerError();
      setRun(activeRun);
    },
    onDiscardedUpload(owner, id) {
      promptQueue.rememberUnusedImages(owner.projectId, owner.sessionId, [id]);
    },
    onError: showComposerError,
  });
  composerImages.set(composerAttachments);
  createRecoveryPanel({
    container: $("#recovery-list"),
    summary: $("#recovery-summary"),
    store: recoveryStore,
    onResume: (run, owner) => {
      const ownsView = recoveryMatchesWorkspace(owner, navigation.get());
      if (ownsView) {
        monitorRun(run);
        if (composerError.dataset.code === "recovery_required") hideComposerError();
      }
      void Promise.all([loadRuns(), loadTasks(), ...(ownsView ? [refreshSelectedTimeline()] : [])]);
    },
    onAbandon: async (owner) => {
      queueBlocked.unblock(`${owner.project_id}/${owner.session_id}`);
      if (recoveryMatchesWorkspace(owner, navigation.get())) await dispatchQueued();
    },
  });

  function updateDecisionCount() {
    const approvals = Number(approvalsStore.get().data?.total ?? 0);
    const recovery = recoveryStore.get().data;
    const interrupted = recovery?.resume_required
      ? Math.max(1, Number(recovery.total ?? recovery.items?.length ?? 0)) : 0;
    const total = approvals + interrupted;
    const count = $("#approval-count");
    count.textContent = String(total);
    count.hidden = total === 0;
  }
  approvalsStore.subscribe(updateDecisionCount);
  recoveryStore.subscribe(updateDecisionCount);
  const settingsView = createSettingsView({
    form: $("#settings-form"),
    store: settingsStore,
    navigation,
    onApplied: () => Promise.all([loadBootstrap(), loadCatalogs()]),
  });
  const purgeConfirmation = createProjectPurgeConfirmation({
    dialog: $("#project-purge-confirm-dialog"), recovery: purgeRecovery,
    onResolved: () => {
      void loadProjects();
      // Native dialog close restores its previous focus after this callback.
      // Focus the result panel only after that restoration has settled.
      window.requestAnimationFrame(() => purgeRecoveryPanel.focus());
    },
  });
  const projectPanel = createProjectPanel({
    panel: $('[data-settings-panel="projects"]'), projectsStore,
    modelsStore, projectDialog, navigation, purgeRecovery, purgeConfirmation,
  });
  const purgeRecoveryPanel = createProjectPurgeRecoveryPanel({
    panel: $("#project-purge-recovery"), notice: $("#project-purge-notice"),
    recovery: purgeRecovery, navigation,
    unsentSnapshots: draftStore.unsentSnapshots,
    onReview: (intent, origin) => projectPanel.openPurgeReview(intent, origin),
  });
  const schedulePanel = createSchedulePanel({
    panel: $('[data-settings-panel="schedules"]'),
    projectsStore, agentsStore, modelsStore,
  });
  const feedbackPanel = createFeedbackPanel({
    panel: $('[data-settings-panel="feedback"]'), navigation,
  });
  createResourcePanels({
    modelsStore,
    agentsStore,
    stores: {
      modules: modulesStore,
      skills: skillsStore,
      mcp: mcpStore,
      permissions: permissionsStore,
      storage: storageStore,
      diagnostics: diagnosticsStore,
      migrations: migrationsStore,
    },
    reload: (name) => name === "models" ? loadModels()
      : name === "modules" ? Promise.all([loadResource("modules"), loadAgents()])
        : loadResource(name),
  });

  let sendBlockedByState = true;
  function syncSendDisabled() {
    const localCommand = slashCommands.isExact(prompt.value.trim());
    send.disabled = sendBlockedByState ||
      ((composerImages?.isUploading() ||
        composerImages?.hasUnsupportedDraft()) && !localCommand);
  }

  function syncStopBusy() {
    stop.setAttribute("aria-disabled", String(Boolean(activeRun &&
      (activeRun.cancel_requested || stoppingRunIds.has(activeRun.id)))));
  }

  function setRun(run) {
    const wasActive = Boolean(activeRun);
    activeRun = run && !terminalState(run) ? run : null;
    if (wasActive !== Boolean(activeRun)) promptQueue.render();
    const guide = settingsStore.get().data?.composer?.submit_mode === "guide";
    const shown = run ?? { state: sessionWritable ? "idle" : selectedSessionStatus };
    runStatus.dataset.state = shown.state;
    runStatus.lastElementChild.textContent = runStateText(shown.state);
    send.hidden = false;
    stop.hidden = !activeRun;
    syncStopBusy();
    const route = navigation.get();
    const creatingNewTask = !route.sessionId &&
      Boolean(newTaskController?.isBusy());
    const pendingNewTask = !route.sessionId && Boolean(draftStore.newTask());
    const migratingNewTask = !route.sessionId &&
      Boolean(newTaskController?.isMigrating());
    const selectingProjectDraft = !route.sessionId && selectedDraftKey === "" &&
      !pendingNewTask && !projectDraftSelection?.owner();
    const firstSubmission = draftStore.submission(selectedDraftKey);
    const creatingSession = Boolean(creatingSessionKey) &&
      creatingSessionKey === `${route.projectId}/${route.sessionId}`;
    const serviceFailed = Boolean(bootstrapFailure());
    if (newTaskController?.isPreparing() || migratingNewTask)
      newTaskComposerFocus.capture();
    // Keep keyboard focus while a newly created session loads its detail.
    prompt.disabled = serviceFailed || messageActionBusy || purgeRecovery.isPaused() ||
      selectingProjectDraft ||
      projectDraftSelection?.isMigrating() ||
      newTaskController?.isPreparing() ||
      (!sessionWritable && !creatingSession) || migratingNewTask;
    newTaskComposerFocus.restore();
    sendBlockedByState = serviceFailed || purgeRecovery.isPaused() || !(sessionWritable || creatingSession) ||
      composerProfile.isBusy() || messageActionBusy ||
      !draftStore.isLoaded(selectedDraftKey) ||
      selectingProjectDraft ||
      draftStore.isRunUncertain(selectedDraftKey) ||
      draftStore.submissions(selectedDraftKey).length >= 20 ||
      projectDraftSelection?.isMigrating() ||
      newTaskController?.isPreparing() ||
      (!route.sessionId && Boolean(newTaskController?.isBlocked())) ||
      migratingNewTask ||
      Boolean(submissionController?.isReleasing(selectedKey));
    syncSendDisabled();
    composerImages?.setWritable(!serviceFailed && !messageActionBusy && !purgeRecovery.isPaused() &&
      sessionWritable && !creatingSession && !pendingNewTask);
    composerProfile.setRunActive(Boolean(activeRun),
      creatingNewTask || messageActionBusy || purgeRecovery.isPaused());
    send.setAttribute("aria-label", activeRun || creatingNewTask
      ? t("composer.queue", {}, "加入待发送队列")
      : t("shell.send", {}, "发送任务"));
    composerHint.textContent = purgeRecovery.isPaused() ? t("error.purgeReviewRequired") : messageActionBusy
      ? t("messageAction.busy", {}, "请等待当前消息操作完成")
      : draftStore.isRunUncertain(selectedDraftKey)
      ? t("composer.hintReviewRun") : (selectingProjectDraft ||
          !draftStore.isLoaded(selectedDraftKey))
      ? t("composer.hintLoadingDraft") : firstSubmission?.state === "rejected"
      ? t("composer.hintRejectedSubmission") : firstSubmission?.state === "posting" &&
          !submissionController?.isBusy(selectedKey)
      ? t("composer.hintReviewSubmission") : draftStore.submissions(selectedDraftKey).length
      ? t("composer.hintSavingSubmission") : activeRun
      ? (guide
        ? t("composer.hintGuide", {}, "Enter 中断并发送 · Ctrl Enter 排队")
        : t("composer.hintQueue", {}, "Enter 排队 · Ctrl Enter 中断并发送"))
      : route.sessionId && runsStore.get().status === "error"
      ? t("composer.hintRunListUnavailable")
      : t("composer.hintIdle", {}, "Enter 发送 · Shift Enter 换行");
    $("#shortcut-enter-description").textContent = guide
      ? t("composer.shortcutEnterGuide", {}, "发送；运行中中断并优先发送")
      : t("composer.shortcutEnterQueue", {}, "发送；运行中加入待发送队列");
    $("#shortcut-control-enter-description").textContent = guide
      ? t("composer.shortcutControlGuide", {}, "运行中加入待发送队列")
      : t("composer.shortcutControlQueue", {}, "中断当前运行，优先发送输入");
    mobileActivity.hidden = !activeRun && !creatingNewTask;
  }
  settingsStore.subscribe(() => setRun(activeRun));
  let purgeWasPaused = purgeRecovery.isPaused();
  purgeRecovery.subscribe(() => {
    const paused = purgeRecovery.isPaused();
    setRun(activeRun);
    if (purgeWasPaused && !paused) {
      draftStore.resumeSaves();
      void projectDraftSelection.restoreLegacy();
      void newTaskController.reconcile();
      if (selectedKey) void submissionController.reconcile(selectedKey);
    }
    purgeWasPaused = paused;
  });

  function currentWorkspace() {
    const route = navigation.get();
    const session = sessionDetailStore.get().data;
    if (session && session.project_id === route.projectId &&
        session.id === route.sessionId)
      return session.workspace_root || "";
    const project = (projectsStore.get().data?.items ?? [])
      .find((item) => item.id === (route.projectId || "default"));
    return project?.managed ? project.workspace_root || "" : "";
  }

  function syncWorkspaceChip() {
    const root = currentWorkspace();
    workspaceLabel.textContent = root
      ? root.split(/[\\/]/).filter(Boolean).at(-1) || root
      : t("composer.localWorkspace", {}, "本地工作区");
    workspaceChip.title = root
      ? t("composer.workspacePath", { path: root }, `当前工作目录：${root}`)
      : t("composer.workspace", {}, "当前工作目录");
  }

  function updateContext(state) {
    clear(contextList);
    const route = navigation.get();
    const session = state.data?.project_id === route.projectId &&
      state.data?.id === route.sessionId ? state.data : null;
    if (!session) {
      contextList.append(
        element("dt", { text: t("inspector.context.status", {}, "状态") }),
        element("dd", { text: state.status === "loading"
          ? t("inspector.context.loading", {}, "正在载入…")
          : route.sessionId ? t("inspector.context.noSession", {}, "未选择会话")
            : t("inspector.context.newTask", {}, "新任务") }),
        element("dt", { text: t("inspector.context.project", {}, "项目") }),
        element("dd", { text: route.projectId || "default" }),
        element("dt", { text: t("inspector.context.workspace", {}, "工作区") }),
        element("dd", { text: currentWorkspace() ||
          t("composer.localWorkspace", {}, "本地工作区") }),
      );
      return;
    }
    const reasoningKeys = {
      none: "settings.reasoningNone", minimal: "settings.reasoningMinimal",
      low: "settings.reasoningLow", medium: "settings.reasoningMedium",
      high: "settings.reasoningHigh", xhigh: "settings.reasoningXhigh",
      max: "settings.reasoningMax",
    };
    const effort = session.reasoning_effort;
    const reasoning = effort ? t(reasoningKeys[effort] || "", {}, effort)
      : t("inspector.context.auto", {}, "自动");
    const values = [
      [t("inspector.context.project", {}, "项目"), session.project_id],
      [t("inspector.context.agent", {}, "Agent"), session.agent_id],
      [t("inspector.context.model", {}, "模型"), session.model_id],
      [t("inspector.context.protocol", {}, "协议"), session.protocol],
      [t("inspector.context.reasoning", {}, "推理"), reasoning],
      [t("inspector.context.workspace", {}, "工作区"), session.workspace_root ||
        t("inspector.context.default", {}, "默认")],
      [t("inspector.context.outputLimit", {}, "输出上限"), session.max_output_tokens
        ? `${session.max_output_tokens} tokens` : t("inspector.context.default", {}, "默认")],
      [t("inspector.context.configRevision", {}, "配置版本"), session.config_revision],
      [t("inspector.context.moduleGeneration", {}, "模块代次"), session.module_generation],
      [t("inspector.context.skillGeneration", {}, "Skill 代次"), session.skill_generation],
    ];
    for (const [label, value] of values) {
      contextList.append(element("dt", { text: label }), element("dd", { text: value || "—" }));
    }
  }

  function currentExportSession() {
    const route = navigation.get();
    const session = sessionDetailStore.get().data;
    return route.view === "workspace" && session?.project_id === route.projectId &&
      session?.id === route.sessionId ? session : null;
  }

  function updateExportButtons() {
    const available = Boolean(currentExportSession());
    for (const button of exportButtons) button.disabled = !available;
    openTrace.disabled = !available;
  }

  function syncPromptPlaceholder() {
    prompt.placeholder = sessionWritable ? t("shell.prompt", {}, "向墨斗描述任务…")
      : t("composer.readOnly", {}, "该会话不可运行；请先恢复到进行中");
  }

  function sessionStatusSuffix(status) {
    if (status === "archived") return ` · ${t("run.archived")}`;
    if (status === "trash") return ` · ${t("run.trash")}`;
    return "";
  }

  sessionDetailStore.subscribe((state) => {
    const session = state.data;
    updateExportButtons();
    sessionWritable = session ? session.status === "active" : !navigation.get().sessionId;
    selectedSessionStatus = session?.status ?? (navigation.get().sessionId ? "loading" : "active");
    promptQueue.render();
    syncPromptPlaceholder();
    setRun(activeRun);
    focusForkComposerWhenReady();
    if (session) {
      const title = session.title || t("nav.untitled");
      sessionTitle.textContent = title;
      const statusText = sessionStatusSuffix(session.status);
      sessionSubtitle.textContent = `${session.agent_id} · ${session.model_id}${statusText}`;
      mobileTitle.textContent = title;
      mobileMeta.textContent = statusText;
    }
    syncWorkspaceChip();
    updateContext(state);
  });
  createSessionLoadNotice({ navigation, store: sessionDetailStore,
    conversation: $(".conversation"), notice: $("#conversation-load"),
    heading: $("#conversation-load-title"),
    description: $("#conversation-load-description"),
    retry: $("#conversation-load-retry"), sessionTitle, sessionSubtitle,
    mobileTitle, mobileMeta, prompt });
  projectsStore.subscribe(() => {
    syncWorkspaceChip();
    if (!navigation.get().sessionId) updateContext(sessionDetailStore.get());
  });
  subscribeLocale(() => {
    renderDraftStatus();
    setRun(activeRun);
    updateContext(sessionDetailStore.get());
    syncPromptPlaceholder();
    syncWorkspaceChip();
    syncRuntimeLabel();
    const focusedAction = composerError.contains(document.activeElement)
      ? [...composerError.querySelectorAll(".composer-error-action")]
        .indexOf(document.activeElement) : -1;
    const currentError = composerErrorState;
    if (composerError.dataset.code === "recovery_required")
      showComposerError(recoveryRequiredError(), currentError?.note);
    else if (composerError.dataset.code === "run_admission_uncertain")
      showComposerError(uncertainRunError(), currentError?.note);
    else if (composerError.dataset.code === "submission_unconfirmed")
      showComposerError(submissionUncertainError(), currentError?.note);
    else if (composerError.dataset.code === "submission_rejected")
      showComposerError(submissionRejectedError(), currentError?.note);
    else if (composerError.dataset.code === "new_task_unconfirmed")
      showComposerError(newTaskReviewError(), currentError?.note);
    else if (!composerError.hidden && currentError)
      showComposerError(currentError.error, currentError.note);
    if (focusedAction >= 0)
      (composerError.querySelectorAll(".composer-error-action")[focusedAction] ?? prompt)
        .focus({ preventScroll: true });
    skipLink.textContent = t(settingsActive ? "shell.skipSettings" : "shell.skip");
    if (settingsActive) syncSettingsTitle();
    const session = sessionDetailStore.get().data;
    if (session && selectedKey === `${session.project_id}/${session.id}`) {
      const statusText = sessionStatusSuffix(session.status);
      sessionSubtitle.textContent = `${session.agent_id} · ${session.model_id}${statusText}`;
      mobileMeta.textContent = statusText;
      if (!session.title) {
        sessionTitle.textContent = t("nav.untitled");
        mobileTitle.textContent = t("nav.untitled");
      }
    } else if (!selectedKey) {
      sessionTitle.textContent = t("shell.newTask");
      sessionSubtitle.textContent = t("shell.newTaskSubtitle", {
        project: $("#composer-project").value || "default" });
      mobileTitle.textContent = t("shell.newTask");
      mobileMeta.textContent = "";
    }
  });

  function bootstrapFailure() {
    const state = bootstrapStore.get();
    if (state.status !== "ready" || state.data?.stage !== "failed") return "";
    const message = state.data.message || "";
    if (message.includes("external Home is already in use"))
      return t("shell.homeInUse", {},
        "便携数据目录正由另一个 mdo 进程使用。请关闭那个实例后重启当前程序。");
    return t("shell.bootstrapFailed", { message },
      `本地服务初始化失败：${message}`);
  }

  function syncRuntimeLabel() {
    const state = bootstrapStore.get();
    const failure = bootstrapFailure();
    runtimeState.dataset.state = state.status === "error" || failure ? "error"
      : state.data?.ready ? "ready" : "loading";
    runtimeLabel.textContent = failure || (state.status === "error"
      ? errorMessage(state.error)
      : state.data?.ready
        ? t("shell.localService", { version: state.data.version }, `本地服务 ${state.data.version}`)
        : state.data?.message || t("shell.connecting", {}, "正在连接本地服务…"));
    if (failure || composerError.dataset.code === "bootstrap_failed") {
      hideComposerError();
      renderDraftStatus();
      setRun(activeRun);
    }
  }
  bootstrapStore.subscribe(syncRuntimeLabel);

  tasksStore.subscribe((state) => {
    const active = (state.data?.items ?? []).some((item) => !item.terminal);
    mobileActivity.hidden = !active && !activeRun;
  });

  function findActiveRun() {
    const selected = navigation.get();
    const run = [...(runsStore.get().data?.items ?? [])].reverse().find((item) =>
      item.project_id === selected.projectId && item.session_id === selected.sessionId && !terminalState(item));
    if (run) {
      monitorRun(run);
      void maybeCancelPriorityRun();
    }
    else if (!activeRun) {
      setRun(null);
      void dispatchQueued();
    }
  }
  runsStore.subscribe(findActiveRun);
  runsStore.subscribe(() => { void promptQueue.flushUnusedImages(); });

  function scheduleRunPoll(delay = 750) {
    window.clearTimeout(runMonitor);
    runMonitor = window.setTimeout(async () => {
      if (!activeRun || document.hidden) return;
      const runId = activeRun.id;
      const version = routeVersion;
      const key = selectedKey;
      const stillSelected = () => routeVersion === version && selectedKey === key;
      const stillPollingRun = () => stillSelected() && activeRun?.id === runId;
      const resumeCurrentRun = () => {
        if (activeRun?.id === runId && selectedKey === key && !document.hidden)
          scheduleRunPoll();
      };
      let responseApplied = false;
      try {
        const run = await readRun(runId);
        // Clearing the timer cannot cancel a read already in flight.
        if (!stillPollingRun()) { resumeCurrentRun(); return; }
        const returnFocus = document.activeElement === stop;
        setRun(run);
        responseApplied = true;
        await Promise.all([refreshSelectedTimeline(), refreshSelectedAsks()]);
        if (!stillSelected() || (activeRun && activeRun.id !== runId)) {
          resumeCurrentRun();
          return;
        }
        if (terminalState(run)) {
          await Promise.all([loadSessions(), loadRuns(), loadTasks(), loadRecovery()]);
          if (!stillSelected() || (activeRun && activeRun.id !== runId)) {
            resumeCurrentRun();
            return;
          }
          await refreshSelectedQueue();
          if (!stillSelected() || (activeRun && activeRun.id !== runId)) {
            resumeCurrentRun();
            return;
          }
          if (returnFocus &&
              (document.activeElement === stop ||
                document.activeElement === document.body) &&
              !prompt.disabled &&
              shell.dataset.inspector !== "open" &&
              shell.dataset.sidebar !== "open" &&
              !document.querySelector("dialog[open]"))
            prompt.focus({ preventScroll: true });
          return;
        }
        if (stillPollingRun()) scheduleRunPoll();
      } catch (error) {
        if (!stillSelected() || (!responseApplied && !stillPollingRun()) ||
            (activeRun && activeRun.id !== runId)) {
          resumeCurrentRun();
          return;
        }
        setRun(null);
        showComposerError(error);
      }
    }, delay);
  }

  function monitorRun(run) {
    if (!run || terminalState(run)) { setRun(run); return; }
    if (activeRun?.id === run.id && runMonitor) return;
    setRun(run);
    scheduleRunPoll(300);
  }

  async function maybeCancelPriorityRun() {
    if (purgeRecovery.isPaused()) return;
    const selected = navigation.get();
    const run = activeRun;
    const key = `${selected.projectId}/${selected.sessionId}`;
    const entry = promptQueue.peek(selected.projectId, selected.sessionId);
    if (!run || terminalState(run) || run.project_id !== selected.projectId ||
        run.session_id !== selected.sessionId || queueBlocked.has(key) ||
        entry?.state !== "pending" || !entry.priority ||
        priorityCancelAttempts.has(run.id)) return;
    priorityCancelAttempts.add(run.id);
    try {
      const cancelled = await cancelRun(run.id);
      if (navigation.get().projectId !== selected.projectId ||
          navigation.get().sessionId !== selected.sessionId) return;
      if (terminalState(cancelled)) {
        setRun(cancelled);
        await Promise.all([refreshSelectedTimeline(), loadTasks(), loadRuns()]);
        await dispatchQueued();
      } else monitorRun(cancelled);
    } catch (error) {
      // Keep the attempt recorded so polling does not hammer a failed request.
      // The normal Stop action remains available for an explicit retry.
      showComposerError(error);
    }
  }

  async function ensurePromptReady(projectId, sessionId, priority = false) {
    for (let attempt = 0; attempt < 4; attempt += 1) {
      try {
        const recovery = await readRecovery(projectId, sessionId);
        if (!recovery?.resume_required) return;
        if (priority && recovery.total === 0) {
          await abandonRecovery(recovery);
          await loadRecovery();
          return;
        }
        void loadRecovery();
        throw recoveryRequiredError();
      } catch (error) {
        if (!priority || attempt === 3 ||
            !["recovery_state_conflict", "session_busy"].includes(error?.code))
          throw error;
        // Cancellation can become terminal just before agent_done reaches the
        // ledger. Re-read its revision and sequence; never reuse a stale one.
        await new Promise((resolve) => window.setTimeout(resolve, 150));
      }
    }
  }

  async function dispatchQueued() {
    if (purgeRecovery.isPaused()) return;
    const selected = navigation.get();
    const key = `${selected.projectId}/${selected.sessionId}`;
    const session = sessionDetailStore.get().data;
    if (!selected.sessionId || !session || session.project_id !== selected.projectId ||
        session.id !== selected.sessionId || session.status !== "active" ||
        activeRun || messageActionBusy ||
        queueBlocked.has(key) ||
        promptQueue.peek(selected.projectId, selected.sessionId)?.state !== "pending") return;
    if (!await draftStore.ensureLoaded(key) || draftStore.isRunUncertain(key)) return;
    if (runsStore.get().status !== "ready") return;
    if ((runsStore.get().data?.items ?? []).some((run) =>
      run.project_id === selected.projectId && run.session_id === selected.sessionId && !terminalState(run))) return;
    await promptQueue.exclusive(selected.projectId, selected.sessionId,
      async () => {
        const stillSelected = () => navigation.get().projectId === selected.projectId &&
          navigation.get().sessionId === selected.sessionId;
        const entry = promptQueue.peek(selected.projectId, selected.sessionId);
        if (messageActionBusy || !entry || entry.state !== "pending") return;
        try {
          await ensurePromptReady(selected.projectId, selected.sessionId,
            Boolean(entry.priority));
          if (!stillSelected() || messageActionBusy ||
              draftStore.isRunUncertain(key)) return;
          await promptQueue.markSending(selected.projectId, selected.sessionId, entry.id);
          const run = await startRun(selected.projectId, selected.sessionId,
            entry.text, entry.attachments ?? [], entry.id);
          if (stillSelected()) monitorRun(run);
          await promptQueue.remove(selected.projectId, selected.sessionId, entry.id);
          if (stillSelected()) hideComposerError();
          await Promise.all([...(stillSelected() ? [refreshSelectedTimeline()] : []),
            loadTasks(), loadRuns(), loadRecovery()]);
        } catch (error) {
          if (error?.code === "session_busy") {
            try {
              await promptQueue.select(selected.projectId, selected.sessionId);
              const current = promptQueue.find(selected.projectId,
                selected.sessionId, entry.id);
              let deferred = current?.state === "pending";
              if (current?.state === "sending" && !current.run_id &&
                  !current.start_claimed) {
                await promptQueue.retry(selected.projectId,
                  selected.sessionId, entry.id);
                deferred = true;
              }
              if (deferred) {
                queueBlocked.unblock(key);
                if (stillSelected()) hideComposerError();
                void loadRuns();
                return;
              }
            } catch { /* A competing claim needs the normal review path. */ }
          }
          if (error?.code !== "recovery_required") queueBlocked.block(key);
          if (error.runAdmissionUncertain) {
            draftStore.setRunUncertain(key, true);
            void loadRuns();
            if (stillSelected()) setRun(activeRun);
          }
          try { await promptQueue.select(selected.projectId, selected.sessionId); }
          catch { /* Preserve the original dispatch error. */ }
          const accepted = Boolean(promptQueue.find(selected.projectId,
            selected.sessionId, entry.id)?.run_id);
          if (stillSelected()) showComposerError(accepted
            ? uncertainRunError() : error, accepted ? "" :
            (error.runAdmissionUncertain
              ? () => t("composer.runAdmissionUncertain") : ""));
          else toast(`${t("composer.backgroundQueueFailed",
            { title: session.title, error: errorMessage(error) },
            `后台会话“${session.title}”的待发送消息未发出：${errorMessage(error)}`)}` +
            (error.runAdmissionUncertain
              ? ` ${t("composer.runAdmissionUncertain")}` : ""), "error");
        }
      });
  }

  async function refreshSelectedQueue() {
    const selected = navigation.get();
    if (selected.view !== "workspace" || !selected.sessionId) return;
    try {
      await promptQueue.select(selected.projectId, selected.sessionId);
      await submissionController.reconcile(`${selected.projectId}/${selected.sessionId}`);
      if (draftStore.isRunUncertain(`${selected.projectId}/${selected.sessionId}`))
        showComposerError(uncertainRunError());
      const current = navigation.get();
      if (current.projectId !== selected.projectId ||
          current.sessionId !== selected.sessionId) return;
      await maybeCancelPriorityRun();
      await dispatchQueued();
    } catch (error) {
      const current = navigation.get();
      if (current.projectId !== selected.projectId ||
          current.sessionId !== selected.sessionId) return;
      const first = draftStore.submissions(
        `${selected.projectId}/${selected.sessionId}`)[0];
      if (first && ["posting", "rejected"].includes(first.state)) {
        const reviewError = first.state === "rejected"
          ? submissionRejectedError() : submissionUncertainError();
        if (composerError.dataset.code !== reviewError.code)
          showComposerError(reviewError, error);
      } else showComposerError(error);
    }
  }

  navigation.subscribe(async ({ view, projectId, sessionId, settingsSection }) => {
    shell.toggleAttribute("data-settings-open", view === "settings");
    updateExportButtons();
    if (pendingForkComposerFocus &&
        (view !== "workspace" || `${projectId}/${sessionId}` !== pendingForkComposerFocus))
      pendingForkComposerFocus = "";
    const nextSignature = `${view}/${projectId}/${sessionId}/${settingsSection}`;
    if (nextSignature !== routeSignature) {
      routeSignature = nextSignature;
      routeVersion += 1;
    }
    if (view === "settings") {
      const enteringSettings = !settingsActive;
      if (!settingsActive) inspectorBeforeSettings = shell.dataset.inspector;
      settingsActive = true;
      settingsWorkspace.hidden = false;
      for (const region of agentWorkspaceRegions) region.hidden = true;
      settingsView.setActive(true);
      skipLink.href = "#settings-content";
      skipLink.textContent = t("shell.skipSettings");
      settingsView.selectSection(settingsSection);
      const standalonePage = !["general", "agent", "web"].includes(settingsSection);
      syncSettingsTitle(settingsSection);
      $("#settings-revision").hidden = standalonePage;
      $("#settings-actions").hidden = standalonePage;
      if (settingsSection === "schedules") void schedulePanel.refresh();
      if (settingsSection === "feedback") void feedbackPanel.refresh();
      closeDrawers();
      setDrawer("inspector", false, { persist: false });
      if (enteringSettings) $("#settings-title").focus({ preventScroll: true });
      if (!settingsStore.get().data) await loadSettings();
      return;
    }
    const focusWasInSettings = settingsActive &&
      settingsWorkspace.contains(document.activeElement);
    settingsWorkspace.hidden = true;
    for (const region of agentWorkspaceRegions) region.hidden = false;
    if (settingsActive) settingsView.setActive(false);
    if (focusWasInSettings) prompt.focus();
    timelineView.restorePreviewScroll();
    skipLink.href = "#timeline";
    skipLink.textContent = t("shell.skip");
    if (settingsActive) {
      settingsActive = false;
      setDrawer("inspector", (paneLayout?.inspectorOpen() ??
        inspectorBeforeSettings === "open") && wideLayout.matches,
        { persist: false });
    }
    composer.toggleAttribute("data-new-task", !sessionId);
    const key = projectId && sessionId ? `${projectId}/${sessionId}` : "";
    const nextDraftKey = projectDraftSelection.key({ projectId, sessionId });
    const currentProjectId = projectId || "default";
    // New tasks share an empty draft key, but a transient composer error still
    // belongs to the project where it occurred.
    if (!key && !selectedKey && lastWorkspaceProjectId &&
        lastWorkspaceProjectId !== currentProjectId)
      hideComposerError();
    lastWorkspaceProjectId = currentProjectId;
    const version = routeVersion;
    const stillSelected = () => {
      const route = navigation.get();
      return routeVersion === version && route.view === "workspace" &&
        route.projectId === projectId && route.sessionId === sessionId &&
        selectedKey === key;
    };
    if (!key) {
      const project = projectId || "default";
      sessionTitle.textContent = t("shell.newTask");
      sessionSubtitle.textContent = t("shell.newTaskSubtitle", { project });
      mobileTitle.textContent = t("shell.newTask");
      mobileMeta.textContent = "";
      syncWorkspaceChip();
    }
    // Returning from Settings may keep the same selected session, so refresh
    // the context before the same-session fast path below.
    updateContext(sessionDetailStore.get());
    if (key === selectedKey && nextDraftKey === selectedDraftKey) {
      if (key) {
        const finishLoad = queueBlocked.beginLoad(key);
        try {
          const detail = await loadSession(projectId, sessionId);
          if (!stillSelected()) return;
          if (detail.status !== "ready" || detail.data?.project_id !== projectId ||
              detail.data.id !== sessionId) {
            if (detail.status === "ready")
              sessionDetailStore.setError(new Error("Session detail does not match the selected task"));
            return;
          }
          // Returning from Settings may refresh the sidebar, but that catalog
          // must not hold the queue gate for this already selected session.
          void loadSessions();
          if (!stillSelected()) return;
          await promptQueue.select(projectId, sessionId);
          if (!stillSelected()) return;
          await submissionController.reconcile(key);
        } catch (error) {
          if (stillSelected()) showComposerError(error);
          return;
        }
        finally { finishLoad(); }
        if (!stillSelected()) return;
        void maybeCancelPriorityRun();
        void dispatchQueued();
      }
      return;
    }
    // The new-session controller already copied the editor into the target
    // draft. Capturing it again would resurrect a sent prompt in the source.
    if (migratedDraftTarget !== key &&
        !(skipLegacyDraftCapture && selectedDraftKey === ""))
      draftStore.capture(selectedDraftKey, prompt.value, composerAttachments);
    migratedDraftTarget = "";
    skipLegacyDraftCapture = false;
    selectedKey = key;
    selectedDraftKey = nextDraftKey;
    hideComposerError();
    draftStore.select(nextDraftKey);
    window.clearTimeout(runMonitor);
    runMonitor = 0;
    activeRun = null;
    setRun(null);
    if (!key) {
      selectRecovery("", "");
      clearTimeline();
      clearTodo();
      clearAsks();
      clearFeedback();
      sessionDetailStore.reset();
      void newTaskController?.reconcile();
      return;
    }
    selectRecovery(projectId, sessionId);
    sessionDetailStore.reset();
    selectTimeline(projectId, sessionId);
    void selectTodo(projectId, sessionId);
    void selectAsks(projectId, sessionId);
    void selectFeedback(projectId, sessionId);
    const finishLoad = queueBlocked.beginLoad(key, "runtime");
    try {
      const detailReady = waitForSelectedDetail({ navigation,
        store: sessionDetailStore, isSelected: stillSelected });
      void loadSession(projectId, sessionId);
      const [detail] = await Promise.all([detailReady, loadRuns(), loadRecovery(),
        promptQueue.select(projectId, sessionId)]);
      if (!stillSelected()) return;
      if (detail?.status !== "ready") return;
      if (detail.data?.project_id !== projectId || detail.data?.id !== sessionId) {
        sessionDetailStore.setError(new Error("Session detail does not match the selected task"));
        return;
      }
      await submissionController.reconcile(key);
    } catch (error) {
      if (stillSelected()) showComposerError(error);
      return;
    }
    finally {
      finishLoad();
      if (creatingSessionKey === key) {
        creatingSessionKey = "";
        if (stillSelected()) setRun(activeRun);
      }
    }
    if (!stillSelected()) return;
    findActiveRun();
    void maybeCancelPriorityRun();
    void dispatchQueued();
  });

  function hideComposerError() {
    composerErrorState = null;
    const failure = bootstrapFailure();
    if (failure) {
      composerError.hidden = false;
      composerError.textContent = failure;
      composerError.dataset.code = "bootstrap_failed";
      delete composerError.dataset.queueItemId;
      return;
    }
    composerError.hidden = true;
    composerError.textContent = "";
    delete composerError.dataset.code;
    delete composerError.dataset.queueItemId;
  }
  function recoveryRequiredError() {
    const error = new Error(t("composer.recoveryRequired", {},
      "上轮运行尚未恢复，请先在“决策”中处理；输入和待发送消息会保留。"));
    error.code = "recovery_required";
    return error;
  }
  function uncertainRunError() {
    const selected = navigation.get();
    const accepted = Boolean(promptQueue.peek(selected.projectId,
      selected.sessionId)?.run_id);
    const error = new Error(t(accepted ? "composer.runAcceptedReview" :
      "composer.runAdmissionUncertain"));
    error.code = "run_admission_uncertain";
    return error;
  }
  function submissionUncertainError() {
    const error = new Error(t("composer.submissionUncertain"));
    error.code = "submission_unconfirmed";
    return error;
  }
  function submissionRejectedError() {
    const error = new Error(t("composer.submissionRejected"));
    error.code = "submission_rejected";
    return error;
  }
  function syncRecoveryNotice(state) {
    const selected = navigation.get();
    const recovery = state.data;
    const required = !activeRun && recovery?.resume_required === true &&
      !recovery.unavailable && recovery.project_id === selected.projectId &&
      recovery.session_id === selected.sessionId;
    if (required) {
      if (composerError.hidden) {
        showComposerError(recoveryRequiredError());
      }
    } else if (state.status === "ready" &&
               composerError.dataset.code === "recovery_required") {
      hideComposerError();
    }
  }
  recoveryStore.subscribe(syncRecoveryNotice);
  function bindComposerReview(review, ownsContext, runReview,
    presentFailure = (failure) => [failure, ""]) {
    let reviewing = false;
    review.addEventListener("click", async () => {
      if (reviewing || !ownsContext()) return;
      reviewing = true;
      review.setAttribute("aria-disabled", "true");
      try {
        if (await runReview() && ownsContext()) {
          const hadFocus = document.activeElement === review;
          hideComposerError();
          if (hadFocus) prompt.focus();
        }
      } catch (failure) {
        if (ownsContext()) {
          const hadFocus = document.activeElement === review;
          const [error, note] = presentFailure(failure);
          showComposerError(error, note);
          if (hadFocus)
            (composerError.querySelector(".composer-error-action") ?? prompt)
              .focus();
        }
      } finally {
        reviewing = false;
        review.removeAttribute("aria-disabled");
      }
    });
  }
  function localComposerError(key, fallback = "", code = "") {
    const error = new Error(t(key, {}, fallback));
    error.code = code;
    error.localizedMessageKey = key;
    error.localizedMessageFallback = fallback;
    return error;
  }
  function newTaskReviewError() {
    const key = newTaskController.canChangeProfile()
      ? "composer.newTaskRejected" : "composer.newTaskReview";
    return localComposerError(key, "", "new_task_unconfirmed");
  }
  function showComposerError(error, note = "") {
    if (bootstrapFailure()) { hideComposerError(); return; }
    composerErrorState = { error, note };
    composerError.textContent = error?.localizedMessageKey
      ? t(error.localizedMessageKey, {}, error.localizedMessageFallback || error.message)
      : errorMessage(error);
    const detail = typeof note === "function" ? note()
      : typeof note === "object" && note ? errorMessage(note) : note;
    if (detail) composerError.append(" ", detail);
    composerError.dataset.code = error?.code || "";
    if (error?.queueItemId) composerError.dataset.queueItemId = error.queueItemId;
    else delete composerError.dataset.queueItemId;
    if (error?.runAdmissionUncertain || error?.code === "run_admission_uncertain") {
      const acknowledge = element("button", {
        className: "composer-error-action",
        text: t("composer.reviewedRun"),
        attrs: { type: "button" },
      });
      acknowledge.addEventListener("click", () => {
        if (!selectedKey || !draftStore.isRunUncertain(selectedKey)) return;
        draftStore.setRunUncertain(selectedKey, false);
        hideComposerError();
        setRun(activeRun);
        promptQueue.render();
        void dispatchQueued();
        prompt.focus();
      });
      composerError.append(acknowledge);
    }
    if (["submission_unconfirmed", "submission_rejected"].includes(error?.code)) {
      const multiple = draftStore.submissions(selectedKey).length > 1;
      const review = element("button", {
        className: "composer-error-action",
        text: t(error.code === "submission_rejected"
          ? (multiple ? "composer.retryRejectedSubmission" :
            "composer.restoreRejectedSubmission")
          : (multiple ? "composer.retryReviewedSubmission" :
            "composer.reviewSubmission")),
        attrs: { type: "button" },
      });
      const key = selectedKey;
      bindComposerReview(review, () => selectedOwnsDraft(key),
        () => submissionController.review(key), (failure) => {
          const first = draftStore.submissions(key)[0];
          if (!first || !["posting", "rejected"].includes(first.state))
            return [failure, ""];
          return [first.state === "rejected"
            ? submissionRejectedError() : submissionUncertainError(),
          failure];
        });
      composerError.append(review);
    }
    if (error?.code === "new_task_unconfirmed") {
      const review = element("button", {
        className: "composer-error-action",
        text: t(newTaskController.canChangeProfile()
          ? "composer.retryNewTaskProfile" : "composer.retryNewTask"),
        attrs: { type: "button" },
      });
      const routeVersionAtReview = routeVersion;
      bindComposerReview(review,
        () => routeVersion === routeVersionAtReview &&
          !navigation.get().sessionId,
        () => newTaskController.review(composerProfile.selection()));
      composerError.append(review);
    }
    if (error?.code === "recovery_required") {
      const openDecisions = element("button", {
        className: "composer-error-action",
        text: t("composer.openRecovery", {}, "打开恢复决策"),
        attrs: { type: "button" },
      });
      openDecisions.addEventListener("click", () => {
        selectInspectorTab("decisions");
        setDrawer("inspector", true);
        const target = document.querySelector("#recovery-list .recovery-submit") ??
          $("#decisions-tab");
        target.focus();
        target.scrollIntoView({ block: "nearest" });
      });
      composerError.append(openDecisions);
    }
    composerError.hidden = false;
  }

  async function ensureSession(text, profile, { stageDraft = true } = {}) {
    const origin = navigation.get();
    const originVersion = routeVersion;
    if (origin.sessionId) return origin;
    const title = taskTitle(text, t("composer.imageTask", {}, "图片任务"));
    const owner = await newTaskController.createForAttachment({
      projectId: origin.projectId || "default", title,
      profile });
    if (stageDraft && routeVersion === originVersion && prompt.value)
      draftStore.edit(`${owner.projectId}/${owner.sessionId}`,
        prompt.value, composerAttachments, true);
    return owner;
  }

  function selectedOwnsDraft(key) {
    const route = navigation.get();
    return route.view === "workspace" && selectedKey === key &&
      (key ? `${route.projectId}/${route.sessionId}` === key : !route.sessionId);
  }

  function clearSubmittedComposer(key, submission) {
    draftStore.clearIfMatches(key, submission.text,
      submission.attachments);
    if (!selectedOwnsDraft(key) || prompt.value !== submission.text ||
        JSON.stringify(composerAttachments) !==
          JSON.stringify(submission.attachments)) return;
    prompt.value = "";
    composerAttachments = [];
    composerImages.clear();
    resizePrompt();
    tokenMeter.refresh();
    prompt.focus();
  }

  async function submitExistingSession(origin, rawInput, attachments, interrupt,
    profile) {
    const key = `${origin.projectId}/${origin.sessionId}`;
    hideComposerError();
    try {
      await submissionController.submit(key, rawInput, attachments, interrupt,
        profile);
    } catch (error) {
      if (selectedOwnsDraft(key)) showComposerError(error);
      else toast(t("composer.backgroundQueueFailed",
        { title: key, error: errorMessage(error) }), "error");
    }
  }

  async function submitPrompt({ text, attachments = [], interrupt = false,
    fromComposer = true }) {
    if (messageActionBusy) {
      showComposerError(localComposerError("messageAction.busy",
        "请等待当前消息操作完成"));
      return;
    }
    fileMentions.hide();
    // Commands act on the UI and keep an unsent image draft. They must stay
    // usable while an attachment is still being stored.
    if (fromComposer && slashCommands.consumeExact(text)) return;
    if ((!text && !attachments.length) || composerImages.isUploading()) return;
    const rawInput = fromComposer ? prompt.value : text;
    const origin = navigation.get();
    const originVersion = routeVersion;
    if (!draftStore.isLoaded(selectedDraftKey) &&
        !await draftStore.ensureLoaded(selectedDraftKey)) return;
    if (draftStore.isRunUncertain(selectedDraftKey)) {
      showComposerError(uncertainRunError());
      return;
    }
    if (routeVersion !== originVersion) return;
    if (composerProfile.isBusy()) {
      showComposerError(localComposerError("composer.profileBusy",
        "请等待会话配置更新完成"));
      return;
    }
    if (fromComposer && attachments.length &&
        !composerImages.supportsCurrentModel()) {
      showComposerError(unsupportedModelError());
      return;
    }
    const profile = composerProfile.selection();
    if (!origin.sessionId && !attachments.length) {
      hideComposerError();
      try {
        await newTaskController.submit({ projectId: origin.projectId || "default",
          text: rawInput, profile, fromComposer });
      } catch (error) { showComposerError(error); }
      return;
    }
    try {
      const owner = origin.sessionId ? origin :
        await ensureSession(rawInput, profile, { stageDraft: fromComposer });
      await submitExistingSession(owner, rawInput, attachments, interrupt,
        profile);
    } catch (error) { showComposerError(error); }
  }
  composer.addEventListener("submit", (event) => {
    event.preventDefault();
    const interrupt = interruptRequested;
    interruptRequested = false;
    void submitPrompt({ text: prompt.value.trim(),
      attachments: [...composerAttachments], interrupt });
  });

  stop.addEventListener("click", async () => {
    if (!activeRun || activeRun.cancel_requested ||
        stoppingRunIds.has(activeRun.id)) return;
    const runId = activeRun.id;
    const { projectId, sessionId } = navigation.get();
    const stillSelected = () => {
      const route = navigation.get();
      return route.projectId === projectId && route.sessionId === sessionId &&
        (!activeRun || activeRun.id === runId);
    };
    stoppingRunIds.add(runId);
    syncStopBusy();
    try {
      const run = await cancelRun(runId);
      if (stillSelected()) {
        const returnFocus = document.activeElement === stop;
        if (activeRun?.id === runId) setRun(run);
        if (returnFocus && stop.hidden && !prompt.disabled)
          prompt.focus({ preventScroll: true });
        await Promise.all([refreshSelectedTimeline(), loadTasks(), loadRuns(),
          ...(terminalState(run) ? [loadRecovery()] : [])]);
        if (terminalState(run)) await dispatchQueued();
      } else await Promise.all([loadTasks(), loadRuns()]);
    } catch (error) {
      if (stillSelected()) showComposerError(error);
      else toast(errorMessage(error), "error");
    } finally {
      stoppingRunIds.delete(runId);
      syncStopBusy();
    }
  });

  function resizePrompt() {
    prompt.style.height = "auto";
    prompt.style.height = `${Math.min(prompt.scrollHeight, 336)}px`;
  }
  window.addEventListener("resize", resizePrompt);
  prompt.addEventListener("compositionstart", () => { promptComposing = true; });
  prompt.addEventListener("compositionend", () => { promptComposing = false; });
  prompt.addEventListener("blur", () => { promptComposing = false; });
  prompt.addEventListener("input", () => {
    resizePrompt();
    draftStore.edit(selectedDraftKey, prompt.value, composerAttachments);
    tokenMeter.refresh();
    syncSendDisabled();
  });
  prompt.addEventListener("keydown", (event) => {
    if (slashCommands.onKeyDown(event)) return;
    if (fileMentions.onKeyDown(event)) return;
    if (event.key === "Enter" && !event.shiftKey &&
        !isImeKey(event, promptComposing)) {
      event.preventDefault();
      const modified = event.ctrlKey || event.metaKey;
      const guide = settingsStore.get().data?.composer?.submit_mode === "guide";
      interruptRequested = Boolean(activeRun &&
        (guide ? !modified : modified));
      composer.requestSubmit();
    }
  });

  for (const starter of document.querySelectorAll("[data-prompt-key]")) {
    starter.addEventListener("click", () => {
      if (submittingCurrent() || composerImages.isUploading() ||
          composerProfile.isBusy()) return;
      void submitPrompt({ text: t(starter.dataset.promptKey, {}, starter.dataset.prompt),
        fromComposer: false });
    });
  }
  document.querySelector("[data-starter-schedules]")?.addEventListener("click", () =>
    navigation.openSettings("schedules"));

  const dialog = $("#new-session-dialog");
  const dialogForm = $("#new-session-form");
  const dialogError = $("#new-session-error");
  const createButton = $("#create-session");
  const actionDialog = $("#session-action-dialog");
  const actionForm = $("#session-action-form");
  const actionFields = $("#session-action-fields");
  const actionTitleField = $("#session-title-field");
  const actionSequenceField = $("#session-sequence-field");
  const actionTitle = $("#session-action-title");
  const actionDescription = $("#session-action-description");
  const actionError = $("#session-action-error");
  const actionConfirm = $("#confirm-session-action");
  let pendingSessionAction = null;

  function validateSessionTitle(field, error) {
    const bytes = sessionTitleUtf8Bytes(field.value.trim());
    if (bytes <= SESSION_TITLE_UTF8_LIMIT) return true;
    error.textContent = t("sessionAction.titleTooLong",
      { bytes, max: SESSION_TITLE_UTF8_LIMIT },
      `标题占 ${bytes} 字节，最多 ${SESSION_TITLE_UTF8_LIMIT} 字节`);
    error.hidden = false;
    field.focus();
    return false;
  }

  function syncSessionActionCopy() {
    if (!pendingSessionAction) return;
    const [title, description, confirm] = sessionActionDialogCopy(
      pendingSessionAction.action, pendingSessionAction.session);
    actionTitle.textContent = title;
    actionDescription.textContent = description;
    actionConfirm.textContent = confirm;
  }
  subscribeLocale(() => { if (actionDialog.open) syncSessionActionCopy(); });

  async function refreshSelectedSession(session) {
    const selected = navigation.get();
    if (selected.projectId === session.project_id && selected.sessionId === session.id) {
      await loadSession(session.project_id, session.id);
    }
  }

  async function applySessionAction(action, session, title = "") {
    let updated;
    if (action === "rename") updated = await patchSession(session, { title });
    else if (action === "pin") updated = await patchSession(session, { pinned: !session.pinned });
    else if (action === "archive") updated = await patchSession(session, { archived: true });
    else if (action === "unarchive") updated = await patchSession(session, { archived: false });
    else if (action === "trash") updated = await trashSession(session);
    else if (action === "restore") updated = await restoreSession(session);
    else if (action === "fork") {
      updated = await forkSession(session, { title, through_sequence: actionForm.elements.through_sequence.value });
      navigation.select(updated.project_id, updated.id);
    } else if (action === "truncate") updated = await truncateSession(session, actionForm.elements.through_sequence.value);
    else if (action === "clear") updated = await clearSession(session);
    else throw new TypeError("unknown session action");
    await refreshSelectedSession(updated);
    if (["unarchive", "restore"].includes(action) && updated.status === "active")
      await refreshSelectedQueue();
    if (["truncate", "clear"].includes(action)) {
      const selected = navigation.get();
      if (selected.projectId === updated.project_id && selected.sessionId === updated.id)
        await Promise.all([reloadSelectedTimeline(),
          selectTodo(updated.project_id, updated.id)]);
    }
    toast(sessionActionToast(action, updated));
    return updated;
  }

  async function handleSessionAction(action, session) {
    if (action === "export" || action === "export_json") {
      if (action === "export") toast(t("sessionAction.preparingMarkdown", {}, "正在整理 Markdown 会话记录…"));
      const file = action === "export_json" ? await exportSession(session) : {
        blob: new Blob([formatSessionMarkdown(session, await loadSessionTranscript(session))],
          { type: "text/markdown;charset=utf-8" }),
        filename: sessionMarkdownFilename(session),
      };
      const url = URL.createObjectURL(file.blob);
      const link = document.createElement("a");
      link.href = url;
      link.download = file.filename;
      link.click();
      window.setTimeout(() => URL.revokeObjectURL(url), 0);
      toast(action === "export_json"
        ? t("sessionAction.jsonDownloading", {}, "JSON 备份已开始下载")
        : t("sessionAction.markdownDownloading", {}, "Markdown 已开始下载"));
      return;
    }
    if (!["rename", "trash", "fork", "truncate", "clear"].includes(action)) return applySessionAction(action, session);
    let history = null;
    if (action === "fork" || action === "truncate") history = await loadSessionHistory(session);
    pendingSessionAction = { action, session: history ? { ...session, etag: history.etag, revision: history.revision } : session };
    const hasTitle = action === "rename" || action === "fork";
    const hasSequence = action === "fork" || action === "truncate";
    syncSessionActionCopy();
    actionFields.hidden = !hasTitle && !hasSequence;
    actionTitleField.hidden = !hasTitle;
    actionSequenceField.hidden = !hasSequence;
    actionForm.elements.title.required = hasTitle;
    actionForm.elements.title.value = action === "rename" ? session.title || "" :
      action === "fork" ? sessionForkTitle(session) : "";
    actionForm.elements.through_sequence.required = hasSequence;
    actionForm.elements.through_sequence.value = hasSequence ? String(history.last_sequence) : "";
    actionForm.elements.through_sequence.max = hasSequence ? String(history.last_sequence) : "";
    actionConfirm.className = ["trash", "truncate", "clear"].includes(action) ? "danger-button" : "primary-button";
    actionError.hidden = true;
    if (!actionDialog.open) actionDialog.showModal();
    if (action === "rename" || action === "fork")
      window.setTimeout(() => actionForm.elements.title.select(), 0);
  }

  $("#close-session-action").addEventListener("click", () => actionDialog.close());
  $("#cancel-session-action").addEventListener("click", () => actionDialog.close());
  actionForm.elements.title.addEventListener("input", () => { actionError.hidden = true; });
  actionForm.addEventListener("submit", async (event) => {
    event.preventDefault();
    if (!pendingSessionAction || !actionForm.reportValidity()) return;
    if (actionTitleField.hidden === false &&
        !validateSessionTitle(actionForm.elements.title, actionError)) return;
    actionError.hidden = true;
    actionConfirm.disabled = true;
    try {
      const action = pendingSessionAction.action;
      const updated = await applySessionAction(action, pendingSessionAction.session,
        actionForm.elements.title.value.trim());
      actionDialog.close();
      pendingSessionAction = null;
      if (action === "fork") {
        pendingForkComposerFocus = `${updated.project_id}/${updated.id}`;
        focusForkComposerWhenReady();
      }
    } catch (error) {
      actionError.textContent = errorMessage(error);
      actionError.hidden = false;
      actionError.focus();
    } finally {
      actionConfirm.disabled = false;
    }
  });

  const newSessionProfile = createNewSessionProfile({
    projectInput: dialogForm.elements.project_id,
    projectOptions: $("#new-session-projects"),
    agentSelect: $("#agent-select"), modelSelect: $("#model-select"),
    reasoningSelect: $("#new-session-reasoning"),
    permissionSelect: $("#new-session-permission"),
    projectsStore, agentsStore, modelsStore,
    currentSelection: () => composerProfile.selection(),
  });

  function openNewSession() {
    dialogError.hidden = true;
    dialogError.textContent = "";
    dialogForm.elements.project_id.value = navigation.get().projectId || navigation.preferredProject();
    newSessionProfile.resetForOpen();
    if (!dialog.open) dialog.showModal();
    window.setTimeout(() => dialogForm.elements.title.focus(), 0);
  }
  function openNewTask() {
    newTaskComposerFocus.cancel();
    showActiveSessions();
    navigation.newTask(navigation.get().projectId || navigation.preferredProject());
    closeDrawers();
    prompt.focus();
  }
  $("#new-session").addEventListener("click", openNewTask);
  $("#new-session-configure").addEventListener("click", openNewSession);
  $("#close-new-session").addEventListener("click", () => dialog.close());
  dialogForm.elements.title.addEventListener("input", () => { dialogError.hidden = true; });
  dialogForm.addEventListener("submit", async (event) => {
    event.preventDefault();
    if (event.submitter?.value === "cancel") { dialog.close(); return; }
    if (!dialogForm.reportValidity()) return;
    if (!validateSessionTitle(dialogForm.elements.title, dialogError)) return;
    createButton.disabled = true;
    const values = Object.fromEntries(new FormData(dialogForm));
    try {
      const session = await createSession(values);
      showActiveSessions();
      dialog.close();
      dialogForm.elements.title.value = "";
      navigation.select(session.project_id, session.id);
      closeDrawers();
      cancelSessionComposerFocus();
      cancelSessionComposerFocus = focusSessionComposerAfterNavigation({
        navigation, sessionDetailStore, prompt,
        projectId: session.project_id, sessionId: session.id,
        origin: mobileLayout.matches ? $("#open-sidebar") : $("#new-session-configure"),
      });
    } catch (error) {
      dialogError.textContent = errorMessage(error);
      dialogError.hidden = false;
      dialogError.focus();
    } finally {
      createButton.disabled = false;
    }
  });

  const drawerReturnFocus = { sidebar: null, inspector: null };
  function setDrawer(name, open, options = {}) {
    if (open && mobileLayout.matches) {
      const other = name === "sidebar" ? "inspector" : "sidebar";
      if (shell.dataset[other] === "open")
        setDrawer(other, false, { persist: false });
    }
    shell.dataset[name] = open ? "open" : "closed";
    const button = name === "sidebar" ?
      (mobileLayout.matches ? $("#open-sidebar") : $("#desktop-sidebar-toggle"))
      : (mobileLayout.matches ? $("#open-inspector") : $("#toggle-inspector"));
    const panel = name === "sidebar" ? $("#sidebar") : $("#inspector");
    if (open && panel.inert) {
      const opener = options.returnFocus ?? document.activeElement;
      drawerReturnFocus[name] = opener instanceof HTMLElement &&
        opener !== document.body && opener.tabIndex >= 0 &&
        !panel.contains(opener) ? opener : button;
    }
    const inactive = !open;
    if (inactive && panel.contains(document.activeElement)) {
      const opener = drawerReturnFocus[name];
      const reachable = opener?.isConnected && opener.getClientRects().length &&
        !opener.matches(":disabled") && !opener.closest("[hidden], [inert]");
      (reachable ? opener : button).focus();
    }
    panel.inert = inactive;
    button.setAttribute("aria-expanded", String(open));
    if (name === "sidebar")
      $("#desktop-sidebar-toggle").setAttribute("aria-expanded", String(open));
    if (name === "inspector") $("#toggle-inspector").setAttribute("aria-expanded", String(open));
    if (open && options.focus !== false &&
        (name === "sidebar" ? mobileLayout.matches : !wideLayout.matches)) {
      (name === "sidebar" ? $("#close-sidebar") :
        panel.querySelector('[role="tab"][aria-selected="true"]'))?.focus();
    }
    if (options.persist === false) paneLayout?.apply();
    else paneLayout?.remember(name, open);
  }
  document.addEventListener("keydown", (event) => {
    if (event.key !== "Tab" || document.querySelector("dialog[open]")) return;
    const panel = mobileLayout.matches && shell.dataset.sidebar === "open"
      ? $("#sidebar") : !wideLayout.matches && shell.dataset.inspector === "open"
        ? $("#inspector") : null;
    if (!panel) return;
    const items = [...panel.querySelectorAll(
      'a[href], button:not([disabled]), input:not([disabled]), select:not([disabled]), textarea:not([disabled]), [tabindex]:not([tabindex="-1"])'
    )].filter((item) => item.getClientRects().length &&
      !item.closest("[hidden], [inert]"));
    if (!items.length) return;
    const first = items[0];
    const last = items[items.length - 1];
    if (!panel.contains(document.activeElement) ||
        (event.shiftKey ? document.activeElement === first :
          document.activeElement === last)) {
      event.preventDefault();
      (event.shiftKey ? last : first).focus();
    }
  });
  function closeDrawers() {
    if (mobileLayout.matches) setDrawer("sidebar", false, { persist: false });
    if (!wideLayout.matches) setDrawer("inspector", false, { persist: false });
  }
  $("#open-sidebar").addEventListener("click", () => setDrawer("sidebar", true));
  $("#close-sidebar").addEventListener("click", () => setDrawer("sidebar", false));
  $("#desktop-sidebar-toggle").addEventListener("click", () => setDrawer("sidebar", true));
  $("#open-inspector").addEventListener("click", () => setDrawer("inspector", true));
  $("#close-inspector").addEventListener("click", () => setDrawer("inspector", false));
  $("#toggle-inspector").addEventListener("click", () => setDrawer("inspector", shell.dataset.inspector !== "open"));
  $("#scrim").addEventListener("click", closeDrawers);
  wideLayout.addEventListener("change", (event) => setDrawer("inspector",
    !settingsActive && event.matches && (paneLayout?.inspectorOpen() ?? false),
    { persist: false }));
  mobileLayout.addEventListener("change", (event) => {
    setDrawer("sidebar", !event.matches && (paneLayout?.sidebarOpen() ?? true),
      { persist: false });
    // The inspector may stay open across this breakpoint. The visible opener
    // changes from the desktop button to the mobile button (or back).
    setDrawer("inspector", shell.dataset.inspector === "open",
      { persist: false, focus: false });
  });
  setDrawer("sidebar", !mobileLayout.matches, { persist: false });
  setDrawer("inspector", false, { persist: false });
  paneLayout = createPaneLayout({ shell, mobileLayout, wideLayout,
    sidebarHandle: $("#sidebar-resize"), inspectorHandle: $("#inspector-resize"),
    onLoaded(saved) {
      setDrawer("sidebar", !mobileLayout.matches && saved.sidebar_open,
        { persist: false });
      setDrawer("inspector", !settingsActive && wideLayout.matches &&
        saved.inspector_open, { persist: false });
    },
  });

  function selectInspectorTab(tabName) {
    for (const name of ["tasks", "decisions", "trace", "context"]) {
      const selected = tabName === name;
      $(`#${name}-tab`).setAttribute("aria-selected", String(selected));
      $(`#${name}-tab`).tabIndex = selected ? 0 : -1;
      $(`#${name}-panel`).hidden = !selected;
    }
    if (tabName === "trace") tracePanel.render();
  }
  $("#tasks-tab").addEventListener("click", () => selectInspectorTab("tasks"));
  $("#decisions-tab").addEventListener("click", () => selectInspectorTab("decisions"));
  $("#trace-tab").addEventListener("click", () => selectInspectorTab("trace"));
  $("#context-tab").addEventListener("click", () => selectInspectorTab("context"));
  $(".inspector-header .tab-list").addEventListener("keydown", (event) => {
    const names = ["tasks", "decisions", "trace", "context"];
    const current = names.indexOf(event.target?.id?.replace(/-tab$/, ""));
    if (current < 0) return;
    let next = current;
    if (event.key === "ArrowRight") next = (current + 1) % names.length;
    else if (event.key === "ArrowLeft") next = (current - 1 + names.length) % names.length;
    else if (event.key === "Home") next = 0;
    else if (event.key === "End") next = names.length - 1;
    else return;
    event.preventDefault();
    selectInspectorTab(names[next]);
    const tab = $(`#${names[next]}-tab`);
    tab.focus();
    tab.scrollIntoView({ block: "nearest", inline: "nearest" });
  });
  openTrace.addEventListener("click", () => {
    selectInspectorTab("trace");
    setDrawer("inspector", true);
    $("#trace-tab").scrollIntoView({ block: "nearest", inline: "nearest" });
  });
  $("#open-settings").addEventListener("click", () => navigation.openSettings("general"));
  $("#open-schedules").addEventListener("click", () => navigation.openSettings("schedules"));
  $("#close-settings").addEventListener("click", () => {
    navigation.backToWorkspace();
  });
  workspaceChip.addEventListener("click", () => { selectInspectorTab("context"); setDrawer("inspector", true); });

  async function toggleTheme() {
    if (themeToggleBusy) return;
    if (settingsView.hasPendingChanges()) {
      toast(t("settings.resolvePendingTheme", {},
        "先预览、应用或放弃尚未保存的设置"), "error");
      return;
    }
    themeToggleBusy = true;
    try {
      const settings = settingsStore.get().data ?? (await loadSettings()).data;
      if (!settings?.appearance || !settings.etag)
        throw new Error(t("settings.notLoaded", {}, "当前设置尚未载入"));
      const theme = settings.appearance.theme === "dark" ? "light" : "dark";
      const patch = { appearance: { theme } };
      await previewSettings(patch);
      await applySettings(patch, settings.etag);
      toast(theme === "dark" ? t("settings.themeDarkApplied", {}, "已切换为深色主题")
        : t("settings.themeLightApplied", {}, "已切换为浅色主题"));
    } catch (error) { toast(errorMessage(error), "error"); }
    finally { themeToggleBusy = false; }
  }

  async function exportSelectedSession() {
    const session = currentExportSession();
    if (!session) return;
    try { await handleSessionAction("export", session); }
    catch (error) { toast(errorMessage(error), "error"); }
  }
  for (const button of exportButtons)
    button.addEventListener("click", () => void exportSelectedSession());

  shortcuts = createKeyboardShortcuts({
    dialog: $("#shortcuts-dialog"), navigation, search: conversationSearch,
    onNew: openNewTask,
    onExport: exportSelectedSession,
    onSettings: (open) => open ? navigation.openSettings("general")
      : $("#close-settings").click(),
    onToggleTheme: toggleTheme,
    onSessionSearch: () => {
      if (shell.dataset.sidebar !== "open") setDrawer("sidebar", true);
      $("#session-search").focus();
    },
    onStop: () => stop.click(), isRunning: () => Boolean(activeRun),
    isDrawerOpen: () => (mobileLayout.matches && shell.dataset.sidebar === "open") ||
      (!wideLayout.matches && shell.dataset.inspector === "open"),
    closeDrawers,
  });
  $("#open-shortcuts").addEventListener("click", () => shortcuts.openHelp());
  $("#toggle-theme").addEventListener("click", () => void toggleTheme());

  function scheduleTaskRefresh() {
    window.clearTimeout(tasksTimer);
    if (document.hidden) return;
    const active = (tasksStore.get().data?.items ?? []).some((item) => !item.terminal);
    tasksTimer = window.setTimeout(async () => {
      await loadTasks();
      await refreshSelectedTask();
      scheduleTaskRefresh();
    }, active ? 1400 : 5000);
  }
  tasksStore.subscribe(scheduleTaskRefresh);
  function scheduleRunsRefresh() {
    window.clearTimeout(runsTimer);
    if (document.hidden) return;
    const snapshot = runsStore.get();
    if (snapshot.status === "loading" || snapshot.status === "refreshing") return;
    runsTimer = window.setTimeout(async () => {
      await loadRuns();
      await refreshSelectedQueue();
    },
      Number(snapshot.data?.active_runs ?? 0) > 0 ? 1500 : 8000);
  }
  runsStore.subscribe(scheduleRunsRefresh);
  const sessionMetadataSync = createSessionMetadataSync({ navigation,
    store: sessionDetailStore, readSession, loadSessions,
    onRestored: refreshSelectedQueue });
  function scheduleSessionRefresh() {
    window.clearTimeout(sessionTimer);
    if (document.hidden) return;
    sessionTimer = window.setTimeout(async () => {
      try { await sessionMetadataSync.refresh(); }
      catch { /* Keep the last known state until the next bounded check. */ }
      scheduleSessionRefresh();
    }, 8000);
  }
  function scheduleApprovalRefresh() {
    window.clearTimeout(approvalsTimer);
    if (document.hidden) return;
    const pending = Number(approvalsStore.get().data?.total ?? 0);
    approvalsTimer = window.setTimeout(async () => {
      await Promise.all([loadApprovals(), loadRecovery(), refreshSelectedAsks()]);
      scheduleApprovalRefresh();
    }, pending || asksStore.get().data?.items?.length ||
      recoveryStore.get().data?.resume_required ? 1000 : 3000);
  }
  approvalsStore.subscribe(scheduleApprovalRefresh);
  asksStore.subscribe(scheduleApprovalRefresh);
  recoveryStore.subscribe(scheduleApprovalRefresh);
  window.setInterval(() => {
    if (!document.hidden) refreshRelativeTimes();
  }, 30_000);
  document.addEventListener("visibilitychange", () => {
    if (document.hidden) {
      window.clearTimeout(tasksTimer);
      window.clearTimeout(runsTimer);
      window.clearTimeout(sessionTimer);
      window.clearTimeout(approvalsTimer);
    }
    else {
      refreshRelativeTimes();
      scheduleTaskRefresh();
      void loadRuns().then(refreshSelectedQueue);
      void loadSessions();
      void sessionMetadataSync.refresh().catch(() => {});
      scheduleSessionRefresh();
      scheduleApprovalRefresh();
      if (activeRun) scheduleRunPoll(100);
    }
  });

  // Begin restoring panel geometry as soon as settings choose the locale;
  // unrelated resource requests must not hold it behind their completion.
  const settingsReady = loadSettings();
  void settingsReady.then(() => settingsView.localeReady())
    .then(() => {
      if (settingsStore.get().status === "ready")
        document.documentElement.dataset.mdoConfiguredLocale = document.documentElement.lang;
      return paneLayout.load();
    });
  let initialLoadWarned = false;
  function reportInitialLoad(results) {
    if (initialLoadWarned ||
        !results.some((result) => result.status === "rejected")) return;
    initialLoadWarned = true;
    toast(t("resource.partialLoad", {}, "部分资源暂时无法载入，可继续重试。"), "error");
  }
  // Host health, task, approval, and Settings catalogs populate their own
  // subscribed views. A delayed health response must not cover an already
  // loaded conversation with the startup timeout overlay.
  // A slow unrelated resource must not keep an explicit conversation route on
  // the uninitialized new-task shell.
  void Promise.allSettled([
    loadBootstrap(),
    loadManagementResources(),
    loadTasks(),
    loadApprovals(),
  ]).then(reportInitialLoad);
  const sessionsReady = loadSessions();
  const catalogsReady = loadCatalogs();
  // A direct session URL already names its destination. Its sidebar and
  // selection catalogs may fill in after the conversation becomes usable;
  // routes without a session still need the catalog to choose a destination.
  const explicitSession = navigation.get().view === "workspace" &&
    Boolean(navigation.get().sessionId);
  if (explicitSession)
    void Promise.allSettled([sessionsReady, catalogsReady]).then(reportInitialLoad);
  document.documentElement.dataset.mdoStartupStage = "resources";
  const initial = await Promise.allSettled([
    ...(!explicitSession ? [sessionsReady, catalogsReady] : []),
    settingsReady,
    // The explicit route's navigation load already owns these reads and holds
    // its queue gate until they settle. A second boot read can stall the
    // startup overlay without making dispatch any safer.
    ...(!explicitSession ? [loadRecovery(), loadRuns()] : []),
  ]);
  reportInitialLoad(initial);

  // Only the opt-in startup choice waits for its language pack. Explicit
  // conversation routes still become usable without a locale fetch gate.
  if (!entryHash && !location.hash &&
      settingsStore.get().data?.workspace?.open_mode === "ask")
    await settingsView.localeReady();

  document.documentElement.dataset.mdoStartupStage = "navigation";
  await startWorkspaceNavigation({ navigation, settingsStore, sessionsStore,
    runsStore,
    sessionDetailStore,
    dialog: $("#startup-choice-dialog"),
    title: $("#startup-last-title"),
    continueButton: $("#startup-continue"),
    newButton: $("#startup-new"), prompt, entryHash });
  if (!navigation.get().sessionId) void newTaskController.reconcile();
  scheduleTaskRefresh();
  scheduleSessionRefresh();
  scheduleApprovalRefresh();
}
