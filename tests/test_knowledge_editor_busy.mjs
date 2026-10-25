import assert from "node:assert/strict";
import test from "node:test";
import {readFileSync} from "node:fs";
import {testDocument,descendants} from "./helpers/extension-dom.mjs";
import {knowledgeMetadata,knowledgeBase64} from "../app/web/js/features/settings/knowledge-format.js";

const tick=()=>new Promise(resolve=>setImmediate(resolve));
const file=(path,text)=>({path,base64:knowledgeBase64(new TextEncoder().encode(text))});
const deferred=()=>{let resolve,reject;const promise=new Promise((yes,no)=>{resolve=yes;reject=no;});return {promise,resolve,reject};};
let sequence=0;
async function fixture(files=[file("index.md","# Index")]){
  const previous=globalThis.document;globalThis.document=testDocument();let device="local",notify,changes=0,write;
  const calls=[],pending=()=>{write=deferred();return write;};pending();
  globalThis.__knowledgeBusy={api:{get:async()=>({data:{content:JSON.stringify(knowledgeMetadata({name:"Original",description:"Reference"})),files,bundle_revision:"bundle-digest"}}),put:async(...args)=>{calls.push(structuredClone(args));return await write.promise;}},targetState:()=>({selected:{id:device}}),subscribeTarget:fn=>{notify=fn;return()=>{};}};
  const url=new URL("../app/web/js/features/settings/knowledge-editor.js",import.meta.url);
  const source=readFileSync(url,"utf8").replace('import { api } from "../../api/client.js";','const {api}=globalThis.__knowledgeBusy;').replace('import { targetState, subscribeTarget } from "../../api/target.js";','const {targetState,subscribeTarget}=globalThis.__knowledgeBusy;').replace(/from "(\.[^"]+)"/g,(_,p)=>`from ${JSON.stringify(new URL(p,url).href)}`)+`\n// fixture ${++sequence}`;
  const {createKnowledgeEditor}=await import("data:text/javascript;base64,"+Buffer.from(source).toString("base64"));
  const editor=createKnowledgeEditor({owner:document.body,onChange:()=>changes++});await editor.open({item:{id:"reference",revision:"metadata-digest"}});
  const nodes=()=>descendants(document.body),form=nodes().find(n=>n.tagName==="FORM");
  return {editor,form,nodes,calls,pending,get write(){return write;},get changes(){return changes;},
    submit(){form.dispatchEvent(new Event("submit",{cancelable:true}));},
    switch(){device="other";notify();},destroy(){editor.destroy();globalThis.document=previous;delete globalThis.__knowledgeBusy;}};
}
const controls=f=>f.nodes().filter(n=>["INPUT","SELECT","TEXTAREA","BUTTON"].includes(n.tagName)&&!n.classList.contains("mdo-layer-control"));

test("save freezes all fields and file operations; a failed write restores original disabled states",async()=>{
  const f=await fixture([file("index.md","# Index"),{path:"binary.bin",base64:"AAEC/w=="}]);
  try{
    const selector=f.nodes().find(n=>n.tagName==="SELECT");selector.value="1";selector.dispatchEvent(new Event("change"));
    const name=f.nodes().find(n=>n.name==="name"),source=f.nodes().find(n=>n.tagName==="TEXTAREA"&&n.className==="extension-source");
    name.value="Edited draft";f.form.dispatchEvent(new Event("input"));assert.equal(source.disabled,true);
    const before=new Map(controls(f).map(n=>[n,!!n.disabled]));
    f.submit();assert(controls(f).every(n=>n.disabled));assert.equal(f.form.getAttribute("aria-busy"),"true");
    f.nodes().find(n=>n.textContent==="新建文档").click();f.submit();assert.equal(f.calls.length,1);
    assert.equal(f.calls[0][1].files.length,2);assert.equal(JSON.parse(f.calls[0][1].content).name,"Edited draft");
    f.write.reject(Error("storage failed"));await tick();
    for(const [control,wasDisabled]of before)assert.equal(control.disabled,wasDisabled);
    assert.equal(name.value,"Edited draft");assert.equal(f.editor.hasPendingChanges(),true);assert.equal(f.changes,0);
    assert.equal(f.form.getAttribute("aria-busy"),"false");assert(f.nodes().some(n=>n.textContent==="storage failed"));
    f.pending();f.submit();f.write.resolve({data:{}});await tick();assert.equal(f.changes,1);assert.equal(f.editor.hasPendingChanges(),false);
  }finally{f.destroy();}
});

test("a pending file read excludes saves, file mutations and close; success preserves every imported file",async()=>{
  const f=await fixture(),read=deferred();
  try{
    const input=f.nodes().find(n=>n.type==="file");input.files=[{name:"imported.md",size:5,arrayBuffer:()=>read.promise}];input.dispatchEvent(new Event("change"));
    assert(controls(f).every(n=>n.disabled));f.submit();assert.equal(f.calls.length,0);
    f.nodes().find(n=>n.textContent==="移除文档").click();f.nodes().find(n=>n.textContent==="取消").click();assert(f.editor.hasPendingChanges());
    read.resolve(new TextEncoder().encode("hello").buffer);await tick();
    assert.equal(f.form.getAttribute("aria-busy"),"false");f.submit();
    assert.deepEqual(f.calls[0][1].files.map(v=>v.path),["index.md","imported.md"]);
    f.write.resolve({data:{}});await tick();assert.equal(f.changes,1);
  }finally{f.destroy();}
});

test("a failed multi-file read is atomic and keeps the editor usable",async()=>{
  const f=await fixture();
  try{
    const input=f.nodes().find(n=>n.type==="file");input.files=[{name:"first.md",size:1,arrayBuffer:async()=>new Uint8Array([1]).buffer},{name:"failed.md",size:1,arrayBuffer:async()=>{throw Error("read denied");}}];input.dispatchEvent(new Event("change"));await tick();
    assert(f.nodes().some(n=>n.textContent==="read denied"));assert.equal(input.disabled,false);
    f.submit();assert.deepEqual(f.calls[0][1].files.map(v=>v.path),["index.md"]);f.write.resolve({data:{}});await tick();
  }finally{f.destroy();}
});

test("late file reads from a previous device cannot populate the new editor",async()=>{
  const f=await fixture(),read=deferred();
  try{
    const input=f.nodes().find(n=>n.type==="file");input.files=[{name:"late.md",size:1,arrayBuffer:()=>read.promise}];input.dispatchEvent(new Event("change"));
    f.switch();assert.equal(f.editor.hasPendingChanges(),false);read.resolve(new Uint8Array([1]).buffer);await tick();assert.equal(f.calls.length,0);assert.equal(f.changes,0);
  }finally{f.destroy();}
});
