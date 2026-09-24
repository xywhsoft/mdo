import { resourceId } from "../../api/client.js";
import { element } from "../../utils/dom.js";

// A new task has no session yet. Its project lives in the URL so refreshing a
// draft does not silently move the eventual session back to "default".
export function createComposerProject({ select, navigation, projectsStore,
  sessionsStore }) {
  let optionKey = "";

  function addId(ids, value) {
    try { ids.add(resourceId(value, "project")); }
    catch { /* Catalog errors must not make the picker unusable. */ }
  }

  function sync() {
    const route = navigation.get();
    const current = route.projectId || "default";
    const ids = new Set(["default"]);
    addId(ids, current);
    for (const project of projectsStore.get().data?.items ?? [])
      addId(ids, project.id);
    for (const session of sessionsStore.get().data?.items ?? [])
      addId(ids, session.project_id);
    const projects = new Map((projectsStore.get().data?.items ?? [])
      .map((project) => [project.id, project]));
    const ordered = ["default", ...[...ids].filter((id) => id !== "default")
      .sort((a, b) => a.localeCompare(b, "zh-CN"))];
    const nextKey = ordered.map((id) => `${id}:${projects.get(id)?.name ?? ""}`).join("\n");
    if (nextKey !== optionKey) {
      select.replaceChildren(...ordered.map((id) => element("option", {
        text: id === "default" ? "默认项目" : projects.get(id)?.name || id,
        attrs: { value: id },
      })));
      optionKey = nextKey;
    }
    select.value = current;
    select.hidden = route.view !== "workspace" || Boolean(route.sessionId);
  }

  select.addEventListener("change", () => {
    navigation.newTask(select.value, { replace: true });
  });
  projectsStore.subscribe(sync);
  sessionsStore.subscribe(sync);
  navigation.subscribe(sync);
  return Object.freeze({ sync });
}
