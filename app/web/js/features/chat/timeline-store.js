import { liveConnection } from "../../api/live.js";
import { api, resourceId } from "../../api/client.js";
import { isTransientReadError } from "../../api/read-recovery.js";
import { createResourceStore } from "../../state/store.js";
import { mergeConversationTurns, summarizeConversationEvents } from "./conversation-history.js";
import { t } from "../../i18n.js";
import { targetState, subscribeTarget } from "../../api/target.js";
import { createSessionCache } from "./session-cache.js";

const MAX_PAGES_PER_REFRESH = 4;
const FALLBACK_POLL_MS = 2500;

export const timelineStore = createResourceStore({
  projectId: "",
  sessionId: "",
  cursor: 0,
  latestEventId: 0,
  historyLost: false,
  events: [],
  turns: [],
});

let generation = 0;
let refreshVersion = 0;
let pollTimer = 0;
let pollDelay = FALLBACK_POLL_MS;
let historyRequest = null;
let indexRequest = null;
let selectionAbort = null;
let initialRequest = null;
const sessionCache = createSessionCache({ maxBytes:
  globalThis.navigator?.userAgent?.includes("Android") ? 12 * 1024 * 1024 : 24 * 1024 * 1024 });
function cacheKey(data) {
  const target = targetState().selected;
  return `${target ? `${target.owner}/${target.id}` : "local"}/${data.projectId}/${data.sessionId}`;
}
function rememberTimeline() {
  const data = timelineStore.get().data;
  if (data?.sessionId) sessionCache.put(cacheKey(data), data);
}
export function clearTimelineCache() { sessionCache.clear(); }
subscribeTarget(state => {
  if (state.runtimeChanged || ["remote_revoked", "connector_login_required", "connector_account_changed"].includes(state.error?.code))
    clearTimelineCache();
});

// These are background reads, never mutation retries. Temporary transport or
// service failures keep the last conversation visible and retry with a cap.
export const isTransientTimelineError = isTransientReadError;

function endpoint(data) {
  return `/projects/${data.projectId}/sessions/${data.sessionId}`;
}

function updateTurns(data, events, additions = []) {
  const ranges = events.map(historyRemovalRange).filter(Boolean);
  const retained = (data.turns ?? []).filter(turn => ranges.every(({ first, end }) =>
    turn.first_event_id < first || turn.first_event_id >= end));
  return mergeConversationTurns(mergeConversationTurns(retained, additions),
    summarizeConversationEvents(events)).filter(turn => ranges.every(({ first, end }) =>
    turn.first_event_id < first || turn.first_event_id >= end));
}

export function historyRemovalRange(event) {
  if (event.kind !== "history_truncated") return null;
  const first = Number(event.source_event_id);
  const end = Number(event.event_id);
  return Number.isSafeInteger(first) && first > 0 &&
    Number.isSafeInteger(end) && first <= end ? { first, end } : null;
}

export function mergeTimelineEvents(retained, additions) {
  const unique = new Map(retained.map(event => [event.event_id, event]));
  for (const event of additions) unique.set(event.event_id, event);
  const events = [...unique.values()].sort((a, b) => a.event_id - b.event_id);
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
  const current = timelineStore.get().data;
  if (token !== generation || document.hidden ||
      (liveConnection.isConnected() && !current?.initializing)) return;
  pollTimer = window.setTimeout(() => current?.initializing
    ? loadInitialTimeline(token) : refreshTimeline(token), pollDelay);
}

