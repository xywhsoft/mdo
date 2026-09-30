import { element, clear, errorMessage, toast } from "../../utils/dom.js";
import { subscribeLocale, t } from "../../i18n.js";
import {
  reloadCatalog, setMcpEnabled, disconnectMcp, refreshMcp,
  applyLegacyMigration,
} from "../../state/resources.js";
import { createModelConfigPanel } from "./model-config-panel.js";

function card(title, description, meta = [], actions = []) {
  const body = [element("h3", { text: title }), element("p", {
    text: description || t("resource.noDescription", {}, "暂无说明"),
  })];
  if (meta.length) body.push(element("div", { className: "resource-meta" }, meta.map((value) => element("span", { text: value }))));
  if (actions.length) body.push(element("div", { className: "resource-actions" }, actions));
  return element("article", { className: "resource-card" }, body);
}

function action(label, handler, tone = "neutral") {
  const className = tone === "danger" ? "task-cancel"
    : tone === "primary" ? "primary-button" : "secondary-button";
  const button = element("button", { className, text: label, attrs: { type: "button" } });
  button.addEventListener("click", async () => {
    button.disabled = true;
    try { await handler(); }
    catch (error) { toast(errorMessage(error), "error"); }
    finally { button.disabled = false; }
  });
  return button;
}

