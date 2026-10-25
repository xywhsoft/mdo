// Portable declarations only. This module never resolves or renders private
// instructions, memory, model credentials or MCP authorization state.
export const ROLE_MIN_BUILD = 70000009;
export const isRoleKind = kind => kind === "agents" || kind === "subagents";
export function roleDescriptor(resource) {
  if (!isRoleKind(resource?.kind)) throw Error("请选择 Agent 或 SubAgent 作品");
  const value = JSON.parse(resource.content);
  if (!value || Array.isArray(value) || value.schema !== "mdo.agent.v1") throw Error("角色作品需要 mdo.agent.v1 配置");
  const mode = value.mode || (resource.kind === "agents" ? "primary" : "subagent");
  if (!["both", resource.kind === "agents" ? "primary" : "subagent"].includes(mode)) throw Error("角色运行模式与分类不符");
  const code = value.code === true || value.code === "true";
  if (code !== Boolean(resource.files?.some(f => f.path === "program.c"))) throw Error("C 角色需要包含 program.c 源码");
  return { ...value, mode, code };
}
export function roleDependencies(resource) {
  const d = roleDescriptor(resource), result = [];
  const add = (kind, id, required) => {
    if (!id) return;
    const old = result.find(r => r.kind === kind && r.id === id);
    if (old) old.required ||= required; else result.push({kind, id, required});
  };
  const parent = d.extends;
  if (parent && !["mdo.general", "mdo.default"].includes(parent)) {
    const kind = parent.startsWith("agent.") ? "agents" : parent.startsWith("subagent.") ? "subagents" : resource.kind;
    add(kind, parent.replace(/^(?:agent|subagent)\./, ""), true);
  }
  if (d.model !== "inherit") add("models", d.model, true);
  for (const [key, kind] of [["skills", "skills"], ["mcp_servers", "mcp"]])
    for (const id of Array.isArray(d[key]) ? d[key] : []) add(kind, id, true);
  for (const [key, kind] of [["knowledge", "knowledge"], ["external_tools", "external-tools"]]) {
    const source = d.context?.[key];
    if (source === false || source?.enabled === false || source?.enabled === "false") continue;
    for (const id of source?.include || []) add(kind, id, false);
  }
  for (const id of d.delegation?.allow || []) add("roles", id, false);
  return result;
}
export function roleSummary(resource) {
  const d = roleDescriptor(resource);
  return [["开发方式", d.code ? "C 程序 + 配置" : "配置式"], ["运行模式", d.mode],
    ["继承角色", d.extends || "无"], ["模型", d.model && d.model !== "inherit" ? d.model : "跟随会话"],
    ["工具范围", Array.isArray(d.tools) ? d.tools.join("、") || "无工具" : d.tools || "继承授权工具"],
    ["权限配置", d.permission_profile || "继承用户授权"], ["自有文件", String(resource.files?.length || 0)]];
}
