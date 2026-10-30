import { api, resourceId } from "../api/client.js";
import { startResourceOperation } from "./resource-operation-start.js";

export async function reloadCatalog(name, options = {}) {
  if (!["models", "skills", "modules", "mcp"].includes(name)) throw new TypeError("resource cannot be reloaded");
  if (name === "modules") return startResourceOperation("/modules/reload", { kind: "module_reload", target: "" }, options);
  return (await api.post(`/${name}/reload`)).data;
}

export async function setMcpEnabled(serverId, enabled) {
  const server = resourceId(serverId, "MCP server");
  return (await api.put(`/mcp/${server}/enabled`, { enabled })).data;
}

export async function disconnectMcp(serverId) {
  const server = resourceId(serverId, "MCP server");
  return (await api.post(`/mcp/${server}/disconnect`)).data;
}

export async function refreshMcp(serverId, options = {}) {
  const server = resourceId(serverId, "MCP server");
  return startResourceOperation(`/mcp/${server}/refresh`, { kind: "mcp_refresh", target: server }, options);
}
