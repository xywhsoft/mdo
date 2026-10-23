import { element as make, icon } from "./controls.js";

export const workbenchTools = Object.freeze([
  { id: "mdo.browser", title: "浏览器", icon: "globe", description: "浏览网页、调试和预览应用。" },
  { id: "mdo.files", title: "文件", icon: "folder", description: "浏览项目文件、查看和编辑内容。" },
  { id: "mdo.terminal", title: "终端", icon: "terminal", description: "在项目目录中运行命令。" },
  { id: "mdo.changes", title: "变更", icon: "review", description: "查看代码修改与差异。" },
]);

/** Launchers and recent records are supplied by the manager, so all actions
 * keep their original device/project/owner instead of using live navigation. */
export function mountStartView(root, { signal, context, launch, contributions, opened, resume }) {
  root.classList.add("wb-start-view");
  const heading = make("h2", "", "新标签页"), scope = make("p", "wb-start-context"), grid = make("div", "wb-start-tools"), extra = make("section", "wb-start-extra"), recent = make("section", "wb-start-recent"), status = make("p", "wb-start-status");
  const project = context.workspace?.split(/[\\/]/).filter(Boolean).at(-1) || (context.project === "default" ? "默认项目" : context.project);
  scope.textContent = project || "当前项目"; scope.title = context.workspace || project || ""; status.setAttribute("role", "status");
  let busy = false, previous = "";
  const activate = async execute => {
    if (busy || signal.aborted) return; busy = true; root.setAttribute("aria-busy", "true"); status.textContent = "";
    try { await execute(); }
    catch (error) { if (!signal.aborted) status.textContent = error.message || String(error); }
    finally { busy = false; root.removeAttribute("aria-busy"); }
  };
  const card = (tool, execute) => {
    const button = make("button", "wb-start-tool"), glyph = make("span", "wb-start-glyph"), text = make("span", "wb-start-label"), title = make("strong", "", tool.title), description = make("span", "", tool.description || "");
    button.type = "button"; button.setAttribute("aria-label", `打开${tool.title}`); button.dataset.tool = tool.id;
    glyph.append(icon(tool.icon || "file")); text.append(title, description); button.append(glyph, text, make("span", "wb-start-open", "打开"));
    button.onclick = () => void activate(execute); return button;
  };
  for (const tool of workbenchTools) grid.append(card(tool, () => launch(tool.id)));
  root.append(heading, scope, grid, status, extra, recent);
  const refresh = () => {
    if (signal.aborted) return;
    const plugins = contributions(), entries = opened(), identity = JSON.stringify([plugins.map(row => [row.id,row.title,row.description]), entries.map(row => [row.id,row.title,row.type])]);
    if (identity === previous) return; previous = identity; extra.replaceChildren(); recent.replaceChildren();
    if (plugins.length) { extra.append(make("h3", "", "插件")); for (const plugin of plugins) extra.append(card(plugin, () => launch(plugin.id))); }
    if (entries.length) {
      recent.append(make("h3", "", "已打开"));
      for (const row of entries) {
        const button = make("button", "wb-start-recent-item"); button.type = "button"; button.title = row.title;
        button.append(icon(workbenchTools.find(tool => tool.id === row.type)?.icon || "file"), make("span", "", row.title)); button.onclick = () => void activate(() => resume(row.id)); recent.append(button);
      }
    }
  };
  refresh(); return { refresh };
}
