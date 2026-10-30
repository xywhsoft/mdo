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
function fixture({ failTools = false, hold = false, holdCatalog = false, staleEvidence = false, missingBeta = false, textOnly = false, partialMetadata = false } = {}) {
  const previous = { document: globalThis.document, fetch: globalThis.fetch };
  const document = testDocument(), create = document.createElement;
  document.querySelector = selector => document.body.querySelector(selector);
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
    else if (path === "/api/v1/models/providers") {if(holdCatalog)await paused;data = { providers: [{id:"openai", name:"OpenAI", variants:[{id:"api", label:"regular", base:"https://api.openai.com/v1", protocol:"openai-responses", models:["alpha","beta"]}]}] };}
    else if (path === "/api/v1/models/parameters") data = { items: body.models.map(id => ({id,protocol:body.protocol,lookup_state:"ok",registry_version:"fixture",fields: missingBeta && id === "beta" ? {} : {
      ...(textOnly ? {tools:{value:false,source:"api"}} : {}),context_tokens:{value:64000,source:"api"},max_output_tokens:{value:16000,source:"api"},reasoning:{value:{kind:"none"},source:"api"}}})) };
    else if (path === "/api/v1/models/discover") data = { items: ["alpha", "beta", "unselected"],...(partialMetadata?{parameters:[{id:"alpha",protocol:body.protocol,fields:{
      context_tokens:{value:4096,source:"api"},max_output_tokens:{value:1024,source:"api"}}}]}:{}) };
    else if (path === "/api/v1/models/add") {
      if (hold) await paused;
      options.signal?.throwIfAborted();
      if (staleEvidence || (rejectTools && body.model.id.endsWith("alpha"))) {
        status = staleEvidence ? 409 : 422;
        error = { code: staleEvidence ? "model_config_changed" : "model_tools_test_failed", message: "fixture rejection" };
      } else {
        if (!config.providers.some(p => p.id === body.provider.id))
          config.providers.push({...structuredClone(body.provider),credential:{secret_ref:"vault:"+"a".repeat(64)}});
        config.items.push(structuredClone(body.model));
        const tools=body.model.capabilities.includes("tool-call-output");
        const record={verified:true,reply:{state:"passed"},...(tools?{tools:{state:"passed"}}:{})};
        config.verification[body.model.id]=record;
        if(body.make_default&&tools)config.default_model=body.model.id;
        status=201;data={added:true,verified:true,agent_ready:tools,verification_saved:true,model_id:body.model.id,verification:record};
      }
    } else if (path === "/api/v1/settings/models") {
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
        record.verified = record.reply?.state === "passed" && (!config.items.find(item => item.id === body.model_id)?.capabilities.includes("tool-call-output") || record.tools?.state === "passed");
        data = { verified: true, latency_ms: 2, verification: structuredClone(record) };
        if (staleEvidence) delete config.verification[body.model_id];
      }
    } else assert.fail(`Unexpected request ${path}`);
    return new Response(JSON.stringify({ ok: status >= 200 && status < 300, data, error }), { status, headers: { ETag: '"fixture-revision"' } });
  };
  const panel = createModelConfigPanel(container);
  const button = label => {
    const node = descendants(document.querySelector(".model-setup-layer") || container).find(node => node.tagName === "BUTTON" && node.textContent === label);
    assert(node, `Missing button ${label}`); return node;
  };
  const input = (name, value) => {
    const node = descendants(document.body).find(node => node.name === name); assert(node, `Missing input ${name}`);
    node.value = value; node.dispatchEvent(new Event("input")); return node;
  };
  async function add() {
    await panel.ensureLoaded(); button("添加供应商").click(); await flush(); button("OpenAI").click(); input("api-key", "fixture-key");
    button("下一步").click(); await flush();
    button("下一步").click(); await flush();
    const checkbox = input("make-default", ""); checkbox.checked = true; checkbox.dispatchEvent(new Event("change"));
    button("添加并验证（2）").click(); await flush();
  }
  return { panel, container, get surface(){return document.querySelector(".model-setup-layer") || container;}, calls, add, button, input, config: () => config, release, allowTools: () => { rejectTools = false; },
    restore() { panel.destroy(); release(); Object.assign(globalThis, previous); } };
}
test("wizard fetches models and missing parameters automatically and commits only selected profiles", async () => {
  const f=fixture();try {
    await f.add();
    const adds=f.calls.filter(c=>c.path.endsWith("/add"));
    assert.deepEqual(adds.map(c=>c.body.model.wire_model),["alpha","beta"]);
    assert.equal(adds[0].body.model.proxy.kind,"provider");
    assert.equal(f.calls.filter(c=>c.path.endsWith("/discover")).length,1);
    const lookup=f.calls.find(c=>c.path.endsWith("/parameters"));assert.equal(lookup.body.catalog_only,true);assert(!lookup.body.key);
    assert(!f.calls.some(c=>c.path.endsWith("/setup")));
    assert.equal(f.config().default_model,"openai-alpha");
    assert(f.container.textContent.includes("已验证"));
    assert(!f.container.textContent.includes("测试回复"));assert(!f.container.textContent.includes("测试工具调用"));
    assert(descendants(f.container).some(n=>n.className==="model-parameter-link"));
  } finally {f.restore();}
});
test("failed validation leaves only failed drafts; successes never get probed twice", async()=>{
  const f=fixture({failTools:true});try {
    await f.add();assert(f.config().items.some(m=>m.wire_model==="beta"));
    assert(!f.config().items.some(m=>m.wire_model==="alpha"));
    assert(descendants(f.surface).some(n=>n.className==="resource-error"&&n.textContent), f.surface.textContent);
    f.allowTools();f.button("添加并验证（1）").click();await flush();
    assert.equal(f.calls.filter(c=>c.path.endsWith("/add")&&c.body.model.wire_model==="beta").length,1);
    assert(f.config().items.some(m=>m.wire_model==="alpha"));
  }finally{f.restore();}
});

