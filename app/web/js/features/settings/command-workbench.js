import { api } from "../../api/client.js";
import { subscribeTarget, targetState } from "../../api/target.js";
import { clear, element, errorMessage } from "../../utils/dom.js";
import { portableId } from "./extension-formats.js";
import { newCommand } from "./command-template.js";
import { customizableResource, storeReadOnlyMessage } from "./resource-credits.js";
import { openLayer } from "../../components/layers.js";

export function createCommandWorkbench({ onChange = () => {}, onShare, owner: pageOwner } = {}) {
  let dialog = null;
  const targetKey = () => targetState().selected?.id || "local";
  function close(force = false) {
    if (!dialog) return true;
    if (!force && (dialog._busy() || dialog._dirty() && !window.confirm("放弃尚未保存的项目命令修改？"))) return false;
    dialog.dispose({ restoreFocus: true }); return true;
  }
  const unsubscribe = subscribeTarget(() => { if (dialog && dialog._target !== targetKey()) close(true); });
  async function open() {
    if (dialog) { dialog.closeButton.focus(); return; }
    const target = targetKey(), controller = new AbortController();
    let busy = false, selected = null, revision = "new", baseline = "", serial = 0;
    const dirty = () => source.value !== baseline || !selected && Boolean(id.value);
    const live = () => dialog === view && target === targetKey() && !controller.signal.aborted;
    let view; view = openLayer({ title: "项目编辑器命令", icon: "terminal", width: "60rem", height: "80%", owner: pageOwner,
      beforeClose: () => !busy && (!dirty() || window.confirm("放弃尚未保存的项目命令修改？")),
      onClosed() { controller.abort(); if (dialog === view) dialog = null; } });
    dialog = view; view._target = target; view._busy = () => busy; view._dirty = dirty;
    view.element.classList.add("command-authoring");
    const project = element("select", { attrs: { "aria-label": "项目" } }, [element("option", { text: "默认项目", attrs: { value: "default" } })]);
    project.value = "default";
    const list = element("div", { className: "extension-list" });
    const id = element("input", { attrs: { placeholder: "my-command", "aria-label": "命令 ID", maxlength: 64 } });
    const source = element("textarea", { className: "extension-source", attrs: { "aria-label": "C 命令源代码", spellcheck: "false" } });
    const status = element("p", { attrs: { role: "status" } });
    const options = () => ({ signal: controller.signal });
    const discard = () => !dirty() || window.confirm("放弃尚未保存的项目命令修改？");
    const controls = () => { if (!live()) return; view.element.querySelectorAll("button,input,textarea,select").forEach(n => n.disabled = busy); id.disabled = busy || Boolean(selected); };
    const action = (text, handler) => {
      const b = element("button", { className: "secondary-button", text, attrs: { type: "button" } });
      b.addEventListener("click", () => { void run(handler); }); return b;
    };
    async function run(handler) {
      if (busy || !live()) return; busy = true; controls();
      try { await handler(); } catch (error) { if (live() && error.name !== "AbortError") status.textContent = errorMessage(error); }
      finally { busy = false; controls(); }
    }
    function reset() { selected = null; revision = "new"; id.value = ""; source.value = newCommand(); baseline = source.value; status.textContent = "新建 .mdo/commands/<名称>.c"; controls(); }
    async function load() {
      const token = ++serial, scope = project.value; reset(); clear(list);
      const response = await api.get(`/commands?project=${encodeURIComponent(scope)}`, options());
      if (!live() || token !== serial || scope !== project.value) return;
      for (const item of Object.values(response.data || {})) {
        const row = element("div", { className: "extension-row" }, [element("span", { text: `/${item.id} · ${item.scope} · ${item.enabled ? "启用" : "停用"}${item.valid ? "" : " · 配置无效"}` })]);
        if (item.scope === "project" || customizableResource(item)) row.append(action(item.scope === "project" ? "编辑" : "复制到项目", async () => {
          if (!discard()) return;
          const token = ++serial;
          const path = item.scope === "project" ? `/commands/project/${item.id}?project=${encodeURIComponent(scope)}` : `/extensions/commands/${item.id}`;
          const data = (await api.get(path, options())).data;
          if (!live() || token !== serial || scope !== project.value) return;
          // Recheck provenance from the authoritative source read before copying.
          if (item.scope !== "project" && !customizableResource(data)) throw Error(storeReadOnlyMessage());
          selected = item; id.value = item.id; source.value = data.content; baseline = source.value;
          revision = item.scope === "project" ? data.revision : "new";
          status.textContent = item.scope === "project" ? "修改当前项目命令" : "保存后创建当前项目的同名覆盖文件";
        }));
        else row.append(element("small", { text: storeReadOnlyMessage() }));
        if (onShare && item.scope === "project" && item.valid) row.append(action("分享到创作中心", async () => {
          if (!discard()) return;
          close(true); await onShare({ ...item, project: scope });
        }));
        list.append(row);
      }
      controls();
    }
    async function mutate(kind) {
      if (!portableId(id.value)) throw Error("请输入合法命令 ID");
      if (kind !== "save" && selected?.scope !== "project") throw Error("请先选择项目命令");
      const path = `/commands/project/${id.value}${kind === "toggle" ? "/enabled" : ""}?project=${encodeURIComponent(project.value)}`;
      const opt = { ...options(), ifMatch: `"${revision}"` };
      if (kind === "delete") {
        if (!window.confirm(`删除项目命令 /${id.value} 的覆盖文件？`)) return;
        await api.delete(path, opt);
      } else if (kind === "toggle") await api.post(path, { enabled: !selected.enabled }, opt);
      else await api.put(path, { content: source.value }, opt);
      if (live()) { onChange(); await load(); }
    }
    id.addEventListener("input", () => { if (source.value === baseline && portableId(id.value)) { source.value = newCommand(id.value); baseline = source.value; } });
    let previousProject = project.value;
    project.onchange = () => { if (!discard()) { project.value = previousProject; return; } previousProject = project.value; void run(load); };
    view.body.append(element("p", { className: "extension-help", text: "项目命令优先于个人和内置命令。保存仅校验元数据；C 代码在调用时编译运行。商店安装的命令通过原列表更新和卸载。" }), project,
      element("div", { className: "command-workbench-columns" }, [list, element("div", { className: "command-workbench-editor" }, [id, source, status])]));
    const dismiss = element("button", { className: "secondary-button", text: "关闭", attrs: { type: "button" } });
    dismiss.addEventListener("click", () => { void view.close("cancel"); });
    view.footer.append(action("新建", () => { if (discard()) reset(); }), action("保存", () => mutate("save")), action("启用 / 停用", () => mutate("toggle")), action("删除覆盖文件", () => mutate("delete")), dismiss);
    reset();
    await run(async () => {
      const items = (await api.get("/projects", options())).data.items || [];
      if (!live()) return;
      for (const item of items) if (item.id !== "default") project.append(element("option", { text: item.name || item.id, attrs: { value: item.id } }));
      await load();
    });
  }
  return { open, close, hasPendingChanges: () => Boolean(dialog && (dialog._busy() || dialog._dirty())), destroy() { close(true); unsubscribe(); } };
}
