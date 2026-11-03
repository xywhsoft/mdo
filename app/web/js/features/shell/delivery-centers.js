import { createUpdatePanel } from "../update/update-panel.js";
import { createDownloadCenter } from "../downloads/center.js";
import { createNotificationCenter } from "../notifications/center.js";

// One owner wires the delivery surfaces and releases their subscriptions.
// App boot only supplies navigation and existing page entry points.
export function createDeliveryCenters({ updateRoot, navigation, openDevices, openAccount, page = window }) {
  const updatePanel = createUpdatePanel({ root: updateRoot, openDevices, entries: [] });
  const downloads = createDownloadCenter({ updatePanel, navigation });
  const notifications = createNotificationCenter({ updatePanel, navigation, openAccount, openDownloads: () => downloads?.open() });
  let destroyed = false;
  function destroy() {
    if (destroyed) return; destroyed = true;
    page.removeEventListener("pagehide", destroy);
    notifications.destroy(); downloads?.destroy(); updatePanel?.destroy();
  }
  page.addEventListener("pagehide", destroy, { once: true });
  return { refresh() { if (!destroyed) return notifications.refresh(); }, destroy };
}
