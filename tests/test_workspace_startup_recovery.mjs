import assert from "node:assert/strict";
import test from "node:test";
import { startWorkspaceNavigation } from "../app/web/js/features/shell/workspace-startup.js";
import { createResourceStore } from "../app/web/js/state/store.js";
import { createRequestRecovery } from "../app/web/js/api/request-recovery.js";

const settle = () => new Promise(setImmediate);
const session = { project_id: "default", id: "saved", status: "active", title: "Saved" };
const alternate = { ...session, id: "other", title: "Other" };
const response = data => Response.json({ ok: true, data });
const failed = status => Response.json({ ok: false, error: { code: "network_error", message: "fixture" } }, { status });
async function until(predicate) {
  for (let i = 0; i < 100; i++) { if (predicate()) return; await new Promise(r => setTimeout(r, 2)); }
  assert.fail("Expected finite recovery did not settle");
}
function fixture({ hash = "", saved = { project_id: "default", session_id: "saved" } } = {}) {
  const previous = { fetch: globalThis.fetch, location: globalThis.location,
    document: globalThis.document, window: globalThis.window };
  const events = new EventTarget(), notices = [], requests = [], listeners = new Set();
  const body = { isConnected: true };
  globalThis.window = events;
  events.setTimeout = () => 0;
  globalThis.location = { hash };
  globalThis.document = { body, activeElement: body,
    querySelector: () => ({ append(node) { notices.push(node.textContent); } }),
    createElement: () => ({ setAttribute() {}, remove() {} }) };
  let route = { view: "workspace", projectId: hash ? "default" : "", sessionId: hash ? "chosen" : "" };
  const navListeners = new Set();
  const navigation = {
    get: () => route,
    subscribe(fn) { navListeners.add(fn); fn(route); return () => navListeners.delete(fn); },
    select(projectId, sessionId) {
      if (route.projectId === projectId && route.sessionId === sessionId) return;
      route = { view: "workspace", projectId, sessionId };
      for (const fn of navListeners) fn(route);
    },
    newTask(projectId) { this.select(projectId, ""); }, clear() { this.select("", ""); },
  };
  let readable = true, writable = true, factories = 0;
  const f = { events, notices, requests, navigation, listeners,
    get factories() { return factories; },
    get saved() { return saved; },
    set writable(value) { writable = value; },
    set readable(value) { readable = value; },
    read: async path => response(path.endsWith("workspace-state") ? saved : session),
    write: async (_path, options) => { saved = JSON.parse(options.body); return response(saved); },
    restore() { events.dispatchEvent(new Event("pagehide")); Object.assign(globalThis, previous); },
  };
  globalThis.fetch = async (url, options) => {
    const path = String(url), method = options.method;
    requests.push({ path, method, signal: options.signal, body: options.body });
    return method === "GET" ? f.read(path, options) : f.write(path, options);
  };
  f.open = () => startWorkspaceNavigation({ navigation,
    settingsStore: { get: () => ({ data: { workspace: { open_mode: "last" } } }) },
    sessionsStore: { get: () => ({ data: { items: [alternate] } }) },
    sessionDetailStore: createResourceStore(), dialog: {}, title: {}, continueButton: {}, newButton: {},
    prompt: { disabled: false, focus() {} }, entryHash: hash,
    canPersistSelection: () => writable, shouldRestore: () => readable,
    eventTarget: events, onNotice: message => notices.push(message),
    subscribeWritable(fn) { listeners.add(fn); fn(); return () => listeners.delete(fn); },
    createRecovery(options) {
      factories++;
      return createRequestRecovery({ ...options, random: () => 0,
        setTimer: (fn, delay) => setTimeout(fn, Math.min(delay, 5)), clearTimer: clearTimeout });
    },
  });
  return f;
}

test("saved selection and candidate share quiet read recovery", async () => {
  const f = fixture(); let savedReads = 0, details = 0;
  f.read = async path => path.endsWith("workspace-state")
    ? ++savedReads <= 2 ? failed(503) : response(f.saved)
    : ++details <= 2 ? failed(429) : response(session);
  try {
    await f.open();
    assert.equal(savedReads, 3); assert.equal(details, 3);
    assert.equal(f.navigation.get().sessionId, "saved");
    assert.deepEqual(f.notices, []); assert.equal(f.factories, 1);
  } finally { f.restore(); }
});

test("an explicit task never reports obsolete startup read failures", async () => {
  const f = fixture({ hash: "#/projects/default/sessions/chosen" });
  f.read = async () => failed(503);
  try {
    await f.open(); await until(() => f.requests.some(r => r.method === "PUT"));
    assert.equal(f.requests.filter(r => r.method === "GET").length, 6);
    assert.deepEqual(f.notices, []); assert.equal(f.saved.session_id, "chosen");
  } finally { f.restore(); }
});

test("exhausted startup reads give one final notice and usable catalog fallback", async () => {
  const f = fixture(); f.writable = false; f.read = async () => failed(503);
  try {
    await f.open();
    assert.equal(f.requests.length, 6); assert.equal(f.notices.length, 1);
    assert.equal(f.navigation.get().sessionId, "other");
  } finally { f.restore(); }
});

test("an unreadable startup fallback preserves the original saved choice until user navigation", async () => {
  const f = fixture(); f.read = async () => failed(503);
  try {
    await f.open(); await settle();
    assert.equal(f.navigation.get().sessionId, "other");
    assert.equal(f.saved.session_id, "saved");
    assert.equal(f.requests.filter(r => r.method === "PUT").length, 0);
    f.navigation.newTask("default"); f.navigation.select("default", "other");
    await until(() => f.saved.session_id === "other");
    assert.equal(f.requests.filter(r => r.method === "PUT").length, 1);
  } finally { f.restore(); }
});

