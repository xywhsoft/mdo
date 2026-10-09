import { api, resourceId } from "../../api/client.js";
import { toast } from "../../utils/dom.js";
import { t } from "../../i18n.js";
import { focusSessionComposerAfterNavigation } from "./session-composer-focus.js";

const endpoint = "/workspace-state";

async function lastSessionCandidate(saved, sessions) {
  if (saved?.project_id && saved?.session_id) {
    try {
      const project = resourceId(saved.project_id, "project");
      const session = resourceId(saved.session_id, "session");
      const result = (await api.get(`/projects/${project}/sessions/${session}`)).data;
      if (result) return result;
    } catch { /* A deleted or unreadable session falls back to the catalog. */ }
  }
  return (sessions ?? []).find((item) => item.status === "active") ?? null;
}

function runningSessionCandidate(runs, sessions) {
  const active = new Map((sessions ?? []).filter((session) =>
    session.status === "active").map((session) =>
      [`${session.project_id}/${session.id}`, session]));
  for (const run of runs ?? []) {
    if (run.terminal !== false) continue;
    const session = active.get(`${run.project_id}/${run.session_id}`);
    if (session) return session;
  }
  return null;
}

export async function startWorkspaceNavigation({ navigation, settingsStore,
  sessionsStore, runsStore, sessionDetailStore, dialog, title, continueButton, newButton,
  prompt, entryHash, shouldRestore = () => true, canPersistSelection = () => true,
  subscribeWritable = null }) {
  let lastSavedKey = "";
  let savedReady = false;
  let target = null;
  let saving = false;
  async function flushSelection() {
    if (!savedReady || saving || !canPersistSelection()) return;
    saving = true;
    while (target && target.key !== lastSavedKey && canPersistSelection()) {
      const next = target;
      try {
        await api.put(endpoint, {
          project_id: next.projectId, session_id: next.sessionId,
        });
        lastSavedKey = next.key;
      } catch {
        if (canPersistSelection()) toast(t("startup.saveFailed", {},
          "无法保存上次会话；下次启动可能打开其他任务。"), "error");
        break;
      }
    }
    saving = false;
  }
  navigation.subscribe(({ view, projectId, sessionId }) => {
    if (view !== "workspace" || !projectId || !sessionId) return;
    const key = `${projectId}/${sessionId}`;
    target = { key, projectId, sessionId };
    void flushSelection();
  });
  // Remember the latest local navigation while continuity is being verified.
  // Resume its normal save after the gate opens, never write through the gate.
  subscribeWritable?.(() => { void flushSelection(); });

  const savedPromise = (async () => {
    let saved = null;
    try { saved = (await api.get(endpoint)).data; }
    catch { toast(t("startup.readFailed", {},
      "无法读取上次会话，将使用当前会话列表。"), "error"); }
    lastSavedKey = saved?.project_id && saved?.session_id
      ? `${saved.project_id}/${saved.session_id}` : "";
    savedReady = true;
    void flushSelection();
    return saved;
  })();

  function openLastSession(session) {
    const projectId = session.project_id;
    const sessionId = session.id;
    navigation.select(projectId, sessionId, { replace: true });
    focusSessionComposerAfterNavigation({ navigation, sessionDetailStore,
      prompt, projectId, sessionId, origin: document.body });
  }

  function focusExplicitWorkspace() {
    const route = navigation.get();
    if (route.view !== "workspace") return;
    if (route.projectId && route.sessionId) {
      focusSessionComposerAfterNavigation({ navigation, sessionDetailStore,
        prompt, projectId: route.projectId, sessionId: route.sessionId,
        origin: document.body });
      return;
    }
    const active = document.activeElement;
    if (!prompt.disabled && (active === document.body || !active?.isConnected))
      prompt.focus();
  }

  // Hash routes are explicit user choices, including #/ for a blank task.
  if (entryHash || location.hash) { focusExplicitWorkspace(); return; }
  if (!shouldRestore()) return;
  const saved = await savedPromise;
  // Every await can outlive a user choice. Check before all branches, including
  // "new" and live-session restoration, not only after the candidate lookup.
  if (location.hash || !shouldRestore()) return;
  const mode = settingsStore.get().data?.workspace?.open_mode ?? "last";
  if (mode === "new") {
    navigation.newTask(saved?.project_id || "default", { replace: true });
    prompt.focus();
    return;
  }
  // The user's explicit startup mode and route win. In "last" mode, resume
  // a live conversation before falling back to the last selected session.
  if (mode === "last") {
    const running = runningSessionCandidate(runsStore?.get().data?.items,
      sessionsStore.get().data?.items);
    if (running) { openLastSession(running); return; }
  }
  const candidate = await lastSessionCandidate(saved,
    sessionsStore.get().data?.items);
  if (location.hash || !shouldRestore()) return;
  if (!candidate) { navigation.clear(); prompt.focus(); return; }
  if (mode !== "ask") {
    openLastSession(candidate);
    return;
  }

  title.textContent = candidate.title || t("startup.untitled", {}, "未命名任务");
  function openNew() {
    dialog.close();
    navigation.newTask(candidate.project_id, { replace: true });
    prompt.focus();
  }
  continueButton.addEventListener("click", () => {
    dialog.close();
    openLastSession(candidate);
  }, { once: true });
  newButton.addEventListener("click", openNew, { once: true });
  dialog.addEventListener("cancel", (event) => {
    event.preventDefault();
    openNew();
  }, { once: true });
  dialog.showModal();
  continueButton.focus();
}
