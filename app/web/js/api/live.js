import { targetLiveSocket } from "./target.js";

// One authenticated, same-origin connection per page. Commands remain HTTP;
// this channel only subscribes to events and announces changed resources.
// Injected dependencies let reconnect/visibility behavior run without a browser.
export function createLiveConnection({
  socket = targetLiveSocket,
  url = () => {
    const target = new URL("/api/v1/live", location.href);
    target.protocol = target.protocol === "https:" ? "wss:" : "ws:";
    return target.href;
  },
  setTimer = (...args) => window.setTimeout(...args),
  clearTimer = (id) => window.clearTimeout(id),
  random = Math.random,
} = {}) {
  const listeners = new Set();
  let connection = null;
  let token = "";
  let healthy = false;
  let paused = false;
  let retry = 0;
  let watchdog = 0;
  let attempts = 0;
  let selection = 0;
  let selected = { projectId: "", sessionId: "", cursor: () => 0 };
  const publish = (event) => { for (const listener of listeners) listener(event); };
  function status(value) {
    if (healthy === value) return;
    healthy = value;
    publish({ type: "status", connected: healthy });
  }
  function close() {
    clearTimer(retry); retry = 0;
    clearTimer(watchdog); watchdog = 0;
    const previous = connection;
    connection = null;
    previous?.close();
    status(false);
  }
  function send(value) {
    if (connection?.readyState !== 1) return false;
    try { connection.send(JSON.stringify(value)); return true; }
    catch { fail(connection); return false; }
  }
  function subscribeSelection() {
    if (!healthy) return;
    const cursor = Number(selected.cursor());
    send({ type: "subscribe", project_id: selected.projectId,
      session_id: selected.sessionId, after: Number.isSafeInteger(cursor) && cursor >= 0 ? cursor : 0,
      selection });
  }
  function reconnect() {
    if (!token || paused || retry) return;
    const delay = Math.min(500 * (2 ** Math.min(attempts++, 5)), 15_000);
    retry = setTimer(() => { retry = 0; connect(); }, delay + Math.round(random() * delay * 0.2));
  }
  function fail(current) {
    if (current !== connection) return;
    close(); reconnect();
  }
  function armWatchdog(current, delay = 45_000) {
    clearTimer(watchdog);
    watchdog = setTimer(() => fail(current), delay);
  }
  function connect() {
    if (!token || paused || connection) return;
    let current;
    try { current = socket(url(), ["mdo.live.v1", `mdo.token.${token}`]); }
    catch { reconnect(); return; }
    connection = current;
    armWatchdog(current, 10_000);
    current.onmessage = ({ data }) => {
      if (current !== connection || typeof data !== "string") return;
      let event;
      try { event = JSON.parse(data); } catch { fail(current); return; }
      if (!event || typeof event !== "object") { fail(current); return; }
      armWatchdog(current);
      if (event.type === "ready") {
        if (event.version !== 1) { fail(current); return; }
        attempts = 0;
        // The ready snapshot starts a fresh subscription even without a route
        // change. Read the cursor now, after any offline fallback requests.
        status(true);
        subscribeSelection();
      } else if (event.type === "ping") send({ type: "pong" });
      else if (healthy && event.type === "events") {
        if (event.selection === selection && event.project_id === selected.projectId &&
            event.session_id === selected.sessionId) publish(event);
      } else if (healthy && event.type === "changed") publish(event);
    };
    current.onerror = () => fail(current);
    current.onclose = () => fail(current);
  }
  return {
    isConnected: () => healthy,
    subscribe(listener) { listeners.add(listener); return () => listeners.delete(listener); },
    start(value) {
      if (!/^[0-9a-f]{32}-(0|[1-9][0-9]{0,19})$/.test(value ?? "")) return;
      if (token && token !== value) close();
      token = value;
      connect();
    },
    select(projectId, sessionId, cursor = () => 0) {
      selected = { projectId, sessionId, cursor };
      selection += 1;
      subscribeSelection();
    },
    pause(value) { paused = Boolean(value); if (paused) close(); else connect(); },
    stop() { token = ""; close(); },
  };
}

export const liveConnection = createLiveConnection();
