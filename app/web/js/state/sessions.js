import { api, resourceId } from "../api/client.js";
import { createResourceStore } from "./store.js";
import { withSessionRuntime } from "./session-runtime.js";
import { t } from "../i18n.js";

export const sessionsStore = createResourceStore({ generation: 0, items: [] });
export const sessionDetailStore = createResourceStore();

export function loadSessions() {
  return sessionsStore.load(async () => (await api.get("/sessions")).data);
}

export function loadSession(projectId, sessionId) {
  const project = resourceId(projectId, "project");
  const session = resourceId(sessionId, "session");
  return sessionDetailStore.load(async () => {
    const response = await api.get(`/projects/${project}/sessions/${session}`);
    return { ...response.data, etag: response.etag };
  });
}

export async function createSession(input) {
  const body = { project_id: resourceId(input.project_id, "project") };
  for (const key of ["title", "agent_id", "model_id", "reasoning_effort", "permission_profile"]) {
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

export async function loadSessionHistory(session) {
  const response = await api.get(`${endpoint(session)}/history`);
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

export async function truncateSession(session, throughSequence) {
  return refreshAfter(await api.post(`${endpoint(session)}/truncate`, {
    through_sequence: sequence(throughSequence),
  }, { ifMatch: etag(session) }));
}

export async function clearSession(session) {
  return refreshAfter(await api.post(`${endpoint(session)}/clear`, undefined, { ifMatch: etag(session) }));
}

export function exportSession(session) {
  return api.download(`${endpoint(session)}/export`);
}

// Session events are durable but served in small pages. Keep a one-click
// transcript export bounded, and report when its source cannot be complete.
const TRANSCRIPT_PAGE_SIZE = 32;
const TRANSCRIPT_MAX_EVENTS = 4096;

export async function loadSessionTranscript(session) {
  let cursor = 0;
  let latestEventId = null;
  let historyLost = false;
  let textTruncated = false;
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
      textTruncated ||= Boolean(event.text_truncated);
    }
    const previous = cursor;
    cursor = next;
    if (cursor >= latestEventId) break;
    if (next === previous || !(replay.items?.length)) {
      historyLost = true;
      break;
    }
  }
  return { events, historyLost, textTruncated,
    limitReached: events.length >= TRANSCRIPT_MAX_EVENTS && cursor < latestEventId };
}
