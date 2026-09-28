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

export async function startWorkspaceNavigation({ navigation, settingsStore,
  sessionsStore, sessionDetailStore, dialog, title, continueButton, newButton,
  prompt, entryHash }) {
  let saved = null;
  try { saved = (await api.get(endpoint)).data; }
  catch { toast(t("startup.readFailed", {},
    "无法读取上次会话，将使用当前会话列表。"), "error"); }

  let lastSavedKey = saved?.project_id && saved?.session_id
    ? `${saved.project_id}/${saved.session_id}` : "";
  let target = null;
  let saving = false;
  async function flushSelection() {
    if (saving) return;
    saving = true;
    while (target && target.key !== lastSavedKey) {
      const next = target;
      try {
        await api.put(endpoint, {
          project_id: next.projectId, session_id: next.sessionId,
        });
        lastSavedKey = next.key;
      } catch {
        toast(t("startup.saveFailed", {},
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

  function openLastSession(session) {
    const projectId = session.project_id;
    const sessionId = session.id;
    navigation.select(projectId, sessionId, { replace: true });
    let done = false;
    let unsubscribeDetail = () => {};
    let unsubscribeRoute = () => {};
    function finish(focus) {
      if (done) return;
      done = true;
      unsubscribeDetail();
      unsubscribeRoute();
      if (focus && !prompt.disabled) prompt.focus();
    }
    unsubscribeDetail = sessionDetailStore.subscribe((state) => {
      if (state.status === "error") { finish(false); return; }
      if (state.status === "ready" && state.data?.project_id === projectId &&
          state.data?.id === sessionId) finish(true);
    });
    if (done) { unsubscribeDetail(); return; }
    unsubscribeRoute = navigation.subscribe((route) => {
      if (route.view !== "workspace" || route.projectId !== projectId ||
          route.sessionId !== sessionId) finish(false);
    });
    if (done) unsubscribeRoute();
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
  const mode = settingsStore.get().data?.workspace?.open_mode ?? "last";
  if (mode === "new") {
    navigation.newTask(saved?.project_id || "default", { replace: true });
    prompt.focus();
    return;
  }
  const candidate = await lastSessionCandidate(saved,
    sessionsStore.get().data?.items);
  if (location.hash) { focusExplicitWorkspace(); return; }
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
