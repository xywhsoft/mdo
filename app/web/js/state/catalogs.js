import { api } from "../api/client.js";
import { createResourceStore } from "./store.js";

export const modelsStore = createResourceStore({ providers: [], models: [] });
export const agentsStore = createResourceStore({ items: [] });

export function loadModels() {
  return modelsStore.load(async () => (await api.get("/models")).data);
}

export function loadAgents() {
  return agentsStore.load(async () => (await api.get("/agents")).data);
}

export function loadCatalogs() {
  return Promise.all([loadModels(), loadAgents()]);
}
