import { api, resourceId } from "../api/client.js";
import { createResourceStore } from "./store.js";
import { t } from "../i18n.js";
import { isTransientReadError } from "../api/read-recovery.js";

const EMPTY = Object.freeze({
  projectId: "", sessionId: "", total: 0, items: [], loaded: false,
});
let selection = EMPTY;
export const asksStore = createResourceStore(EMPTY, {
  recoverRead: isTransientReadError, retainDataOnError: isTransientReadError,
});
export const selectedAsks = () => selection;

export function clearAsks() {
  selection = EMPTY;
  asksStore.reset();
}

export async function selectAsks(projectId, sessionId) {
  const project = resourceId(projectId, "project");
  const session = resourceId(sessionId, "session");
  selection = Object.freeze({ projectId: project, sessionId: session });
  asksStore.reset({ ...EMPTY, ...selection });
  await refreshSelectedAsks({ retry: true });
}

export function refreshSelectedAsks({ retry = false } = {}) {
  const selected = selection;
  if (!selected.sessionId || (!retry && asksStore.isPending()))
    return Promise.resolve(asksStore.get());
  return asksStore.load(async signal => {
    const response = await api.get(
      `/projects/${selected.projectId}/sessions/${selected.sessionId}/asks`, { signal });
    const data = response.data;
    if (!Array.isArray(data?.items)) throw new Error(t("ask.invalidResponse", {}, "询问响应无效"));
    const previous = asksStore.get().data;
    // Preserve healthy identical snapshots and their mounted editors. The
    // store owns cancellation/generations, so obsolete reads cannot publish.
    return previous?.loaded && previous.projectId === selected.projectId &&
      previous.sessionId === selected.sessionId && previous.total === data.total &&
      JSON.stringify(previous.items) === JSON.stringify(data.items) ? previous :
      { ...selected, total: data.total, items: data.items, loaded: true };
  }, { background: !retry });
}

export function validateAskAnswer(id, answer) {
  const number = String(id);
  if (!/^[1-9][0-9]*$/.test(number)) throw new TypeError(t("ask.invalidId", {}, "询问 ID 无效"));
  const text = String(answer).trim();
  if (!text) throw new TypeError(t("ask.answerRequired", {}, "请填写回答"));
  if (new TextEncoder().encode(text).length > 1024)
    throw new TypeError(t("ask.answerTooLong", {}, "回答不能超过 1024 字节"));
  return { id: number, answer: text };
}

export async function answerAsk(projectId, sessionId, id, answer) {
  const project = resourceId(projectId, "project");
  const session = resourceId(sessionId, "session");
  const validated = validateAskAnswer(id, answer);
  const reply = await api.put(`/projects/${project}/sessions/${session}/asks/${validated.id}`,
    { answer: validated.answer });
  // Lock the accepted answer before any secondary read can stall or fail.
  // A fresh read supersedes pre-answer snapshots without repeating the PUT.
  if (selection.projectId === project && selection.sessionId === session)
    void refreshSelectedAsks({ retry: true });
  return reply.data;
}
