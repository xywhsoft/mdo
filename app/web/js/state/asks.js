import { api, resourceId } from "../api/client.js";
import { createResourceStore } from "./store.js";
import { t } from "../i18n.js";

export const asksStore = createResourceStore({
  projectId: "", sessionId: "", total: 0, items: [], loaded: false,
});

let generation = 0;
let signature = "";

export function clearAsks() {
  generation += 1;
  signature = "";
  asksStore.setData({ projectId: "", sessionId: "", total: 0, items: [],
    loaded: false });
}

export async function selectAsks(projectId, sessionId) {
  const project = resourceId(projectId, "project");
  const session = resourceId(sessionId, "session");
  generation += 1;
  signature = "";
  asksStore.setData({ projectId: project, sessionId: session,
    total: 0, items: [], loaded: false });
  await refreshSelectedAsks();
}

export async function refreshSelectedAsks() {
  const token = generation;
  const selected = asksStore.get().data;
  if (!selected?.projectId || !selected?.sessionId) return;
  try {
    const response = await api.get(
      `/projects/${selected.projectId}/sessions/${selected.sessionId}/asks`);
    if (token !== generation) return;
    const data = response.data;
    if (!Array.isArray(data.items)) throw new Error(t("ask.invalidResponse", {}, "询问响应无效"));
    const next = JSON.stringify(data.items);
    if (next !== signature) {
      signature = next;
      asksStore.setData({ projectId: selected.projectId,
        sessionId: selected.sessionId, total: data.total,
        items: data.items, loaded: true });
    }
  } catch (error) {
    if (token === generation) asksStore.setError(error);
  }
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
  await api.put(`/projects/${project}/sessions/${session}/asks/${validated.id}`,
    { answer: validated.answer });
  await refreshSelectedAsks();
}
