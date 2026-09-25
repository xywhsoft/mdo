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
import { recoveryStore, selectRecovery, loadRecovery, abandonRecovery } from "./state/recovery.js";
import { navigation } from "./state/navigation.js";
import { createSessionList } from "./features/sessions/session-list.js";
import { formatSessionMarkdown, sessionMarkdownFilename } from "./features/sessions/session-export.js";
import { createProjectDialog } from "./features/sessions/project-dialog.js";
import { timelineStore, selectTimeline, clearTimeline, refreshSelectedTimeline, reloadSelectedTimeline } from "./features/chat/timeline-store.js";
import { todoStore, selectTodo, clearTodo } from "./state/todo.js";
import { createTimelineView } from "./features/chat/timeline.js";
import { createMessageEditDialog } from "./features/chat/message-edit-dialog.js";
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
import { createKeyboardShortcuts } from "./features/shell/keyboard-shortcuts.js";
import { createRunNotifications } from "./features/shell/run-notifications.js";
import { startWorkspaceNavigation } from "./features/shell/workspace-startup.js";
import { createPaneLayout } from "./features/shell/pane-layout.js";
import { api } from "./api/client.js";
import { clear, element, errorMessage, toast } from "./utils/dom.js";

const $ = (selector) => {
  const node = document.querySelector(selector);
  if (!node) throw new Error(`Missing UI element: ${selector}`);
  return node;
};

function terminalState(run) {
  return Boolean(run?.terminal);
}

