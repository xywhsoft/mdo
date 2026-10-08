import { api, resourceId } from "../api/client.js";
import { createResourceStore } from "./store.js";
import { timelineStore, historyRemovalRange } from "../features/chat/timeline-store.js";
import { isTransientReadError } from "../api/read-recovery.js";
import { t } from "../i18n.js";

const EMPTY = Object.freeze({ projectId: "", sessionId: "", eventId: 0, items: [] });
const recover = error => isTransientReadError(error) || error?.code === "todo_not_synced";
export const todoStore = createResourceStore(EMPTY, {
  recoverRead: recover, retainDataOnError: recover,
});
let selection = EMPTY;
let observedEventId = 0;
let observedBoundaryId = 0;
let observedBoundaryRange = null;
export const selectedTodo = () => selection;

export function clearTodo() {
  selection = EMPTY;
  observedEventId = observedBoundaryId = 0;
  observedBoundaryRange = null;
  todoStore.reset();
}

export async function selectTodo(projectId, sessionId) {
  selection = Object.freeze({ projectId: resourceId(projectId, "project"),
    sessionId: resourceId(sessionId, "session") });
  observedEventId = observedBoundaryId = 0;
  observedBoundaryRange = null;
  todoStore.reset({ ...EMPTY, ...selection });
  await refreshSelectedTodo({ retry: true });
  observeTimeline(timelineStore.get());
}

export function refreshSelectedTodo({ retry = false } = {}) {
  const selected = selection;
  if (!selected.sessionId || (!retry && todoStore.isPending()))
    return Promise.resolve(todoStore.get());
  return todoStore.load(async signal => {
    const data = (await api.get(
      `/projects/${selected.projectId}/sessions/${selected.sessionId}/todo`, { signal })).data;
    // The store cancels obsolete reads before another selection/tool event.
    // Never let their results mutate the projection floor either.
    if (signal.aborted) throw new DOMException("Todo read cancelled", "AbortError");
    const eventId = Number(data?.event_id);
    if (!Array.isArray(data?.items) || data.event_id === null || !Number.isSafeInteger(eventId) || eventId < 0)
      throw new Error(t("todo.invalidResponse", {}, "计划响应无效"));
    if (eventId < observedEventId || (observedBoundaryRange &&
        eventId >= observedBoundaryRange.first && eventId < observedBoundaryRange.end))
      // Projection lag and transport errors share the same finite read budget.
      throw Object.assign(new Error(t("todo.notSynced", {}, "计划状态尚未同步，请检查工具结果")),
        { code: "todo_not_synced" });
    observedEventId = Math.max(observedEventId, eventId);
    const previous = todoStore.get().data;
    return previous?.projectId === selected.projectId && previous.sessionId === selected.sessionId &&
      previous.eventId === eventId && JSON.stringify(previous.items) === JSON.stringify(data.items)
      ? previous : { ...selected, eventId, items: data.items };
  }, { background: !retry });
}

function observeTimeline(state) {
  const data = state.data, selected = selection;
  if (!selected.sessionId || selected.projectId !== data?.projectId ||
      selected.sessionId !== data?.sessionId) return;
  const boundary = [...(data.events ?? [])].reverse().find(event =>
    historyRemovalRange(event) && Number(event.event_id) > observedBoundaryId);
  if (boundary) {
    // Restoring history may lower the plan's event ID, including zero after
    // clear. Cancel old requests and remove their monotonic floor first.
    observedBoundaryId = Number(boundary.event_id);
    observedBoundaryRange = historyRemovalRange(boundary);
    observedEventId = 0;
    todoStore.reset({ ...EMPTY, ...selected });
    void refreshSelectedTodo({ retry: true });
    return;
  }
  const latest = [...(data.events ?? [])].reverse().find(event =>
    event.kind === "tool_done" && event.tool_name === "mdo.todo" && event.success &&
    event.agent_depth === 0 && Number(event.event_id) > observedEventId);
  if (latest) {
    observedEventId = Number(latest.event_id);
    void refreshSelectedTodo({ retry: true });
  }
}

timelineStore.subscribe(observeTimeline);
