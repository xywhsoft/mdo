import { mountIcons } from "./components/icons.js";
import { bootstrapStore, loadBootstrap } from "./state/bootstrap.js";
import {
  sessionsStore, sessionDetailStore, loadSessions, loadSession, createSession,
  patchSession, trashSession, restoreSession,
} from "./state/sessions.js";
import { modelsStore, agentsStore, loadCatalogs, loadModels, loadAgents } from "./state/catalogs.js";
import { settingsStore, loadSettings } from "./state/settings.js";
import {
  modulesStore, skillsStore, mcpStore, permissionsStore, storageStore,
  diagnosticsStore, loadResource, loadManagementResources,
} from "./state/resources.js";
import { tasksStore, loadTasks } from "./state/tasks.js";
import { runsStore, loadRuns, startRun, readRun, cancelRun } from "./state/runs.js";
import { navigation } from "./state/navigation.js";
import { createSessionList } from "./features/sessions/session-list.js";
import { timelineStore, selectTimeline, clearTimeline, refreshSelectedTimeline } from "./features/chat/timeline-store.js";
import { createTimelineView } from "./features/chat/timeline.js";
import { createTaskPanel } from "./features/tasks/task-panel.js";
import { createSettingsView } from "./features/settings/settings-view.js";
import { createResourcePanels } from "./features/settings/resource-panels.js";
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

  const shell = $("#app-shell");
  const wideLayout = window.matchMedia("(min-width: 1181px)");
  const mobileLayout = window.matchMedia("(max-width: 760px)");
  shell.dataset.inspector = wideLayout.matches ? "open" : "closed";
  const prompt = $("#prompt");
  const composer = $("#composer");
  const send = $("#send");
  const stop = $("#stop");
  const composerError = $("#composer-error");
  const runStatus = $("#run-status");
  const runtimeState = $("#runtime-state");
  const runtimeLabel = $("#runtime-label");
  const sessionTitle = $("#session-title");
  const sessionSubtitle = $("#session-subtitle");
  const mobileTitle = $("#mobile-session-title");
  const mobileMeta = $("#mobile-session-meta");
  const contextList = $("#context-list");
  const workspaceLabel = $("#workspace-label");
  const reasoningLabel = $("#reasoning-label");
  const mobileActivity = $("#mobile-activity-dot");
  const settingsWorkspace = $("#settings-workspace");
  const skipLink = $(".skip-link");
  const agentWorkspaceRegions = [$(".workspace-header"), $(".conversation"), $(".composer-region")];
  let settingsActive = false;
  let inspectorBeforeSettings = shell.dataset.inspector;
  let sessionWritable = true;
  let selectedSessionStatus = "active";
  let activeRun = null;
  let runMonitor = 0;
  let selectedKey = "";
  let tasksTimer = 0;

  const sessionList = createSessionList({
    container: $("#session-list"),
    count: $("#session-count"),
    store: sessionsStore,
    filter: $("#session-status-filter"),
    navigation,
    onSelect(session) {
      navigation.select(session.project_id, session.id);
      closeDrawers();
    },
    onAction: handleSessionAction,
  });
  $("#session-search").addEventListener("input", (event) => sessionList.setQuery(event.target.value));

  createTimelineView({ container: $("#timeline"), welcome: $("#welcome"), store: timelineStore });
  createTaskPanel({
    container: $("#task-list"),
    summary: $("#task-summary"),
    store: tasksStore,
    onChanged: () => void loadRuns(),
  });
  const settingsView = createSettingsView({
    form: $("#settings-form"),
    store: settingsStore,
    navigation,
    onApplied: () => Promise.all([loadBootstrap(), loadCatalogs()]),
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
    },
    reload: (name) => name === "models" ? loadModels()
      : name === "modules" ? Promise.all([loadResource("modules"), loadAgents()])
        : loadResource(name),
  });

  function setRun(run) {
    activeRun = run && !terminalState(run) ? run : null;
    const shown = run ?? { state: sessionWritable ? "idle" : selectedSessionStatus };
    runStatus.dataset.state = shown.state;
    runStatus.lastElementChild.textContent = runStateText(shown.state);
    send.hidden = Boolean(activeRun);
    stop.hidden = !activeRun;
    prompt.disabled = Boolean(activeRun) || !sessionWritable;
    send.disabled = !sessionWritable;
    mobileActivity.hidden = !activeRun;
  }

  function updateContext(state) {
    clear(contextList);
    const session = state.data;
    if (!session) {
      contextList.append(element("dt", { text: "状态" }), element("dd", { text: state.status === "loading" ? "正在载入…" : "未选择会话" }));
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
      workspaceLabel.textContent = session.workspace_root ? session.workspace_root.split(/[\\/]/).filter(Boolean).at(-1) || session.workspace_root : "本地工作区";
      reasoningLabel.textContent = session.reasoning_effort || "自动";
    }
    updateContext(state);
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
    if (run) monitorRun(run);
    else if (!activeRun) setRun(null);
  }
  runsStore.subscribe(findActiveRun);

  function scheduleRunPoll(delay = 750) {
    window.clearTimeout(runMonitor);
    runMonitor = window.setTimeout(async () => {
      if (!activeRun || document.hidden) return;
      try {
        const run = await readRun(activeRun.id);
        setRun(run);
        await refreshSelectedTimeline();
        if (terminalState(run)) {
          await Promise.all([loadSessions(), loadRuns(), loadTasks()]);
          prompt.disabled = false;
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

  navigation.subscribe(async ({ view, projectId, sessionId, settingsSection }) => {
    if (view === "settings") {
      if (!settingsActive) inspectorBeforeSettings = shell.dataset.inspector;
      settingsActive = true;
      settingsWorkspace.hidden = false;
      for (const region of agentWorkspaceRegions) region.hidden = true;
      skipLink.href = "#settings-content";
      skipLink.textContent = "跳到设置内容";
      settingsView.selectSection(settingsSection);
      closeDrawers();
      if (!settingsStore.get().data) await loadSettings();
      return;
    }
    settingsWorkspace.hidden = true;
    for (const region of agentWorkspaceRegions) region.hidden = false;
    skipLink.href = "#timeline";
    skipLink.textContent = "跳到对话";
    if (settingsActive) {
      settingsActive = false;
      setDrawer("inspector", inspectorBeforeSettings === "open" && wideLayout.matches);
    }
    const key = projectId && sessionId ? `${projectId}/${sessionId}` : "";
    if (key === selectedKey) return;
    selectedKey = key;
    window.clearTimeout(runMonitor);
    runMonitor = 0;
    activeRun = null;
    setRun(null);
    hideComposerError();
    if (!key) {
      clearTimeline();
      sessionDetailStore.reset();
      sessionTitle.textContent = "新任务";
      sessionSubtitle.textContent = "选择会话，或向默认 Agent 发起任务";
      mobileTitle.textContent = "墨斗";
      mobileMeta.textContent = "Agent 工作台";
      return;
    }
    sessionDetailStore.reset();
    selectTimeline(projectId, sessionId);
    await Promise.all([loadSession(projectId, sessionId), loadRuns()]);
    findActiveRun();
  });

  function hideComposerError() {
    composerError.hidden = true;
    composerError.textContent = "";
  }
  function showComposerError(error) {
    composerError.textContent = errorMessage(error);
    composerError.hidden = false;
  }

  async function ensureSession(text) {
    const selected = navigation.get();
    if (selected.sessionId) return selected;
    const title = text.trim().split(/\r?\n/, 1)[0].slice(0, 80);
    const session = await createSession({ project_id: "default", title });
    navigation.select(session.project_id, session.id);
    selectTimeline(session.project_id, session.id);
    return { projectId: session.project_id, sessionId: session.id };
  }

  composer.addEventListener("submit", async (event) => {
    event.preventDefault();
    const text = prompt.value.trim();
    if (!text || activeRun) return;
    hideComposerError();
    send.disabled = true;
    try {
      const selected = await ensureSession(text);
      const run = await startRun(selected.projectId, selected.sessionId, text);
      prompt.value = "";
      resizePrompt();
      monitorRun(run);
      await Promise.all([refreshSelectedTimeline(), loadTasks(), loadRuns()]);
    } catch (error) {
      showComposerError(error);
      prompt.focus();
    } finally {
      send.disabled = !sessionWritable;
    }
  });

  stop.addEventListener("click", async () => {
    if (!activeRun) return;
    stop.disabled = true;
    try {
      const run = await cancelRun(activeRun.id);
      setRun(run);
      await Promise.all([refreshSelectedTimeline(), loadTasks(), loadRuns()]);
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
  prompt.addEventListener("input", resizePrompt);
  prompt.addEventListener("keydown", (event) => {
    if (event.key === "Enter" && !event.shiftKey && !event.isComposing) {
      event.preventDefault();
      composer.requestSubmit();
    }
  });

  for (const starter of document.querySelectorAll("[data-prompt]")) {
    starter.addEventListener("click", () => {
      prompt.value = starter.dataset.prompt;
      resizePrompt();
      prompt.focus();
    });
  }

  const dialog = $("#new-session-dialog");
  const dialogForm = $("#new-session-form");
  const dialogError = $("#new-session-error");
  const createButton = $("#create-session");
  const actionDialog = $("#session-action-dialog");
  const actionForm = $("#session-action-form");
  const actionFields = $("#session-action-fields");
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
    else throw new TypeError("unknown session action");
    await refreshSelectedSession(updated);
    toast({ rename: "会话已重命名", pin: updated.pinned ? "会话已置顶" : "已取消置顶", archive: "会话已归档", unarchive: "会话已移回进行中", trash: "会话已移到回收站", restore: "会话已恢复" }[action]);
    return updated;
  }

  async function handleSessionAction(action, session) {
    if (action !== "rename" && action !== "trash") return applySessionAction(action, session);
    pendingSessionAction = { action, session };
    const rename = action === "rename";
    actionTitle.textContent = rename ? "重命名会话" : "移到回收站";
    actionDescription.textContent = rename ? "新标题会同步写入会话元数据。" : `“${session.title || "未命名任务"}”可从回收站恢复。`;
    actionFields.hidden = !rename;
    actionForm.elements.title.required = rename;
    actionForm.elements.title.value = rename ? session.title || "" : "";
    actionConfirm.textContent = rename ? "保存" : "移到回收站";
    actionConfirm.className = rename ? "primary-button" : "danger-button";
    actionError.hidden = true;
    if (!actionDialog.open) actionDialog.showModal();
    if (rename) window.setTimeout(() => actionForm.elements.title.select(), 0);
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
  }
  agentsStore.subscribe(fillCatalogSelects);
  modelsStore.subscribe(fillCatalogSelects);

  function openNewSession() {
    dialogError.hidden = true;
    dialogError.textContent = "";
    if (!dialog.open) dialog.showModal();
    window.setTimeout(() => dialogForm.elements.title.focus(), 0);
  }
  $("#new-session").addEventListener("click", openNewSession);
  $("#close-new-session").addEventListener("click", () => dialog.close());
  dialogForm.addEventListener("submit", async (event) => {
    event.preventDefault();
    if (event.submitter?.value === "cancel") { dialog.close(); return; }
    if (!dialogForm.reportValidity()) return;
    createButton.disabled = true;
    const values = Object.fromEntries(new FormData(dialogForm));
    try {
      const session = await createSession(values);
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

  function setDrawer(name, open) {
    shell.dataset[name] = open ? "open" : "closed";
    const button = name === "sidebar" ? $("#open-sidebar") : $("#open-inspector");
    const panel = name === "sidebar" ? $("#sidebar") : $("#inspector");
    const inactive = name === "sidebar" ? mobileLayout.matches && !open : !open;
    if (inactive && panel.contains(document.activeElement)) button.focus();
    panel.inert = inactive;
    button.setAttribute("aria-expanded", String(open));
    if (name === "inspector") $("#toggle-inspector").setAttribute("aria-expanded", String(open));
  }
  function closeDrawers() { setDrawer("sidebar", false); setDrawer("inspector", false); }
  $("#open-sidebar").addEventListener("click", () => setDrawer("sidebar", true));
  $("#close-sidebar").addEventListener("click", () => setDrawer("sidebar", false));
  $("#open-inspector").addEventListener("click", () => setDrawer("inspector", true));
  $("#close-inspector").addEventListener("click", () => setDrawer("inspector", false));
  $("#toggle-inspector").addEventListener("click", () => setDrawer("inspector", shell.dataset.inspector !== "open"));
  $("#scrim").addEventListener("click", closeDrawers);
  wideLayout.addEventListener("change", (event) => setDrawer("inspector", !settingsActive && event.matches));
  mobileLayout.addEventListener("change", () => setDrawer("sidebar", shell.dataset.sidebar === "open"));
  setDrawer("sidebar", false);
  setDrawer("inspector", !settingsActive && wideLayout.matches);

  function selectInspectorTab(tabName) {
    const tasks = tabName === "tasks";
    $("#tasks-tab").setAttribute("aria-selected", String(tasks));
    $("#context-tab").setAttribute("aria-selected", String(!tasks));
    $("#tasks-panel").hidden = !tasks;
    $("#context-panel").hidden = tasks;
  }
  $("#tasks-tab").addEventListener("click", () => selectInspectorTab("tasks"));
  $("#context-tab").addEventListener("click", () => selectInspectorTab("context"));
  $("#open-settings").addEventListener("click", () => navigation.openSettings("general"));
  $("#close-settings").addEventListener("click", () => {
    navigation.backToWorkspace();
    if (!navigation.get().sessionId) {
      const first = sessionsStore.get().data?.items?.[0];
      if (first) navigation.select(first.project_id, first.id, { replace: true });
    }
  });
  $("#workspace-chip").addEventListener("click", () => { selectInspectorTab("context"); setDrawer("inspector", true); });
  $("#reasoning-chip").addEventListener("click", () => { selectInspectorTab("context"); setDrawer("inspector", true); });

  document.addEventListener("keydown", (event) => {
    if (event.key === "Escape") {
      if (dialog.open) dialog.close();
      else closeDrawers();
    }
    if ((event.ctrlKey || event.metaKey) && event.key.toLowerCase() === "n") {
      event.preventDefault();
      openNewSession();
    }
    if (event.key === "/" && document.activeElement?.tagName !== "INPUT" && document.activeElement?.tagName !== "TEXTAREA") {
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
      scheduleTaskRefresh();
    }, active ? 1400 : 5000);
  }
  tasksStore.subscribe(scheduleTaskRefresh);
  document.addEventListener("visibilitychange", () => {
    if (document.hidden) window.clearTimeout(tasksTimer);
    else {
      scheduleTaskRefresh();
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
    loadRuns(),
  ]);
  if (initial.some((result) => result.status === "rejected")) {
    toast("部分资源暂时无法载入，可继续重试。", "error");
  }

  const selected = navigation.get();
  if (selected.view === "workspace" && !selected.sessionId) {
    const first = sessionsStore.get().data?.items?.[0];
    if (first) navigation.select(first.project_id, first.id, { replace: true });
  }
  scheduleTaskRefresh();
}
