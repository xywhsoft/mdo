import { resourceId } from "../api/client.js";

const listeners = new Set();
let current = Object.freeze({ view: "workspace", projectId: "", sessionId: "", settingsSection: "" });
let newTaskGuard = null;

function workspaceFromHistory() {
  const saved = history.state?.mdoWorkspace;
  if (!saved || typeof saved !== "object") return null;
  try {
    const projectId = saved.projectId ? resourceId(saved.projectId, "project") : "";
    const sessionId = saved.sessionId ? resourceId(saved.sessionId, "session") : "";
    return sessionId && !projectId ? null : { projectId, sessionId };
  } catch { return null; }
}

let lastWorkspace = Object.freeze(workspaceFromHistory() ??
  { projectId: "", sessionId: "" });

function parseHash() {
  if (location.hash === "#/schedules")
    return { view: "schedules", projectId: "", sessionId: "", settingsSection: "" };
  const settings = /^#\/settings\/([a-z][a-z0-9-]*)$/.exec(location.hash);
  if (settings) return { view: "settings", projectId: "", sessionId: "", settingsSection: settings[1] };
  const newTask = /^#\/projects\/([^/]+)\/new$/.exec(location.hash);
  if (newTask) {
    try {
      return { view: "workspace", projectId: resourceId(newTask[1], "project"),
        sessionId: "", settingsSection: "" };
    } catch { /* An invalid project route falls back to a blank task. */ }
  }
  const match = /^#\/projects\/([^/]+)\/sessions\/([^/]+)$/.exec(location.hash);
  if (!match) return { view: "workspace", projectId: "", sessionId: "", settingsSection: "" };
  try {
    return {
      view: "workspace",
      projectId: resourceId(match[1], "project"),
      sessionId: resourceId(match[2], "session"),
      settingsSection: "",
    };
  } catch {
    return { view: "workspace", projectId: "", sessionId: "", settingsSection: "" };
  }
}

function publish({ refresh = false } = {}) {
  let next = parseHash();
  if (next.view === "workspace" && !next.sessionId && newTaskGuard) {
    const owner = newTaskGuard.owner();
    if (owner && owner !== next.projectId) {
      const projectId = resourceId(owner, "project");
      // A single portable new-task draft belongs to its original project.
      // Replace the attempted route so Back cannot revisit a false owner.
      history.replaceState(history.state, "", `#/projects/${projectId}/new`);
      // The home route has no requested project. Assigning its journal owner
      // is ordinary first-send navigation, not an attempted project switch.
      if (next.projectId) newTaskGuard.onRedirect?.(projectId);
      next = { ...next, projectId };
    }
  }
  const changed = Object.keys(current).some((key) => current[key] !== next[key]);
  if (!changed && !refresh) return;
  if (changed) current = Object.freeze(next);
  if (current.view === "workspace")
    lastWorkspace = Object.freeze({ projectId: current.projectId, sessionId: current.sessionId });
  // Reloading data in the current route is not leaving that route. Menus,
  // search, previews and in-progress forms listen only for real navigation.
  for (const subscription of listeners)
    if (changed || subscription.refresh) subscription.listener(current);
}

window.addEventListener("hashchange", publish);
publish();

function openPage(hash) {
  location.hash = hash;
  // Retain the conversation on this history entry so returning still works
  // after refreshing a utility page.
  const state = history.state && typeof history.state === "object"
    ? history.state : {};
  history.replaceState({ ...state, mdoWorkspace: lastWorkspace }, "");
  publish({ refresh: true });
}

export const navigation = Object.freeze({
  get: () => current,
  // Utility pages hide the composer without selecting a different draft.
  // In-flight attachment operations still belong to this retained workspace.
  workspace: () => lastWorkspace,
  preferredProject: () => lastWorkspace.projectId || "default",
  setNewTaskGuard(owner, onRedirect) {
    newTaskGuard = { owner, onRedirect };
  },
  revalidate: () => publish({ refresh: true }),
  subscribe(listener, { refresh = false } = {}) {
    const subscription = { listener, refresh };
    listeners.add(subscription);
    listener(current);
    return () => listeners.delete(subscription);
  },
  select(projectId, sessionId, options = {}) {
    const hash = `#/projects/${resourceId(projectId, "project")}/sessions/${resourceId(sessionId, "session")}`;
    if (options.replace) history.replaceState(null, "", hash);
    else location.hash = hash;
    publish({ refresh: true });
  },
  newTask(projectId = lastWorkspace.projectId || "default", options = {}) {
    const hash = `#/projects/${resourceId(projectId, "project")}/new`;
    if (options.replace) history.replaceState(null, "", hash);
    else location.hash = hash;
    publish({ refresh: true });
  },
  openSettings(section = "general") {
    openPage(`#/settings/${resourceId(section, "settings section")}`);
  },
  openSchedules: () => openPage("#/schedules"),
  backToWorkspace() {
    if (lastWorkspace.sessionId)
      navigation.select(lastWorkspace.projectId, lastWorkspace.sessionId);
    else if (lastWorkspace.projectId) navigation.newTask(lastWorkspace.projectId);
    else navigation.clear();
  },
  clear() {
    history.replaceState(null, "", `${location.pathname}${location.search}#/`);
    publish({ refresh: true });
  },
});