test("a deleted saved task uses the catalog without a network error", async () => {
  const f = fixture(); f.writable = false;
  f.read = async path => path.endsWith("workspace-state") ? response(f.saved) : failed(404);
  try { await f.open(); assert.equal(f.navigation.get().sessionId, "other"); assert.deepEqual(f.notices, []); }
  finally { f.restore(); }
});

test("a mismatched candidate cannot restore a different task under the saved identity", async () => {
  const f = fixture(); f.writable = false;
  f.read = async path => response(path.endsWith("workspace-state") ? f.saved : { ...session, id: "foreign" });
  try { await f.open(); assert.equal(f.navigation.get().sessionId, "other"); assert.equal(f.notices.length, 1); }
  finally { f.restore(); }
});

test("user intent wins during backoff and suppresses obsolete failures", async () => {
  const f = fixture(); f.writable = false; f.read = async () => failed(503);
  try {
    const opening = f.open(); await settle(); f.readable = false;
    f.navigation.newTask("chosen");
    await opening;
    assert.equal(f.navigation.get().projectId, "chosen"); assert.equal(f.notices.length, 0);
  } finally { f.restore(); }
});

test("page departure settles an adapter that ignores abort without navigation or feedback", async () => {
  const f = fixture(); f.read = async () => new Promise(() => {});
  try {
    let finished = false; const opening = f.open().then(() => { finished = true; });
    await settle(); f.events.dispatchEvent(new Event("pagehide"));
    await until(() => finished); await opening;
    assert.equal(f.requests[0].signal.aborted, true); assert.deepEqual(f.notices, []);
    assert.equal(f.navigation.get().sessionId, "");
  } finally { f.restore(); }
});

test("a lost accepted save is confirmed by quiet reads without another PUT", async () => {
  const f = fixture({ hash: "#/projects/default/sessions/chosen" });
  const write = f.write; let confirming = false, confirmations = 0;
  f.write = async (...args) => { await write(...args); confirming = true; throw new TypeError("reply lost"); };
  f.read = async () => confirming && ++confirmations <= 2 ? failed(503) : response(f.saved);
  try {
    await f.open(); await until(() => confirmations === 3);
    await settle();
    assert.equal(f.requests.filter(r => r.method === "PUT").length, 1);
    assert.equal(f.saved.session_id, "chosen"); assert.deepEqual(f.notices, []);
  } finally { f.restore(); }
});

test("a hung accepted save reaches bounded read confirmation", async () => {
  const f = fixture({ hash: "#/projects/default/sessions/chosen" }); const write = f.write;
  f.write = async (...args) => { await write(...args); return new Promise(() => {}); };
  try {
    await f.open(); await until(() => f.requests.filter(r => r.method === "GET").length === 2);
    await settle();
    assert.equal(f.requests.filter(r => r.method === "PUT").length, 1); assert.deepEqual(f.notices, []);
  } finally { f.restore(); }
});

test("a failed unconfirmed save reports once and is not replayed by writable notifications", async () => {
  const f = fixture({ hash: "#/projects/default/sessions/chosen" }); let written = false;
  f.write = async () => { written = true; throw new TypeError("offline"); };
  f.read = async () => written ? failed(503) : response(f.saved);
  try {
    await f.open(); await until(() => f.notices.length === 1);
    for (const fn of f.listeners) { fn(); fn(); }
    await settle();
    assert.equal(f.requests.filter(r => r.method === "PUT").length, 1);
    assert.equal(f.requests.filter(r => r.method === "GET").length, 7);
    assert.match(f.notices[0], /确认/);
  } finally { f.restore(); }
});

test("a newer destination survives slow acceptance of the previous save", async () => {
  const f = fixture({ hash: "#/projects/default/sessions/chosen" }); let release;
  const write = f.write;
  f.write = async (...args) => {
    const result = await write(...args);
    if (JSON.parse(args[1].body).session_id === "chosen") await new Promise(r => { release = r; });
    return result;
  };
  try {
    await f.open(); await until(() => Boolean(release));
    f.navigation.select("default", "latest"); release();
    await until(() => f.saved.session_id === "latest");
    assert.equal(f.requests.filter(r => r.method === "PUT").length, 2); assert.deepEqual(f.notices, []);
  } finally { f.restore(); }
});

test("a malformed saved selection cannot be replaced by an automatic fallback", async () => {
  const f = fixture(); f.read = async () => response({ project_id: "default", session_id: null });
  try {
    await f.open(); await settle();
    assert.equal(f.requests.length, 1); assert.equal(f.notices.length, 1);
    assert.equal(f.saved.session_id, "saved"); assert.equal(f.navigation.get().sessionId, "other");
  } finally { f.restore(); }
});

test("page departure cancels an uncertain save and cannot replay or report it", async () => {
  const f = fixture({ hash: "#/projects/default/sessions/chosen" });
  f.write = async () => new Promise(() => {});
  try {
    await f.open(); await until(() => f.requests.some(r => r.method === "PUT"));
    f.events.dispatchEvent(new Event("pagehide"));
    await new Promise(r => setTimeout(r, 30));
    assert.equal(f.requests.find(r => r.method === "PUT").signal.aborted, true);
    assert.equal(f.requests.length, 2); assert.deepEqual(f.notices, []);
    assert.equal(f.listeners.size, 0);
  } finally { f.restore(); }
});
