import assert from "node:assert/strict";
import test from "node:test";
import { createCommandWorkbench } from "../app/web/js/features/settings/command-workbench.js";
class Node extends EventTarget {
  constructor(tag){super();this.tagName=tag.toUpperCase();this.children=[];this.value="";this.isConnected=true;this.attributes={};}
  setAttribute(k,v){this.attributes[k]=v;if(k==="value")this.value=v;}
  append(...nodes){for(const n of nodes){this.children.push(n);if(n&&typeof n==="object")n.parent=this;}if(this.tagName==="SELECT"&&!this.value)this.value=this.children[0]?.value||"";}
  replaceChildren(...n){this.children=[];this.append(...n);}
  querySelectorAll(selector){return this.children.flatMap(n=>n&&typeof n==="object"?[...(selector.split(",").some(t=>n.tagName===t.toUpperCase())?[n]:[]),...n.querySelectorAll(selector)]:[]);}
  showModal(){this.open=true;}remove(){if(this.parent)this.parent.children=this.parent.children.filter(n=>n!==this);this.isConnected=false;}focus(){}
}
const all=n=>[n,...n.children.flatMap(v=>v&&typeof v==="object"?all(v):[])];
test("project command edits are versioned, remain C source and protect unsaved changes",async context=>{
  const before={document:globalThis.document,window:globalThis.window};const body=new Node("body"),page=new EventTarget();
  page.confirm=()=>false;globalThis.document={body,createElement:tag=>new Node(tag)};globalThis.window=page;
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
    nodes.find(n=>n.textContent==="保存").onclick();await changedSignal;await new Promise(resolve=>setImmediate(resolve));
    assert.equal(saved,true);assert.equal(changed,1);assert.equal(workbench.hasPendingChanges(),false);
    assert.ok(all(body).some(n=>n.textContent==="/ui-test · project · 启用"));
  }finally{workbench.destroy();globalThis.document=before.document;globalThis.window=before.window;}
});
