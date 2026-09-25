import { api, resourceId } from "../api/client.js";
import { createResourceStore } from "./store.js";
import { withSessionRuntime } from "./session-runtime.js";

export const runsStore = createResourceStore({ active_runs: 0, items: [] });

export function loadRuns() {
  return runsStore.load(async () => (await api.get("/runs")).data);
}

export async function startRun(projectId, sessionId, prompt, attachments = []) {
  const project = resourceId(projectId, "project");
  const session = resourceId(sessionId, "session");
  const body = attachments.length ? { prompt, attachments } : { prompt };
  return withSessionRuntime(project, session, async () =>
    (await api.post(`/projects/${project}/sessions/${session}/runs`,
      body)).data);
}

export async function readRun(runId) {
  const run = resourceId(runId, "run");
  return (await api.get(`/runs/${run}`)).data;
}

export async function cancelRun(runId) {
  const run = resourceId(runId, "run");
  return (await api.delete(`/runs/${run}`)).data;
}
