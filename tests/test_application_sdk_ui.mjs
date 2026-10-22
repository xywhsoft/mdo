import assert from "node:assert/strict";
import test from "node:test";
class Node extends EventTarget {
  constructor(tag) { super(); this.tagName=tag;this.children=[]; }
  append(...children) { for(const child of children){child.parent=this;this.children.push(child);} }
  remove() { if(this.parent)this.parent.children=this.parent.children.filter(c=>c!==this);this.parent=null; }
  showModal(){this.open=true;}
  close(){if(this.open){this.open=false;this.dispatchEvent(new Event("close"));}}
}
globalThis.window=new EventTarget();globalThis.document=new Node("document");document.body=new Node("body");const sidebar=new Node("aside");
document.hidden=false;document.createElement=tag=>new Node(tag);document.querySelector=()=>sidebar;
const stored=new Map();globalThis.sessionStorage={getItem:key=>stored.get(key)||null,setItem:(key,value)=>stored.set(key,value)};
globalThis.fetch=async(url,options={})=>{
  const input=options.body?JSON.parse(options.body):null;
  let data={};if(input?.method==="plugin.list")data={items:[],recovery_mode:false};if(input?.method==="client.poll")data=[];
  return Response.json({ok:true,data},{headers:{"X-Mdo-Write-Token":"a".repeat(32)+"-0"}});
};
const { createApplicationUi }=await import("../app/web/js/features/shell/application-sdk.js");
test("UI contribution lifecycle closes owned panels and releases menus/keybindings",async()=>{
  const navigation={preferredProject:()=>"default",workspace:()=>({projectId:"default",sessionId:"s"}),get:()=>({projectId:"default",sessionId:"s"})};
  const app=createApplicationUi({navigation,executeActions:async()=>{}});let cleaned=0,menuCalls=0;
  try {
    const removePanel=app.sdk.ui.registerPanel("qa.panel",{render(root,{signal}){assert.equal(signal.aborted,false);return ()=>cleaned++;}});
    await app.sdk.ui.openPanel("qa.panel");assert.equal(document.body.children.length,1);
    removePanel();assert.equal(cleaned,1);assert.equal(document.body.children.length,0);
    await assert.rejects(()=>app.sdk.ui.openPanel("qa.panel"),/not registered/);
    app.sdk.ui.registerMenu("qa.menu",{label:"Test",execute:()=>menuCalls++});sidebar.children[0].onclick();assert.equal(menuCalls,1);
    app.sdk.ui.registerKeybinding("qa.key",{code:"KeyJ",mod:true,execute:()=>menuCalls++});
    const event=new Event("keydown");Object.assign(event,{code:"KeyJ",ctrlKey:true});document.dispatchEvent(event);assert.equal(menuCalls,2);
  } finally {app.destroy();}
  assert.equal(sidebar.children.length,0);
  assert.throws(()=>app.sdk.ui.registerPanel("qa.late",{}),/disposed/);
});
