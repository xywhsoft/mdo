import { clear, element } from "../../utils/dom.js";
import { subscribeLocale, t } from "../../i18n.js";
import { applyAgentProfileDefaults, fillAgentOptions,
  fillReasoningOptions, projectProfileDefaults } from "./composer-profile.js";

// The configured-task dialog has its own choices. Changing the target project
// selects that project's defaults; editing the model afterwards remains manual.
export function createNewSessionProfile({ projectInput, projectOptions,
  agentSelect, modelSelect, reasoningSelect, permissionSelect,
  projectsStore, agentsStore, modelsStore, currentSelection }) {
  function catalog() { return modelsStore.get().data ?? { models: [] }; }
  function agents() { return agentsStore.get().data?.items ?? []; }
  function projects() { return projectsStore.get().data?.items ?? []; }

  function fillCatalogSelects() {
    const selectedModel = modelSelect.value;
    fillAgentOptions(agentSelect, agents());
    clear(modelSelect);
    for (const model of catalog().models ?? []) {
      const suffix = model.free ? t("model.freeSuffix", {}, " · 免费") : "";
      modelSelect.append(element("option", {
        text: `${model.name || model.id}${suffix}`,
        attrs: { value: model.id },
      }));
    }
    if (selectedModel && [...modelSelect.options].some((option) =>
      option.value === selectedModel)) modelSelect.value = selectedModel;
    const model = (catalog().models ?? []).find((item) =>
      item.id === modelSelect.value);
    fillReasoningOptions(reasoningSelect, model, reasoningSelect.value);
  }

  function fillProjectOptions() {
    clear(projectOptions);
    projectOptions.append(element("option", {
      text: t("nav.defaultProject", {}, "默认项目"),
      attrs: { value: "default" },
    }));
    for (const project of projects()) {
      if (project.id === "default") continue;
      projectOptions.append(element("option", {
        text: project.name || project.id,
        attrs: { value: project.id },
      }));
    }
  }

  function applyAgentDefaults(fallback) {
    const agent = agents().find((item) => item.id === agentSelect.value);
    applyAgentProfileDefaults({ agent, fallback, models: catalog().models ?? [],
      modelSelect, reasoningSelect, permissionSelect });
  }
  function applyProjectDefaults() {
    applyAgentDefaults(projectProfileDefaults(projectInput.value,
      projects(), agents(), catalog()));
  }

  agentSelect.addEventListener("change", applyProjectDefaults);
  function onProjectInput() {
    if (projectInput.value === "default" || projects().some((project) =>
      project.id === projectInput.value)) applyProjectDefaults();
  }
  projectInput.addEventListener("input", onProjectInput);
  projectInput.addEventListener("change", applyProjectDefaults);
  function onModelChange() {
    const model = (catalog().models ?? []).find((item) =>
      item.id === modelSelect.value);
    fillReasoningOptions(reasoningSelect, model,
      model?.default_reasoning_effort);
  }
  modelSelect.addEventListener("change", onModelChange);
  const unsubscribers = [agentsStore.subscribe(fillCatalogSelects),
    modelsStore.subscribe(fillCatalogSelects),
    projectsStore.subscribe(fillProjectOptions),
    subscribeLocale(fillCatalogSelects), subscribeLocale(fillProjectOptions)];
  return Object.freeze({
    resetForOpen() { applyAgentDefaults(currentSelection()); },
    destroy() {
      unsubscribers.forEach((unsubscribe) => unsubscribe());
      agentSelect.removeEventListener("change", applyProjectDefaults);
      projectInput.removeEventListener("input", onProjectInput);
      projectInput.removeEventListener("change", applyProjectDefaults);
      modelSelect.removeEventListener("change", onModelChange);
    },
  });
}
