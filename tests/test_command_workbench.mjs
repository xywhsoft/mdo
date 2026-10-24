import assert from "node:assert/strict";
import test from "node:test";
import { createCommandWorkbench } from "../app/web/js/features/settings/command-workbench.js";
import { TestElement as Node, testDocument, descendants as all } from "./helpers/extension-dom.mjs";

test("project command edits are versioned, remain C source and protect unsaved changes",async context=>{
  const before={document:globalThis.document,window:globalThis.window};const body=new Node("body"),page=new EventTarget();
  page.confirm=()=>false;globalThis.document=Object.assign(new EventTarget(),testDocument(),{body});globalThis.window=page;
  const requests=[];let saved=false,changed=0,finish;
  const changedSignal=new Promise(resolve=>{finish=resolve;});
  context.mock.method(globalThis,"fetch",async(url,options)=>{
    requests.push([url,options]);
    if(options.method==="PUT"){
      assert.equal(url,"/api/v1/commands/project/ui-test?project=default");assert.equal(options.headers.get("If-Match"),'"new"');
      const source=JSON.parse(options.body).content;assert.match(source,/"id": "ui-test"/);assert.match(source,/mdoCommandRun/);saved=true;
      return Response.json({ok:true,data:{revision:"revision-one"}});
    }
    return Response.json({ok:true,data:url.endsWith("/projects")?{items:[]}:saved?{"ui-test":{id:"ui-test",scope:"project",enabled:true,valid:true}}:{}});
  });
  const workbench=createCommandWorkbench({onChange:()=>{changed++;finish();}});
  try{
    await workbench.open();const nodes=all(body);const id=nodes.find(n=>n.attributes["aria-label"]==="命令 ID");
    id.value="ui-test";id.dispatchEvent(new Event("input"));assert.equal(workbench.hasPendingChanges(),true);
    assert.equal(workbench.close(),false);assert.ok(body.children.length);
    nodes.find(n=>n.textContent==="保存").click();await changedSignal;await new Promise(resolve=>setImmediate(resolve));
    assert.equal(saved,true);assert.equal(changed,1);assert.equal(workbench.hasPendingChanges(),false);
    assert.ok(all(body).some(n=>n.textContent==="/ui-test · project · 启用"));
  }finally{workbench.destroy();globalThis.document=before.document;globalThis.window=before.window;}
});

test("project commands share only their project source and installed commands cannot be copied", async context => {
  const old = { document: globalThis.document, window: globalThis.window };
  globalThis.document = Object.assign(new EventTarget(), testDocument()); globalThis.window = new EventTarget(); window.confirm = () => true;
  const read = [], shared = [];
  context.mock.method(globalThis, "fetch", async url => {
    read.push(url);
    return Response.json({ ok: true, data: url.endsWith("/projects") ? { items: [] } : {
      managed: { id: "managed", scope: "user", enabled: true, valid: true, management_known: true, store_managed: true },
      own: { id: "own", scope: "project", enabled: true, valid: true },
    } });
  });
  const workbench = createCommandWorkbench({ onShare: item => shared.push(item) });
  try {
    await workbench.open();
    const layer = document.body.querySelector(".mdo-layer"), rows = layer.querySelectorAll(".extension-row");
    assert(!rows[0].querySelector("button")); assert.match(rows[0].textContent, /不能编辑、复制或再次分享/);
    const footer = layer.querySelector(".mdo-layer-footer");
    assert(footer.querySelectorAll("button").some(b => b.textContent === "保存"));
    rows[1].querySelectorAll("button").find(b => b.textContent === "分享到创作中心").click();
    await new Promise(r => setImmediate(r));
    assert.equal(shared.length, 1); assert.equal(shared[0].project, "default"); assert.equal(shared[0].id, "own");
    assert(!read.some(path => path.includes("/extensions/commands/managed")));
    assert(!document.body.querySelector(".mdo-layer"));
  } finally { workbench.destroy(); Object.assign(globalThis, old); }
});
