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
  for (const key of ["title", "agent_id", "model_id", "reasoning_effort"]) {
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

export async function trashSession(session) {
  return refreshAfter(await api.delete(endpoint(session), { ifMatch: etag(session) }));
}

export async function restoreSession(session) {
  return refreshAfter(await api.post(`${endpoint(session)}/restore`, undefined, { ifMatch: etag(session) }));
}
