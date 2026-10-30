import { setUiText as writeUiText, uiMessage, t as uiText } from "../../i18n.js";

import { api } from "../../api/client.js";
import { targetState, subscribeTarget } from "../../api/target.js";
import { element, clear, errorMessage } from "../../utils/dom.js";
import { expandSkillAuthoringDraft } from "./skill-authoring.js";
import { customizableResource, storeReadOnlyMessage } from "./resource-credits.js";
import { openLayer } from "../../components/layers.js";
import { renderProjectExtensions } from "./project-extension-selector.js";
import { directoryId } from "./directory-documents.js";

const targetKey = () => targetState().selected?.id || "local";
const portable = value => directoryId(value,64);

export function createSkillWorkbench({ onChange, onShare, onEdit=()=>{}, owner: pageOwner }) {
  let dialog = null, controller = null, stopTarget = null;
  function close(force = false) {
    if (!dialog) return true;
    if (!force && dialog._dirty?.() && !window.confirm(uiText("ui.skillWorkbench.discardUnsavedSkillContent", {}, "放弃未保存的 Skill 内容？"))) return false;
    if (!force && dialog._busy?.()) return false;
    dialog.dispose({ restoreFocus: true }); return true;
  }
  async function open({ builder: showBuilder = false, mode: initialMode = "create", id: initialId = "", scope: initialScope = "user", owner: pageRoot = pageOwner } = {}) {
    if (dialog) { dialog.closeButton.focus(); return; }
    const owner = targetKey(), requestController = new AbortController(); controller = requestController;
    const live = () => dialog === view && controller === requestController && targetKey() === owner && !requestController.signal.aborted;
    const options = () => ({ signal: requestController.signal });
    const view = openLayer({ title: showBuilder ? uiMessage("ui.skillWorkbench.skillBuilder", {}, "Skill 构建器") : uiMessage("ui.skillWorkbench.skillSourcesAndProjects", {}, "Skill 来源与项目管理"), icon: "spark", width: "52rem", height: "80%", owner: pageRoot,
      beforeClose: () => !busy && (!dirty() || window.confirm(uiText("ui.skillWorkbench.discardUnsavedSkillContent", {}, "放弃未保存的 Skill 内容？"))),
      onClosed() { requestController.abort(); if (dialog === view) { stopTarget?.(); stopTarget = null; controller = null; dialog = null; } } });
    dialog = view; view.element.classList.add("extension-authoring", "skill-workbench");
    const project = element("select", { attrs: { "aria-label": uiMessage("nav.projects", {}, "项目") } }, [element("option", { text: uiMessage("nav.defaultProject", {}, "默认项目"), attrs: { value: "default" } })]);
    const scope = element("select", { attrs: { "aria-label": uiMessage("ui.skillWorkbench.saveScope", {}, "保存范围") } }, [
      element("option", { text: uiMessage("ui.skillWorkbench.homeSkillsDirectory", {}, "Home 技能目录"), attrs: { value: "user" } })]);
    project.value = "default"; scope.value = "user";
    const status = element("p", { attrs: { role: "status" } });
    const location = element("p", { className: "extension-help" });
    const sources = element("div", { className: "extension-list" }), owned = element("div", { className: "extension-list" });
    const editor = element("div", { className: "skill-workbench-editor" });
    const builder = element("div", { className: "skill-workbench-editor" });
    const sourceSection = element("section");
    let busy = false, baseline = "", sourceControl = null, idControl = null, revision = "new", editingProject = "", roots = {};
    let builderFields = null;
    const editorDirty = () => sourceControl && sourceControl.value !== baseline;
    const builderDirty = () => builderFields && (builderFields.request.value || builderFields.id.value !== builderFields.initialId || builderFields.mode.value !== builderFields.initialMode);
    const dirty = () => editorDirty() || builderDirty();
    view._dirty = dirty; view._busy = () => busy; view._pending = () => busy || dirty();
    const button = (text, action) => {
      const node = element("button", { className: "secondary-button", text, attrs: { type: "button" } });
      node.addEventListener("click", () => { void run(action); }); return node;
    };
    const controls = () => {
      if (!live()) return;
      view.element.querySelectorAll("button,select").forEach(node => { node.disabled = busy; });
      if (idControl) idControl.disabled = busy || revision !== "new";
      if (sourceControl) sourceControl.readOnly = busy;
      if (builderFields) { builderFields.id.disabled = busy; builderFields.request.readOnly = busy; }
    };
    async function run(action) {
      if (busy || !live()) return;
      busy = true; controls(); writeUiText(status, "");
      try { await action(); }
      catch (error) { if (live() && error?.name !== "AbortError") writeUiText(status, errorMessage(error)); }
      finally { busy = false; if (live()) controls(); }
    }
    const endpoint = path => `${path}?project=${encodeURIComponent(project.value)}`;
    const resetEditor = () => { clear(editor); sourceControl = idControl = null; baseline = ""; };
    const dismiss = element("button", { className: "secondary-button", text: uiMessage("context.close", {}, "关闭"), attrs: { type: "button" } });
    dismiss.addEventListener("click", () => { void view.close("cancel"); });
    function footer(title, actions = []) {
      view.update({ title }); view.footer.replaceChildren(...actions, dismiss); controls();
      sourceSection.hidden = actions.length > 0;
    }
    function showSources() {
      if (!discardEditor() || builderDirty() && !window.confirm(uiText("ui.skillWorkbench.discardTheUnsentBuildRequest", {}, "放弃未发送的构建需求？"))) return;
      resetEditor(); clear(builder); builderFields = null; footer(uiMessage("ui.skillWorkbench.skillSourcesAndProjects", {}, "Skill 来源与项目管理"));
    }
    async function load() {
      const selected = project.value;
      roots = {}; writeUiText(location, uiMessage("ui.skillWorkbench.readingSkillsDirectory", {}, "正在读取技能目录…")); clear(sources); clear(owned);
      const all=await api.get(endpoint("/skills/sources"),options());
      if(!live()||selected!==project.value)return;
      roots=all.data;writeUiText(location, uiMessage("ui.skillWorkbench.homeSkillsValue1ProjectsOnlySelectEnabledSkillsAnd", { value1: roots.user_directory||uiMessage("ui.skillWorkbench.unavailable", {}, "不可用") }, "Home 技能目录：{value1}；项目仅选择启停，不另存源码。"));
      await renderProjectExtensions(owned,{kind:"skills",project:selected,signal:requestController.signal,isCurrent:()=>live()&&selected===project.value,onChange});
      for (const item of all.data.items || []) sources.append(element("article", { className: "resource-card" }, [
        element("h3", { text: `${item.name || item.id} · ${item.origin}/${item.provider}` }),
        element("code", { text: item.source_path }),
        ...(customizableResource(item)?[button(uiMessage("ui.skillWorkbench.importToHome", {}, "导入到 Home"), async () => {
          const targetId = window.prompt(uiText("ui.skillWorkbench.newSkillIdExistingDestinationsAreNotOverwritten", {}, "新 Skill ID（目标已存在时拒绝覆盖）"), item.id);
          if (targetId === null) return;
          if (!portable(targetId)) throw new Error(uiText("ui.skillWorkbench.idMustStartWithALowercaseLetterAndContain", {}, "ID 必须以小写字母开头，仅含小写字母、数字、点、下划线或连字符，最多 64 个字符"));
          const destination = roots.user_directory;
          if (!window.confirm(uiText("ui.skillWorkbench.copyValue1ToValue2Value3OriginalFilesAreRetained", { value1: item.source_path, value2: destination, value3: targetId }, "复制 {value1} 到 {value2}/{value3}？\n原文件会保留，脚本不会执行。"))) return;
          await api.post(endpoint("/skills/migrate"), { origin: item.origin, provider: item.provider, id: item.id, target_id: targetId, scope: "user" }, options());
          if (live()) { onChange(); await load(); writeUiText(status, uiMessage("ui.skillWorkbench.theEntireDirectoryWasImportedToHomeEditAnd", {}, "完整目录已导入 Home，可在本机列表编辑并启用。")); }
        })]:[element("p",{className:"extension-help",text:storeReadOnlyMessage()})]),
      ]));
      if (all.data.truncated) sources.append(element("p", { className: "extension-help", text: uiMessage("ui.skillWorkbench.onlyTheFirst128SourcesAreShownUseFolder", {}, "来源较多，仅显示前 128 项；可通过原有文件夹导入入口选择其他技能。") }));
      for (const issue of all.data.diagnostics || []) sources.append(element("p", { className: "extension-error", text: `${issue.source_path || `${issue.origin}/${issue.provider}`}：${issue.message}` }));
    }
    const discardEditor = () => !editorDirty() || window.confirm(uiText("ui.skillWorkbench.discardUnsavedSkillContent", {}, "放弃未保存的 Skill 内容？"));
    function showReport(report) {
      if (typeof report?.valid !== "boolean" || !Array.isArray(report.errors) || !Array.isArray(report.warnings))
        throw new Error(uiText("ui.skillWorkbench.skillValidationReturnedAnInvalidResult", {}, "Skill 校验返回了无效结果"));
      writeUiText(status, [report.valid ? (report.resources_checked ? uiMessage("ui.skillWorkbench.formatAndDirectoryResourcesPassedValidation", {}, "格式与目录资源检查通过。") : uiMessage("ui.skillWorkbench.formatPassedValidationActualDirectoryResourcesHaveNotBeen", {}, "格式检查通过；尚未检查实际目录资源。")) : uiMessage("ui.skillWorkbench.validationFailedCorrectTheIssuesAndRetry", {}, "检查未通过，请修正后重试。"),
        ...report.errors, ...report.warnings, uiMessage("ui.skillWorkbench.thisCheckDidNotExecuteScriptsOrTestModel", {}, "此检查未执行脚本或模型行为测试。")].join("\n"));
    }
    function openBuilder({ mode = "create", id = "", saveScope } = {}) {
      if (!discardEditor()) return;
      if (builderDirty() && !window.confirm(uiText("ui.skillWorkbench.discardTheUnsentBuildRequest", {}, "放弃未发送的构建需求？"))) return;
      scope.value="user";
      resetEditor();
      clear(builder);
      const taskMode = element("select", { attrs: { "aria-label": uiMessage("ui.skillWorkbench.buildMode", {}, "构建模式") } }, [
        element("option", { text: uiMessage("ui.skillWorkbench.newSkill", {}, "新建 Skill"), attrs: { value: "create" } }),
        element("option", { text: uiMessage("ui.skillWorkbench.improveExistingSkill", {}, "改进已有 Skill"), attrs: { value: "improve" } }),
        element("option", { text: uiMessage("ui.skillWorkbench.inspectSkillReadOnly", {}, "只读检查 Skill"), attrs: { value: "review" } })]);
      taskMode.value = mode;
      const skillId = element("input", { attrs: { "aria-label": uiMessage("ui.skillWorkbench.buildSkillId", {}, "构建 Skill ID"), placeholder: uiMessage("ui.skillWorkbench.skillIdRequiredForImproveInspectOptionalForNew", {}, "Skill ID（改进/检查必填，新建可选）"), maxlength: 64 } });
      skillId.value = id;
      const requirements = element("textarea", { attrs: { "aria-label": uiMessage("ui.skillWorkbench.buildRequest", {}, "构建需求"), rows: 5, placeholder: uiMessage("ui.skillWorkbench.taskActivationScenariosSampleOutputOrIssuesToImprove", {}, "希望解决的任务、触发场景、输出样例，或已有技能需要改进的问题") } });
      builderFields = { mode: taskMode, id: skillId, request: requirements, initialMode: mode, initialId: id };
      builder.append(element("p", { text: uiMessage("ui.skillWorkbench.chooseAModeAndDescribeYourRequestASeparate", {}, "选择模式并描述需求。将打开独立对话，准备好草稿，由你发送后开始；现有对话保持不变。目标以当前保存范围和项目为准。") }),
        element("label", { className: "extension-field" }, [element("span", { text: uiMessage("ui.skillWorkbench.buildMode", {}, "构建模式") }), taskMode]),
        element("label", { className: "extension-field" }, [element("span", { text: "Skill ID" }), skillId]),
        element("label", { className: "extension-field" }, [element("span", { text: uiMessage("ui.skillWorkbench.buildRequest", {}, "构建需求") }), requirements]));
      footer(uiMessage("ui.skillWorkbench.skillBuilder", {}, "Skill 构建器"), [button(uiMessage("ui.skillWorkbench.backToSources", {}, "返回来源"), showSources), button(uiMessage("ui.skillWorkbench.openBuilderConversation", {}, "打开构建对话"), async () => {
          if (!discardEditor()) return;
          const projectId = project.value, selectedScope = scope.value, selectedMode = taskMode.value;
          const text = await expandSkillAuthoringDraft({ directory: roots.user_directory,
            scope: selectedScope, mode: selectedMode, id: skillId.value, request: requirements.value, projectId }, options());
          if (!live() || project.value !== projectId || scope.value !== selectedScope) return;
          close(true);
          window.dispatchEvent(new CustomEvent("mdo-create-skill", { detail: { text, projectId, target: owner,
            title: ({ create: uiText("ui.skillWorkbench.createSkill", {}, "创建 Skill"), improve: uiText("ui.skillWorkbench.improveSkill", {}, "改进 Skill"), review: uiText("ui.skillWorkbench.checkSkill", {}, "检查 Skill") })[selectedMode] } }));
        })]);
      requirements.focus(); controls();
    }
    let previous = "default";
    project.addEventListener("change", () => {
      if (!discardEditor() || builderDirty() && !window.confirm(uiText("ui.skillWorkbench.changeTheBuilderConversationSProjectYourRequestIs", {}, "更换构建对话所属项目？当前需求将保留。"))) { project.value = previous; return; }
      previous = project.value;
      if (sourceControl) { resetEditor(); footer(uiMessage("ui.skillWorkbench.skillSourcesAndProjects", {}, "Skill 来源与项目管理")); }
      void run(load);
    });
    sourceSection.append(element("h3", { text: uiMessage("ui.skillWorkbench.projectSkills", {}, "项目技能") }), owned, element("h3", { text: uiMessage("ui.skillWorkbench.importableSkillSources", {}, "可迁移的技能来源") }), sources);
    view.body.append(element("p", { text: uiMessage("ui.skillWorkbench.browseThirdPartySkillsOnThisDeviceAndExplicitly", {}, "浏览当前设备的第三方技能来源，显式导入完整目录到 Home。项目只选择 Home 技能的启停；AI 构建也写入 Home。") }),
      element("div", { className: "extension-toolbar" }, [project, scope,
        button(uiMessage("ui.skillWorkbench.refreshSources", {}, "刷新来源"), load), button(uiMessage("ui.skillWorkbench.newHomeSkill", {}, "新建 Home Skill"), async () => { if(discardEditor()){close(true);await onEdit();} }),
        button(uiMessage("ui.skillWorkbench.writeSkillWithAi", {}, "用 AI 编写 Skill"), () => openBuilder())]), location, status, builder, editor, sourceSection);
    footer(uiMessage("ui.skillWorkbench.skillSourcesAndProjects", {}, "Skill 来源与项目管理"));
    stopTarget = subscribeTarget(() => { if (targetKey() !== owner) close(true); });
    await run(async () => {
      const response = await api.get("/projects", options()); if (!live()) return;
      for (const item of response.data.items || []) if (item.id !== "default") project.append(element("option", { text: item.name || item.id, attrs: { value: item.id } }));
      await load();
    });
    if (live() && showBuilder) { scope.value = "user"; openBuilder({ mode: initialMode, id: initialId }); }
  }
  return { open, close, hasPendingChanges: () => Boolean(dialog?._pending?.()), destroy: () => close(true) };
}