async function refreshTimeline(token = generation) {
  const current = timelineStore.get().data;
  if (!current?.sessionId || current.initializing || token !== generation) return;
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
        { signal: selectionAbort?.signal },
      );
      if (!isCurrent()) return;
      const replay = response.data;
      const additions = (replay.items ?? []).filter((item) => Number(item.event_id) > cursor);
      if (replay.history_lost) {
        historyLost = true;
      }
      const merged = mergeTimelineEvents(events, additions);
      events = merged.events;
      if (merged.cleared) historyLost = false;
      latestEventId = Number(replay.latest_event_id ?? latestEventId);
      const next = Number(replay.next_cursor ?? cursor);
      changed ||= additions.length > 0 || next !== cursor || replay.history_lost;
      cursor = next;
      if (additions.length < 32 || cursor >= latestEventId) break;
    }
    if (changed || timelineStore.get().status === "error") {
      const latest = timelineStore.get().data;
      events = mergeTimelineEvents(latest.events, events).events;
      timelineStore.setData({ ...latest, cursor, latestEventId, historyLost, events, syncing: false, syncError: null,
        turns: updateTurns(latest, events) });
      pollDelay = FALLBACK_POLL_MS;
    } else {
      if (current.syncing) timelineStore.setData({ ...timelineStore.get().data, syncing: false, syncError: null });
      pollDelay = Math.min(Math.round(pollDelay * 1.45), 8000);
    }
  } catch (error) {
    if (!isCurrent()) return;
    if (error.name === "AbortError") return;
    if (current.syncing) timelineStore.setData({ ...timelineStore.get().data, syncing: false, syncError: error });
    if (error?.code !== "session_events_unavailable" && error?.code !== "session_not_found") {
      if (!isTransientTimelineError(error)) timelineStore.setError(error);
      pollDelay = Math.min(pollDelay * 2, 15_000);
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
  return reloadTimeline(project, session);
}

async function reloadTimeline(projectId, sessionId, force = false) {
  rememberTimeline();
  generation += 1;
  const token = generation;
  selectionAbort?.abort();
  selectionAbort = new AbortController();
  historyRequest = indexRequest = null;
  initialRequest = null;
  liveConnection.select("", "");
  stopTimer();
  pollDelay = FALLBACK_POLL_MS;
  const cached = force ? null : sessionCache.get(cacheKey({ projectId, sessionId }));
  if (cached) {
    timelineStore.setData({ ...cached, cached: true, syncing: true, syncError: null, loadingHistory: false, historyError: null });
    await refreshTimeline(token);
    if (token === generation) subscribeLiveTimeline();
    return;
  }
  timelineStore.setData({
    projectId,
    sessionId,
    cursor: 0,
    latestEventId: 0,
    historyLost: false,
    events: [],
    turns: [],
    initializing: true,
    hasOlder: false,
  });
  return loadInitialTimeline(token);
}

function loadInitialTimeline(token) {
  if (token !== generation) return Promise.resolve();
  if (initialRequest) return initialRequest;
  const request = Promise.resolve().then(async () => {
    if (token !== generation) return;
    try {
      const current = timelineStore.get().data;
      const page = (await api.get(`${endpoint(current)}/turns?limit=64`,
        { signal: selectionAbort.signal })).data;
      if (token !== generation) return;
      const selected = page.items.slice(-4);
      const start = selected[0]?.first_event_id ?? Math.max(1, page.latest_event_id);
      const events = await readHistoryRange(current, start, page.latest_event_id, token);
      if (token !== generation) return;
      timelineStore.setData({ ...current, initializing: false, cursor: page.latest_event_id,
        latestEventId: page.latest_event_id, events, turns: page.items,
        firstLoadedTurn: selected[0]?.first_event_id ?? 0,
        hasOlder: Boolean(page.has_more || page.items.length > selected.length),
        indexHasMore: page.has_more, indexBefore: page.next_before,
        historyLost: Boolean(page.history_lost) });
      subscribeLiveTimeline();
      if (!liveConnection.isConnected()) await refreshTimeline(token);
    } catch (error) {
      if (token !== generation || error.name === "AbortError") return;
      if (isTransientTimelineError(error)) {
        timelineStore.setData({ ...timelineStore.get().data, syncError: error });
        pollDelay = Math.min(pollDelay * 2, 15_000);
        schedulePoll(token);
      } else timelineStore.setError(error);
    } finally {
      if (initialRequest === request) initialRequest = null;
    }
  });
  initialRequest = request;
  return request;
}

// Historical pages never advance the live cursor. New pushes may arrive while
// older pages load; merge into the current state at commit, not its old copy.
async function readHistoryRange(data, first, end, token) {
  let cursor = first - 1;
  const events = [];
  while (cursor < end && token === generation) {
    const page = (await api.get(`${endpoint(data)}/events?after=${cursor}&limit=32`,
      { signal: selectionAbort?.signal })).data;
    if (token !== generation) return [];
      const next = Number(page.next_cursor);
    if (!Number.isSafeInteger(next) || next <= cursor)
      throw new Error(t("messageAction.historyChanged", {}, "消息已不在当前会话历史中，请刷新会话"));
    events.push(...(page.items ?? []).filter(event => event.event_id >= first && event.event_id <= end));
    cursor = next;
  }
  return events;
}

export function loadOlderTimeline() {
  const data = timelineStore.get().data;
  if (!data?.sessionId || !data.hasOlder || data.initializing) return Promise.resolve();
  if (historyRequest) return historyRequest;
  const token = generation;
  timelineStore.setData({ ...data, loadingHistory: true, historyError: null });
  const request = (async () => {
    try {
      const page = (await api.get(`${endpoint(data)}/turns?before=${data.firstLoadedTurn}&limit=4`,
        { signal: selectionAbort?.signal })).data;
      if (token !== generation) return;
      const events = page.items.length ? await readHistoryRange(data,
        page.items[0].first_event_id, data.firstLoadedTurn - 1, token) : [];
      if (token !== generation) return;
      const current = timelineStore.get().data;
      const merged = mergeTimelineEvents(current.events, events).events;
      const turns = updateTurns(current, merged, page.items);
      const valid = page.items.filter(item => turns.some(turn => turn.first_event_id === item.first_event_id));
      timelineStore.setData({ ...current, events: merged, turns,
        firstLoadedTurn: valid[0]?.first_event_id ?? current.firstLoadedTurn,
        hasOlder: page.items.length && !valid.length ? current.hasOlder : Boolean(page.has_more),
        loadingHistory: false });
    } catch (error) {
      if (token === generation && error.name !== "AbortError")
        timelineStore.setData({ ...timelineStore.get().data, loadingHistory: false, historyError: error });
    } finally { if (historyRequest === request) historyRequest = null; }
  })();
  historyRequest = request;
  return request;
}

export function loadOlderConversationIndex() {
  const data = timelineStore.get().data;
  if (!data?.indexHasMore || indexRequest) return indexRequest ?? Promise.resolve();
  const token = generation;
  const request = (async () => {
    try {
      const page = (await api.get(`${endpoint(data)}/turns?before=${data.indexBefore}&limit=64`,
        { signal: selectionAbort?.signal })).data;
      if (token !== generation) return;
      const current = timelineStore.get().data;
      const turns = updateTurns(current, current.events, page.items);
      const stale = page.items.length && !page.items.some(item => turns.some(turn =>
        turn.first_event_id === item.first_event_id));
      timelineStore.setData({ ...current, turns,
        indexHasMore: stale ? current.indexHasMore : page.has_more,
        indexBefore: stale ? current.indexBefore : page.next_before });
    } finally { if (indexRequest === request) indexRequest = null; }
  })();
  indexRequest = request;
  return request;
}

export async function revealConversationTurn(id) {
  const data = timelineStore.get().data;
  const turn = data?.turns?.find(item => item.first_event_id === id);
  if (!turn) return;
  if (data.events.some(event => event.event_id === id)) return;
  const token = generation;
  const events = await readHistoryRange(data, id, turn.end_event_id, token);
  if (token !== generation) return;
  const current = timelineStore.get().data;
  const merged = mergeTimelineEvents(current.events, events).events;
  const first = Math.min(current.firstLoadedTurn || id, id);
  timelineStore.setData({ ...current, events: merged, turns: updateTurns(current, merged),
    firstLoadedTurn: first, hasOlder: Boolean(current.indexHasMore ||
      current.turns.some(item => item.first_event_id < first)) });
}

export function reloadSelectedTimeline() {
  const current = timelineStore.get().data;
  if (!current?.sessionId) return Promise.resolve();
  return reloadTimeline(current.projectId, current.sessionId, true);
}

export function refreshSelectedTimeline() {
  if (timelineStore.get().data?.initializing) return Promise.resolve();
  if (liveConnection.isConnected()) { subscribeLiveTimeline(); return Promise.resolve(); }
  pollDelay = FALLBACK_POLL_MS;
  return refreshTimeline(generation);
}

export function clearTimeline() {
  rememberTimeline();
  generation += 1;
  refreshVersion += 1;
  stopTimer();
  selectionAbort?.abort();
  historyRequest = indexRequest = null;
  initialRequest = null;
  liveConnection.select("", "");
  timelineStore.setData({ projectId: "", sessionId: "", cursor: 0, latestEventId: 0, historyLost: false, events: [], turns: [] });
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
  if (!current?.sessionId || current.initializing || current.projectId !== replay.project_id || current.sessionId !== replay.session_id ||
      !Array.isArray(replay.items)) return;
  const next = Number(replay.next_cursor);
  const latest = Number(replay.latest_event_id);
  if (!Number.isSafeInteger(next) || next < current.cursor ||
      !Number.isSafeInteger(latest) || latest < next) return;
  const additions = replay.items.filter((item) => Number.isSafeInteger(Number(item.event_id)) &&
    Number(item.event_id) > current.cursor && Number(item.event_id) <= next);
  const merged = mergeTimelineEvents(current.events, additions);
  let historyLost = merged.cleared ? false : current.historyLost || Boolean(replay.history_lost);
  const events = merged.events;
  refreshVersion += 1;
  stopTimer();
  const turns = updateTurns(current, events);
  timelineStore.setData({ ...current, cursor: next, latestEventId: latest, historyLost, events, turns,
    ...(merged.cleared ? { hasOlder: false, indexHasMore: false, indexBefore: 0,
      firstLoadedTurn: turns[0]?.first_event_id ?? 0 } : {}) });
}

liveConnection.subscribe((event) => {
  if (event.type === "events") applyLiveTimeline(event);
  else if (event.type === "status") {
    if (event.connected) {
      stopTimer();
      if (timelineStore.get().data?.initializing) void loadInitialTimeline(generation);
    }
    else { pollDelay = FALLBACK_POLL_MS; schedulePoll(generation); }
  }
});

document.addEventListener("visibilitychange", () => {
  if (!document.hidden && timelineStore.get().data?.initializing) void loadInitialTimeline(generation);
  else if (!document.hidden && !liveConnection.isConnected()) void refreshTimeline(generation);
  else stopTimer();
});
