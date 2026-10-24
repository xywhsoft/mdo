import assert from "node:assert/strict";
import test from "node:test";
import {readFileSync} from "node:fs";
import {testDocument,descendants} from "./helpers/extension-dom.mjs";
import {knowledgeMetadata,parseKnowledge,knowledgeBase64,knowledgeText,knowledgeFolder,validateKnowledgeFiles} from "../app/web/js/features/settings/knowledge-format.js";
const file=(path,text)=>({path,base64:knowledgeBase64(new TextEncoder().encode(text))});
const doc=()=>knowledgeMetadata({name:"XRT",description:"Reference"});
const flush=()=>new Promise(resolve=>setImmediate(resolve));
test("knowledge separates user description, model hint and portable document paths",()=>{
 const d=parseKnowledge(JSON.stringify(doc()));assert.equal(d.description,"Reference");assert(d.prompt.includes("{$path}"));
 assert.equal(knowledgeText(file("index.md","中文索引")),"中文索引");
 assert.throws(()=>parseKnowledge(JSON.stringify({...d,install:"curl bad"})),/不支持/);
 assert.throws(()=>validateKnowledgeFiles(d,[file("../outside.md","bad")]));
 assert.throws(()=>validateKnowledgeFiles(d,[file("index.md","index"),file("INDEX.md","duplicate")]));
 assert.throws(()=>validateKnowledgeFiles(d,[file("index.md","index"),file("docs","a"),file("docs/entry.md","b")]));
 assert.throws(()=>validateKnowledgeFiles(d,[file("index.md","index"),file("Knowledge.json/entry.md","b")]));
 assert.throws(()=>validateKnowledgeFiles(d,[file("other.md","missing index")]));
});
test("knowledge folder imports preserve hierarchy and original binary bytes without machine paths",async()=>{
 const a=new File(["# Index"],"index.md");Object.defineProperty(a,"webkitRelativePath",{value:"xrt/index.md"});
 const b=new File([new Uint8Array([0,1,2,255])],"sample.pdf");Object.defineProperty(b,"webkitRelativePath",{value:"xrt/assets/sample.pdf"});
 const imported=await knowledgeFolder([a,b]);assert.equal(imported.id,"xrt");assert.deepEqual(imported.files.map(f=>f.path),["index.md","assets/sample.pdf"]);assert.equal(imported.files[1].base64,"AAEC/w==");validateKnowledgeFiles(JSON.parse(imported.content),imported.files);
});
test("knowledge editor keeps drafts on conflicts and same-device notifications; buttons use the common footer",async()=>{
 const previous=globalThis.document;globalThis.document=testDocument();const create=document.createElement.bind(document);document.createElement=tag=>{const e=create(tag);if(tag==="form"){const set=e.setAttribute.bind(e);e.setAttribute=(key,value)=>{if(key==="id")e.attributes.id=value;else set(key,value);};Object.defineProperty(e,"id",{get(){return descendants(e).find(n=>n.name==="id")||e.attributes.id;}});}return e;};let device="local",notify,changes=0,reject=true;const calls=[];
 globalThis.__knowledgeTest={api:{get:async()=>({data:{content:JSON.stringify(doc()),files:[file("index.md","# Index")],bundle_revision:"bundle-digest"}}),put:async(...args)=>{calls.push(args);if(reject)throw Error("knowledge_bundle_conflict");return{data:{}};}},targetState:()=>({selected:{id:device}}),subscribeTarget:fn=>{notify=fn;return()=>{};}};
 const url=new URL("../app/web/js/features/settings/knowledge-editor.js",import.meta.url),source=readFileSync(url,"utf8").replace('import { api } from "../../api/client.js";',"const {api}=globalThis.__knowledgeTest;").replace('import { targetState, subscribeTarget } from "../../api/target.js";',"const {targetState,subscribeTarget}=globalThis.__knowledgeTest;").replace(/from "(\.[^"]+)"/g,(_,p)=>`from ${JSON.stringify(new URL(p,url).href)}`);
 let editor;try{const {createKnowledgeEditor}=await import("data:text/javascript;base64,"+Buffer.from(source).toString("base64"));editor=createKnowledgeEditor({owner:document.body,onChange:()=>changes++});await editor.open({item:{id:"xrt",revision:"metadata-digest"}});
   const nodes=()=>descendants(document.body),form=nodes().find(n=>n.tagName==="FORM"),save=nodes().find(n=>n.tagName==="BUTTON"&&n.textContent==="保存");
   assert(save.parentElement.className.includes("footer"));assert.equal(save.getAttribute("form"),form.getAttribute("id"));notify();assert(nodes().includes(form));
   form.dispatchEvent(new Event("submit",{cancelable:true}));await flush();assert.equal(calls[0][1].bundle_revision,"bundle-digest");assert.equal(calls[0][2].ifMatch,'"metadata-digest"');assert(nodes().some(n=>n.textContent?.includes("knowledge_bundle_conflict")));assert.equal(changes,0);
   reject=false;form.dispatchEvent(new Event("submit",{cancelable:true}));await flush();assert.equal(changes,1);
   await editor.open();device="other";notify();assert.equal(editor.hasPendingChanges(),false);
 }finally{editor?.destroy();globalThis.document=previous;delete globalThis.__knowledgeTest;}
});
