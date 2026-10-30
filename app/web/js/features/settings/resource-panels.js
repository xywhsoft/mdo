import { createModelConfigPanel } from "./model-config-panel.js";
import { createExtensionPanels } from "./extension-panels.js";
import { createPluginWorkbench } from "./plugin-workbench.js";

export function createResourcePanels() {
  const extensionPanels = createExtensionPanels();
  const modelPanel = createModelConfigPanel(document.querySelector("#settings-models-list"));
  const pluginPanel = createPluginWorkbench(document.querySelector("#settings-extensions-list"));
  return Object.freeze({
    hasPendingChanges: () => extensionPanels.hasPendingChanges() || pluginPanel.hasPendingChanges(),
    selectSection(section) {
      extensionPanels.selectSection(section);
      pluginPanel.selectSection(section);
      if (section === "models") void modelPanel.ensureLoaded();
    },
    destroy() { pluginPanel.destroy(); extensionPanels.destroy(); modelPanel.destroy(); },
  });
}
