import { api, resourceId } from "../api/client.js";
import { createResourceStore } from "./store.js";

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
  return refreshAfter(await api.put(`${endpoint(session)}/profile`, body,
    { ifMatch: etag(session) }));
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
