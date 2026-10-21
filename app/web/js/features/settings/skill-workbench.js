import { api } from "../../api/client.js";
import { targetState, subscribeTarget } from "../../api/target.js";
import { element, clear, errorMessage } from "../../utils/dom.js";
import { skillAuthoringPrompt } from "./skill-authoring.js";

const targetKey = () => targetState().selected?.id || "local";
const portable = value => /^[a-z][a-z0-9._-]{0,63}$/.test(value);

export function createSkillWorkbench({ onChange }) {
  let dialog = null, controller = null, stopTarget = null;
  function close(force = false) {
    if (!dialog) return true;
    if (!force && dialog._dirty?.() && !window.confirm("放弃未保存的 Skill 内容？")) return false;
    const focus = dialog._returnFocus;
    controller?.abort(); stopTarget?.(); dialog.close(); dialog.remove(); dialog = null;
    if (focus?.isConnected) focus.focus(); return true;
  }
  async function open() {
    if (dialog) { dialog.focus(); return; }
    const owner = targetKey(), requestController = new AbortController(); controller = requestController;
    const live = () => dialog && controller === requestController && targetKey() === owner && !requestController.signal.aborted;
    const options = () => ({ signal: requestController.signal });
    dialog = element("dialog", { className: "extension-editor", attrs: { "aria-label": "Skill 来源与项目管理" } });
    dialog._returnFocus = document.activeElement;
    const project = element("select", { attrs: { "aria-label": "项目" } }, [element("option", { text: "默认项目", attrs: { value: "default" } })]);
    const scope = element("select", { attrs: { "aria-label": "保存范围" } }, [
      element("option", { text: "个人技能", attrs: { value: "user" } }), element("option", { text: "所选项目", attrs: { value: "project" } })]);
    const status = element("p", { attrs: { role: "status" } });
    const location = element("p", { className: "extension-help" });
    const sources = element("div", { className: "extension-list" }), owned = element("div", { className: "extension-list" });
    const editor = element("div", { className: "skill-workbench-editor" });
    let busy = false, baseline = "", sourceControl = null, idControl = null, revision = "new", editingProject = "", roots = {};
    const dirty = () => sourceControl && sourceControl.value !== baseline;
    dialog._dirty = dirty; dialog._pending = () => busy || dirty();
    const button = (text, action) => {
      const node = element("button", { className: "secondary-button", text, attrs: { type: "button" } });
      node.addEventListener("click", () => { void run(action); }); return node;
    };
    const controls = () => {
      if (!dialog) return;
      dialog.querySelectorAll("button,select").forEach(node => { node.disabled = busy; });
      if (idControl) idControl.disabled = busy || revision !== "new";
      if (sourceControl) sourceControl.readOnly = busy;
    };
    async function run(action) {
      if (busy || !live()) return;
      busy = true; controls(); status.textContent = "";
      try { await action(); }
      catch (error) { if (live() && error?.name !== "AbortError") status.textContent = errorMessage(error); }
      finally { busy = false; if (live()) controls(); }
    }
    const endpoint = path => `${path}?project=${encodeURIComponent(project.value)}`;
    const resetEditor = () => { clear(editor); sourceControl = idControl = null; baseline = ""; };
    async function load() {
      const selected = project.value;
      const [all, local] = await Promise.all([api.get(endpoint("/skills/sources"), options()), api.get(endpoint("/skills/project"), options())]);
      if (!live() || selected !== project.value) return;
      roots = all.data;
      location.textContent = `个人：${roots.user_directory || "不可用"} · 项目：${roots.project_directory || "不可用"}`;
      clear(sources); clear(owned);
      for (const item of local.data.items || []) owned.append(element("article", { className: "resource-card" }, [
        element("h3", { text: item.name || item.id }), element("code", { text: item.path }),
        element("p", { text: `${item.enabled === false ? "已停用" : "已启用"}${item.valid ? "" : " · 格式或资源无效"}` }),
        button("编辑", async () => { if (!discardEditor()) return; await edit(item.id); }),
        button(item.enabled === false ? "启用" : "停用", async () => {
          const data = (await api.get(endpoint(`/skills/project/${encodeURIComponent(item.id)}`), options())).data;
          await api.post(endpoint(`/skills/project/${encodeURIComponent(item.id)}/enabled`), { enabled: !data.enabled }, { ...options(), ifMatch: `"${data.revision}"` });
          if (live()) { onChange(); await load(); }
        }),
        button("移除", async () => {
          if (!window.confirm(`移除项目技能 ${item.id}？文件会保留在项目 .mdo/skill-trash 中。`)) return;
          const data = (await api.get(endpoint(`/skills/project/${encodeURIComponent(item.id)}`), options())).data;
          await api.delete(endpoint(`/skills/project/${encodeURIComponent(item.id)}`), { ...options(), ifMatch: `"${data.revision}"` });
          if (live()) { onChange(); if (idControl?.value === item.id) resetEditor(); await load(); }
        }),
      ]));
      if (!(local.data.items || []).length) owned.append(element("p", { text: "所选项目尚未创建 .mdo Skill。" }));
      for (const item of all.data.items || []) sources.append(element("article", { className: "resource-card" }, [
        element("h3", { text: `${item.name || item.id} · ${item.origin}/${item.provider}` }),
        element("code", { text: item.source_path }),
        button("复制到所选范围", async () => {
          const targetId = window.prompt("新 Skill ID（目标已存在时拒绝覆盖）", item.id);
          if (targetId === null) return;
          if (!portable(targetId)) throw new Error("ID 必须以小写字母开头，仅含小写字母、数字、点、下划线或连字符，最多 64 个字符");
          const destination = scope.value === "project" ? roots.project_directory : roots.user_directory;
          if (!window.confirm(`复制 ${item.source_path} 到 ${destination}/${targetId}？\n原文件会保留，脚本不会执行。`)) return;
          await api.post(endpoint("/skills/migrate"), { origin: item.origin, provider: item.provider, id: item.id, target_id: targetId, scope: scope.value }, options());
          if (live()) { onChange(); await load(); status.textContent = "已复制。个人技能可在原列表编辑，项目技能在上方编辑。"; }
        }),
      ]));
      if (all.data.truncated) sources.append(element("p", { className: "extension-help", text: "来源较多，仅显示前 128 项；可通过原有文件夹导入入口选择其他技能。" }));
      for (const issue of all.data.diagnostics || []) sources.append(element("p", { className: "extension-error", text: `${issue.source_path || `${issue.origin}/${issue.provider}`}：${issue.message}` }));
    }
    const discardEditor = () => !dirty() || window.confirm("放弃未保存的 Skill 内容？");
    async function edit(id = "") {
      const selected = project.value;
      const item = id ? (await api.get(endpoint(`/skills/project/${encodeURIComponent(id)}`), options())).data : null;
      if (!live() || selected !== project.value) return;
      clear(editor); editingProject = selected;
      idControl = element("input", { attrs: { "aria-label": "Skill ID", placeholder: "skill-id", maxlength: 64 } });
      idControl.value = id; idControl.disabled = Boolean(item); revision = item?.revision || "new";
      sourceControl = element("textarea", { attrs: { "aria-label": "SKILL.md", rows: 16, spellcheck: "false" } });
      sourceControl.value = item?.content || "---\nname: New Skill\ndescription: Describe when to use this Skill.\n---\n\nWrite the workflow here.\n";
      baseline = item?.content || "";
      editor.append(element("h3", { text: `编辑项目 ${selected} 的 SKILL.md` }), idControl, sourceControl, button("保存到项目", async () => {
        if (!portable(idControl.value)) throw new Error("请使用有效的 Skill ID");
        const text = sourceControl.value, id = idControl.value;
        const data = (await api.put(`/skills/project/${encodeURIComponent(id)}?project=${encodeURIComponent(editingProject)}`, { content: text },
          { ...options(), ifMatch: `"${revision}"` })).data;
        if (!live()) return;
        revision = data.revision; baseline = text; idControl.disabled = true;
        onChange(); await load(); status.textContent = "已保存；项目技能在下次任务或调用时生效。";
      }));
    }
    let previous = "default";
    project.addEventListener("change", () => {
      if (!discardEditor()) { project.value = previous; return; }
      previous = project.value; resetEditor(); void run(load);
    });
    const header = element("div", { className: "extension-editor-heading" }, [element("h2", { text: "Skill 来源与项目管理" }), button("关闭", () => close())]);
    dialog.append(header, element("p", { text: "浏览当前设备上的技能来源，复制到个人或所选项目。所有导入均保留原文件并检查格式。" }),
      element("div", { className: "extension-toolbar" }, [project, scope,
        button("刷新来源", load), button("新建项目 Skill", async () => { if (discardEditor()) await edit(); }),
        button("用 AI 编写 Skill", () => {
          const text = skillAuthoringPrompt({ directory: scope.value === "project" ? roots.project_directory : roots.user_directory, scope: scope.value });
          if (!discardEditor()) return;
          const projectId = project.value; close(true);
          window.dispatchEvent(new CustomEvent("mdo-create-skill", { detail: { text, projectId, target: owner } }));
        })]), location, status, editor, element("h3", { text: "项目技能" }), owned, element("h3", { text: "可迁移的技能来源" }), sources);
    dialog.addEventListener("cancel", event => { event.preventDefault(); if (!busy) close(); });
    document.body.append(dialog); dialog.showModal();
    stopTarget = subscribeTarget(() => { if (targetKey() !== owner) close(true); });
    await run(async () => {
      const response = await api.get("/projects", options()); if (!live()) return;
      for (const item of response.data.items || []) if (item.id !== "default") project.append(element("option", { text: item.name || item.id, attrs: { value: item.id } }));
      await load();
    });
  }
  return { open, close, hasPendingChanges: () => Boolean(dialog?._pending?.()), destroy: () => close(true) };
}
