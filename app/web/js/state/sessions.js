import { api, resourceId } from "../api/client.js";
import { createResourceStore } from "./store.js";
import { withSessionRuntime } from "./session-runtime.js";
import { t } from "../i18n.js";
import { downloadSessionBackup } from "../api/backup-download.js";

export const sessionsStore = createResourceStore({ generation: 0, items: [] });
export const sessionDetailStore = createResourceStore();

export function loadSessions() {
  return sessionsStore.load(async () => (await api.get("/sessions")).data);
}

export function loadSession(projectId, sessionId) {
  return sessionDetailStore.load(() => readSession(projectId, sessionId));
}

export async function readSession(projectId, sessionId) {
  const project = resourceId(projectId, "project");
  const session = resourceId(sessionId, "session");
  const response = await api.get(`/projects/${project}/sessions/${session}`);
  return { ...response.data, etag: response.etag };
}

export async function createSession(input) {
  const body = { project_id: resourceId(input.project_id, "project") };
  for (const key of ["title", "agent_id", "model_id", "reasoning_effort",
    "permission_profile", "client_session_id"]) {
    if (input[key]) body[key] = input[key];
  }
  const response = await api.post("/sessions", body);
  await loadSessions();
  return response.data;
}

function endpoint(session) {
  const project = resourceId(session.project_id, "project");
  const id = resourceId(session.id, "session");
  return `/projects/${project}/sessions/${id}`;
}

function etag(session) {
  if (session.etag) return session.etag;
  const id = resourceId(session.id, "session");
  const revision = Number(session.revision);
  if (!Number.isSafeInteger(revision) || revision < 1) throw new TypeError("session revision is invalid");
  return `"mdo-session-${id}-${revision}"`;
}

async function refreshAfter(response) {
  await loadSessions();
  return { ...response.data, etag: response.etag };
}

export async function patchSession(session, patch) {
  return refreshAfter(await api.patch(endpoint(session), patch, { ifMatch: etag(session) }));
}

export async function updateSessionProfile(session, profile) {
  const body = {
    model_id: profile.model_id,
    reasoning_effort: profile.reasoning_effort,
    permission_profile: profile.permission_profile,
  };
  const response = await withSessionRuntime(session.project_id, session.id,
    () => api.put(`${endpoint(session)}/profile`, body,
      { ifMatch: etag(session) }));
  return refreshAfter(response);
}

export async function trashSession(session) {
  return refreshAfter(await api.delete(endpoint(session), { ifMatch: etag(session) }));
}

export async function restoreSession(session) {
  return refreshAfter(await api.post(`${endpoint(session)}/restore`, undefined, { ifMatch: etag(session) }));
}

export async function loadSessionHistory(session, options = {}) {
  const response = await api.get(`${endpoint(session)}/history`, options);
  return { ...response.data, etag: response.etag };
}

function sequence(value) {
  const number = Number(value);
  if (!Number.isSafeInteger(number) || number < 0) throw new TypeError("session sequence is invalid");
  return number;
}

export async function forkSession(session, input) {
  const body = { through_sequence: sequence(input.through_sequence) };
  if (input.title) body.title = input.title;
  const response = await api.post(`${endpoint(session)}/fork`, body, { ifMatch: etag(session) });
  await loadSessions();
  return { ...response.data, etag: response.etag };
}

export async function truncateSession(session, throughSequence, sourceEventId, options = {}) {
  const body = { through_sequence: sequence(throughSequence) };
  if (sourceEventId !== undefined) {
    body.source_event_id = sequence(sourceEventId);
    if (!body.source_event_id) throw new TypeError("source event ID must be positive");
  }
  if (options.editId) {
    if (!/^[0-9a-f]{32}$/.test(options.editId) || !body.source_event_id)
      throw new TypeError("Guarded edit identity is invalid");
    body.client_edit_id = options.editId;
  }
  const response = await api.post(`${endpoint(session)}/truncate`, body,
    { ifMatch: etag(session), signal: options.signal });
  // The acknowledged cut is enough to continue. A sidebar refresh must not
  // delay the next run or convert a successful history edit into a timeout.
  void loadSessions();
  return { ...response.data, etag: response.etag };
}

export async function clearSession(session) {
  return refreshAfter(await api.post(`${endpoint(session)}/clear`, undefined, { ifMatch: etag(session) }));
}

export function exportSession(session, options) {
  return downloadSessionBackup(session.project_id, session.id, options);
}

