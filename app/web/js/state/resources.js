import { api, resourceId } from "../api/client.js";
import { createResourceStore } from "./store.js";
import { t } from "../i18n.js";

export const modulesStore = createResourceStore({ modules: [], tools: [] });
export const skillsStore = createResourceStore({ items: [] });
export const mcpStore = createResourceStore({ items: [] });
export const permissionsStore = createResourceStore();
export const storageStore = createResourceStore();
export const diagnosticsStore = createResourceStore({ total: 0, items: [] });
export const migrationsStore = createResourceStore({ count: 0, items: [] });

const STORES = Object.freeze({
  modules: modulesStore,
  skills: skillsStore,
  mcp: mcpStore,
  permissions: permissionsStore,
  storage: storageStore,
  diagnostics: diagnosticsStore,
  migrations: migrationsStore,
});
const pendingReads = new Map();
const SECTION_RESOURCES = Object.freeze({
  extensions: ["modules", "skills", "mcp"],
  permissions: ["permissions"],
  diagnostics: ["storage", "diagnostics", "migrations"],
});

export function loadResource(name) {
  const store = STORES[name];
  if (!store) throw new TypeError("unknown resource store");
  const path = name === "migrations" ? "/migrations/legacy" : `/${name}`;
  const read = store.load(async () => (await api.get(path)).data);
  pendingReads.set(name, read);
  const settled = () => {
    // An explicit refresh may supersede this read. The store already ignores
    // its old reply; keep tracking the newer read until it also settles.
    if (pendingReads.get(name) === read) pendingReads.delete(name);
  };
  void read.then(settled, settled);
  return read;
}

export function ensureSettingsResources(section) {
  // These catalogs belong to their Settings page, not to ordinary chat boot.
  // Re-entering a page shares in-flight reads and retains successful results;
  // a failed read is retried only on explicit refresh or the next page visit.
  const names = Object.hasOwn(SECTION_RESOURCES, section) ? SECTION_RESOURCES[section] : [];
  return Promise.all(names.map((name) => {
    if (pendingReads.has(name)) return pendingReads.get(name);
    const store = STORES[name];
    return store.get().status === "ready" ? store.get() : loadResource(name);
  }));
}

async function awaitOperation(operation) {
  let current = operation;
  for (let attempt = 0; !current.terminal && attempt < 100; attempt += 1) {
    await new Promise((resolve) => window.setTimeout(resolve, 250));
    current = (await api.get(`/operations/${resourceId(current.id, "operation")}`)).data;
  }
  if (!current.terminal) throw new Error(t("resource.operationRunning", {},
    "操作仍在后台运行，请稍后刷新。"));
  if (current.state !== "succeeded") throw new Error(current.message ||
    t("resource.reloadFailed", {}, "目录刷新失败"));
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

export async function applyLegacyMigration(sourceId, previewToken) {
  const source = resourceId(sourceId, "migration source");
  if (!/^[0-9a-f]{64}$/.test(String(previewToken ?? ""))) {
    throw new TypeError("migration preview token is invalid");
  }
  return (await api.post("/migrations/legacy", {
    source_id: source,
    preview_token: previewToken,
  })).data;
}