function runStateText(state) {
  return ({
    created: "准备中",
    running: "运行中",
    succeeded: "已完成",
    failed: "运行失败",
    cancelled: "已停止",
    timed_out: "已超时",
    archived: "已归档",
    trash: "回收站",
    loading: "载入中",
  })[state] ?? "就绪";
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
  let tasksTimer = 0;
  let runsTimer = 0;
  let approvalsTimer = 0;
  let submitting = false;
  let messageActionBusy = false;
  let interruptRequested = false;
  let themeToggleBusy = false;
  let composerAttachments = [];
  let composerImages = null;
  let shortcuts;
  const queueBlocked = new Set();
  // A cancelled run can remain nonterminal through several polls. Avoid
  // repeating DELETE while its persisted priority queue item is still waiting.
  const priorityCancelAttempts = new Set();

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

  const sessionList = createSessionList({
    container: $("#session-list"),
    count: $("#session-count"),
    store: sessionsStore,
    projectsStore,
    filter: $("#session-status-filter"),
    navigation,
    onSelect(session) {
      navigation.select(session.project_id, session.id);
      closeDrawers();
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

  async function replaceAndRunMessage(sequence, text, attachments, label) {
    if (messageActionBusy) throw new Error("请等待当前消息操作完成");
    if (!Number.isSafeInteger(sequence) || sequence < 1 ||
        (!text.trim() && !attachments.length))
      throw new Error("这条消息没有可用的编辑边界或完整文本");
    const session = sessionDetailStore.get().data;
    const selected = navigation.get();
    if (!session || session.project_id !== selected.projectId ||
        session.id !== selected.sessionId)
      throw new Error("请先选择会话");
    if (activeRun || submitting || composerImages?.isUploading())
      throw new Error("请在当前运行结束后操作消息");
    if (promptQueue.peek(selected.projectId, selected.sessionId) ||
        prompt.value.trim() || composerAttachments.length)
      throw new Error("请先处理草稿和待发送队列，再编辑历史消息");
    messageActionBusy = true;
    try {
      const history = await loadSessionHistory(session);
      const current = navigation.get();
      if (current.projectId !== selected.projectId || current.sessionId !== selected.sessionId)
        throw new Error("会话已切换，请重新选择消息");
      if (sequence > history.last_sequence)
        throw new Error("消息已不在当前会话历史中，请刷新会话");
      const updated = await truncateSession({ ...session, etag: history.etag,
        revision: history.revision }, sequence - 1);
      sessionDetailStore.setData(updated);
      await Promise.all([reloadSelectedTimeline(),
        selectTodo(updated.project_id, updated.id)]);
      let run;
      try { run = await startRun(updated.project_id, updated.id, text.trim(), attachments); }
      catch (error) {
        const key = `${updated.project_id}/${updated.id}`;
        prompt.value = text;
        composerAttachments = [...attachments];
        composerImages.set(attachments);
        draftStore.edit(key, text, attachments, true);
        prompt.dispatchEvent(new Event("input", { bubbles: true }));
        showComposerError(error);
        throw error;
      }
      monitorRun(run);
      void Promise.allSettled([refreshSelectedTimeline(), loadTasks(),
        loadRuns(), loadRecovery()]);
      toast(label === "重试" ? "已在当前会话重试" : "已在当前会话发送编辑后的消息");
    } finally { messageActionBusy = false; }
  }

  let conversationSearch;
  const timelineView = createTimelineView({
    container: $("#timeline"), welcome: $("#welcome"),
    toBottom: $("#to-bottom"), store: timelineStore,
    feedbackStore,
    onSearchCount: (count, historyLost) => conversationSearch?.setCount(count, historyLost),
    onFeedback: async (eventId, value) => {
      const selected = navigation.get();
      if (!selected.projectId || !selected.sessionId) throw new Error("请先选择会话");
      await setFeedback(selected.projectId, selected.sessionId, eventId, value);
    },
    onFork: async (throughSequence) => {
      const session = sessionDetailStore.get().data;
      if (!session || activeRun) throw new Error("请在当前运行结束后分叉会话");
      const history = await loadSessionHistory(session);
      const boundary = throughSequence ?? history.last_sequence;
      if (!Number.isSafeInteger(boundary) || boundary < 0 ||
          boundary > history.last_sequence)
        throw new Error("此回复已不在当前会话历史中，请刷新会话");
      const fork = await forkSession({ ...session, etag: history.etag, revision: history.revision }, {
        title: `${session.title || "未命名任务"}（分支）`,
        through_sequence: boundary,
      });
      navigation.select(fork.project_id, fork.id);
      toast("已创建会话分支");
    },
    onEdit: async (sequence, text, attachments) => {
      const edited = await messageEditDialog.open(text, attachments);
      if (edited !== null) await replaceAndRunMessage(sequence, edited, attachments, "编辑");
    },
    onRetry: (sequence, text, attachments) =>
      replaceAndRunMessage(sequence, text, attachments, "重试"),
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
      else if (command === "/model") $("#composer-model").focus();
      else if (command === "/settings") navigation.openSettings("general");
      else if (command === "/theme") {
        navigation.openSettings("general");
        window.setTimeout(() => $("#setting-theme").focus(), 0);
      } else if (command === "/help") {
        shortcuts.openHelp();
      } else if (command === "/stop") {
        if (!activeRun) throw new Error("当前没有运行中的任务");
        stop.click();
      } else {
        if (!session) throw new Error("请先选择会话");
        if (command === "/export") await handleSessionAction("export", session);
        else {
          if (activeRun) throw new Error("请在当前运行结束后修改会话历史");
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
    activeRun = run && !terminalState(run) ? run : null;
    const guide = settingsStore.get().data?.composer?.submit_mode === "guide";
    const shown = run ?? { state: sessionWritable ? "idle" : selectedSessionStatus };
    runStatus.dataset.state = shown.state;
    runStatus.lastElementChild.textContent = runStateText(shown.state);
    send.hidden = false;
    stop.hidden = !activeRun;
    prompt.disabled = !sessionWritable;
    send.disabled = !sessionWritable || submitting ||
      composerImages?.isUploading() || composerProfile.isBusy();
    composerImages?.setWritable(sessionWritable && !submitting);
    composerProfile.setRunActive(Boolean(activeRun));
    send.setAttribute("aria-label", activeRun ? "加入待发送队列" : "发送任务");
    composerHint.textContent = activeRun
      ? (guide
        ? "Enter 中断并发送 · Ctrl Enter 排队"
        : "Enter 排队 · Ctrl Enter 中断并发送")
      : "Enter 发送 · Shift Enter 换行";
    $("#shortcut-enter-description").textContent = guide
      ? "发送；运行中中断并优先发送" : "发送；运行中加入待发送队列";
    $("#shortcut-control-enter-description").textContent = guide
      ? "运行中加入待发送队列" : "中断当前运行，优先发送输入";
    mobileActivity.hidden = !activeRun;
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
      ? root.split(/[\\/]/).filter(Boolean).at(-1) || root : "本地工作区";
    workspaceChip.title = root ? `当前工作目录：${root}` : "当前工作目录";
  }

  function updateContext(state) {
    clear(contextList);
    const route = navigation.get();
    const session = state.data?.project_id === route.projectId &&
      state.data?.id === route.sessionId ? state.data : null;
    if (!session) {
      contextList.append(
        element("dt", { text: "状态" }),
        element("dd", { text: state.status === "loading" ? "正在载入…" :
          route.sessionId ? "未选择会话" : "新任务" }),
        element("dt", { text: "项目" }),
        element("dd", { text: route.projectId || "default" }),
        element("dt", { text: "工作区" }),
        element("dd", { text: currentWorkspace() || "本地工作区" }),
      );
      return;
    }
    const values = [
      ["项目", session.project_id],
      ["Agent", session.agent_id],
      ["模型", session.model_id],
      ["协议", session.protocol],
      ["推理", session.reasoning_effort || "自动"],
      ["工作区", session.workspace_root || "默认"],
      ["输出上限", session.max_output_tokens ? `${session.max_output_tokens} tokens` : "默认"],
      ["配置版本", session.config_revision],
      ["模块代次", session.module_generation],
      ["Skill 代次", session.skill_generation],
    ];
    for (const [label, value] of values) {
      contextList.append(element("dt", { text: label }), element("dd", { text: value || "—" }));
    }
  }

  sessionDetailStore.subscribe((state) => {
    const session = state.data;
    sessionWritable = session ? session.status === "active" : !navigation.get().sessionId;
    selectedSessionStatus = session?.status ?? (navigation.get().sessionId ? "loading" : "active");
    prompt.placeholder = sessionWritable ? "向墨斗描述任务…" : "该会话不可运行；请先恢复到进行中";
    setRun(activeRun);
    if (session) {
      const title = session.title || "未命名任务";
      sessionTitle.textContent = title;
      const statusText = session.status === "archived" ? " · 已归档" : session.status === "trash" ? " · 回收站" : "";
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

  bootstrapStore.subscribe((state) => {
    runtimeState.dataset.state = state.status === "error" ? "error" : state.data?.ready ? "ready" : "loading";
    runtimeLabel.textContent = state.status === "error"
      ? errorMessage(state.error)
      : state.data?.ready ? `本地服务 ${state.data.version}` : state.data?.message || "正在连接本地服务…";
  });

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
          await dispatchQueued();
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
    const path = `/projects/${projectId}/sessions/${sessionId}/recovery`;
    for (let attempt = 0; attempt < 4; attempt += 1) {
      try {
        const response = await api.get(path);
        if (!response.data?.resume_required) return;
        if (priority && response.data.total === 0) {
          await abandonRecovery(response.data);
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
    await promptQueue.exclusive(async () => {
      const entry = promptQueue.peek(selected.projectId, selected.sessionId);
      if (!entry || entry.state !== "pending") return;
      try {
        await ensurePromptReady(selected.projectId, selected.sessionId,
          Boolean(entry.priority));
        if (navigation.get().projectId !== selected.projectId ||
            navigation.get().sessionId !== selected.sessionId) return;
        await promptQueue.markSending(selected.projectId, selected.sessionId, entry.id);
        const run = await startRun(selected.projectId, selected.sessionId,
          entry.text, entry.attachments ?? []);
        if (navigation.get().projectId === selected.projectId &&
            navigation.get().sessionId === selected.sessionId) monitorRun(run);
        await promptQueue.remove(selected.projectId, selected.sessionId, entry.id);
        hideComposerError();
        await Promise.all([refreshSelectedTimeline(), loadTasks(), loadRuns(), loadRecovery()]);
      } catch (error) {
        if (error?.code !== "recovery_required") queueBlocked.add(key);
        try { await promptQueue.select(selected.projectId, selected.sessionId); }
        catch { /* Preserve the original dispatch error. */ }
        showComposerError(error);
      }
    });
  }

  navigation.subscribe(async ({ view, projectId, sessionId, settingsSection }) => {
    if (view === "settings") {
      if (!settingsActive) inspectorBeforeSettings = shell.dataset.inspector;
      settingsActive = true;
      settingsWorkspace.hidden = false;
      for (const region of agentWorkspaceRegions) region.hidden = true;
      skipLink.href = "#settings-content";
      skipLink.textContent = "跳到设置内容";
      settingsView.selectSection(settingsSection);
      const standalonePage = !["general", "agent", "web"].includes(settingsSection);
      $("#settings-title").textContent = settingsSection === "projects"
        ? "项目" : settingsSection === "schedules" ? "计划任务" : "设置";
      $("#settings-revision").hidden = standalonePage;
      $("#settings-actions").hidden = standalonePage;
      if (settingsSection === "schedules") void schedulePanel.refresh();
      closeDrawers();
      setDrawer("inspector", false, { persist: false });
      if (!settingsStore.get().data) await loadSettings();
      return;
    }
    settingsWorkspace.hidden = true;
    for (const region of agentWorkspaceRegions) region.hidden = false;
    skipLink.href = "#timeline";
    skipLink.textContent = "跳到对话";
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
      sessionTitle.textContent = "新任务";
      sessionSubtitle.textContent = `将在 ${project} 项目中创建任务`;
      mobileTitle.textContent = "新任务";
      mobileMeta.textContent = project;
      syncWorkspaceChip();
      updateContext(sessionDetailStore.get());
    }
    if (key === selectedKey) {
      if (key) {
        queueBlocked.add(key);
        try {
          await promptQueue.select(projectId, sessionId);
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
    } catch (error) { showComposerError(error); return; }
    queueBlocked.delete(key);
    findActiveRun();
    void maybeCancelPriorityRun();
    void dispatchQueued();
  });

  function hideComposerError() {
    composerError.hidden = true;
    composerError.textContent = "";
    delete composerError.dataset.code;
  }
  function recoveryRequiredError() {
    const error = new Error("上轮运行尚未恢复，请先在“决策”中处理；输入和待发送消息会保留。");
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
  function showComposerError(error) {
    composerError.textContent = errorMessage(error);
    composerError.dataset.code = error?.code || "";
    if (error?.code === "recovery_required") {
      const openDecisions = element("button", {
        className: "composer-error-action",
        text: "打开恢复决策",
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

  async function ensureSession(text, stageDraft = true) {
    const selected = navigation.get();
    if (selected.sessionId) return selected;
    const title = text.trim().split(/\r?\n/, 1)[0].slice(0, 80) || "图片任务";
    const session = await createSession({ project_id: selected.projectId || "default", title,
      ...composerProfile.selection() });
    showActiveSessions();
    if (stageDraft)
      draftStore.edit(`${session.project_id}/${session.id}`, text, [], true);
    navigation.select(session.project_id, session.id);
    selectTimeline(session.project_id, session.id);
    return { projectId: session.project_id, sessionId: session.id };
  }

  async function submitPrompt({ text, attachments = [], interrupt = false,
    fromComposer = true }) {
    fileMentions.hide();
    if ((!text && !attachments.length) || submitting ||
        composerImages.isUploading()) return;
    if (fromComposer && !attachments.length &&
        await slashCommands.consumeExact(text)) return;
    if (composerProfile.isBusy()) {
      showComposerError(new Error("请等待会话配置更新完成"));
      return;
    }
    hideComposerError();
    submitting = true;
    send.disabled = true;
    const originatingKey = selectedKey;
    try {
      const selected = await ensureSession(text, fromComposer);
      if (activeRun) {
        if (!await promptQueue.enqueue(selected.projectId, selected.sessionId, text,
          { first: interrupt, priority: interrupt, attachments }))
          throw new Error("待发送队列已满（最多 20 条）");
        queueBlocked.delete(`${selected.projectId}/${selected.sessionId}`);
        if (fromComposer) {
          prompt.value = "";
          composerAttachments = [];
          composerImages.clear();
          draftStore.clear(`${selected.projectId}/${selected.sessionId}`);
          resizePrompt();
          tokenMeter.refresh();
        }
        if (interrupt) {
          await maybeCancelPriorityRun();
        }
        return;
      }
      await ensurePromptReady(selected.projectId, selected.sessionId);
      const run = await startRun(selected.projectId, selected.sessionId, text, attachments);
      if (fromComposer) {
        prompt.value = "";
        composerAttachments = [];
        composerImages.clear();
        draftStore.clear(`${selected.projectId}/${selected.sessionId}`);
        if (!originatingKey) draftStore.clear("");
        resizePrompt();
        tokenMeter.refresh();
      }
      monitorRun(run);
      await Promise.all([refreshSelectedTimeline(), loadTasks(), loadRuns(), loadRecovery()]);
    } catch (error) {
      if (!fromComposer && !prompt.value.trim() && !composerAttachments.length) {
        prompt.value = text;
        draftStore.edit(selectedKey, text, [], true);
        resizePrompt();
        tokenMeter.refresh();
      }
      showComposerError(error);
      prompt.focus();
    } finally {
      submitting = false;
      setRun(activeRun);
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
    prompt.style.height = `${Math.min(prompt.scrollHeight, 220)}px`;
  }
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
      interruptRequested = Boolean(activeRun && (guide ? !modified : modified));
      composer.requestSubmit();
    }
  });

  for (const starter of document.querySelectorAll("[data-prompt]")) {
    starter.addEventListener("click", () => {
      if (submitting || composerImages.isUploading() || composerProfile.isBusy()) return;
      void submitPrompt({ text: starter.dataset.prompt, fromComposer: false });
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
    toast({ rename: "会话已重命名", pin: updated.pinned ? "会话已置顶" : "已取消置顶", archive: "会话已归档", unarchive: "会话已移回进行中", trash: "会话已移到回收站", restore: "会话已恢复", fork: "已创建会话分支", truncate: "会话历史已截断", clear: "会话历史已清空" }[action]);
    return updated;
  }

  async function handleSessionAction(action, session) {
    if (action === "export" || action === "export_json") {
      if (action === "export") toast("正在整理 Markdown 会话记录…");
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
      toast(action === "export_json" ? "JSON 备份已开始下载" : "Markdown 已开始下载");
      return;
    }
    if (!["rename", "trash", "fork", "truncate", "clear"].includes(action)) return applySessionAction(action, session);
    let history = null;
    if (action === "fork" || action === "truncate") history = await loadSessionHistory(session);
    pendingSessionAction = { action, session: history ? { ...session, etag: history.etag, revision: history.revision } : session };
    const hasTitle = action === "rename" || action === "fork";
    const hasSequence = action === "fork" || action === "truncate";
    const content = {
      rename: ["重命名会话", "新标题会同步写入会话元数据。", "保存"],
      trash: ["移到回收站", `“${session.title || "未命名任务"}”可从回收站恢复。`, "移到回收站"],
      fork: ["创建会话分支", "从指定消息序列创建独立会话。原会话不会改变。", "创建分支"],
      truncate: ["截断会话历史", "指定序列之后的模型账本将被永久移除。", "截断历史"],
      clear: ["清空会话历史", "模型账本将被永久清空，并重新注入当前系统提示词。", "清空历史"],
    }[action];
    actionTitle.textContent = content[0];
    actionDescription.textContent = content[1];
    actionFields.hidden = !hasTitle && !hasSequence;
    actionTitleField.hidden = !hasTitle;
    actionSequenceField.hidden = !hasSequence;
    actionForm.elements.title.required = hasTitle;
    actionForm.elements.title.value = action === "rename" ? session.title || "" : action === "fork" ? `${session.title || "未命名任务"}（分支）` : "";
    actionForm.elements.through_sequence.required = hasSequence;
    actionForm.elements.through_sequence.value = hasSequence ? String(history.last_sequence) : "";
    actionForm.elements.through_sequence.max = hasSequence ? String(history.last_sequence) : "";
    actionConfirm.textContent = content[2];
    actionConfirm.className = ["trash", "truncate", "clear"].includes(action) ? "danger-button" : "primary-button";
    actionError.hidden = true;
    if (!actionDialog.open) actionDialog.showModal();
    if (action === "rename") window.setTimeout(() => actionForm.elements.title.select(), 0);
  }

  $("#close-session-action").addEventListener("click", () => actionDialog.close());
  $("#cancel-session-action").addEventListener("click", () => actionDialog.close());
  actionForm.addEventListener("submit", async (event) => {
    event.preventDefault();
    if (!pendingSessionAction || !actionForm.reportValidity()) return;
    actionError.hidden = true;
    actionConfirm.disabled = true;
    try {
      await applySessionAction(pendingSessionAction.action, pendingSessionAction.session, actionForm.elements.title.value.trim());
      actionDialog.close();
      pendingSessionAction = null;
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
    for (const name of ["tasks", "decisions", "context"]) {
      const selected = tabName === name;
      $(`#${name}-tab`).setAttribute("aria-selected", String(selected));
      $(`#${name}-panel`).hidden = !selected;
    }
  }
  $("#tasks-tab").addEventListener("click", () => selectInspectorTab("tasks"));
  $("#decisions-tab").addEventListener("click", () => selectInspectorTab("decisions"));
  $("#context-tab").addEventListener("click", () => selectInspectorTab("context"));
  $("#open-settings").addEventListener("click", () => navigation.openSettings("general"));
  $("#open-schedules").addEventListener("click", () => navigation.openSettings("schedules"));
  $("#close-settings").addEventListener("click", () => {
    navigation.backToWorkspace();
  });
  workspaceChip.addEventListener("click", () => { selectInspectorTab("context"); setDrawer("inspector", true); });

  shortcuts = createKeyboardShortcuts({
    dialog: $("#shortcuts-dialog"), navigation, search: conversationSearch,
    onNew: openNewTask,
    onExport: async () => {
      const session = sessionDetailStore.get().data;
      if (!session) return;
      try { await handleSessionAction("export", session); }
      catch (error) { toast(errorMessage(error), "error"); }
    },
    onSettings: (open) => open ? navigation.openSettings("general")
      : $("#close-settings").click(),
    onToggleTheme: async () => {
      if (themeToggleBusy) return;
      if (settingsView.hasPendingChanges()) {
        toast("先预览、应用或放弃尚未保存的设置", "error");
        return;
      }
      themeToggleBusy = true;
      try {
        const settings = settingsStore.get().data ?? (await loadSettings()).data;
        if (!settings?.appearance || !settings.etag)
          throw new Error("当前设置尚未载入");
        const theme = settings.appearance.theme === "dark" ? "light" : "dark";
        const patch = { appearance: { theme } };
        await previewSettings(patch);
        await applySettings(patch, settings.etag);
        toast(theme === "dark" ? "已切换为深色主题" : "已切换为浅色主题");
      } catch (error) { toast(errorMessage(error), "error"); }
      finally { themeToggleBusy = false; }
    },
    onStop: () => stop.click(), isRunning: () => Boolean(activeRun),
    isDrawerOpen: () => shell.dataset.sidebar === "open" ||
      (mobileLayout.matches && shell.dataset.inspector === "open"),
    closeDrawers,
  });
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
    runsTimer = window.setTimeout(() => void loadRuns(),
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
      void loadRuns();
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
    toast("部分资源暂时无法载入，可继续重试。", "error");
  }

  await startWorkspaceNavigation({ navigation, settingsStore, sessionsStore,
    dialog: $("#startup-choice-dialog"),
    title: $("#startup-last-title"),
    continueButton: $("#startup-continue"),
    newButton: $("#startup-new"), prompt, entryHash });
  scheduleTaskRefresh();
  scheduleApprovalRefresh();
}
