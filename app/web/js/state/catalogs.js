import { api, resourceId } from "../api/client.js";
import { createResourceStore } from "./store.js";
import { isTransientReadError } from "../api/read-recovery.js";

const readOptions = { recoverRead: isTransientReadError };
export const modelsStore = createResourceStore({ providers: [], models: [] }, readOptions);
export const agentsStore = createResourceStore({ items: [] }, readOptions);
export const projectsStore = createResourceStore({ items: [] }, readOptions);

export function loadModels() {
  return modelsStore.load(async signal => (await api.get("/models", { signal })).data);
}

export function loadAgents() {
  return agentsStore.load(async signal => (await api.get("/agents", { signal })).data);
}

export function loadProjects() {
  return projectsStore.load(async signal => (await api.get("/projects", { signal })).data);
}

// A fresh foreground/reconnect may resume an exhausted transient read. Do not
// disturb successful catalogs, active recovery or permanent access failures.
export function recoverCatalogs() {
  return Promise.all([[modelsStore, loadModels], [agentsStore, loadAgents],
    [projectsStore, loadProjects]].filter(([store]) => {
      const state = store.get();
      return state.status === "error" && isTransientReadError(state.error);
    }).map(([, load]) => load()));
}

export async function createProject(input) {
  const response = await api.post("/projects", input);
  await loadProjects();
  return response.data;
}

export async function readProject(id) {
  const response = await api.get(`/projects/${resourceId(id, "project")}`);
  return { ...response.data, etag: response.etag };
}

export async function updateProject(id, input, etag) {
  const response = await api.put(`/projects/${resourceId(id, "project")}`,
    input, { ifMatch: etag });
  await loadProjects();
  return { ...response.data, etag: response.etag };
}

export async function unregisterProject(id, etag) {
  const response = await api.delete(`/projects/${resourceId(id, "project")}`,
    { ifMatch: etag });
  await loadProjects();
  return response.data;
}

export async function readProjectPurgePreview(id, options = {}) {
  const response = await api.get(
    `/projects/${resourceId(id, "project")}/purge-preview`, options);
  return { ...response.data, etag: response.etag };
}

export function loadCatalogs() {
  return Promise.all([loadModels(), loadAgents(), loadProjects()]);
}
