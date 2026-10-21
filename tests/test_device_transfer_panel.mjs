import assert from "node:assert/strict";
import test from "node:test";

class Node {
  constructor(tag) { this.tag = tag; this.children = []; this.listeners = new Map(); this.hidden = false; this.disabled = false; this.value = ""; this.open = false; this.isConnected = true; }
  setAttribute(key, value) { this[key] = key === "hidden" ? true : value; }
  append(...children) { this.children.push(...children); if (this.tag === "select" && !this.value) this.value = this.children[0]?.value || ""; }
  replaceChildren(...children) { this.children = children; this.value = ""; }
  addEventListener(key, callback) { this.listeners.set(key, callback); }
  get options() { return this.children; }
  showModal() { this.open = true; }
  close() { this.open = false; this.listeners.get("close")?.(); }
  focus() {}
  async click() { if (!this.disabled) await this.listeners.get("click")?.(); }
}
globalThis.document = { body: new Node("body"), createElement: tag => new Node(tag), activeElement: null };
globalThis.window = { addEventListener() {} };
globalThis.sessionStorage = { getItem() { return null; } };
const { createDeviceTransferPanel } = await import("../app/web/js/features/transfers/device-transfer-panel.js");
const originalSet = globalThis.setTimeout, originalClear = globalThis.clearTimeout;
const timers = new Map(); let timerId = 0;
globalThis.setTimeout = callback => { timers.set(++timerId, callback); return timerId; };
globalThis.clearTimeout = key => timers.delete(key);
const flush = async () => { for (let i = 0; i < 15; i++) await new Promise(setImmediate); };
const all = node => [node, ...node.children.flatMap(all)];
const find = text => all(document.body).find(node => node.textContent === text);
const device = "b".repeat(32), transfer = "a".repeat(32), other = "c".repeat(32);
const reply = data => Response.json({ ok: true, data });

test("transfer dialog reuses an uncertain start and discards superseded task reads", async () => {
  const writes = [], reads = []; let lost = true, items = [], delayed = null;
  globalThis.fetch = async (path, options = {}) => {
    const method = options.method || "GET"; reads.push(`${method} ${path}`);
    if (path.endsWith("/devices")) return reply({ device_id: "f".repeat(32), listing: { devices: [{ id: device, name: "Target", online: true }] } });
    if (path === "/api/v1/device-transfers" && method === "GET") return reply({ outgoing: items.filter(item => !item.incoming), incoming: items.filter(item => item.incoming) });
    if (path === "/api/v1/device-transfers" && method === "POST") {
      writes.push(JSON.parse(options.body));
      if (lost) { lost = false; throw new Error("reply lost"); }
      return reply({ id: writes.at(-1).id, kind: "session", state: "awaiting_confirmation", target_projects: [] });
    }
    if (path.endsWith(transfer) && delayed) return await delayed.promise;
    if (path.endsWith(other)) return reply({ id: other, state: "receiving_files", incoming: true });
    return reply({ id: writes.at(-1)?.id || transfer, state: "awaiting_confirmation", target_projects: [] });
  };
  try {
    const panel = createDeviceTransferPanel();
    panel.openSession({ id: "source", project_id: "default", title: "Source" }); await flush();
    await find("选择设备").click(); await flush();
    await find("选择设备").click(); await flush();
    assert.equal(writes.length, 2); assert.equal(writes[0].id, writes[1].id, "lost response minted a second transfer");
    find("关闭").click(); await flush();
    items = [{ id: transfer, kind: "project", title: "Outgoing", state: "paused" },
      { id: other, kind: "project", title: "Incoming", state: "receiving_files", incoming: true }];
    panel.openTasks(); await flush();
    let resolve; delayed = { promise: new Promise(done => { resolve = done; }) };
    await all(document.body).find(node => node.tag === "button" && node.textContent?.includes("Outgoing")).click(); await flush();
    assert.equal(find("选择设备").hidden, true, "task list allowed starting a project without a source identity");
    await all(document.body).find(node => node.tag === "button" && node.textContent?.includes("Incoming")).click(); await flush();
    resolve(reply({ id: transfer, kind: "project", state: "completed", destination_project_id: "wrong" })); await flush();
    assert.ok(reads.some(path => path.endsWith(`/incoming/${other}`)), "incoming task was not polled");
    assert.equal(find("在目标设备打开").hidden, true, "late outgoing read replaced the selected incoming task");
    assert.equal(all(document.body).find(node => node.role === "status").textContent, "receiving_files");
    find("关闭").click();
  } finally { globalThis.setTimeout = originalSet; globalThis.clearTimeout = originalClear; }
});
