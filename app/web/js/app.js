import { mountIcons } from "./components/icons.js";
import { bootstrapStore, loadBootstrap } from "./state/bootstrap.js";
import {
  sessionsStore, sessionDetailStore, loadSessions, loadSession, createSession,
  patchSession, trashSession, restoreSession, loadSessionHistory, forkSession,
  truncateSession, clearSession, exportSession, loadSessionTranscript,
} from "./state/sessions.js";
import { modelsStore, agentsStore, projectsStore, loadCatalogs, loadModels, loadAgents } from "./state/catalogs.js";
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
import { formatSessionMarkdown, sessionMarkdownFilename } from "./features/sessions/session-export.js";
import { createProjectDialog } from "./features/sessions/project-dialog.js";
import { timelineStore, selectTimeline, clearTimeline, refreshSelectedTimeline, reloadSelectedTimeline } from "./features/chat/timeline-store.js";
import { todoStore, selectTodo, clearTodo } from "./state/todo.js";
import { createTimelineView } from "./features/chat/timeline.js";
import { createTracePanel } from "./features/chat/trace-panel.js";
import { createMessageEditDialog } from "./features/chat/message-edit-dialog.js";
import { runMessageReplacement } from "./features/chat/message-replacement.js";
import { createConversationSearch } from "./features/chat/conversation-search.js";
import { feedbackStore, selectFeedback, clearFeedback, setFeedback } from "./features/chat/feedback-store.js";
import { createConversationDocks } from "./features/chat/conversation-docks.js";
import { createPromptQueue } from "./features/chat/prompt-queue.js";
import { createDraftStore } from "./features/chat/draft-store.js";
import { createComposerImages } from "./features/chat/composer-images.js";
import { createComposerProject } from "./features/chat/composer-project.js";
import { createImagePreview } from "./features/chat/image-preview.js";
import { createSlashCommands } from "./features/chat/slash-commands.js";
import { createFileMentions } from "./features/chat/file-mentions.js";
import { createComposerProfile, fillReasoningOptions } from "./features/chat/composer-profile.js";
import { createTokenMeter } from "./features/chat/token-meter.js";
import { createTaskPanel } from "./features/tasks/task-panel.js";
import { createDecisionPanel } from "./features/approvals/decision-panel.js";
import { createRecoveryPanel } from "./features/approvals/recovery-panel.js";
import { createSettingsView } from "./features/settings/settings-view.js";
import { createSchedulePanel } from "./features/settings/schedule-panel.js";
import { createProjectPanel } from "./features/settings/project-panel.js";
import { createResourcePanels } from "./features/settings/resource-panels.js";
import { createFeedbackPanel } from "./features/settings/feedback-panel.js";
import { createKeyboardShortcuts } from "./features/shell/keyboard-shortcuts.js";
import { createRunNotifications } from "./features/shell/run-notifications.js";
import { startWorkspaceNavigation } from "./features/shell/workspace-startup.js";
import { focusSessionComposerAfterNavigation } from "./features/shell/session-composer-focus.js";
import { createPaneLayout } from "./features/shell/pane-layout.js";
import { api } from "./api/client.js";
import { clear, element, errorMessage, toast } from "./utils/dom.js";
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

  const shell = $("#app-shell");
  const wideLayout = window.matchMedia("(min-width: 1204px)");
  const mobileLayout = window.matchMedia("(max-width: 760px)");
  shell.dataset.inspector = "closed";
  const prompt = $("#prompt");
  const composer = $("#composer");
  const send = $("#send");
  const stop = $("#stop");
  const composerError = $("#composer-error");
  const composerHint = $("#composer-hint");
  const draftStatus = $("#draft-status");
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
  let runMonitor = 0;
  let selectedKey = "";
  let creatingSessionKey = "";
  let tasksTimer = 0;
  let runsTimer = 0;
  let approvalsTimer = 0;
  const submissionLanes = new Map();
  const uncertainAdmissions = new Map();
  let routeVersion = 0;
  let routeSignature = "";
  let messageActionBusy = false;
  let pendingForkComposerFocus = "";
  let interruptRequested = false;
  let themeToggleBusy = false;
  let composerAttachments = [];
  let composerImages = null;
  let shortcuts;
  const queueBlocked = new Set();
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

  function composerScope(route = navigation.get()) {
    return `${route.projectId || "default"}/${route.sessionId || "@new"}`;
  }

  function activeSubmissionLane(route = navigation.get()) {
    const scope = composerScope(route);
    return submissionLanes.get(scope) ??
      (creatingSessionKey === `${route.projectId}/${route.sessionId}`
        ? submissionLanes.get(`${route.projectId}/@new`) : null);
  }

  function submittingCurrent() {
    return Boolean(activeSubmissionLane());
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
    store: sessionsStore,
    projectsStore,
    filter: $("#session-status-filter"),
    navigation,
    onSelect(session, event) {
      cancelSessionComposerFocus();
      navigation.select(session.project_id, session.id);
      closeDrawers();
      // Keyboard selection keeps its place in the sidebar; a desktop pointer
      // selection hands focus to the composer once the target is writable.
      if (!mobileLayout.matches && event.detail > 0)
        cancelSessionComposerFocus = focusSessionComposerAfterNavigation({
          navigation, sessionDetailStore, prompt,
          projectId: session.project_id, sessionId: session.id,
          origin: event.currentTarget,
        });
    },
    onAction: handleSessionAction,
    onAddProject: () => projectDialog.open(),
    onManageProject(projectId) {
      navigation.openSettings("projects");
      closeDrawers();
      projectPanel.focusProject(projectId);
    },
    onNewInProject(projectId) {
      showActiveSessions();
      navigation.newTask(projectId);
      closeDrawers();
      prompt.focus();
    },
  });
  const actionMenus = [
    ["#session-action-control", "#session-actions", "#session-header-menu"],
    ["#session-action-control-mobile", "#session-actions-mobile", "#session-header-menu-mobile"],
  ].map(([control, button, menu]) => createSessionActionMenu({
    control: $(control), button: $(button), menu: $(menu), navigation,
    store: sessionDetailStore, onAction: handleSessionAction,
  }));
  mobileLayout.addEventListener("change", () => {
    for (const menu of actionMenus) menu.close();
  });
  function showActiveSessions() {
    $("#session-search").value = "";
    sessionList.showActive();
  }
  $("#session-search").addEventListener("input", (event) => sessionList.setQuery(event.target.value));
  createComposerProject({ select: $("#composer-project"), navigation,
    projectsStore, sessionsStore });

  const messageEditDialog = createMessageEditDialog({
    dialog: $("#message-edit-dialog"), form: $("#message-edit-form"),
    input: $("#message-edit-input"), cancel: $("#cancel-message-edit"),
  });
  createRunNotifications({ runsStore, navigation, settingsStore,
    onUnreadChange: (keys) => sessionList.setUnread(keys) });

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
    if (activeRun || submittingCurrent() || composerImages?.isUploading())
      throw new Error(t("messageAction.waitForRun", {}, "请在当前运行结束后操作消息"));
    if (promptQueue.peek(selected.projectId, selected.sessionId) ||
        prompt.value.trim() || composerAttachments.length)
      throw new Error(t("messageAction.resolveDraft", {}, "请先处理草稿和待发送队列，再编辑历史消息"));
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
    try {
      const result = await runMessageReplacement({ session, sequence, text,
        attachments, isCurrent: stillSelected,
        loadHistory: loadSessionHistory, truncate: truncateSession, startRun,
        onTruncated(updated) {
          sessionDetailStore.setData(updated);
          void Promise.allSettled([reloadSelectedTimeline(),
            selectTodo(updated.project_id, updated.id)]);
        },
        onStartFailure(updated, error, current) {
          const key = `${updated.project_id}/${updated.id}`;
          draftStore.edit(key, text, attachments, true);
          if (!current) return;
          prompt.value = text;
          composerAttachments = [...attachments];
          composerImages.set(attachments);
          prompt.dispatchEvent(new Event("input", { bubbles: true }));
          showComposerError(error);
          prompt.focus();
        },
        onStarted(run) { monitorRun(run); },
      });
      void Promise.allSettled([...(stillSelected() ? [refreshSelectedTimeline()] : []),
        loadTasks(), loadRuns(), loadRecovery()]);
      if (stillSelected()) {
        if (document.activeElement === document.body ||
            !document.activeElement?.isConnected) prompt.focus();
        toast(action === "retry"
          ? t("messageAction.retried", {}, "已在当前会话重试")
          : t("messageAction.edited", {}, "已在当前会话发送编辑后的消息"));
      } else if (!result.current)
        toast(t("messageAction.backgroundRun", {}, "原会话已在后台重新运行"));
    } finally { messageActionBusy = false; }
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
      const history = await loadSessionHistory(session);
      if (!isCurrentMessageOwner(owner, version))
        throw new Error(t("messageAction.ownerChanged", {}, "会话已切换，请重新选择消息"));
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
    onEdit: async (sequence, text, attachments, owner) => {
      const version = routeVersion;
      if (!isCurrentMessageOwner(owner, version))
        throw new Error(t("messageAction.ownerChanged", {}, "会话已切换，请重新选择消息"));
      assertMessageReplacementReady(sequence, text, attachments);
      const edited = await messageEditDialog.open(text, attachments);
      if (edited !== null) {
        if (!isCurrentMessageOwner(owner, version))
          throw new Error(t("messageAction.ownerChanged", {}, "会话已切换，请重新选择消息"));
        await replaceAndRunMessage(sequence, edited, attachments, "edit");
      }
    },
    onRetry: (sequence, text, attachments, owner) => {
      if (!isCurrentMessageOwner(owner))
        throw new Error(t("messageAction.ownerChanged", {}, "会话已切换，请重新选择消息"));
      return replaceAndRunMessage(sequence, text, attachments, "retry");
    },
  });
  conversationSearch = createConversationSearch({
    bar: $("#conversation-find"), input: $("#conversation-find-input"),
    count: $("#conversation-find-count"), closeButton: $("#close-find"),
    openButtons: [$("#open-find"), $("#open-find-mobile")],
    navigation, prompt, onQuery: (query) => timelineView.search(query),
  });
  createImagePreview({
    dialog: $("#image-preview"), image: $("#image-preview-content"),
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
  });
  createConversationDocks({
    container: $("#conversation-docks"), navigation, tasksStore, approvalsStore,
    asksStore,
    todoStore,
    runsStore,
    onOpenTasks: () => { selectInspectorTab("tasks"); setDrawer("inspector", true); },
    onChanged: () => Promise.all([loadTasks(), loadRuns(), refreshSelectedAsks()]),
  });
  const promptQueue = createPromptQueue({
    container: $("#prompt-queue"), navigation,
    isRunActive: () => Boolean(activeRun),
    stagedEntries: () => activeSubmissionLane()?.pending.map((item) => ({
      text: item.text, staged: true,
    })) ?? [],
    onRetry: async () => {
      const selected = navigation.get();
      const first = promptQueue.peek(selected.projectId, selected.sessionId);
      if (first?.state === "sending")
        await promptQueue.retry(selected.projectId, selected.sessionId, first.id);
      queueBlocked.delete(`${selected.projectId}/${selected.sessionId}`);
      await maybeCancelPriorityRun();
      await dispatchQueued();
    },
    onRemoved: async () => {
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
    sessionStore: sessionDetailStore, timelineStore, modelsStore,
  });
  const draftStore = createDraftStore({
    onRestore(text, attachments) {
      prompt.value = text;
      composerAttachments = attachments;
      composerImages?.set(attachments);
      resizePrompt();
      tokenMeter.refresh();
    },
    onError(error) {
      draftStatus.textContent = `草稿未保存：${errorMessage(error)}`;
      draftStatus.hidden = false;
    },
    onSaved() {
      draftStatus.hidden = true;
      draftStatus.textContent = "";
    },
  });
  draftStore.select("");
  const composerProfile = createComposerProfile({
    modelSelect: $("#composer-model"), reasoningSelect: $("#composer-reasoning"),
    permissionSelect: $("#composer-permission"), navigation,
    sessionStore: sessionDetailStore, modelsStore, agentsStore, projectsStore,
    isRunActive: () => Boolean(activeRun),
    onBusyChange: () => setRun(activeRun),
    onSelectionChange() {
      tokenMeter.refresh();
      if (composerError.dataset.code === "image_model_unsupported" &&
          composerImages?.supportsCurrentModel()) hideComposerError();
    },
  });
  composerImages = createComposerImages({
    composer, prompt, button: $("#composer-attach"), input: $("#composer-file"),
    strip: $("#composer-images"), navigation, modelsStore,
    sessionStore: sessionDetailStore, ensureSession,
    onChange(attachments) {
      composerAttachments = attachments;
      draftStore.edit(selectedKey, prompt.value, attachments);
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
        if (error?.code !== "attachment_in_use" &&
            error?.code !== "attachment_not_found" && selectedKey === key)
          showComposerError(error);
      }
      return true;
    },
    onUploading(uploading) {
      if (uploading && ["image_model_unsupported", "image_selection_invalid"]
        .includes(composerError.dataset.code)) hideComposerError();
      setRun(activeRun);
    },
    onError: showComposerError,
  });
  composerImages.set(composerAttachments);
  createRecoveryPanel({
    container: $("#recovery-list"),
    summary: $("#recovery-summary"),
    store: recoveryStore,
    onResume: (run) => {
      monitorRun(run);
      if (composerError.dataset.code === "recovery_required") hideComposerError();
      void Promise.all([loadRuns(), loadTasks(), refreshSelectedTimeline()]);
    },
    onAbandon: async () => {
      const selected = navigation.get();
      queueBlocked.delete(`${selected.projectId}/${selected.sessionId}`);
      await dispatchQueued();
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
  const projectPanel = createProjectPanel({
    panel: $('[data-settings-panel="projects"]'), projectsStore,
    modelsStore, projectDialog, navigation,
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
    const route = navigation.get();
    const lane = activeSubmissionLane(route);
    const pendingCount = lane?.pending.length ?? 0;
    const creatingSession = Boolean(creatingSessionKey) &&
      creatingSessionKey === `${route.projectId}/${route.sessionId}`;
    // Keep keyboard focus while a newly created session loads its detail.
    prompt.disabled = !sessionWritable && !creatingSession;
    send.disabled = !(sessionWritable || (creatingSession && lane)) ||
      composerImages?.isUploading() || composerProfile.isBusy();
    composerImages?.setWritable(sessionWritable && !creatingSession &&
      !(lane && !route.sessionId));
    composerProfile.setRunActive(Boolean(activeRun || lane));
    send.setAttribute("aria-label", activeRun || lane
      ? t("composer.queue", {}, "加入待发送队列")
      : t("shell.send", {}, "发送任务"));
    composerHint.textContent = pendingCount
      ? t("composer.hintPendingAdmission", { count: pendingCount })
      : lane && !activeRun ? t("composer.hintSubmitting") : activeRun
      ? (guide
        ? t("composer.hintGuide", {}, "Enter 中断并发送 · Ctrl Enter 排队")
        : t("composer.hintQueue", {}, "Enter 排队 · Ctrl Enter 中断并发送"))
      : t("composer.hintIdle", {}, "Enter 发送 · Shift Enter 换行");
    $("#shortcut-enter-description").textContent = guide
      ? t("composer.shortcutEnterGuide", {}, "发送；运行中中断并优先发送")
      : t("composer.shortcutEnterQueue", {}, "发送；运行中加入待发送队列");
    $("#shortcut-control-enter-description").textContent = guide
      ? t("composer.shortcutControlGuide", {}, "运行中加入待发送队列")
      : t("composer.shortcutControlQueue", {}, "中断当前运行，优先发送输入");
    mobileActivity.hidden = !activeRun && !lane;
  }
  settingsStore.subscribe(() => setRun(activeRun));

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
    syncPromptPlaceholder();
    setRun(activeRun);
    focusForkComposerWhenReady();
    if (session) {
      const title = session.title || t("nav.untitled");
      sessionTitle.textContent = title;
      const statusText = sessionStatusSuffix(session.status);
      sessionSubtitle.textContent = `${session.project_id} · ${session.agent_id} · ${session.model_id}${statusText}`;
      mobileTitle.textContent = title;
      mobileMeta.textContent = `${session.model_id || session.agent_id}${statusText}`;
    }
    syncWorkspaceChip();
    updateContext(state);
  });
  projectsStore.subscribe(() => {
    syncWorkspaceChip();
    if (!navigation.get().sessionId) updateContext(sessionDetailStore.get());
  });
  subscribeLocale(() => {
    setRun(activeRun);
    updateContext(sessionDetailStore.get());
    syncPromptPlaceholder();
    syncWorkspaceChip();
    syncRuntimeLabel();
    if (composerError.dataset.code === "recovery_required")
      showComposerError(recoveryRequiredError());
    skipLink.textContent = t(settingsActive ? "shell.skipSettings" : "shell.skip");
    if (settingsActive) syncSettingsTitle();
    const session = sessionDetailStore.get().data;
    if (session && selectedKey === `${session.project_id}/${session.id}`) {
      const statusText = sessionStatusSuffix(session.status);
      sessionSubtitle.textContent = `${session.project_id} · ${session.agent_id} · ${session.model_id}${statusText}`;
      mobileMeta.textContent = `${session.model_id || session.agent_id}${statusText}`;
      if (!session.title) {
        sessionTitle.textContent = t("nav.untitled");
        mobileTitle.textContent = t("nav.untitled");
      }
    } else if (!selectedKey) {
      sessionTitle.textContent = t("shell.newTask");
      sessionSubtitle.textContent = t("shell.newTaskSubtitle", {
        project: $("#composer-project").value || "default" });
      mobileTitle.textContent = t("shell.newTask");
    }
  });

  function syncRuntimeLabel() {
    const state = bootstrapStore.get();
    runtimeState.dataset.state = state.status === "error" ? "error" : state.data?.ready ? "ready" : "loading";
    runtimeLabel.textContent = state.status === "error"
      ? errorMessage(state.error)
      : state.data?.ready
        ? t("shell.localService", { version: state.data.version }, `本地服务 ${state.data.version}`)
        : state.data?.message || t("shell.connecting", {}, "正在连接本地服务…");
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

  function scheduleRunPoll(delay = 750) {
    window.clearTimeout(runMonitor);
    runMonitor = window.setTimeout(async () => {
      if (!activeRun || document.hidden) return;
      try {
        const run = await readRun(activeRun.id);
        setRun(run);
        await Promise.all([refreshSelectedTimeline(), refreshSelectedAsks()]);
        if (terminalState(run)) {
          await Promise.all([loadSessions(), loadRuns(), loadTasks(), loadRecovery()]);
          await refreshSelectedQueue();
          prompt.focus();
          return;
        }
        scheduleRunPoll();
      } catch (error) {
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
    const selected = navigation.get();
    const key = `${selected.projectId}/${selected.sessionId}`;
    const session = sessionDetailStore.get().data;
    if (!selected.sessionId || !session || session.project_id !== selected.projectId ||
        session.id !== selected.sessionId || session.status !== "active" || activeRun ||
        queueBlocked.has(key) ||
        promptQueue.peek(selected.projectId, selected.sessionId)?.state !== "pending") return;
    if ((runsStore.get().data?.items ?? []).some((run) =>
      run.project_id === selected.projectId && run.session_id === selected.sessionId && !terminalState(run))) return;
    await promptQueue.exclusive(selected.projectId, selected.sessionId,
      async () => {
        const stillSelected = () => navigation.get().projectId === selected.projectId &&
          navigation.get().sessionId === selected.sessionId;
        const entry = promptQueue.peek(selected.projectId, selected.sessionId);
        if (!entry || entry.state !== "pending") return;
        try {
          await ensurePromptReady(selected.projectId, selected.sessionId,
            Boolean(entry.priority));
          if (!stillSelected()) return;
          await promptQueue.markSending(selected.projectId, selected.sessionId, entry.id);
          const run = await startRun(selected.projectId, selected.sessionId,
            entry.text, entry.attachments ?? []);
          if (stillSelected()) monitorRun(run);
          await promptQueue.remove(selected.projectId, selected.sessionId, entry.id);
          if (stillSelected()) hideComposerError();
          await Promise.all([...(stillSelected() ? [refreshSelectedTimeline()] : []),
            loadTasks(), loadRuns(), loadRecovery()]);
        } catch (error) {
          if (error?.code !== "recovery_required") queueBlocked.add(key);
          try { await promptQueue.select(selected.projectId, selected.sessionId); }
          catch { /* Preserve the original dispatch error. */ }
          if (stillSelected()) showComposerError(error);
          else toast(t("composer.backgroundQueueFailed",
            { title: session.title, error: errorMessage(error) },
            `后台会话“${session.title}”的待发送消息未发出：${errorMessage(error)}`),
            "error");
        }
      });
  }

  async function refreshSelectedQueue() {
    const selected = navigation.get();
    if (selected.view !== "workspace" || !selected.sessionId) return;
    try {
      await promptQueue.select(selected.projectId, selected.sessionId);
      resolveObservedAdmission(`${selected.projectId}/${selected.sessionId}`);
      const current = navigation.get();
      if (current.projectId !== selected.projectId ||
          current.sessionId !== selected.sessionId) return;
      await maybeCancelPriorityRun();
      await dispatchQueued();
    } catch (error) {
      const current = navigation.get();
      if (current.projectId === selected.projectId &&
          current.sessionId === selected.sessionId) showComposerError(error);
    }
  }

  navigation.subscribe(async ({ view, projectId, sessionId, settingsSection }) => {
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
      if (!settingsActive) inspectorBeforeSettings = shell.dataset.inspector;
      settingsActive = true;
      settingsWorkspace.hidden = false;
      for (const region of agentWorkspaceRegions) region.hidden = true;
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
      if (!settingsStore.get().data) await loadSettings();
      return;
    }
    settingsWorkspace.hidden = true;
    for (const region of agentWorkspaceRegions) region.hidden = false;
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
    if (!key) {
      const project = projectId || "default";
      sessionTitle.textContent = t("shell.newTask");
      sessionSubtitle.textContent = t("shell.newTaskSubtitle", { project });
      mobileTitle.textContent = t("shell.newTask");
      mobileMeta.textContent = project;
      syncWorkspaceChip();
    }
    // Returning from Settings may keep the same selected session, so refresh
    // the context before the same-session fast path below.
    updateContext(sessionDetailStore.get());
    if (key === selectedKey) {
      if (key) {
        queueBlocked.add(key);
        try {
          await promptQueue.select(projectId, sessionId);
          resolveObservedAdmission(key);
          queueBlocked.delete(key);
          void maybeCancelPriorityRun();
          void dispatchQueued();
        } catch (error) { showComposerError(error); }
      }
      return;
    }
    draftStore.capture(selectedKey, prompt.value, composerAttachments);
    selectedKey = key;
    draftStore.select(key);
    window.clearTimeout(runMonitor);
    runMonitor = 0;
    activeRun = null;
    setRun(null);
    hideComposerError();
    if (!key) {
      selectRecovery("", "");
      clearTimeline();
      clearTodo();
      clearAsks();
      clearFeedback();
      sessionDetailStore.reset();
      return;
    }
    selectRecovery(projectId, sessionId);
    sessionDetailStore.reset();
    selectTimeline(projectId, sessionId);
    void selectTodo(projectId, sessionId);
    void selectAsks(projectId, sessionId);
    void selectFeedback(projectId, sessionId);
    queueBlocked.add(key);
    try {
      await Promise.all([loadSession(projectId, sessionId), loadRuns(), loadRecovery(),
        promptQueue.select(projectId, sessionId)]);
      resolveObservedAdmission(key);
    } catch (error) { showComposerError(error); return; }
    finally {
      if (creatingSessionKey === key) {
        creatingSessionKey = "";
        setRun(activeRun);
      }
    }
    queueBlocked.delete(key);
    findActiveRun();
    void maybeCancelPriorityRun();
    void dispatchQueued();
  });

  function hideComposerError() {
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
  function showComposerError(error, note = "") {
    composerError.textContent = errorMessage(error);
    if (note) composerError.append(" ", note);
    composerError.dataset.code = error?.code || "";
    if (error?.queueItemId) composerError.dataset.queueItemId = error.queueItemId;
    else delete composerError.dataset.queueItemId;
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

  async function ensureSession(text, { stageDraft = true } = {}) {
    const origin = navigation.get();
    const originVersion = routeVersion;
    if (origin.sessionId) return origin;
    const title = text.trim().split(/\r?\n/, 1)[0].slice(0, 80) ||
      t("composer.imageTask", {}, "图片任务");
    const session = await createSession({ project_id: origin.projectId || "default", title,
      ...composerProfile.selection() });
    if (stageDraft && routeVersion === originVersion) {
      // A second prompt typed during session creation belongs to the new
      // conversation; stage it before navigation restores the composer.
      draftStore.edit(`${session.project_id}/${session.id}`,
        prompt.value, composerAttachments, true);
    }
    if (routeVersion === originVersion) {
      showActiveSessions();
      creatingSessionKey = `${session.project_id}/${session.id}`;
      navigation.select(session.project_id, session.id);
      selectTimeline(session.project_id, session.id);
    }
    return { projectId: session.project_id, sessionId: session.id };
  }

  function selectedOwnsDraft(key) {
    const route = navigation.get();
    return route.view === "workspace" && selectedKey === key &&
      (key ? `${route.projectId}/${route.sessionId}` === key : !route.sessionId);
  }

  function consumeComposerInput(ownerKey) {
    prompt.value = "";
    composerAttachments = [];
    composerImages.clear();
    draftStore.clear(ownerKey);
    resizePrompt();
    tokenMeter.refresh();
  }

  function restoreUnsentItems(ownerKey, items) {
    const visible = selectedOwnsDraft(ownerKey);
    if (visible) draftStore.capture(ownerKey, prompt.value, composerAttachments);
    let restored = null;
    // Prepending in reverse keeps the original send order ahead of any new draft.
    for (let index = items.length - 1; index >= 0; index -= 1)
      restored = draftStore.restoreUnsent(ownerKey,
        items[index].raw, items[index].attachments);
    if (visible && restored) {
      prompt.value = restored.text;
      composerAttachments = restored.attachments;
      composerImages.set(restored.attachments);
      resizePrompt();
      tokenMeter.refresh();
    }
    return { restored, visible };
  }

  function resolveObservedAdmission(key) {
    const uncertain = uncertainAdmissions.get(key);
    if (!uncertain) return;
    const [projectId, sessionId] = key.split("/");
    if (!promptQueue.observed(projectId, sessionId, uncertain.id)) return;
    uncertainAdmissions.delete(key);
    const visible = selectedOwnsDraft(key);
    if (visible && composerError.dataset.queueItemId === uncertain.id)
      hideComposerError();
    if (visible && (prompt.value !== uncertain.text ||
        JSON.stringify(composerAttachments) !== JSON.stringify(uncertain.attachments))) return;
    if (!draftStore.clearIfMatches(key, uncertain.text, uncertain.attachments)) return;
    if (!visible) return;
    prompt.value = "";
    composerAttachments = [];
    composerImages.clear();
    resizePrompt();
    tokenMeter.refresh();
  }

  function rememberUncertainAdmission(key, error, unsent, restored) {
    if (!error.queueAdmissionUncertain || !error.queueItemId ||
        unsent.length !== 1 || !restored || restored.merged) return;
    uncertainAdmissions.set(key, { id: error.queueItemId,
      text: restored.text, attachments: restored.attachments });
  }

  async function drainPendingSubmissions(lane, selected) {
    const ownerKey = `${selected.projectId}/${selected.sessionId}`;
    while (lane.pending.length) {
      const item = lane.pending[0];
      try {
        if (!await promptQueue.enqueue(selected.projectId, selected.sessionId,
          item.text, { first: item.interrupt && lane.admitted === 0,
            priority: item.interrupt,
            attachments: item.attachments }))
          throw new Error(t("composer.queueFull", {}, "待发送队列已满（最多 20 条）"));
        lane.pending.shift();
        lane.admitted += 1;
        queueBlocked.delete(ownerKey);
        if (item.interrupt && selectedOwnsDraft(ownerKey))
          await maybeCancelPriorityRun();
        // The first run can finish before the queue POST returns.
        if (selectedOwnsDraft(ownerKey)) await dispatchQueued();
      } catch (error) {
        const unsent = lane.pending.splice(0);
        const { restored, visible } = unsent.length
          ? restoreUnsentItems(ownerKey, unsent)
          : { restored: null, visible: selectedOwnsDraft(ownerKey) };
        rememberUncertainAdmission(ownerKey, error, unsent, restored);
        if (visible) {
          showComposerError(error, error.queueAdmissionUncertain
            ? t("composer.queueAdmissionUncertain") : restored
              ? t(restored.merged ? "composer.unsentMerged" : "composer.unsentRestored") : "");
          prompt.focus();
        } else {
          toast(t("composer.backgroundQueueFailed", {
            title: ownerKey,
            error: `${errorMessage(error)}${error.queueAdmissionUncertain
              ? ` ${t("composer.queueAdmissionUncertain")}` : ""}`,
          }), "error");
        }
        resolveObservedAdmission(ownerKey);
        break;
      } finally {
        setRun(activeRun);
        promptQueue.render();
      }
    }
  }

  async function submitPrompt({ text, attachments = [], interrupt = false,
    fromComposer = true }) {
    fileMentions.hide();
    if ((!text && !attachments.length) || composerImages.isUploading()) return;
    const origin = navigation.get();
    const originVersion = routeVersion;
    if (fromComposer && !attachments.length &&
        slashCommands.consumeExact(text)) return;
    if (routeVersion !== originVersion) return;
    const existingLane = activeSubmissionLane(origin);
    if (existingLane) {
      if (!fromComposer) return;
      if (existingLane.pending.length >= 20) {
        showComposerError(new Error(t("composer.queueFull", {},
          "待发送队列已满（最多 20 条）")));
        return;
      }
      hideComposerError();
      existingLane.pending.push({ text, raw: prompt.value,
        attachments: [...attachments], interrupt });
      consumeComposerInput(selectedKey);
      setRun(activeRun);
      promptQueue.render();
      return;
    }
    if (composerProfile.isBusy()) {
      showComposerError(new Error(t("composer.profileBusy", {}, "请等待会话配置更新完成")));
      return;
    }
    hideComposerError();
    let submissionScope = composerScope(origin);
    const lane = { pending: [], admitted: 0 };
    submissionLanes.set(submissionScope, lane);
    const originatingKey = selectedKey;
    const submittedInput = fromComposer ? prompt.value : "";
    const originatingRun = activeRun;
    if (fromComposer) consumeComposerInput(originatingKey);
    setRun(activeRun);
    let selected = null;
    let accepted = false;
    let selectedVersion = originVersion;
    const selectedIsCurrent = () => {
      const current = navigation.get();
      return routeVersion === selectedVersion && (!selected ||
        (current.projectId === selected.projectId &&
          current.sessionId === selected.sessionId));
    };
    try {
      selected = await ensureSession(text, { stageDraft: fromComposer });
      selectedVersion = routeVersion;
      const targetScope = `${selected.projectId}/${selected.sessionId}`;
      if (targetScope !== submissionScope) {
        submissionLanes.delete(submissionScope);
        submissionScope = targetScope;
        submissionLanes.set(submissionScope, lane);
        setRun(activeRun);
      }
      if (originatingRun) {
        if (!await promptQueue.enqueue(selected.projectId, selected.sessionId, text,
          { first: interrupt, priority: interrupt, attachments }))
          throw new Error(t("composer.queueFull", {}, "待发送队列已满（最多 20 条）"));
        accepted = true;
        lane.admitted += 1;
        queueBlocked.delete(`${selected.projectId}/${selected.sessionId}`);
        if (interrupt && selectedIsCurrent()) {
          await maybeCancelPriorityRun();
        }
        // The previous run may finish while the queue POST is in flight.
        // Its completion check can see an empty queue, so retry dispatch here.
        if (selectedIsCurrent()) await dispatchQueued();
        return;
      }
      await ensurePromptReady(selected.projectId, selected.sessionId);
      const run = await startRun(selected.projectId, selected.sessionId, text, attachments);
      accepted = true;
      if (fromComposer && !originatingKey && selectedIsCurrent()) draftStore.clear("");
      if (selectedIsCurrent()) monitorRun(run);
      await Promise.all([...(selectedIsCurrent() ? [refreshSelectedTimeline()] : []),
        loadTasks(), loadRuns(), loadRecovery()]);
    } catch (error) {
      let restored = null;
      const ownerKey = selected
        ? `${selected.projectId}/${selected.sessionId}` : originatingKey;
      const unsent = [...(!accepted && fromComposer
        ? [{ raw: submittedInput, attachments }] : []),
        ...(!accepted ? lane.pending.splice(0) : [])];
      if (unsent.length) {
        if (!originatingKey && selected && selectedIsCurrent())
          draftStore.clear("");
        restored = restoreUnsentItems(ownerKey, unsent).restored;
      }
      rememberUncertainAdmission(ownerKey, error, unsent, restored);
      if (selectedIsCurrent()) {
        if (!fromComposer && !prompt.value.trim() && !composerAttachments.length) {
          prompt.value = text;
          draftStore.edit(selectedKey, text, [], true);
          resizePrompt();
          tokenMeter.refresh();
        }
        showComposerError(error, restored
          ? error.queueAdmissionUncertain
            ? t("composer.queueAdmissionUncertain")
            : t(restored.merged ? "composer.unsentMerged" : "composer.unsentRestored")
          : "");
        prompt.focus();
      } else {
        toast(t("composer.backgroundSendFailed", { error: errorMessage(error) },
          `后台任务未发出：${errorMessage(error)}`), "error");
      }
      resolveObservedAdmission(ownerKey);
    } finally {
      try {
        if (accepted && selected) await drainPendingSubmissions(lane, selected);
      } finally {
        submissionLanes.delete(submissionScope);
        setRun(activeRun);
        promptQueue.render();
      }
    }
  }

  composer.addEventListener("submit", (event) => {
    event.preventDefault();
    const interrupt = interruptRequested;
    interruptRequested = false;
    void submitPrompt({ text: prompt.value.trim(),
      attachments: [...composerAttachments], interrupt });
  });

  stop.addEventListener("click", async () => {
    if (!activeRun) return;
    stop.disabled = true;
    try {
      const run = await cancelRun(activeRun.id);
      setRun(run);
      await Promise.all([refreshSelectedTimeline(), loadTasks(), loadRuns(),
        ...(terminalState(run) ? [loadRecovery()] : [])]);
      if (terminalState(run)) await dispatchQueued();
    } catch (error) {
      showComposerError(error);
    } finally {
      stop.disabled = false;
    }
  });

  function resizePrompt() {
    prompt.style.height = "auto";
    prompt.style.height = `${Math.min(prompt.scrollHeight, 336)}px`;
  }
  window.addEventListener("resize", resizePrompt);
  prompt.addEventListener("input", () => {
    resizePrompt();
    draftStore.edit(selectedKey, prompt.value, composerAttachments);
    tokenMeter.refresh();
  });
  prompt.addEventListener("keydown", (event) => {
    if (slashCommands.onKeyDown(event)) return;
    if (fileMentions.onKeyDown(event)) return;
    if (event.key === "Enter" && !event.shiftKey && !event.isComposing) {
      event.preventDefault();
      const modified = event.ctrlKey || event.metaKey;
      const guide = settingsStore.get().data?.composer?.submit_mode === "guide";
      interruptRequested = Boolean((activeRun || activeSubmissionLane()) &&
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
  actionForm.addEventListener("submit", async (event) => {
    event.preventDefault();
    if (!pendingSessionAction || !actionForm.reportValidity()) return;
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
    } finally {
      actionConfirm.disabled = false;
    }
  });

  function fillCatalogSelects() {
    const agentSelect = $("#agent-select");
    const modelSelect = $("#model-select");
    const selectedAgent = agentSelect.value;
    const selectedModel = modelSelect.value;
    clear(agentSelect);
    clear(modelSelect);
    for (const agent of agentsStore.get().data?.items ?? []) {
      agentSelect.append(element("option", { text: agent.name || agent.id, attrs: { value: agent.id } }));
    }
    for (const model of modelsStore.get().data?.models ?? []) {
      const suffix = model.free ? " · 免费" : "";
      modelSelect.append(element("option", { text: `${model.name || model.id}${suffix}`, attrs: { value: model.id } }));
    }
    if (selectedAgent && [...agentSelect.options].some((option) => option.value === selectedAgent)) agentSelect.value = selectedAgent;
    if (selectedModel && [...modelSelect.options].some((option) => option.value === selectedModel)) modelSelect.value = selectedModel;
    const model = modelsStore.get().data?.models?.find((item) => item.id === modelSelect.value);
    fillReasoningOptions($("#new-session-reasoning"), model,
      $("#new-session-reasoning").value);
  }
  agentsStore.subscribe(fillCatalogSelects);
  modelsStore.subscribe(fillCatalogSelects);
  $("#model-select").addEventListener("change", () => {
    const model = modelsStore.get().data?.models?.find((item) =>
      item.id === $("#model-select").value);
    fillReasoningOptions($("#new-session-reasoning"), model,
      model?.default_reasoning_effort);
  });

  function openNewSession() {
    dialogError.hidden = true;
    dialogError.textContent = "";
    const profile = composerProfile.selection();
    dialogForm.elements.project_id.value = navigation.get().projectId || navigation.preferredProject();
    $("#model-select").value = profile.model_id;
    const model = modelsStore.get().data?.models?.find((item) =>
      item.id === profile.model_id);
    fillReasoningOptions($("#new-session-reasoning"), model,
      profile.reasoning_effort);
    $("#new-session-permission").value = profile.permission_profile;
    if (!dialog.open) dialog.showModal();
    window.setTimeout(() => dialogForm.elements.title.focus(), 0);
  }
  function openNewTask() {
    showActiveSessions();
    navigation.newTask(navigation.get().projectId || navigation.preferredProject());
    if (mobileLayout.matches) setDrawer("sidebar", false);
    prompt.focus();
  }
  $("#new-session").addEventListener("click", openNewTask);
  $("#new-session-configure").addEventListener("click", openNewSession);
  $("#close-new-session").addEventListener("click", () => dialog.close());
  dialogForm.addEventListener("submit", async (event) => {
    event.preventDefault();
    if (event.submitter?.value === "cancel") { dialog.close(); return; }
    if (!dialogForm.reportValidity()) return;
    createButton.disabled = true;
    const values = Object.fromEntries(new FormData(dialogForm));
    try {
      const session = await createSession(values);
      showActiveSessions();
      dialog.close();
      dialogForm.elements.title.value = "";
      navigation.select(session.project_id, session.id);
    } catch (error) {
      dialogError.textContent = errorMessage(error);
      dialogError.hidden = false;
    } finally {
      createButton.disabled = false;
    }
  });

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
    const inactive = !open;
    if (inactive && panel.contains(document.activeElement)) button.focus();
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
  mobileLayout.addEventListener("change", (event) => setDrawer("sidebar",
    !event.matches && (paneLayout?.sidebarOpen() ?? true), { persist: false }));
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
  void paneLayout.load();

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
    onStop: () => stop.click(), isRunning: () => Boolean(activeRun),
    isDrawerOpen: () => (mobileLayout.matches && shell.dataset.sidebar === "open") ||
      (!wideLayout.matches && shell.dataset.inspector === "open"),
    closeDrawers,
  });
  $("#open-shortcuts").addEventListener("click", () => shortcuts.openHelp());
  $("#toggle-theme").addEventListener("click", () => void toggleTheme());
  document.addEventListener("keydown", (event) => {
    if (event.key === "/" && !document.querySelector("dialog[open]") &&
        document.activeElement?.tagName !== "INPUT" &&
        document.activeElement?.tagName !== "TEXTAREA") {
      event.preventDefault();
      $("#session-search").focus();
    }
  });

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
  document.addEventListener("visibilitychange", () => {
    if (document.hidden) {
      window.clearTimeout(tasksTimer);
      window.clearTimeout(runsTimer);
      window.clearTimeout(approvalsTimer);
    }
    else {
      scheduleTaskRefresh();
      void loadRuns().then(refreshSelectedQueue);
      scheduleApprovalRefresh();
      if (activeRun) scheduleRunPoll(100);
    }
  });

  const initial = await Promise.allSettled([
    loadBootstrap(),
    loadSessions(),
    loadCatalogs(),
    loadSettings(),
    loadManagementResources(),
    loadTasks(),
    loadApprovals(),
    loadRecovery(),
    loadRuns(),
  ]);
  if (initial.some((result) => result.status === "rejected")) {
    toast(t("resource.partialLoad", {}, "部分资源暂时无法载入，可继续重试。"), "error");
  }

  await startWorkspaceNavigation({ navigation, settingsStore, sessionsStore,
    sessionDetailStore,
    dialog: $("#startup-choice-dialog"),
    title: $("#startup-last-title"),
    continueButton: $("#startup-continue"),
    newButton: $("#startup-new"), prompt, entryHash });
  scheduleTaskRefresh();
  scheduleApprovalRefresh();
}