function trackedActions(container, attribute) {
  const pending = new Set();
  const find = (key) => [...container.querySelectorAll(`[${attribute}]`)]
    .find((button) => button.getAttribute(attribute) === key);
  function create(key, label, handler, tone = "neutral") {
    const className = tone === "danger" ? "task-cancel"
      : tone === "primary" ? "primary-button" : "secondary-button";
    const button = element("button", { className, text: label, attrs: {
      type: "button", [attribute]: key,
    } });
    if (pending.has(key)) button.setAttribute("aria-disabled", "true");
    button.addEventListener("click", async () => {
      if (pending.has(key)) return;
      pending.add(key);
      button.setAttribute("aria-disabled", "true");
      try { await handler(); }
      catch (error) { toast(errorMessage(error), "error"); }
      finally {
        pending.delete(key);
        find(key)?.removeAttribute("aria-disabled");
      }
    });
    return button;
  }
  return { create, find };
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

function access(value) {
  if (value === true || value === "true") return t("resource.accessAllowed", {}, "允许");
  if (value === false || value === "false") return t("resource.accessDenied", {}, "禁止");
  if (value === "ask") return t("resource.accessAsk", {}, "询问");
  return String(value);
}

const RESOURCE_CODE_KEYS = Object.freeze({
  builtin: "resource.builtin",
  external_reference: "resource.externalReference",
  external: "resource.storageExternal",
  ephemeral: "resource.storageEphemeral",
  lazy: "resource.storageLazy",
  disconnected: "resource.mcpDisconnected",
  ready: "resource.mcpReady",
  failed: "resource.mcpFailed",
  disabled: "resource.mcpDisabled",
});

function resourceCode(value) {
  const key = RESOURCE_CODE_KEYS[value];
  return key ? t(key, {}, String(value)) : String(value);
}

const PERMISSION_PROFILE_KEYS = Object.freeze({
  "read-only": "shell.permission.readOnly",
  balanced: "shell.permission.balanced",
  "full-access": "shell.permission.fullAccess",
});

function permissionProfile(value) {
  const key = Object.hasOwn(PERMISSION_PROFILE_KEYS, value)
    ? PERMISSION_PROFILE_KEYS[value] : "";
  return key ? t(key, {}, value) : String(value ?? "");
}

// Only translate descriptions shipped by mdo itself. An external resource or
// an edited built-in description remains author-owned text, even with the same ID.
const BUILTIN_DESCRIPTIONS = Object.freeze({
  agent: Object.freeze({
    "mdo.default": ["General coding and knowledge-work Agent with inherited model settings.",
      "resource.defaultAgentDescription"],
  }),
  skill: Object.freeze({
    "project-explorer": ["Inspect a repository and report its structure before making changes.",
      "resource.projectExplorerDescription"],
  }),
  module: Object.freeze({
    "mdo.default-agent": ["Built-in default Agent profile.",
      "resource.defaultAgentModuleDescription"],
    "mdo.core.echo": ["A minimal built-in module used to verify the complete module ABI path.",
      "resource.echoModuleDescription"],
    "mdo.core.todo": ["Publishes a compact plan snapshot for the session conversation dock.",
      "resource.todoModuleDescription"],
  }),
});

export function resourceDescription(kind, item) {
  const entry = BUILTIN_DESCRIPTIONS[kind]?.[item?.id];
  const builtin = !item?.external && (kind === "agent"
    ? item?.id === "mdo.default" : kind !== "skill" || item?.trust === "builtin");
  return builtin && entry && item.description === entry[0]
    ? t(entry[1], {}, entry[0]) : item?.description;
}

function skillMetadata(skill) {
  const meta = [resourceCode(skill.trust)];
  if (skill.external && skill.trust !== "external_reference")
    meta.push(t("resource.external", {}, "外部"));
  if (!skill.external && skill.trust !== "builtin")
    meta.push(t("resource.builtin", {}, "内置"));
  meta.push(t("resource.tokenCount", { count: skill.estimated_tokens },
    `${skill.estimated_tokens} tokens`));
  return meta;
}

export function createResourcePanels({ agentsStore, stores, reload }) {
  const modelsContainer = document.querySelector("#settings-models-list");
  const extensionsContainer = document.querySelector("#settings-extensions-list");
  const permissionsContainer = document.querySelector("#settings-permissions-list");
  const diagnosticsContainer = document.querySelector("#settings-diagnostics-list");
  const unsubscribers = [];
  const extensionActions = trackedActions(extensionsContainer, "data-extension-action");
  const diagnosticActions = trackedActions(diagnosticsContainer, "data-diagnostic-action");
  let confirmingSource = "";
  let migrationResult = null;
  let migrationBusy = false;

  async function reloadChecked(name) {
    await reload(name);
    const state = stores[name].get();
    if (state.status === "error") throw state.error || new Error(t("resource.reloadFailed", {}, "目录刷新失败"));
  }

  async function refreshCatalog(name) {
    await reloadCatalog(name);
    await reloadChecked(name);
    toast(t("resource.catalogRefreshed", { name }, `${name} 目录已刷新`));
  }

  createModelConfigPanel(modelsContainer);

  function renderExtensions() {
    const focusedKey = extensionsContainer.contains(document.activeElement)
      ? document.activeElement?.dataset.extensionAction : "";
    clear(extensionsContainer);
    const modules = stores.modules.get();
    const skills = stores.skills.get();
    const mcp = stores.mcp.get();
    extensionsContainer.append(heading("Agent"));
    for (const agent of agentsStore.get().data?.items ?? []) {
      const toolSummary = agent.tools?.length
        ? t("resource.toolCount", { count: agent.tools.length }, `${agent.tools.length} tools`)
        : t("resource.toolsByPermission", {}, "未设工具白名单 · 仍受权限约束");
      extensionsContainer.append(card(agent.name || agent.id, resourceDescription("agent", agent), [agent.id, permissionProfile(agent.permission_profile),
        toolSummary,
        t("resource.skillCount", { count: agent.skills?.length ?? 0 }, `${agent.skills?.length ?? 0} Skills`)]));
    }
    extensionsContainer.append(heading("Skill", extensionActions.create("skills-reload",
      t("resource.refresh", {}, "刷新"), () => refreshCatalog("skills"))));
    if (skills.status === "error") extensionsContainer.append(empty(errorMessage(skills.error)));
    for (const skill of skills.status === "error" ? [] : skills.data?.items ?? []) {
      extensionsContainer.append(card(skill.name || skill.id, resourceDescription("skill", skill),
        skillMetadata(skill)));
    }
    extensionsContainer.append(heading("Module", extensionActions.create("modules-reload",
      t("resource.rebuild", {}, "重新编译"), () => refreshCatalog("modules"))));
    if (modules.status === "error") extensionsContainer.append(empty(errorMessage(modules.error)));
    for (const module of modules.status === "error" ? [] : modules.data?.modules ?? []) {
      extensionsContainer.append(card(module.name || module.id, resourceDescription("module", module), [module.version,
        module.external ? t("resource.externalTcc", {}, "外部 TCC") : t("resource.builtin", {}, "内置"),
        t("resource.toolCount", { count: module.tool_count }, `${module.tool_count} tools`),
        t("resource.agentCount", { count: module.agent_count }, `${module.agent_count} agents`)]));
    }
    extensionsContainer.append(heading("MCP", extensionActions.create("mcp-reload",
      t("resource.reloadConfig", {}, "重载配置"), () => refreshCatalog("mcp"))));
    if (mcp.status === "error") extensionsContainer.append(empty(errorMessage(mcp.error)));
    else if (!(mcp.data?.items ?? []).length) extensionsContainer.append(empty(t("resource.noMcp", {}, "尚未配置 MCP 服务器")));
    for (const server of mcp.status === "error" ? [] : mcp.data?.items ?? []) {
      const actions = [extensionActions.create(`mcp-toggle:${server.id}`, server.enabled ? t("resource.disable", {}, "停用") :
        t("resource.enable", {}, "启用"), async () => {
        await setMcpEnabled(server.id, !server.enabled);
        await reload("mcp");
      })];
      if (server.enabled) actions.push(extensionActions.create(`mcp-refresh:${server.id}`,
        t("resource.refreshTools", {}, "刷新工具"), async () => { await refreshMcp(server.id); await reload("mcp"); }));
      if (server.connected) actions.push(extensionActions.create(`mcp-disconnect:${server.id}`,
        t("resource.disconnect", {}, "断开"), async () => { await disconnectMcp(server.id); await reload("mcp"); }, "danger"));
      extensionsContainer.append(card(server.name || server.id, server.description, [server.transport, resourceCode(server.state),
        t("resource.toolCount", { count: server.discovered_tool_count ?? 0 }, `${server.discovered_tool_count ?? 0} tools`)], actions));
    }
    if (focusedKey) (extensionActions.find(focusedKey) ||
      (focusedKey.startsWith("mcp-") ? extensionActions.find("mcp-reload") : null))
      ?.focus({ preventScroll: true });
  }

  function renderPermissions(state) {
    clear(permissionsContainer);
    if (state.status === "error") { permissionsContainer.append(empty(errorMessage(state.error))); return; }
    const config = state.data?.configuration;
    if (!config) { permissionsContainer.append(empty(t("resource.permissionsUnavailable", {}, "权限配置不可用"))); return; }
    const defaultProfileName = permissionProfile(config.default_profile);
    permissionsContainer.append(card(t("resource.defaultProfile", { name: defaultProfileName },
      `默认权限方案：${defaultProfileName}`),
      t("resource.permissionDescription", {}, "权限由服务端配置验证并在每个 Agent 会话创建时固定。"),
      [t("resource.revision", { value: state.data.revision }, `revision ${state.data.revision}`)]));
    for (const [name, profile] of Object.entries(config.profiles ?? {})) {
      permissionsContainer.append(card(permissionProfile(name),
        t("resource.workspaceAccess", { read: access(profile.workspace_read), write: access(profile.workspace_write) },
          `工作区读取：${access(profile.workspace_read)} · 写入：${access(profile.workspace_write)}`),
        [t("resource.processAccess", { value: access(profile.process) }, `进程 ${access(profile.process)}`),
          t("resource.networkAccess", { value: access(profile.network) }, `网络 ${access(profile.network)}`)]));
    }
  }

  function renderDiagnostics() {
    const focusedKey = diagnosticsContainer.contains(document.activeElement)
      ? document.activeElement?.getAttribute("data-diagnostic-action") : "";
    clear(diagnosticsContainer);
    const storage = stores.storage.get();
    const diagnostics = stores.diagnostics.get();
    const migrations = stores.migrations.get();
    if (storage.status === "error") diagnosticsContainer.append(empty(errorMessage(storage.error)));
    else if (storage.data) diagnosticsContainer.append(card(t("resource.portableStorage", {}, "便携存储"),
      storage.data.home_path || t("resource.builtinReadOnly", {}, "内置只读资源"),
      [resourceCode(storage.data.persistence),
        t("resource.sessionCount", { count: storage.data.session_count }, `${storage.data.session_count} sessions`),
        t("resource.artifactCount", { count: storage.data.artifact_count }, `${storage.data.artifact_count} artifacts`)]));
    diagnosticsContainer.append(heading(t("resource.legacyMigration", {}, "旧版数据迁移"),
      diagnosticActions.create("migrations-rescan", t("resource.rescan", {}, "重新检测"),
        () => reloadChecked("migrations"))));
    if (migrationResult?.restart_required) {
      const result = card(t("resource.restartTitle", {}, "迁移已完成，需要重启 mdo"),
        t("resource.restartDescription", {}, "完整迁移批次已提交；当前进程仍使用启动时的运行配置。关闭并重新启动 mdo 后再继续工作。"),
        [migrationResult.target_path,
          t("resource.sessionCount", { count: migrationResult.imported_sessions }, `${migrationResult.imported_sessions} sessions`),
          t("resource.memoryCount", { count: migrationResult.imported_memory_entries }, `${migrationResult.imported_memory_entries} memories`),
          t("resource.skippedCount", { count: migrationResult.skipped_items }, `${migrationResult.skipped_items} skipped`)],
        []);
      result.setAttribute("tabindex", "-1");
      result.setAttribute("data-diagnostic-action", "migrations-result");
      diagnosticsContainer.append(result);
    }
    if (migrations.status === "error") {
      diagnosticsContainer.append(empty(errorMessage(migrations.error)));
    } else if (!(migrations.data?.items ?? []).length) {
      diagnosticsContainer.append(empty(t("resource.noLegacyScan", {}, "尚未完成旧数据检测")));
    } else {
      for (const source of migrations.data.items) {
        const label = source.source_id === "portable-data"
          ? t("resource.portableData", {}, "程序旁 data")
          : t("resource.userDirectory", {}, "用户目录 .mdo");
        const description = !source.found ? t("resource.sourceNotFound", {}, "未发现这个旧数据目录。")
          : !source.valid ? source.message || t("resource.sourceInvalid", {}, "旧数据未通过只读校验。")
            : !source.target_available ? t("resource.targetExists", {}, "目标 mdo Home 已存在，迁移不会覆盖现有数据。")
              : t("resource.importable", {}, "只读预览已通过，可以导入到新的便携 Home。");
        const cacheNotice = source.preserve_browser_cache
          ? t("resource.cachePreserved", {}, "便携浏览器缓存会保留；已有用户数据不会被覆盖。") : "";
        const actions = [];
        if (source.importable && migrations.data.requires_confirmation) {
          actions.push(diagnosticActions.create(`migration-review:${source.source_id}`, confirmingSource === source.source_id
            ? t("resource.awaitConfirmation", {}, "等待确认")
            : t("resource.reviewImport", {}, "查看导入确认"), () => {
            if (migrationBusy) return;
            confirmingSource = source.source_id;
            renderDiagnostics();
            diagnosticActions.find(`migration-cancel:${source.source_id}`)?.focus();
          }, "primary"));
        }
        const item = card(label, [description, cacheNotice].filter(Boolean).join(" "), [
          t("resource.fileCount", { count: source.file_count }, `${source.file_count} files`),
          t("resource.projectCount", { count: source.project_count }, `${source.project_count} projects`),
          t("resource.sessionCount", { count: source.session_count }, `${source.session_count} sessions`),
          t("resource.modelCount", { count: source.model_count }, `${source.model_count} models`),
          t("resource.scheduleCount", { count: source.schedule_count }, `${source.schedule_count} schedules`),
          t("resource.memoryCount", { count: source.memory_file_count }, `${source.memory_file_count} memories`),
          t("resource.unsupportedCount", { count: source.unsupported_count }, `${source.unsupported_count} unsupported`),
          t("resource.conflictCount", { count: source.conflict_count }, `${source.conflict_count} conflicts`),
        ], actions);
        item.append(element("dl", { className: "migration-paths" }, [
          element("div", {}, [element("dt", { text: t("resource.source", {}, "来源") }), element("dd", { text: source.source_path })]),
          element("div", {}, [element("dt", { text: t("resource.target", {}, "目标") }), element("dd", { text: source.target_path })]),
        ]));
        if (confirmingSource === source.source_id && source.importable) {
          const cancel = diagnosticActions.create(`migration-cancel:${source.source_id}`,
            t("resource.cancel", {}, "取消"), () => {
              if (migrationBusy) return;
              confirmingSource = "";
              renderDiagnostics();
              diagnosticActions.find(`migration-review:${source.source_id}`)?.focus();
            });
          const confirm = diagnosticActions.create(`migration-confirm:${source.source_id}`,
            t("resource.confirmImport", {}, "确认导入"), async () => {
            if (migrationBusy) return;
            migrationBusy = true;
            renderDiagnostics();
            try {
              migrationResult = await applyLegacyMigration(source.source_id, source.preview_token);
              confirmingSource = "";
              await reload("migrations");
              toast(t("resource.importedRestart", {}, "旧数据已导入，请重启 mdo"));
            } catch (error) {
              toast(errorMessage(error), "error");
              await reload("migrations");
            } finally {
              migrationBusy = false;
              renderDiagnostics();
            }
          }, "danger");
          cancel.disabled = migrationBusy;
          if (migrationBusy) confirm.setAttribute("aria-disabled", "true");
          item.append(element("div", { className: "migration-confirm", attrs: { role: "alert" } }, [
            element("strong", { text: t("resource.importPromptTitle", {}, "确认从此预览导入？") }),
            element("p", { text: t("resource.importPromptDescription", {}, "mdo 将导入到尚无用户数据的 Home，并保留浏览器缓存；旧目录会原样保留，已有用户数据、来源变化或令牌过期都会中止。成功后必须重启 mdo。") }),
            element("div", { className: "resource-actions" }, [cancel, confirm]),
          ]));
        }
        diagnosticsContainer.append(item);
      }
    }
    diagnosticsContainer.append(heading(t("resource.diagnosticsCount", { count: diagnostics.data?.total ?? 0 }, `诊断 (${diagnostics.data?.total ?? 0})`)));
    if (diagnostics.status === "error") diagnosticsContainer.append(empty(errorMessage(diagnostics.error)));
    else if (!(diagnostics.data?.items ?? []).length) diagnosticsContainer.append(empty(t("resource.noDiagnostics", {}, "没有检测到诊断问题")));
    for (const item of diagnostics.status === "error" ? [] : diagnostics.data?.items ?? []) {
      diagnosticsContainer.append(card(`${item.domain} · ${item.stage}`, item.message, [item.subject_id || item.path || "runtime"]));
    }
    if (focusedKey) {
      const restored = diagnosticActions.find(focusedKey);
      if (restored) restored.focus({ preventScroll: true });
      else if (focusedKey.startsWith("migration-confirm:") && migrationResult?.restart_required)
        diagnosticActions.find("migrations-result")?.focus();
    }
  }

  unsubscribers.push(agentsStore.subscribe(renderExtensions));
  unsubscribers.push(stores.modules.subscribe(renderExtensions));
  unsubscribers.push(stores.skills.subscribe(renderExtensions));
  unsubscribers.push(stores.mcp.subscribe(renderExtensions));
  unsubscribers.push(stores.permissions.subscribe(renderPermissions));
  unsubscribers.push(stores.storage.subscribe(renderDiagnostics));
  unsubscribers.push(stores.diagnostics.subscribe(renderDiagnostics));
  unsubscribers.push(stores.migrations.subscribe(renderDiagnostics));
  unsubscribers.push(subscribeLocale(() => {
    renderExtensions();
    renderPermissions(stores.permissions.get());
    renderDiagnostics();
  }));

  return () => unsubscribers.forEach((unsubscribe) => unsubscribe());
}
