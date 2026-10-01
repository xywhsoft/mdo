import { api, resourceId } from "../api/client.js";
import { createResourceStore } from "./store.js";
import { timelineStore, historyRemovalRange } from "../features/chat/timeline-store.js";
import { t } from "../i18n.js";

export const todoStore = createResourceStore({
  projectId: "", sessionId: "", eventId: 0, items: [],
});

let generation = 0;
let observedEventId = 0;
let observedBoundaryId = 0;
let observedBoundaryRange = null;
let retryTimer = 0;
let retryCount = 0;

function stopRetry() {
  window.clearTimeout(retryTimer);
  retryTimer = 0;
  retryCount = 0;
}

export function clearTodo() {
  generation += 1;
  observedEventId = 0;
  observedBoundaryId = 0;
  observedBoundaryRange = null;
  stopRetry();
  todoStore.setData({ projectId: "", sessionId: "", eventId: 0, items: [] });
}

export async function selectTodo(projectId, sessionId) {
  const project = resourceId(projectId, "project");
  const session = resourceId(sessionId, "session");
  const token = ++generation;
  observedEventId = 0;
  observedBoundaryId = 0;
  observedBoundaryRange = null;
  stopRetry();
  todoStore.setData({ projectId: project, sessionId: session,
    eventId: 0, items: [] });
  await refreshTodo(token);
  observeTimeline(timelineStore.get());
}

async function refreshTodo(token = generation) {
  const selected = todoStore.get().data;
  if (!selected?.sessionId || token !== generation) return;
  try {
    const response = await api.get(
      `/projects/${selected.projectId}/sessions/${selected.sessionId}/todo`);
    if (token !== generation) return;
    const data = response.data;
    if (!Array.isArray(data.items) || !Number.isSafeInteger(Number(data.event_id)))
      throw new Error(t("todo.invalidResponse", {}, "计划响应无效"));
    const eventId = Number(data.event_id);
    if (eventId < observedEventId || (observedBoundaryRange &&
        eventId >= observedBoundaryRange.first && eventId < observedBoundaryRange.end)) {
      if (retryCount >= 4) throw new Error(t("todo.notSynced", {},
        "计划状态尚未同步，请检查工具结果"));
      if (!retryTimer) {
        retryTimer = window.setTimeout(() => {
          retryTimer = 0;
          retryCount += 1;
          void refreshTodo(token);
        }, 120 * (2 ** retryCount));
      }
      return;
    }
    stopRetry();
    const previous = todoStore.get().data;
    if (Number(data.event_id) >= Number(previous.eventId)) {
      observedEventId = Math.max(observedEventId, Number(data.event_id));
      todoStore.setData({ projectId: selected.projectId,
        sessionId: selected.sessionId, eventId: Number(data.event_id),
        items: data.items });
    }
  } catch (error) {
    if (token === generation) todoStore.setError(error);
  }
}

function observeTimeline(state) {
  const data = state.data;
  const selected = todoStore.get().data;
  if (!selected?.sessionId || selected.projectId !== data?.projectId ||
      selected.sessionId !== data?.sessionId) return;
  const boundary = [...(data.events ?? [])].reverse().find((event) =>
    historyRemovalRange(event) && Number(event.event_id) > observedBoundaryId);
  if (boundary) {
    // A restored plan can have a lower event ID, including zero after clear.
    // Invalidate older reads and their monotonic-plan floor before reloading.
    observedBoundaryId = Number(boundary.event_id);
    observedBoundaryRange = historyRemovalRange(boundary);
    generation += 1;
    observedEventId = 0;
    stopRetry();
    todoStore.setData({ projectId: selected.projectId, sessionId: selected.sessionId,
      eventId: 0, items: [] });
    void refreshTodo();
    return;
  }
  const latest = [...(data.events ?? [])].reverse().find((event) =>
    event.kind === "tool_done" && event.tool_name === "mdo.todo" &&
    event.success && event.agent_depth === 0 &&
    Number(event.event_id) > observedEventId);
  if (latest) {
    observedEventId = Number(latest.event_id);
    stopRetry();
    void refreshTodo();
  }
}

timelineStore.subscribe(observeTimeline);