test("automatic budgets respect API limits after missing fields are filled from the catalog",async()=>{
  const f=fixture({partialMetadata:true});try{
    await f.add();const body=f.calls.find(c=>c.path.endsWith("/add")&&c.body.model.wire_model==="alpha")?.body;
    assert(body,"Complete merged parameters must allow validation without manual budget correction");
    assert.equal(body.model.window.context_tokens,4096);assert.equal(body.model.window.max_output_tokens,1024);
    assert.equal(body.model.parameters.fields.context_tokens.source,"api");
  }finally{f.restore();}
});
test("closing the panel cancels a pending draft without registering unverified models",async()=>{
  const f=fixture({hold:true});try{await f.add();f.panel.destroy();await flush();
    assert.equal(f.calls.filter(c=>c.path.endsWith("/add")).length,1);assert.equal(f.config().items.length,1);
  }finally{f.restore();}
});
test("stopping preserves unattempted drafts and leaves no phantom installed profiles",async()=>{
  const f=fixture({hold:true});try{await f.add();f.button("停止").click();await flush();
    assert(f.surface.textContent.includes("已停止"));assert.equal(f.config().items.length,1);
    assert(f.button("添加并验证（2）"));
  }finally{f.restore();}
});
test("configuration changes during validation cannot promote a default",async()=>{
  const f=fixture({staleEvidence:true});try{await f.add();assert.equal(f.config().default_model,"existing");
    assert.equal(f.config().items.length,1);assert(descendants(f.surface).some(n=>n.className==="resource-error"&&n.textContent), f.surface.textContent);
  }finally{f.restore();}
});
test("unknown parameters remain editable and block validation until filled",async()=>{
  const f=fixture({missingBeta:true});try{await f.add();assert.equal(f.calls.filter(c=>c.path.endsWith("/add")).length,0);
    f.input("parameter-1-context_tokens","16000");f.input("parameter-1-max_output_tokens","8000");f.input("parameter-1-output-budget","2048");
    const choice=f.input("parameter-1-reasoning","provider-default");choice.dispatchEvent(new Event("change"));
    f.button("添加并验证（2）").click();await flush();assert.equal(f.config().items.length,3);
  }finally{f.restore();}
});
test("deselecting an incomplete profile hides its editor and allows the remaining selection",async()=>{
  const f=fixture({missingBeta:true});try{await f.add();
    const box=descendants(f.surface).find(n=>n.name==="selected-model"&&n.value==="beta");assert(box);
    box.checked=false;box.dispatchEvent(new Event("change"));
    const row=box.closest(".model-wizard-row");assert(row.querySelector(".model-parameter-editor").hidden);
    assert([...row.querySelectorAll("input, select")].filter(n=>n!==box).every(n=>n.disabled));
    f.button("添加并验证（1）").click();await flush();assert(f.config().items.some(m=>m.wire_model==="alpha"));
    assert(!f.config().items.some(m=>m.wire_model==="beta"));
  }finally{f.restore();}
});
test("text-only profiles do not become the Agent default",async()=>{
  const f=fixture({textOnly:true});try{await f.add();assert.equal(f.config().default_model,"existing");
    assert(f.calls.filter(c=>c.path.endsWith("/add")).every(c=>!c.body.model.capabilities.includes("tool-call-output")));
  }finally{f.restore();}
});
test("manual source switches immediately and needs no supplier discovery request",async()=>{
  const f=fixture();try{
    await f.panel.ensureLoaded();f.button("添加供应商").click();await flush();f.button("OpenAI").click();f.input("api-key","fixture-key");
    f.button("下一步").click();await flush();const before=f.calls.filter(c=>c.path.endsWith("/discover")).length;
    f.button("手动填写").click();f.input("manual-models","manual-one\nmanual-two,manual-one");f.button("下一步").click();await flush();
    assert.equal(f.calls.filter(c=>c.path.endsWith("/discover")).length,before);
    assert.equal(descendants(f.surface).filter(n=>n.name==="selected-model").length,2);
    assert(f.button("添加并验证（2）"));
  }finally{f.restore();}
});

