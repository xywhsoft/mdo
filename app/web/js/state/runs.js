import { api, resourceId } from "../api/client.js";
import { createResourceStore } from "./store.js";

export const runsStore = createResourceStore({ active_runs: 0, items: [] });

export function loadRuns() {
  return runsStore.load(async () => (await api.get("/runs")).data);
}

export async function startRun(projectId, sessionId, prompt, attachments = []) {
  const project = resourceId(projectId, "project");
  const session = resourceId(sessionId, "session");
  return (await api.post(`/projects/${project}/sessions/${session}/runs`,
    { prompt, attachments })).data;
}

export async function readRun(runId) {
  const run = resourceId(runId, "run");
  return (await api.get(`/runs/${run}`)).data;
}

export async function cancelRun(runId) {
  const run = resourceId(runId, "run");
  return (await api.delete(`/runs/${run}`)).data;
}
