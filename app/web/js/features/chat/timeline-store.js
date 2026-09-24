import { api, resourceId } from "../../api/client.js";
import { createResourceStore } from "../../state/store.js";

const RETAINED_EVENTS = 640;
const MAX_PAGES_PER_REFRESH = 4;

export const timelineStore = createResourceStore({
  projectId: "",
  sessionId: "",
  cursor: 0,
  latestEventId: 0,
  historyLost: false,
  events: [],
});

let generation = 0;
let pollTimer = 0;
let pollDelay = 800;

function stopTimer() {
  window.clearTimeout(pollTimer);
  pollTimer = 0;
}

function schedulePoll(token) {
  stopTimer();
  if (token !== generation || document.hidden) return;
  pollTimer = window.setTimeout(() => refreshTimeline(token), pollDelay);
}

async function refreshTimeline(token = generation) {
  const current = timelineStore.get().data;
  if (!current?.sessionId || token !== generation) return;
  let cursor = current.cursor;
  let events = current.events;
  let latestEventId = current.latestEventId;
  let historyLost = current.historyLost;
  let changed = false;

  try {
    for (let page = 0; page < MAX_PAGES_PER_REFRESH; page += 1) {
      const response = await api.get(
        `/projects/${current.projectId}/sessions/${current.sessionId}/events?after=${cursor}&limit=32`,
      );
      if (token !== generation) return;
      const replay = response.data;
      const additions = (replay.items ?? []).filter((item) => Number(item.event_id) > cursor);
      if (replay.history_lost) {
        events = additions;
        historyLost = true;
      } else if (additions.length) {
        events = events.concat(additions);
      }
      if (events.length > RETAINED_EVENTS) {
        events = events.slice(-RETAINED_EVENTS);
        historyLost = true;
      }
      latestEventId = Number(replay.latest_event_id ?? latestEventId);
      const next = Number(replay.next_cursor ?? cursor);
      changed ||= additions.length > 0 || next !== cursor || replay.history_lost;
      cursor = next;
      if (additions.length < 32 || cursor >= latestEventId) break;
    }
    if (changed) {
      timelineStore.setData({ ...current, cursor, latestEventId, historyLost, events });
      pollDelay = 700;
    } else {
      pollDelay = Math.min(Math.round(pollDelay * 1.45), 4000);
    }
  } catch (error) {
    if (token !== generation) return;
    if (error?.code !== "session_events_unavailable" && error?.code !== "session_not_found") {
      timelineStore.setError(error);
      pollDelay = 2500;
    }
  } finally {
    schedulePoll(token);
  }
}

export function selectTimeline(projectId, sessionId) {
  const project = resourceId(projectId, "project");
  const session = resourceId(sessionId, "session");
  const current = timelineStore.get().data;
  if (current?.projectId === project && current?.sessionId === session) return;
  void reloadTimeline(project, session);
}

function reloadTimeline(projectId, sessionId) {
  generation += 1;
  pollDelay = 700;
  timelineStore.setData({
    projectId,
    sessionId,
    cursor: 0,
    latestEventId: 0,
    historyLost: false,
    events: [],
  });
  return refreshTimeline(generation);
}

export function reloadSelectedTimeline() {
  const current = timelineStore.get().data;
  if (!current?.sessionId) return Promise.resolve();
  return reloadTimeline(current.projectId, current.sessionId);
}

export function refreshSelectedTimeline() {
  pollDelay = 500;
  return refreshTimeline(generation);
}

export function clearTimeline() {
  generation += 1;
  stopTimer();
  timelineStore.setData({ projectId: "", sessionId: "", cursor: 0, latestEventId: 0, historyLost: false, events: [] });
}

document.addEventListener("visibilitychange", () => {
  if (!document.hidden) void refreshTimeline(generation);
  else stopTimer();
});
