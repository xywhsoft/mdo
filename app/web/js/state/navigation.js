import { resourceId } from "../api/client.js";

const listeners = new Set();
let current = Object.freeze({ projectId: "", sessionId: "" });

function parseHash() {
  const match = /^#\/projects\/([^/]+)\/sessions\/([^/]+)$/.exec(location.hash);
  if (!match) return { projectId: "", sessionId: "" };
  try {
    return {
      projectId: resourceId(match[1], "project"),
      sessionId: resourceId(match[2], "session"),
    };
  } catch {
    return { projectId: "", sessionId: "" };
  }
}

function publish() {
  current = Object.freeze(parseHash());
  for (const listener of listeners) listener(current);
}

window.addEventListener("hashchange", publish);
publish();

export const navigation = Object.freeze({
  get: () => current,
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
  clear() {
    history.replaceState(null, "", `${location.pathname}${location.search}#/`);
    publish();
  },
});
