import { api, resourceId } from "../api/client.js";
import { createResourceStore } from "./store.js";

export const modelsStore = createResourceStore({ providers: [], models: [] });
export const agentsStore = createResourceStore({ items: [] });
export const projectsStore = createResourceStore({ items: [] });

export function loadModels() {
  return modelsStore.load(async () => (await api.get("/models")).data);
}

export function loadAgents() {
  return agentsStore.load(async () => (await api.get("/agents")).data);
}

export function loadProjects() {
  return projectsStore.load(async () => (await api.get("/projects")).data);
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
  return response.data;
}

export function loadCatalogs() {
  return Promise.all([loadModels(), loadAgents(), loadProjects()]);
}
