import { element, clear, errorMessage, toast } from "../../utils/dom.js";
import { reloadCatalog, setMcpEnabled, disconnectMcp, refreshMcp } from "../../state/resources.js";

function card(title, description, meta = [], actions = []) {
  const body = [element("h3", { text: title }), element("p", { text: description || "暂无说明" })];
  if (meta.length) body.push(element("div", { className: "resource-meta" }, meta.map((value) => element("span", { text: value }))));
  if (actions.length) body.push(element("div", { className: "resource-actions" }, actions));
  return element("article", { className: "resource-card" }, body);
}

function action(label, handler, tone = "neutral") {
  const button = element("button", { className: tone === "danger" ? "task-cancel" : "secondary-button", text: label, attrs: { type: "button" } });
  button.addEventListener("click", async () => {
    button.disabled = true;
    try { await handler(); }
    catch (error) { toast(errorMessage(error), "error"); }
    finally { button.disabled = false; }
  });
  return button;
}

function heading(title, actionButton = null) {
  return element("div", { className: "resource-group-heading" }, [
    element("h3", { text: title }),
    actionButton,
  ]);
}

function empty(text) {
  return element("div", { className: "empty-state", text });
}

export function createResourcePanels({ modelsStore, agentsStore, stores, reload }) {
  const modelsContainer = document.querySelector("#settings-models-list");
  const extensionsContainer = document.querySelector("#settings-extensions-list");
  const permissionsContainer = document.querySelector("#settings-permissions-list");
  const diagnosticsContainer = document.querySelector("#settings-diagnostics-list");
  const unsubscribers = [];

  async function refreshCatalog(name) {
    await reloadCatalog(name);
    await reload(name);
    toast(`${name} 目录已刷新`);
  }

  function renderModels() {
    const state = modelsStore.get();
    clear(modelsContainer);
    if (state.status === "error") { modelsContainer.append(empty(errorMessage(state.error))); return; }
    modelsContainer.append(heading("Provider", action("刷新", () => refreshCatalog("models"))));
    for (const provider of state.data?.providers ?? []) {
      modelsContainer.append(card(provider.name || provider.id,
        provider.builtin ? "内置 Provider" : "用户 Provider",
        [provider.id, provider.credential_configured ? "凭据已配置" : "缺少凭据", `${provider.timeout_ms} ms`]));
    }
    modelsContainer.append(heading("模型"));
    for (const model of state.data?.models ?? []) {
      modelsContainer.append(card(model.name || model.id,
        `${model.provider_id} · ${model.default_protocol}`,
        [model.free ? "免费" : "计费", `${model.context_window_tokens} context`, `${model.max_output_tokens} output`]));
    }
  }

  function renderExtensions() {
    clear(extensionsContainer);
    const modules = stores.modules.get();
    const skills = stores.skills.get();
    const mcp = stores.mcp.get();
    if ([modules, skills, mcp].some((state) => state.status === "error")) {
      const failed = [modules, skills, mcp].find((state) => state.status === "error");
      extensionsContainer.append(empty(errorMessage(failed.error)));
      return;
    }
    extensionsContainer.append(heading("Agent"));
    for (const agent of agentsStore.get().data?.items ?? []) {
      extensionsContainer.append(card(agent.name || agent.id, agent.description, [agent.id, agent.permission_profile, `${agent.tools?.length ?? 0} tools`, `${agent.skills?.length ?? 0} Skills`]));
    }
    extensionsContainer.append(heading("Skill", action("刷新", () => refreshCatalog("skills"))));
    for (const skill of skills.data?.items ?? []) {
      extensionsContainer.append(card(skill.name || skill.id, skill.description, [skill.trust, skill.external ? "外部" : "内置", `${skill.estimated_tokens} tokens`]));
    }
    extensionsContainer.append(heading("Module", action("重新编译", () => refreshCatalog("modules"))));
    for (const module of modules.data?.modules ?? []) {
      extensionsContainer.append(card(module.name || module.id, module.description, [module.version, module.external ? "外部 TCC" : "内置", `${module.tool_count} tools`, `${module.agent_count} agents`]));
    }
    extensionsContainer.append(heading("MCP", action("重载配置", () => refreshCatalog("mcp"))));
    if (!(mcp.data?.items ?? []).length) extensionsContainer.append(empty("尚未配置 MCP 服务器"));
    for (const server of mcp.data?.items ?? []) {
      const actions = [action(server.enabled ? "停用" : "启用", async () => {
        await setMcpEnabled(server.id, !server.enabled);
        await reload("mcp");
      })];
      if (server.enabled) actions.push(action("刷新工具", async () => { await refreshMcp(server.id); await reload("mcp"); }));
      if (server.connected) actions.push(action("断开", async () => { await disconnectMcp(server.id); await reload("mcp"); }, "danger"));
      extensionsContainer.append(card(server.name || server.id, server.description, [server.transport, server.state, `${server.discovered_tool_count ?? 0} tools`], actions));
    }
  }

  function renderPermissions(state) {
    clear(permissionsContainer);
    if (state.status === "error") { permissionsContainer.append(empty(errorMessage(state.error))); return; }
    const config = state.data?.configuration;
    if (!config) { permissionsContainer.append(empty("权限配置不可用")); return; }
    permissionsContainer.append(card(`默认 profile：${config.default_profile}`, "权限由服务端配置验证并在每个 Agent 会话创建时固定。", [`revision ${state.data.revision}`]));
    for (const [name, profile] of Object.entries(config.profiles ?? {})) {
      permissionsContainer.append(card(name, `工作区读取：${String(profile.workspace_read)} · 写入：${String(profile.workspace_write)}`, [`进程 ${profile.process}`, `网络 ${profile.network}`]));
    }
  }

  function renderDiagnostics() {
    clear(diagnosticsContainer);
    const storage = stores.storage.get();
    const diagnostics = stores.diagnostics.get();
    if (storage.status === "error" || diagnostics.status === "error") {
      diagnosticsContainer.append(empty(errorMessage(storage.error || diagnostics.error)));
      return;
    }
    if (storage.data) diagnosticsContainer.append(card("便携存储", storage.data.home_path || "内置只读资源", [storage.data.persistence, `${storage.data.session_count} sessions`, `${storage.data.artifact_count} artifacts`]));
    diagnosticsContainer.append(heading(`诊断 (${diagnostics.data?.total ?? 0})`));
    if (!(diagnostics.data?.items ?? []).length) diagnosticsContainer.append(empty("没有检测到诊断问题"));
    for (const item of diagnostics.data?.items ?? []) {
      diagnosticsContainer.append(card(`${item.domain} · ${item.stage}`, item.message, [item.subject_id || item.path || "runtime"]));
    }
  }

  unsubscribers.push(modelsStore.subscribe(renderModels));
  unsubscribers.push(agentsStore.subscribe(renderExtensions));
  unsubscribers.push(stores.modules.subscribe(renderExtensions));
  unsubscribers.push(stores.skills.subscribe(renderExtensions));
  unsubscribers.push(stores.mcp.subscribe(renderExtensions));
  unsubscribers.push(stores.permissions.subscribe(renderPermissions));
  unsubscribers.push(stores.storage.subscribe(renderDiagnostics));
  unsubscribers.push(stores.diagnostics.subscribe(renderDiagnostics));

  return () => unsubscribers.forEach((unsubscribe) => unsubscribe());
}
