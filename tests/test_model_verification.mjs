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
function fixture({ failTools = false, hold = false, holdCatalog = false, holdDiscovery = false, staleEvidence = false, missingBeta = false, textOnly = false, partialMetadata = false, initialConfig = null, holdSave = false } = {}) {
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
  if (initialConfig) config = structuredClone(initialConfig);
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
    else if (path === "/api/v1/models/discover") {if(holdDiscovery)await paused;data = { items: ["alpha", "beta", "unselected"],...(partialMetadata?{parameters:[{id:"alpha",protocol:body.protocol,fields:{
      context_tokens:{value:4096,source:"api"},max_output_tokens:{value:1024,source:"api"}}}]}:{}) };}
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
      if (holdSave) await paused;
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
    button("自动获取").click(); await flush();
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
test("manual input is the default and never requests a supplier model list",async()=>{
  const f=fixture();try{
    await f.panel.ensureLoaded();f.button("添加供应商").click();await flush();f.button("OpenAI").click();f.input("api-key","fixture-key");
    f.button("下一步").click();await flush();const before=f.calls.filter(c=>c.path.endsWith("/discover")).length;
    assert.equal(before,0);assert(f.surface.querySelector("textarea"));assert(f.button("下一步").disabled);
    f.button("手动填写").click();f.input("manual-models","manual-one\nmanual-two,manual-one");f.button("下一步").click();await flush();
    assert.equal(f.calls.filter(c=>c.path.endsWith("/discover")).length,before);
    assert.equal(descendants(f.surface).filter(n=>n.name==="selected-model").length,2);
    assert(f.button("添加并验证（2）"));
  }finally{f.restore();}
});

test("automatic discovery immediately spins and displays selectable models before the next step",async()=>{
  const f=fixture({holdDiscovery:true});try{
    await f.panel.ensureLoaded();f.button("添加供应商").click();await flush();f.button("OpenAI").click();f.input("api-key","fixture-key");
    f.button("下一步").click();f.button("自动获取").click();
    assert(f.surface.querySelector(".model-source-spinner"));assert(f.button("下一步").disabled);
    assert.equal(f.surface.querySelector('[role="tabpanel"]').getAttribute("aria-busy"),"true");
    f.release();await flush();assert(!f.surface.querySelector(".model-source-spinner"));
    const models=descendants(f.surface).filter(n=>n.name==="discovered-model");assert.deepEqual(models.map(n=>n.value),["alpha","beta","unselected"]);
    assert.equal(descendants(f.surface).filter(n=>n.name==="selected-model").length,0);
    models[0].checked=false;models[0].dispatchEvent(new Event("change"));models[1].checked=false;models[1].dispatchEvent(new Event("change"));
    assert(f.button("下一步").disabled);models[1].checked=true;models[1].dispatchEvent(new Event("change"));f.button("下一步").click();await flush();
    assert.deepEqual(descendants(f.surface).filter(n=>n.name==="selected-model").map(n=>n.value),["beta"]);
    assert.deepEqual(f.calls.find(c=>c.path.endsWith("/parameters")).body.models,["beta"]);
    f.button("上一步").click();assert.equal(descendants(f.surface).find(n=>n.name==="discovered-model"&&n.value==="alpha").checked,false);
  }finally{f.restore();}
});

test("discovered installed profiles remain visible but cannot be selected or added twice",async()=>{
  const f=fixture();try{
    await f.add();f.button("添加模型").click();f.button("下一步").click();f.button("自动获取").click();await flush();
    const rows=descendants(f.surface).filter(n=>n.name==="discovered-model");
    assert(rows.find(n=>n.value==="alpha").disabled);assert(rows.find(n=>n.value==="beta").disabled);
    assert.equal(rows.find(n=>n.value==="unselected").checked,true);f.button("下一步").click();await flush();
    assert.deepEqual(descendants(f.surface).filter(n=>n.name==="selected-model").map(n=>n.value),["unselected"]);
  }finally{f.restore();}
});

test("switching back to manual cancels discovery and preserves input against late replies",async()=>{
  const f=fixture({holdDiscovery:true});try{
    await f.panel.ensureLoaded();f.button("添加供应商").click();await flush();f.button("OpenAI").click();f.input("api-key","fixture-key");
    f.button("下一步").click();f.input("manual-models","manual-draft");f.button("自动获取").click();
    f.button("手动填写").click();f.release();await flush();
    assert.equal(f.surface.querySelector("textarea").value,"manual-draft");assert(!f.surface.querySelector(".model-source-spinner"));
    assert(!f.surface.querySelector(".model-source-list"));assert(!f.button("下一步").disabled);
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

function deletionFixture(options = {}) {
  const profile = (id, provider, builtin = false) => ({ ...model(id), provider, wire_model: id,
    builtin, removable: !builtin, window: { context_tokens: 64000, max_output_tokens: 8192 } });
  return fixture({ initialConfig: {
    default_model: "existing", verification: {},
    providers: [
      { id: "existing", name: "Existing", builtin: true, removable: false, endpoints: {} },
      { id: "custom", name: "Custom", builtin: false, removable: true, endpoints: {} },
      { id: "other", name: "Other", builtin: false, removable: true, endpoints: {} },
    ],
    items: [profile("existing", "existing", true), profile("a", "custom"), profile("b", "custom"), profile("c", "other")],
  }, ...options });
}
const supplierRow = (f, id) => f.container.querySelector(".supplier-list").children.find(row => row.dataset.providerId === id);
const removeLayer = () => document.querySelector(".supplier-remove-layer");
const removalButton = label => descendants(removeLayer()).find(node => node.tagName === "BUTTON" && node.textContent === label);

test("supplier X actions are separate from selection and protect the built-in supplier", async () => {
  const f = deletionFixture(); try {
    await f.panel.ensureLoaded(); const row = supplierRow(f, "custom"), select = row.querySelector(".model-config-item"), remove = row.querySelector(".supplier-remove");
    assert(!select.contains(remove)); assert(!remove.disabled);
    assert.equal(remove.getAttribute("aria-label"), "删除供应商 Custom");
    assert.equal(remove.querySelector(".icon").dataset.icon, "close");
    const builtin = supplierRow(f, "existing").querySelector(".supplier-remove");
    assert(builtin.disabled); builtin.click(); assert(!removeLayer());
    remove.click(); assert(removeLayer()?.open); assert(!f.container.contains(removeLayer()));
    assert.equal(select.getAttribute("aria-current"), "false");
    assert(removeLayer().textContent.includes("2 个模型"));
    removalButton("取消").click(); await flush();
    assert(!removeLayer()); assert.equal(f.config().items.length, 4);
    assert(!f.calls.some(call => call.method === "PUT"));
  } finally { f.restore(); }
});

test("confirmed supplier deletion removes its profiles atomically and keeps the current default", async () => {
  const f = deletionFixture(); try {
    await f.panel.ensureLoaded(); supplierRow(f, "custom").querySelector(".supplier-remove").click();
    removalButton("确认删除").click(); await flush();
    assert(!removeLayer()); assert(!supplierRow(f, "custom"));
    assert.deepEqual(f.config().providers.map(p => p.id), ["existing", "other"]);
    assert.deepEqual(f.config().items.map(m => m.id), ["existing", "c"]);
    assert.equal(f.config().default_model, "existing");
    const writes = f.calls.filter(call => call.method === "PUT"); assert.equal(writes.length, 1);
    assert(writes[0].body.patch.items.every(item => writes[0].body.patch.providers.some(p => p.id === item.provider)));
  } finally { f.restore(); }
});

test("deleting the selected default supplier explains and applies the built-in fallback", async () => {
  const f = deletionFixture(); try {
    f.config().default_model = "a"; await f.panel.ensureLoaded();
    supplierRow(f, "custom").querySelector(".supplier-remove").click();
    assert(removeLayer().textContent.includes("默认模型将切换为 existing"));
    removalButton("确认删除").click(); await flush();
    assert.equal(f.config().default_model, "existing");
    assert.equal(supplierRow(f, "existing").querySelector(".model-config-item").getAttribute("aria-current"), "true");
  } finally { f.restore(); }
});

test("a configuration conflict preserves the supplier and the confirmation for review", async () => {
  const f = deletionFixture(); try {
    await f.panel.ensureLoaded(); supplierRow(f, "custom").querySelector(".supplier-remove").click();
    f.config().providers[1].name = "Changed on another page";
    removalButton("确认删除").click(); await flush();
    assert(removeLayer()?.open); assert(!removeLayer().querySelector(".resource-error").hidden);
    assert.equal(f.config().providers.length, 3); assert.equal(f.config().items.length, 4);
    assert(!f.calls.some(call => call.method === "PUT"));
  } finally { f.restore(); }
});

test("a pending supplier deletion cannot submit twice or close its confirmation", async () => {
  const f = deletionFixture({ holdSave: true }); try {
    await f.panel.ensureLoaded(); supplierRow(f, "custom").querySelector(".supplier-remove").click();
    const layer = removeLayer(), apply = removalButton("确认删除");
    apply.click(); await flush(); assert(apply.disabled); apply.click();
    layer.dispatchEvent(new Event("cancel", { cancelable: true })); await flush();
    assert.equal(removeLayer(), layer); assert.equal(f.calls.filter(call => call.method === "PUT").length, 1);
    f.release(); await flush(); assert(!removeLayer()); assert.equal(f.config().providers.length, 2);
  } finally { f.restore(); }
});

test("protected profiles and page disposal cannot leave an active supplier removal", async () => {
  const f = deletionFixture(); try {
    f.config().items[1].removable = false; await f.panel.ensureLoaded();
    assert(supplierRow(f, "custom").querySelector(".supplier-remove").disabled);
    supplierRow(f, "other").querySelector(".supplier-remove").click(); assert(removeLayer());
    f.panel.destroy(); assert(!removeLayer()); assert(!f.calls.some(call => call.method === "PUT"));
  } finally { f.restore(); }
});