test("supplier selection and wizard use an owned public layer with fixed footer actions",async()=>{
  const f=fixture();try{
    await f.panel.ensureLoaded();const background=f.container.textContent;
    f.button("添加供应商").click();await flush();
    const layer=document.querySelector(".model-setup-layer");assert(layer?.open);assert(!f.container.contains(layer));
    assert.equal(f.container.textContent,background);
    f.button("OpenAI").click();f.input("api-key","fixture-key");
    const footer=layer.querySelector(".mdo-layer-footer");assert(footer.contains(f.button("下一步")));
    assert(!layer.querySelector(".mdo-layer-body").contains(f.button("下一步")));
    f.button("取消").click();await flush();assert(!document.querySelector(".model-setup-layer"));assert.equal(f.config().items.length,1);
  }finally{f.restore();}
});

test("closing the wizard layer cancels validation and leaves no late installed drafts",async()=>{
  const f=fixture({hold:true});try{
    await f.add();const layer=document.querySelector(".model-setup-layer");assert(layer?.open);
    layer.dispatchEvent(new Event("cancel",{cancelable:true}));f.release();await flush();
    assert(!document.querySelector(".model-setup-layer"));assert.equal(f.config().items.length,1);
    assert.equal(f.calls.filter(c=>c.path.endsWith("/add")).length,1);
  }finally{f.restore();}
});

test("a late supplier catalog cannot reopen or replace a cancelled window",async()=>{
  const f=fixture({holdCatalog:true});try{
    await f.panel.ensureLoaded();f.button("添加供应商").click();await flush();
    const layer=document.querySelector(".model-setup-layer");assert(layer?.open);
    layer.querySelector(".mdo-layer-controls").lastChild.click();f.release();await flush();
    assert(!document.querySelector(".model-setup-layer"));assert(!f.container.querySelector(".supplier-templates"));
    assert.equal(f.config().items.length,1);
  }finally{f.restore();}
});
