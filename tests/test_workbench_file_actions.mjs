import test from "node:test";
import assert from "node:assert/strict";
import { createFileActions, filePathKey, fileParent, relocatedPath, validateEntryName } from "../app/web/js/features/workbench/file-actions.js";

test("path remapping respects component boundaries and case-sensitive entries",()=>{
 assert.equal(fileParent("C:\\file.txt"),"C:/");assert.equal(fileParent("/file.txt"),"/");
 assert.equal(filePathKey("C:\\Project\\a"),"c:/Project/a");assert.notEqual(filePathKey("C:/A"),filePathKey("C:/a"));
 assert.equal(relocatedPath("C:\\root/nested/file.txt","C:/root/nested","C:/root/renamed",true),"C:/root/renamed/file.txt");
 assert.equal(relocatedPath("/root/foobar/a","/root/foo","/root/new",true),"/root/foobar/a");assert.equal(relocatedPath("/root/a","/root/A","/root/new"),"/root/a");
});
test("entry names reject traversal and Windows devices without imposing Windows rules on POSIX",()=>{
 for(const value of ["", ".", "..", "../file", "dir\\file", "bad\0name"])assert.throws(()=>validateEntryName(value,"/root"));
 for(const value of ["NUL.txt","COM¹.log","LPT9","bad.","bad ","a:b","a?b"])assert.throws(()=>validateEntryName(value,"C:/root"));
 assert.equal(validateEntryName("小说 🌙.txt","C:/root"),"小说 🌙.txt");assert.equal(validateEntryName("a:b","/root"),"a:b");
});
test("rename and remove carry captured revisions and never replace a target",async()=>{
 const calls=[],events=[],target=new EventTarget();target.addEventListener("changed",e=>events.push(e.detail));
 const actions=createFileActions({sdk:{call:async(...args)=>{calls.push(args);return {}; }},context:{project:"p"},signal:new AbortController().signal,directory:()=>"/root",events:target,ask:()=>"new.txt",approve:()=>true});
 await actions.rename({path:"/root/old.txt",revision:"old-version",type:1});await actions.remove({path:"/root/folder",revision:"folder-version",type:2});
 assert.deepEqual(calls.map(call=>call[0]),["files.move","files.remove"]);assert.equal(calls[0][1].revision,"old-version");assert.equal(calls[0][1].replace,false);assert.equal(calls[1][1].recursive,true);
 assert.deepEqual(events.map(e=>[e.kind,e.project]),[["move","p"],["remove","p"]]);
 await assert.rejects(actions.remove({path:"/root",revision:"root",type:2}),/根目录/);await assert.rejects(actions.rename({path:"/root/unknown"}),/尚未读取/);assert.equal(calls.length,2);
});
test("a conflict refreshes once and requires a new decision rather than repeating effects",async()=>{
 let calls=0,refresh=0;
 const actions=createFileActions({sdk:{call:async()=>{calls++;throw {status:412};}},context:{},signal:new AbortController().signal,directory:()=>"/root",ask:()=>"new",refresh:()=>{refresh++;}});
 await assert.rejects(actions.rename({path:"/root/old",revision:"v"}),/重新确认/);assert.equal(calls,1);assert.equal(refresh,1);assert.equal(actions.busy,false);
});
test("creation is exclusive and a cancelled deletion has no effect",async()=>{
 const calls=[];const actions=createFileActions({sdk:{call:async(...args)=>{calls.push(args);return {}; }},context:{},signal:new AbortController().signal,directory:()=>"/root",ask:()=>"new",approve:()=>false});
 await actions.create();await actions.create("/root",true);await actions.remove({path:"/root/old",type:1,revision:"v"});
 assert.deepEqual(calls.map(call=>call[0]),["files.create","files.mkdir"]);assert.equal(calls[1][1].exclusive,true);assert.equal(calls[0][1].text,undefined);
});
