import { openLayer } from "../../components/layers.js";
import { createMcpContext } from "./mcp-context.js";
import { createMcpAuthorization } from "./mcp-authorization.js";
import { api } from "../../api/client.js";
import { targetState, subscribeTarget } from "../../api/target.js";
import { element, clear, errorMessage } from "../../utils/dom.js";
import { newMcp, prepareMcpCredentials, mcpImportDocuments } from "./extension-formats.js";
import { refreshMcp } from "../../state/resources.js";

const targetKey = () => targetState().selected?.id || "local";
const portable = id => /^[a-zA-Z][a-zA-Z0-9._-]{0,63}$/.test(id);

export function createMcpWorkbench({ onChange, onConfigure=()=>{}, onShare=()=>{} }) {
  const context = createMcpContext();
  const authorization = createMcpAuthorization();
  let view=null, dialog = null, controller = null, unsubscribe = null;
  function close(force = false) {
    if (!dialog) return true;
    if (!force && dialog._pending() && !window.confirm("放弃未保存的 MCP 配置？")) return false;
    context.destroy(); authorization.destroy(); controller?.abort(); unsubscribe?.(); const previous=view;view=null;dialog=null;previous?.dispose(); return true;
  }
  function open() {
    if (!close()) return;
    const owner = targetKey(); controller = new AbortController();
    const signal = controller.signal;
    const live = () => dialog && owner === targetKey() && !signal.aborted;
    const options = () => ({ signal });
    view=openLayer({title:"项目 MCP 与配置迁移",icon:"globe",width:"58rem",height:"85%",beforeClose:()=>!dialog?._pending()||confirm("放弃未保存的 MCP 配置？"),onClosed:()=>close(true)});dialog=view.element;dialog.classList.add("extension-authoring");
    const project = element("select", { attrs: { "aria-label": "MCP 所属项目" } }, [
      element("option", { text: "默认项目", attrs: { value: "default" } })]);
    const status = element("p", { attrs: { role: "status" } });
    const list = element("div", { className: "extension-list" });
    const external = element("div", { className: "extension-list" });
    const destination = element("select", { attrs: { "aria-label": "迁移目标范围" } }, [
      element("option", { text: "迁移到个人 MCP", attrs: { value: "user" } }),
      element("option", { text: "迁移到所选项目", attrs: { value: "project" } })]);
    const editor = element("div");
    let busy = false, source = null, baseline = "", editProject = "", editId = "", revision = "new", inputs = [];
    let idInput = null;
    const dirty = () => Boolean((source && (source.value !== baseline || (!editId && idInput?.value))) || imported.value);
    dialog._pending = () => busy || dirty();
    const endpoint = (id = "") => `/mcp/project${id ? `/${encodeURIComponent(id)}` : ""}?project=${encodeURIComponent(project.value)}`;
    const button = (text, action, direct = false) => {
      const node = element("button", { className: "secondary-button", text, attrs: { type: "button" } });
      node.addEventListener("click", () => { if (direct) action(); else void run(action); }); return node;
    };
    function controls() {
      if (!live()) return;
      dialog.querySelectorAll("button,select").forEach(node => { node.disabled = busy || node.dataset.unavailable === "true"; });
      if (source) source.readOnly = busy;
    }
    async function run(action) {
      if (!live() || busy) return;
      busy = true; status.textContent = ""; controls();
      try { await action(); }
      catch (error) { if (live() && error?.name !== "AbortError") status.textContent = errorMessage(error); }
      finally { busy = false; controls(); }
    }
    async function load() {
      const response = await api.get(endpoint(), options()); if (!live()) return;
      clear(list);
      for (const item of response.data.items ?? []) {
        list.append(element("article", { className: "resource-card" }, [
          element("h3", { text: item.name || item.id }),
          element("p", { text: `${item.id} · 项目配置 · ${item.enabled ? "已启用" : "已停用"}` }),
          item.valid ? null : element("p", { className: "extension-error", text: item.error }),
          element("div", { className: "resource-actions" }, [
            item.template_connection ? button("配置连接",()=>onConfigure(item,project.value),true) : button("编辑", () => edit(item)),
            !item.template_connection&&item.valid ? button("分享到创作中心",()=>onShare({...item,project:project.value}),true) : null,
            button("资源、提示词与诊断", async () => { await api.post(endpoint(), {}, options()); if (live()) context.open(item.runtime_id); }),
            item.has_oauth ? button("授权", async () => {
              await api.post(endpoint(), {}, options()); if (live()) authorization.open(item.runtime_id);
            }) : null,
            button(item.enabled ? "停用" : "启用", async () => {
              await api.post(`/mcp/project/${encodeURIComponent(item.id)}/enabled?project=${encodeURIComponent(project.value)}`,
                { enabled: !item.enabled }, { ...options(), ifMatch: `"${item.revision}"` });
              if (live()) { onChange(); await load(); }
            }),
            button("测试连接", async () => {
              await api.post(endpoint(), {}, options()); if (!live()) return;
              await refreshMcp(item.runtime_id, { signal, isCurrent: live });
              if (live()) status.textContent = "连接测试通过";
            }),
            button("删除", async () => {
              if (!window.confirm(`删除项目 MCP ${item.id}？`)) return;
              if(item.template_connection)await api.post(`/mcp/connections?project=${encodeURIComponent(project.value)}`,{id:item.id,action:"delete"},{...options(),ifMatch:`"${item.revision}"`});else await api.delete(endpoint(item.id), { ...options(), ifMatch: `"${item.revision}"` });
              if (live()) { onChange(); await load(); }
            }),
          ]),
        ]));
      }
      if (!(response.data.items ?? []).length) list.append(element("p", { text: "此项目尚未配置 MCP；会继承个人 MCP。" }));
    }
    function edit(item = null) {
      if (dirty() && !window.confirm("放弃未保存的 MCP 配置？")) return;
      view.footer.querySelector("[data-mcp-project-save]")?.remove();
      clear(editor); editId = item?.id || ""; editProject = project.value; revision = item?.revision || "new"; inputs = [];
      const id = element("input", { attrs: { "aria-label": "服务器 ID", placeholder: "server", value: editId } });
      idInput = id;
      id.disabled = Boolean(editId);
      source = element("textarea", { className: "extension-source", attrs: { "aria-label": "MCP JSON 配置", spellcheck: "false" } });
      source.value = baseline = item?.content || JSON.stringify(newMcp("server"), null, 2);
      editor.append(element("h3", { text: item ? `编辑 ${item.id}` : "新增项目 MCP" }), id, source,
        button("保存到项目", async () => {
          const name = editId || id.value.trim(); if (!portable(name)) throw new Error("服务器 ID 格式无效");
          const doc = JSON.parse(source.value); doc.id = name;
          const body = prepareMcpCredentials(doc, inputs);
          await api.put(`/mcp/project/${encodeURIComponent(name)}?project=${encodeURIComponent(editProject)}`,
            body, { ...options(), ifMatch: `"${revision}"` });
          if (!live()) return;
          source = null; inputs = []; clear(editor); onChange(); await load(); status.textContent = "已保存；尚未启动服务器";
        }));
      const save=editor.lastElementChild;save?.setAttribute("data-mcp-project-save","");if(save)view.footer.prepend(save);
    }
    const imported = element("textarea", { attrs: { "aria-label": "导入 MCP JSON", placeholder: "粘贴 mcpServers JSON；凭据会存入本机凭据库", spellcheck: "false" } });
    view.body.append(project,
      element("p", { className: "extension-help", text: "配置保存在项目 .mdo/mcp；同名项目配置覆盖个人配置。保存和读取不会启动服务器，凭据仍保存在本机。" }),
      button("新增项目 MCP", () => edit()), list, editor,
      element("h3", { text: "从其他 Agent 迁移" }),
      element("p", { className: "extension-help", text: "扫描 Claude Code、Codex、ZCode、OpenCode 与 .agents 配置。预览不显示密钥；导入时保管凭据并保留原文件。原 Agent 的权限规则不会迁移。" }),
      destination, button("扫描已有配置", async () => {
        const response = await api.get(`/mcp/sources?project=${encodeURIComponent(project.value)}`, options());
        if (!live()) return; clear(external);
        for (const item of response.data.items ?? []) {
          const name = element("input", { attrs: { "aria-label": `${item.id} 的目标名称`, value: item.id } });
          const migrate = button("导入", async () => {
            if (!portable(name.value)) throw new Error("目标服务器 ID 格式无效");
            await api.post(`/mcp/migrate?project=${encodeURIComponent(project.value)}`, {
              origin: item.origin, source_index: item.source_index, id: item.id,
              revision: item.revision, target_id: name.value, scope: destination.value,
            }, options());
            if (live()) { onChange(); await load(); status.textContent = `已导入 ${name.value}`; }
          });
          migrate.disabled = !item.compatible;
          if (!item.compatible) migrate.dataset.unavailable = "true";
          external.append(element("article", { className: "resource-card" }, [
            element("h3", { text: item.id }),
            element("p", { text: `${item.provider} · ${item.origin === "project" ? "项目" : "个人"} · ${item.credential_count} 项凭据` }),
            item.error ? element("p", { className: "extension-error", text: item.error }) : null,
            name, migrate,
          ]));
        }
        for (const issue of response.data.issues ?? []) external.append(element("p", { className: "extension-error", text: `${issue.provider}：${issue.message}` }));
        if (!(response.data.items ?? []).length) external.append(element("p", { text: "未发现可迁移的 MCP 配置。" }));
      }), external,
      element("h3", { text: "导入现有配置到此项目" }), imported,
      button("导入 JSON", async () => {
        const documents = mcpImportDocuments(imported.value); let completed = 0;
        try { for (const item of documents) {
          if (!live()) return;
          await api.put(endpoint(item.id), { content: item.content, secrets: item.secrets }, { ...options(), ifMatch: '"new"' }); completed++;
        } } finally { if (live()) { onChange(); await load(); status.textContent = `已导入 ${completed}/${documents.length} 个服务器`; } }
        imported.value = "";
      }), status);
    view.footer.append(button("关闭", () => close(), true));
    project.addEventListener("change", () => {
      if (dirty() && !window.confirm("放弃未保存的 MCP 配置？")) { project.value = editProject; return; }
      source = null; inputs = []; clear(editor); void run(load);
      clear(external);
    });
    dialog.addEventListener("cancel", event => { event.preventDefault(); close(); });

    unsubscribe = subscribeTarget(() => { if (owner !== targetKey()) close(true); });
    void run(async () => {
      const response = await api.get("/projects", options()); if (!live()) return;
      for (const item of response.data.items ?? []) if (item.id !== "default" && item.status !== "trash")
        project.append(element("option", { text: item.name || item.id, attrs: { value: item.id } }));
      await load();
    });
  }
  return { open, destroy: () => close(true), hasPendingChanges: () => Boolean(dialog?._pending()) };
}
