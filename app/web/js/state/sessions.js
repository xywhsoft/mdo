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
