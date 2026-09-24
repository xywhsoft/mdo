import { api } from "../api/client.js";
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

export function loadCatalogs() {
  return Promise.all([loadModels(), loadAgents(), loadProjects()]);
}
