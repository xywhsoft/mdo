import { api, ApiError, resourceId } from "../../api/client.js";
import { createRequestRecovery } from "../../api/request-recovery.js";
import { isTransientReadError } from "../../api/read-recovery.js";
import { toast } from "../../utils/dom.js";
import { t } from "../../i18n.js";
import { focusSessionComposerAfterNavigation } from "./session-composer-focus.js";

const endpoint = "/workspace-state";

async function readWorkspace(recovery) {
  const saved = (await recovery.request(signal => api.get(endpoint, { signal }))).data;
  if (typeof saved?.project_id !== "string" || typeof saved.session_id !== "string" ||
      Boolean(saved.project_id) !== Boolean(saved.session_id))
    throw new ApiError("Invalid saved workspace selection", { code: "invalid_response" });
  return saved;
}

async function lastSessionCandidate(saved, sessions, recovery, onFailure) {
  if (saved?.project_id && saved?.session_id) {
    try {
      const project = resourceId(saved.project_id, "project");
      const session = resourceId(saved.session_id, "session");
      const result = (await recovery.request(signal =>
        api.get(`/projects/${project}/sessions/${session}`, { signal }))).data;
      if (result?.project_id !== project || result.id !== session)
        throw new ApiError("Invalid saved session identity", { code: "invalid_response" });
      return result;
    } catch (error) {
      // Deletion is a normal fallback. A temporary read failure must exhaust
      // its quiet recovery before we choose another task.
      if (error?.name !== "AbortError" && error?.status !== 404) onFailure();
    }
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
  subscribeWritable = null, eventTarget = globalThis.window,
  createRecovery = createRequestRecovery,
  onNotice = message => toast(message, "error") }) {
  let lastSavedKey = "";
  let savedReady = false;
  let target = null;
  let saving = false;
  let stopped = false, restorationFinished = false, failedKey = "";
  let restorationReadFailed = false, automaticNavigation = false;
  const readRecovery = createRecovery({ eventTarget: null });
  let writeRecovery = null, unsubscribeRoute = () => {}, unsubscribeWritable = () => {};
  const canRestore = () => !stopped && !entryHash && !location.hash && shouldRestore();
  const readFailed = () => {
    restorationReadFailed = true;
    if (canRestore()) onNotice(t("startup.readFailed", {},
      "无法读取上次会话，将使用当前会话列表。"));
  };
  eventTarget?.addEventListener("pagehide", () => {
    stopped = true; readRecovery.dispose(); writeRecovery?.dispose();
    unsubscribeRoute(); unsubscribeWritable();
  }, { once: true });
  async function flushSelection() {
    if (stopped || !savedReady || saving || !canPersistSelection()) return;
    saving = true;
    while (!stopped && target && target.key !== lastSavedKey &&
        target.key !== failedKey && canPersistSelection()) {
      const next = target;
      writeRecovery = createRecovery({ eventTarget: null });
      try {
        try {
          await writeRecovery.request(signal => api.put(endpoint, {
            project_id: next.projectId, session_id: next.sessionId,
          }, { signal }), { retry: false });
        } catch (error) {
          if (error?.name === "AbortError" || (!isTransientReadError(error) &&
              !["invalid_response", "remote_result_unconfirmed"].includes(error?.code))) throw error;
          // Never replay an uncertain save. Check its actual result first;
          // the write and confirmation share one bounded observation period.
          try {
            const saved = await readWorkspace(writeRecovery);
            if (saved?.project_id !== next.projectId || saved.session_id !== next.sessionId)
              throw error;
          } catch (cause) {
            if (cause?.name === "AbortError") throw cause;
            throw new ApiError("The last session save could not be confirmed", {
              code: "workspace_save_unconfirmed" });
          }
        }
        lastSavedKey = next.key;
      } catch (error) {
        failedKey = next.key;
        if (!stopped && error?.name !== "AbortError" && target?.key === next.key && canPersistSelection())
          onNotice(error?.code === "workspace_save_unconfirmed"
            ? t("startup.saveUnconfirmed", {}, "尚无法确认上次会话已保存；当前对话不受影响，下次启动可能打开其他任务。")
            : t("startup.saveFailed", {}, "无法保存上次会话；下次启动可能打开其他任务。"));
      } finally {
        writeRecovery.dispose(); writeRecovery = null;
      }
    }
    saving = false;
  }
  unsubscribeRoute = navigation.subscribe(({ view, projectId, sessionId }) => {
    if (view !== "workspace" || !projectId || !sessionId) return;
    // A catalog fallback after an unreadable saved choice is only a usable
    // view, not a new user choice. Preserve the original startup record.
    if (automaticNavigation && restorationReadFailed) return;
    const key = `${projectId}/${sessionId}`;
    failedKey = "";
    target = { key, projectId, sessionId };
    void flushSelection();
  });
  // Remember the latest local navigation while continuity is being verified.
  // Resume its normal save after the gate opens, never write through the gate.
  unsubscribeWritable = subscribeWritable?.(() => { void flushSelection(); }) || (() => {});

  const savedPromise = (async () => {
    let saved = null;
    try { saved = await readWorkspace(readRecovery); }
    catch (error) { if (error?.name !== "AbortError") readFailed(); }
    lastSavedKey = saved?.project_id && saved?.session_id
      ? `${saved.project_id}/${saved.session_id}` : "";
    savedReady = !stopped;
    void flushSelection();
    if (restorationFinished || stopped) readRecovery.dispose();
    return saved;
  })();

  function openLastSession(session, automatic = true) {
    const projectId = session.project_id;
    const sessionId = session.id;
    automaticNavigation = automatic;
    try { navigation.select(projectId, sessionId, { replace: true }); }
    finally { automaticNavigation = false; }
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

  async function restoreWorkspace() {
    // Hash routes are explicit user choices, including #/ for a blank task.
    if (entryHash || location.hash) { focusExplicitWorkspace(); return; }
    if (!canRestore()) return;
    const saved = await savedPromise;
    // Every await can outlive a user choice. Check before all branches, including
    // "new" and live-session restoration, not only after the candidate lookup.
    if (!canRestore()) return;
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
      sessionsStore.get().data?.items, readRecovery, readFailed);
    if (!canRestore()) return;
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
      openLastSession(candidate, false);
    }, { once: true });
    newButton.addEventListener("click", openNew, { once: true });
    dialog.addEventListener("cancel", (event) => {
      event.preventDefault();
      openNew();
    }, { once: true });
    dialog.showModal();
    continueButton.focus();
  }
  try { await restoreWorkspace(); }
  finally {
    restorationFinished = true;
    // Explicit routes do not wait for this read. Dispose after it settles,
    // while a fallback owns the same budget through its candidate read.
    if (savedReady || stopped) readRecovery.dispose();
  }
}
