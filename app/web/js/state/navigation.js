import { resourceId } from "../api/client.js";

const listeners = new Set();
let current = Object.freeze({ view: "workspace", projectId: "", sessionId: "", settingsSection: "" });

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

function publish() {
  current = Object.freeze(parseHash());
  if (current.view === "workspace")
    lastWorkspace = Object.freeze({ projectId: current.projectId, sessionId: current.sessionId });
  for (const listener of listeners) listener(current);
}

window.addEventListener("hashchange", publish);
publish();

export const navigation = Object.freeze({
  get: () => current,
  preferredProject: () => lastWorkspace.projectId || "default",
  subscribe(listener) {
    listeners.add(listener);
    listener(current);
    return () => listeners.delete(listener);
  },
  select(projectId, sessionId, options = {}) {
    const hash = `#/projects/${resourceId(projectId, "project")}/sessions/${resourceId(sessionId, "session")}`;
    if (options.replace) history.replaceState(null, "", hash);
    else location.hash = hash;
    publish();
  },
  newTask(projectId = lastWorkspace.projectId || "default", options = {}) {
    const hash = `#/projects/${resourceId(projectId, "project")}/new`;
    if (options.replace) history.replaceState(null, "", hash);
    else location.hash = hash;
    publish();
  },
  openSettings(section = "general") {
    location.hash = `#/settings/${resourceId(section, "settings section")}`;
    // Hash navigation survives a refresh, but module state does not. Remember
    // the workspace on this history entry so Close can return to its origin.
    const state = history.state && typeof history.state === "object"
      ? history.state : {};
    history.replaceState({ ...state, mdoWorkspace: lastWorkspace }, "");
    publish();
  },
  backToWorkspace() {
    if (lastWorkspace.sessionId)
      navigation.select(lastWorkspace.projectId, lastWorkspace.sessionId);
    else if (lastWorkspace.projectId) navigation.newTask(lastWorkspace.projectId);
    else navigation.clear();
  },
  clear() {
    history.replaceState(null, "", `${location.pathname}${location.search}#/`);
    publish();
  },
});
