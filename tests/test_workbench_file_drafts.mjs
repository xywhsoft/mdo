import test from "node:test";
import assert from "node:assert/strict";
import { createFileDrafts } from "../app/web/js/features/workbench/file-drafts.js";
const storage = () => { const values = new Map(); return { values, getItem:key=>values.get(key)??null,setItem:(key,value)=>values.set(key,value) }; };
const context={device:"local",target:"device-one",project:"p",extension:"local-client"};
const value=(text,savedAt=1)=>({text,savedAt,revision:"v"});
test("saving one pane never clears a different pane's unsaved working copy",()=>{
 const store=storage(),a=createFileDrafts({context,panel:"a",storage:()=>store}),b=createFileDrafts({context,panel:"b",storage:()=>store});
 a.write("/file",value("first",1));b.write("/file",value("second",2));a.clear("/file");
 assert.equal(a.read("/file"),null);assert.equal(b.read("/file").text,"second");
 const reopen=createFileDrafts({context,panel:"reopen",storage:()=>store});assert.equal(reopen.read("/file").text,"second");
});
test("target and owner domains stay separate even when the service calls itself local",()=>{
 const store=storage(),a=createFileDrafts({context,panel:"same",storage:()=>store});a.write("/file",value("private"));
 for(const next of [{...context,target:"device-two"},{...context,extension:"other"},{...context,project:"other"}])assert.equal(createFileDrafts({context:next,panel:"same",storage:()=>store}).read("/file"),null);
});
test("rename recovery can use its old path until the new copy is durably stored",()=>{
 const store=storage(),a=createFileDrafts({context,panel:"a",storage:()=>store});a.write("/old",value("working"));
 assert.equal(a.read("/new","/old").text,"working");a.write("/new",value("working"));a.clear("/old");assert.equal(a.read("/new").text,"working");
});
test("legacy drafts remain available only through explicit recovery",()=>{
 const store=storage(),a=createFileDrafts({context,panel:"a",storage:()=>store});store.setItem('mdo.file.draft.'+JSON.stringify([context.device,context.project,"/file"]),JSON.stringify(value("legacy")));
 assert.equal(a.read("/file"),null);assert.equal(a.legacy("/file").text,"legacy");
});
test("a clean editor stays clean after a directory move even when another editor has a draft",()=>{
 const store=storage(),a=createFileDrafts({context,panel:"a",storage:()=>store}),b=createFileDrafts({context,panel:"b",storage:()=>store});
 a.write("/old/file",value("dirty"));b.clear("/old/file");a.write("/new/file",a.own("/old/file"));
 assert.equal(b.read("/new/file","/old/file"),null);b.write("/new/file",b.own("/old/file"));assert.equal(b.read("/new/file"),null);assert.equal(a.read("/new/file").text,"dirty");
});
