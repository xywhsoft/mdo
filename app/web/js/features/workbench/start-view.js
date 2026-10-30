import { setUiText as writeUiText, setUiAttribute as writeUiAttribute, uiMessage, t as uiText, subscribeLocale } from "../../i18n.js";

import { element as make, icon } from "./controls.js";

export const workbenchTools = Object.freeze([
  { id: "mdo.browser", get title() { return uiText("ui.startView.browser", {}, "浏览器"); }, icon: "globe", get description() { return uiText("ui.startView.browseTheWebDebugAndPreviewApplications", {}, "浏览网页、调试和预览应用。"); } },
  { id: "mdo.files", get title() { return uiText("modelConfig.file", {}, "文件"); }, icon: "folder", get description() { return uiText("ui.startView.browseProjectFilesViewAndEditTheirContents", {}, "浏览项目文件、查看和编辑内容。"); } },
  { id: "mdo.terminal", get title() { return uiText("ui.terminalView.terminal", {}, "终端"); }, icon: "terminal", get description() { return uiText("ui.startView.runCommandsInTheProjectDirectory", {}, "在项目目录中运行命令。"); } },
  { id: "mdo.changes", get title() { return uiText("ui.startView.changes", {}, "变更"); }, icon: "review", get description() { return uiText("ui.startView.reviewCodeChangesAndDiffs", {}, "查看代码修改与差异。"); } },
]);

/** Launchers and recent records are supplied by the manager, so all actions
 * keep their original device/project/owner instead of using live navigation. */
export function mountStartView(root, { signal, context, launch, contributions, opened, resume }) {
  root.classList.add("wb-start-view");
  const heading = make("h2", "", uiMessage("ui.startView.newTab", {}, "新标签页")), scope = make("p", "wb-start-context"), grid = make("div", "wb-start-tools"), extra = make("section", "wb-start-extra"), recent = make("section", "wb-start-recent"), status = make("p", "wb-start-status");
  const project = context.workspace?.split(/[\\/]/).filter(Boolean).at(-1) || (context.project === "default" ? uiText("nav.defaultProject", {}, "默认项目") : context.project);
  writeUiText(scope, project || uiMessage("ui.startView.currentProject", {}, "当前项目")); writeUiAttribute(scope, "title", context.workspace || project || ""); status.setAttribute("role", "status");
  let busy = false, previous = "";
  const activate = async execute => {
    if (busy || signal.aborted) return; busy = true; root.setAttribute("aria-busy", "true"); writeUiText(status, "");
    try { await execute(); }
    catch (error) { if (!signal.aborted) writeUiText(status, error.message || String(error)); }
    finally { busy = false; root.removeAttribute("aria-busy"); }
  };
  const card = (tool, execute) => {
    const button = make("button", "wb-start-tool"), glyph = make("span", "wb-start-glyph"), text = make("span", "wb-start-label"), title = make("strong", "", tool.title), description = make("span", "", tool.description || "");
    button.type = "button"; writeUiAttribute(button, "aria-label", uiMessage("ui.startView.openValue1", { value1: tool.title }, "打开{value1}")); button.dataset.tool = tool.id;
    glyph.append(icon(tool.icon || "file")); text.append(title, description); button.append(glyph, text, make("span", "wb-start-open", uiMessage("ui.startView.open", {}, "打开")));
    button.onclick = () => void activate(execute); return button;
  };
  for (const tool of workbenchTools) grid.append(card(tool, () => launch(tool.id)));
  root.append(heading, scope, grid, status, extra, recent);
  const refresh = () => {
    if (signal.aborted) return;
    const plugins = contributions(), entries = opened(), identity = JSON.stringify([plugins.map(row => [row.id,row.title,row.description]), entries.map(row => [row.id,row.title,row.type])]);
    if (identity === previous) return; previous = identity; extra.replaceChildren(); recent.replaceChildren();
    if (plugins.length) { extra.append(make("h3", "", uiMessage("settings.extensions", {}, "插件"))); for (const plugin of plugins) extra.append(card(plugin, () => launch(plugin.id))); }
    if (entries.length) {
      recent.append(make("h3", "", uiMessage("ui.startView.opened", {}, "已打开")));
      for (const row of entries) {
        const button = make("button", "wb-start-recent-item"); button.type = "button"; writeUiAttribute(button, "title", row.title);
        button.append(icon(workbenchTools.find(tool => tool.id === row.type)?.icon || "file"), make("span", "", row.title)); button.onclick = () => void activate(() => resume(row.id)); recent.append(button);
      }
    }
  };
  const stopLocale = subscribeLocale(() => {
    grid.replaceChildren(...workbenchTools.map(tool => card(tool, () => launch(tool.id))));
    previous = ""; refresh();
  });
  signal.addEventListener("abort", stopLocale, { once: true });
  refresh(); return { refresh };
}
