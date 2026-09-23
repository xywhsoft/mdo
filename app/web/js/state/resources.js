import { api, resourceId } from "../api/client.js";
import { createResourceStore } from "./store.js";

export const modulesStore = createResourceStore({ modules: [], tools: [] });
export const skillsStore = createResourceStore({ items: [] });
export const mcpStore = createResourceStore({ items: [] });
export const permissionsStore = createResourceStore();
export const storageStore = createResourceStore();
export const diagnosticsStore = createResourceStore({ total: 0, items: [] });

const STORES = Object.freeze({
  modules: modulesStore,
  skills: skillsStore,
  mcp: mcpStore,
  permissions: permissionsStore,
  storage: storageStore,
  diagnostics: diagnosticsStore,
});

export function loadResource(name) {
  const store = STORES[name];
  if (!store) throw new TypeError("unknown resource store");
  return store.load(async () => (await api.get(`/${name}`)).data);
}

export function loadManagementResources() {
  return Promise.all(Object.keys(STORES).map(loadResource));
}

async function awaitOperation(operation) {
  let current = operation;
  for (let attempt = 0; !current.terminal && attempt < 100; attempt += 1) {
    await new Promise((resolve) => window.setTimeout(resolve, 250));
    current = (await api.get(`/operations/${resourceId(current.id, "operation")}`)).data;
  }
  if (!current.terminal) throw new Error("操作仍在后台运行，请稍后刷新。");
  if (current.state !== "succeeded") throw new Error(current.message || "目录刷新失败");
  return current;
}

export async function reloadCatalog(name) {
  if (!["models", "skills", "modules", "mcp"].includes(name)) throw new TypeError("resource cannot be reloaded");
  const result = (await api.post(`/${name}/reload`)).data;
  if (result?.id && !result.terminal) await awaitOperation(result);
  return result;
}

export async function setMcpEnabled(serverId, enabled) {
  const server = resourceId(serverId, "MCP server");
  return (await api.put(`/mcp/${server}/enabled`, { enabled })).data;
}

export async function disconnectMcp(serverId) {
  const server = resourceId(serverId, "MCP server");
  return (await api.post(`/mcp/${server}/disconnect`)).data;
}

export async function refreshMcp(serverId) {
  const server = resourceId(serverId, "MCP server");
  const operation = (await api.post(`/mcp/${server}/refresh`)).data;
  return operation?.id && !operation.terminal ? awaitOperation(operation) : operation;
}
