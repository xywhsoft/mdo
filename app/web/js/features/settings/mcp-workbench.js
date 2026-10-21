import { api } from "../../api/client.js";
import { targetState, subscribeTarget } from "../../api/target.js";
import { element, clear, errorMessage } from "../../utils/dom.js";
import { newMcp, prepareMcpCredentials, mcpImportDocuments } from "./extension-formats.js";
import { refreshMcp } from "../../state/resources.js";

const targetKey = () => targetState().selected?.id || "local";
const portable = id => /^[a-zA-Z][a-zA-Z0-9._-]{0,63}$/.test(id);

export function createMcpWorkbench({ onChange }) {
  let dialog = null, controller = null, unsubscribe = null;
  function close(force = false) {
    if (!dialog) return true;
    if (!force && dialog._pending() && !window.confirm("放弃未保存的 MCP 配置？")) return false;
    controller?.abort(); unsubscribe?.(); dialog.close(); dialog.remove(); dialog = null; return true;
  }
  function open() {
    if (!close()) return;
    const owner = targetKey(); controller = new AbortController();
    const signal = controller.signal;
    const live = () => dialog && owner === targetKey() && !signal.aborted;
    const options = () => ({ signal });
    dialog = element("dialog", { className: "extension-editor skill-workbench" });
    const project = element("select", { attrs: { "aria-label": "MCP 所属项目" } }, [
      element("option", { text: "默认项目", attrs: { value: "default" } })]);
    const status = element("p", { attrs: { role: "status" } });
    const list = element("div", { className: "extension-list" });
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
      dialog.querySelectorAll("button,select").forEach(node => { node.disabled = busy; });
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
            button("编辑", () => edit(item)),
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
              await api.delete(endpoint(item.id), { ...options(), ifMatch: `"${item.revision}"` });
              if (live()) { onChange(); await load(); }
            }),
          ]),
        ]));
      }
      if (!(response.data.items ?? []).length) list.append(element("p", { text: "此项目尚未配置 MCP；会继承个人 MCP。" }));
    }
    function edit(item = null) {
      if (dirty() && !window.confirm("放弃未保存的 MCP 配置？")) return;
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
    }
    const imported = element("textarea", { attrs: { "aria-label": "导入 MCP JSON", placeholder: "粘贴 mcpServers JSON；凭据会存入本机凭据库", spellcheck: "false" } });
    dialog.append(element("h2", { text: "项目 MCP" }), project,
      element("p", { className: "extension-help", text: "配置保存在项目 .mdo/mcp；同名项目配置覆盖个人配置。保存和读取不会启动服务器，凭据仍保存在本机。" }),
      button("新增项目 MCP", () => edit()), list, editor,
      element("h3", { text: "导入现有配置到此项目" }), imported,
      button("导入 JSON", async () => {
        const documents = mcpImportDocuments(imported.value); let completed = 0;
        try { for (const item of documents) {
          if (!live()) return;
          await api.put(endpoint(item.id), { content: item.content, secrets: item.secrets }, { ...options(), ifMatch: '"new"' }); completed++;
        } } finally { if (live()) { onChange(); await load(); status.textContent = `已导入 ${completed}/${documents.length} 个服务器`; } }
        imported.value = "";
      }), status, button("关闭", () => close(), true));
    project.addEventListener("change", () => {
      if (dirty() && !window.confirm("放弃未保存的 MCP 配置？")) { project.value = editProject; return; }
      source = null; inputs = []; clear(editor); void run(load);
    });
    dialog.addEventListener("cancel", event => { event.preventDefault(); close(); });
    document.body.append(dialog); dialog.showModal();
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
