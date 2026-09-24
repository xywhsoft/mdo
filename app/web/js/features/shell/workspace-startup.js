import { api, resourceId } from "../../api/client.js";
import { toast } from "../../utils/dom.js";

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
  sessionsStore, dialog, title, continueButton, newButton, prompt, entryHash }) {
  let saved = null;
  try { saved = (await api.get(endpoint)).data; }
  catch { toast("无法读取上次会话，将使用当前会话列表。", "error"); }

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
        toast("无法保存上次会话；下次启动可能打开其他任务。", "error");
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

  // Hash routes are explicit user choices, including #/ for a blank task.
  if (entryHash || location.hash) return;
  const mode = settingsStore.get().data?.workspace?.open_mode ?? "last";
  if (mode === "new") {
    navigation.newTask(saved?.project_id || "default", { replace: true });
    return;
  }
  const candidate = await lastSessionCandidate(saved,
    sessionsStore.get().data?.items);
  if (location.hash) return;
  if (!candidate) { navigation.clear(); return; }
  if (mode !== "ask") {
    navigation.select(candidate.project_id, candidate.id, { replace: true });
    return;
  }

  title.textContent = candidate.title || "未命名任务";
  function openNew() {
    dialog.close();
    navigation.newTask(candidate.project_id, { replace: true });
    prompt.focus();
  }
  continueButton.addEventListener("click", () => {
    dialog.close();
    navigation.select(candidate.project_id, candidate.id, { replace: true });
  }, { once: true });
  newButton.addEventListener("click", openNew, { once: true });
  dialog.addEventListener("cancel", (event) => {
    event.preventDefault();
    openNew();
  }, { once: true });
  dialog.showModal();
  continueButton.focus();
}
