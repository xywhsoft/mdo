import { liveConnection } from "../../api/live.js";
import { api, resourceId } from "../../api/client.js";
import { createResourceStore } from "../../state/store.js";

const RETAINED_EVENTS = 640;
const MAX_PAGES_PER_REFRESH = 4;
const FALLBACK_POLL_MS = 2500;

export const timelineStore = createResourceStore({
  projectId: "",
  sessionId: "",
  cursor: 0,
  latestEventId: 0,
  historyLost: false,
  events: [],
});

let generation = 0;
let refreshVersion = 0;
let pollTimer = 0;
let pollDelay = FALLBACK_POLL_MS;

export function historyRemovalRange(event) {
  if (event.kind !== "history_truncated") return null;
  const first = Number(event.source_event_id);
  const end = Number(event.event_id);
  return Number.isSafeInteger(first) && first > 0 &&
    Number.isSafeInteger(end) && first <= end ? { first, end } : null;
}

export function mergeTimelineEvents(retained, additions) {
  const events = retained.concat(additions);
  const ranges = events.map(historyRemovalRange).filter(Boolean);
  // Durable markers identify discarded UI IDs, not reusable model sequences.
  // Apply them to cached events as well as records in the current replay page.
  return { events: ranges.length ? events.filter((event) => ranges.every(({ first, end }) =>
    Number(event.event_id) < first || Number(event.event_id) >= end)) : events,
  cleared: additions.some((event) => historyRemovalRange(event)?.first === 1) };
}

function stopTimer() {
  window.clearTimeout(pollTimer);
  pollTimer = 0;
}

function schedulePoll(token) {
  stopTimer();
  if (token !== generation || document.hidden || liveConnection.isConnected()) return;
  pollTimer = window.setTimeout(() => refreshTimeline(token), pollDelay);
}

async function refreshTimeline(token = generation) {
  const current = timelineStore.get().data;
  if (!current?.sessionId || token !== generation) return;
  const request = ++refreshVersion;
  const isCurrent = () => token === generation && request === refreshVersion;
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
      if (!isCurrent()) return;
      const replay = response.data;
      const additions = (replay.items ?? []).filter((item) => Number(item.event_id) > cursor);
      if (replay.history_lost) {
        historyLost = true;
      }
      const merged = mergeTimelineEvents(replay.history_lost ? [] : events, additions);
      events = merged.events;
      if (merged.cleared) historyLost = false;
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
      pollDelay = FALLBACK_POLL_MS;
    } else {
      pollDelay = Math.min(Math.round(pollDelay * 1.45), 8000);
    }
  } catch (error) {
    if (!isCurrent()) return;
    if (error?.code !== "session_events_unavailable" && error?.code !== "session_not_found") {
      timelineStore.setError(error);
      pollDelay = FALLBACK_POLL_MS;
    }
  } finally {
    if (isCurrent()) schedulePoll(token);
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
  pollDelay = FALLBACK_POLL_MS;
  timelineStore.setData({
    projectId,
    sessionId,
    cursor: 0,
    latestEventId: 0,
    historyLost: false,
    events: [],
  });
  subscribeLiveTimeline();
  return liveConnection.isConnected() ? Promise.resolve() : refreshTimeline(generation);
}

export function reloadSelectedTimeline() {
  const current = timelineStore.get().data;
  if (!current?.sessionId) return Promise.resolve();
  return reloadTimeline(current.projectId, current.sessionId);
}

export function refreshSelectedTimeline() {
  if (liveConnection.isConnected()) { subscribeLiveTimeline(); return Promise.resolve(); }
  pollDelay = FALLBACK_POLL_MS;
  return refreshTimeline(generation);
}

export function clearTimeline() {
  generation += 1;
  refreshVersion += 1;
  stopTimer();
  liveConnection.select("", "");
  timelineStore.setData({ projectId: "", sessionId: "", cursor: 0, latestEventId: 0, historyLost: false, events: [] });
}

function subscribeLiveTimeline() {
  const current = timelineStore.get().data;
  if (!current?.sessionId) return;
  refreshVersion += 1;
  stopTimer();
  liveConnection.select(current.projectId, current.sessionId, () => timelineStore.get().data?.cursor ?? 0);
}

// WebSocket and HTTP replay share the same merge rules, retention and renderer.
// A newer push invalidates in-flight fallback reads, preventing stale overwrites.
export function applyLiveTimeline(replay) {
  const current = timelineStore.get().data;
  if (!current?.sessionId || current.projectId !== replay.project_id || current.sessionId !== replay.session_id ||
      !Array.isArray(replay.items)) return;
  const next = Number(replay.next_cursor);
  const latest = Number(replay.latest_event_id);
  if (!Number.isSafeInteger(next) || next < current.cursor ||
      !Number.isSafeInteger(latest) || latest < next) return;
  const additions = replay.items.filter((item) => Number.isSafeInteger(Number(item.event_id)) &&
    Number(item.event_id) > current.cursor && Number(item.event_id) <= next);
  const merged = mergeTimelineEvents(replay.history_lost ? [] : current.events, additions);
  let historyLost = merged.cleared ? false : current.historyLost || Boolean(replay.history_lost);
  let events = merged.events;
  if (events.length > RETAINED_EVENTS) { events = events.slice(-RETAINED_EVENTS); historyLost = true; }
  refreshVersion += 1;
  stopTimer();
  timelineStore.setData({ ...current, cursor: next, latestEventId: latest, historyLost, events });
}

liveConnection.subscribe((event) => {
  if (event.type === "events") applyLiveTimeline(event);
  else if (event.type === "status") {
    if (event.connected) stopTimer();
    else { pollDelay = FALLBACK_POLL_MS; schedulePoll(generation); }
  }
});

document.addEventListener("visibilitychange", () => {
  if (!document.hidden && !liveConnection.isConnected()) void refreshTimeline(generation);
  else stopTimer();
});
