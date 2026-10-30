import assert from "node:assert/strict";
import test from "node:test";
import { verifyModelSetup } from "../app/web/js/features/settings/model-verification.js";
import { createModelConfigPanel } from "../app/web/js/features/settings/model-config-panel.js";
import { testDocument, descendants } from "./helpers/extension-dom.mjs";

const model = (id, tools = true) => ({ id, name: id, capabilities: tools ? ["tool-call-output"] : [] });
test("setup probes replies before tools, sequentially, and skips undeclared tools", async () => {
  const calls = [], stages = []; let active = 0;
  const result = await verifyModelSetup([model("a"), model("b", false)], {
    async request(id, tools) { assert.equal(++active, 1); await Promise.resolve(); active--; calls.push([id, tools]); return { verified: true, latency_ms: 3 }; },
    onProgress: event => stages.push([event.model.id, event.phase, event.state]),
  });
  assert.deepEqual(calls, [["a", false], ["a", true], ["b", false]]);
  assert(result.get("a").verified && result.get("b").verified);
  assert.equal(stages.filter(stage => stage[2] === "running").length, 3);
});
test("failed replies skip tools and still verify the next selected model", async () => {
  const calls = [];
  const result = await verifyModelSetup([model("a"), model("b")], { request: async (id, tools) => {
    calls.push([id, tools]); if (id === "a") throw Object.assign(new Error("denied"), { code: "authentication_failed" });
    return { verified: true };
  } });
  assert.deepEqual(calls, [["a", false], ["b", false], ["b", true]]);
  assert.equal(result.get("a").verified, false); assert.equal(result.get("b").verified, true);
});
test("tool failures preserve reply success without marking the model verified", async () => {
  const result = await verifyModelSetup([model("a")], { request: async (_, tools) => {
    if (tools) throw new Error("not supported"); return { verified: true };
  } });
  assert.equal(result.get("a").reply.state, "passed"); assert.equal(result.get("a").verified, false);
});
test("an unsuccessful test envelope cannot verify the model", async () => {
  const result = await verifyModelSetup([model("a")], { request: async () => ({ verified: false }) });
  assert.equal(result.get("a").reply.state, "failed");
});
test("cancellation stops remaining phases and models", async () => {
  const controller = new AbortController(), calls = [];
  await assert.rejects(verifyModelSetup([model("a"), model("b")], { signal: controller.signal,
    request: async (id, tools) => { calls.push([id, tools]); controller.abort(); return { verified: true }; },
  }), { name: "AbortError" });
  assert.deepEqual(calls, [["a", false]]);
});