// The event list stays small. A copy action may fetch one original event on
// demand, but must never accept a neighbouring event after journal compaction.
export async function readCompleteSessionEventText(projectId, sessionId,
  eventId, kind, { endEventId = eventId, epoch = "" } = {}) {
  if (!Number.isSafeInteger(eventId) || eventId < 1 || typeof kind !== "string")
    throw new TypeError("session event identity is invalid");
  const path = `${endpoint({ project_id: projectId, id: sessionId })}/events`;
  const fence = epoch ? `&epoch=${resourceId(epoch, "history epoch")}` : "";
  if (endEventId !== undefined && endEventId > eventId) {
    if (!Number.isSafeInteger(endEventId) || endEventId - eventId > 4096 || !/^[0-9a-f]{64}$/.test(epoch)) return null;
    let cursor = eventId - 1, first = null, last = null, text = "";
    while (cursor < endEventId) {
      const page = (await api.get(`${path}?after=${cursor}&limit=32${fence}`)).data;
      if (page.epoch !== epoch || !page.items?.length || page.next_cursor <= cursor) return null;
      for (const event of page.items) {
        if (event.event_id > endEventId) break;
        first ??= event;
        if (first.event_id !== eventId || first.kind !== kind) return null;
        if (event.kind !== kind || event.run_id !== first.run_id || event.agent_id !== first.agent_id ||
            event.agent_turn !== first.agent_turn || event.agent_depth !== first.agent_depth) continue;
        const part = event.text_truncated
          ? await readCompleteSessionEventText(projectId, sessionId, event.event_id, kind, { epoch }) : event.text;
        if (typeof part !== "string" || text.length + part.length > 1048576) return null;
        text += part; last = event;
      }
      cursor = page.next_cursor;
    }
    return last?.event_id === endEventId ? text : null;
  }
  const data = (await api.get(`${path}?after=${eventId - 1}&limit=1&full_text=1${fence}`)).data;
  const [event] = data?.items ?? [];
  if (data?.items?.length !== 1 || event?.event_id !== eventId ||
      event?.kind !== kind || event?.text_truncated !== false ||
      typeof event?.text !== "string") return null;
  return event.text;
}

// Session events are durable but served in small pages. Keep a one-click
// transcript export bounded, and report when its source cannot be complete.
const TRANSCRIPT_PAGE_SIZE = 32;
const TRANSCRIPT_MAX_EVENTS = 4096;
const TRANSCRIPT_MAX_FULL_TEXT_EVENTS = 64;
const TRANSCRIPT_MAX_FULL_TEXT_BYTES = 2 * 1024 * 1024;

export async function loadSessionTranscript(session) {
  let cursor = 0;
  let latestEventId = null;
  let historyLost = false;
  const events = [];
  const path = `${endpoint(session)}/events`;
  while (events.length < TRANSCRIPT_MAX_EVENTS) {
    const replay = (await api.get(`${path}?after=${cursor}&limit=${TRANSCRIPT_PAGE_SIZE}`)).data;
    const latest = Number(replay.latest_event_id);
    const next = Number(replay.next_cursor);
    if (!Number.isSafeInteger(latest) || !Number.isSafeInteger(next) ||
        latest < 0 || next < cursor) throw new Error(t("session.exportInvalidCursor", {},
          "会话事件游标无效，无法导出 Markdown"));
    if (latestEventId === null) latestEventId = latest;
    historyLost ||= Boolean(replay.history_lost);
    for (const event of replay.items ?? []) {
      const id = Number(event.event_id);
      if (!Number.isSafeInteger(id) || id <= cursor || id > latestEventId) continue;
      events.push(event);
    }
    const previous = cursor;
    cursor = next;
    if (cursor >= latestEventId) break;
    if (next === previous || !(replay.items?.length)) {
      historyLost = true;
      break;
    }
  }
  let fullTextReads = 0;
  let fullTextBytes = 0;
  for (let index = 0; index < events.length; index += 1) {
    const event = events[index];
    // Reasoning is omitted by the Markdown formatter. Leave its bounded
    // preview alone and reserve the read budget for exported content.
    if (!event.text_truncated || event.kind === "model_reasoning_delta" ||
        fullTextReads >= TRANSCRIPT_MAX_FULL_TEXT_EVENTS ||
        fullTextBytes >= TRANSCRIPT_MAX_FULL_TEXT_BYTES) continue;
    fullTextReads += 1;
    try {
      const full = await readCompleteSessionEventText(session.project_id,
        session.id, Number(event.event_id), event.kind);
      if (full === null) continue;
      const bytes = new TextEncoder().encode(full).length;
      if (bytes > TRANSCRIPT_MAX_FULL_TEXT_BYTES - fullTextBytes) continue;
      fullTextBytes += bytes;
      events[index] = { ...event, text: full, text_truncated: false };
    } catch { /* Preserve the visible preview and report an incomplete export. */ }
  }
  const textTruncated = events.some((event) => event.text_truncated &&
    event.kind !== "model_reasoning_delta");
  return { events, historyLost, textTruncated,
    limitReached: events.length >= TRANSCRIPT_MAX_EVENTS && cursor < latestEventId };
}