const flush = async () => { for (let i = 0; i < 30; i++) await new Promise(resolve => setImmediate(resolve)); };
function fixture({ failTools = false, hold = false, staleEvidence = false } = {}) {
  const previous = { document: globalThis.document, fetch: globalThis.fetch };
  const document = testDocument(), create = document.createElement;
  document.createElement = tag => {
    const node = create(tag); node.reportValidity = () => true; node.setCustomValidity = () => {};
    node.toggleAttribute = (key, value) => value ? node.setAttribute(key, "") : node.removeAttribute(key);
    return node;
  };
  globalThis.document = document;
  const container = document.createElement("section"); document.body.append(container);
  let config = { default_model: "existing", providers: [{ id: "existing", name: "Existing", builtin: true, endpoints: {} }],
    items: [{ ...model("existing"), provider: "existing", wire_model: "existing", builtin: true }], verification: {} };
  const calls = []; let release, rejectTools = failTools;
  const paused = new Promise(resolve => { release = resolve; });
  globalThis.fetch = async (path, options = {}) => {
    const body = options.body ? JSON.parse(options.body) : null;
    calls.push({ path, body, method: options.method || "GET" });
    let data = {}, status = 200, error;
    if (path === "/api/v1/models/config") data = structuredClone(config);
    else if (path === "/api/v1/models") data = { providers: [], models: [] };
    else if (path === "/api/v1/models/discover") data = { items: ["alpha", "beta", "unselected"] };
    else if (path === "/api/v1/models/setup" || path === "/api/v1/settings/models") {
      config = { ...structuredClone(body.patch), verification: config.verification };
    } else if (path === "/api/v1/models/test") {
      if (hold) await paused;
      const record = config.verification[body.model_id] ||= { verified: false };
      const phase = body.tools ? "tools" : "reply";
      if (rejectTools && body.tools && body.model_id.endsWith("alpha")) {
        record.tools = { state: "failed" }; status = 422;
        error = { code: "model_tools_test_failed", message: "fixture tool rejection" };
      } else {
        record[phase] = { state: "passed" };
        record.verified = record.reply?.state === "passed" && record.tools?.state === "passed";
        data = { verified: true, latency_ms: 2, verification: structuredClone(record) };
        if (staleEvidence) delete config.verification[body.model_id];
      }
    } else assert.fail(`Unexpected request ${path}`);
    return new Response(JSON.stringify({ ok: status === 200, data, error }), { status, headers: { ETag: '"fixture-revision"' } });
  };
  const panel = createModelConfigPanel(container);
  const button = label => {
    const node = descendants(container).find(node => node.tagName === "BUTTON" && node.textContent === label);
    assert(node, `Missing button ${label}`); return node;
  };
  const input = (name, value) => {
    const node = descendants(container).find(node => node.name === name); assert(node, `Missing input ${name}`);
    node.value = value; node.dispatchEvent(new Event("input")); return node;
  };
  async function add() {
    await panel.ensureLoaded(); button("添加供应商").click(); button("OpenAI").click(); input("api-key", "fixture-key");
    button("连接并读取模型").click(); await flush();
    const checkbox = input("make-default", ""); checkbox.checked = true; checkbox.dispatchEvent(new Event("change"));
    button("添加并验证所选模型").click(); await flush();
  }
  return { panel, container, calls, add, button, config: () => config, release, allowTools: () => { rejectTools = false; },
    restore() { panel.destroy(); release(); Object.assign(globalThis, previous); } };
}
test("wizard saves only selected profiles, auto-verifies, and then sets the default", async () => {
  const f = fixture();
  try {
    await f.add();
    const probes = f.calls.filter(call => call.path.endsWith("/test"));
    assert.deepEqual(probes.map(call => call.body), [
      { model_id: "openai-alpha", tools: false }, { model_id: "openai-alpha", tools: true },
      { model_id: "openai-beta", tools: false }, { model_id: "openai-beta", tools: true },
    ]);
    const save = f.calls.find(call => call.path.endsWith("/setup"));
    assert.equal(save.body.patch.default_model, "existing");
    assert.equal(save.body.patch.items[1].proxy.kind, "provider");
    assert.equal(f.config().default_model, "openai-alpha");
    assert(f.container.textContent.includes("2/2")); assert(f.container.textContent.includes("已验证"));
    assert(!f.container.textContent.includes("停止后续验证"));
    await f.panel.reload(); assert(f.container.textContent.includes("已验证"));
  } finally { f.restore(); }
});
test("failed tool validation stays visible, preserves the default, and can be retried", async () => {
  const f = fixture({ failTools: true });
  try {
    await f.add(); assert.equal(f.config().default_model, "existing");
    assert(f.container.textContent.includes("1/2")); assert(f.container.textContent.includes("回复通过"));
    assert(f.container.textContent.includes("失败"));
    assert.equal(f.config().verification["openai-alpha"].verified, false);
    assert.equal(f.button("添加供应商").disabled, false);
    assert(descendants(f.container).some(node => node.textContent === "完整验证 / 重试" && !node.disabled));
    f.allowTools(); f.button("完整验证 / 重试").click(); await flush();
    assert.equal(f.config().verification["openai-alpha"].verified, true);
    assert(f.container.textContent.includes("1/1"));
  } finally { f.restore(); }
});
test("closing the panel cancels pending onboarding and prevents further probes", async () => {
  const f = fixture({ hold: true });
  try {
    await f.add(); f.panel.destroy(); await flush();
    assert.equal(f.calls.filter(call => call.path.endsWith("/test")).length, 1);
    assert.equal(f.config().default_model, "existing");
  } finally { f.restore(); }
});
test("stop remains clickable during verification and keeps the saved models", async () => {
  const f = fixture({ hold: true });
  try {
    await f.add(); const stop = f.button("停止后续验证"); assert.equal(Boolean(stop.disabled), false);
    stop.click(); await flush();
    assert(f.container.textContent.includes("已停止"));
    assert(!descendants(f.container).some(node => node.tagName === "BUTTON" && node.textContent === "停止后续验证"));
    assert.equal(f.calls.filter(call => call.path.endsWith("/test")).length, 1);
    assert.equal(f.config().items.length, 3);
    assert.equal(f.button("添加供应商").disabled, false);
  } finally { f.restore(); }
});
test("evidence invalidated by native configuration changes cannot set the default", async () => {
  const f = fixture({ staleEvidence: true });
  try {
    await f.add(); assert.equal(f.config().default_model, "existing");
    assert(f.container.textContent.includes("0/2")); assert(f.container.textContent.includes("配置已变化"));
  } finally { f.restore(); }
});
