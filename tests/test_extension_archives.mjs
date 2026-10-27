import assert from "node:assert/strict";
import { createHash, randomBytes } from "node:crypto";
import { readFileSync } from "node:fs";
import test from "node:test";
import { createDownloadRegistry } from "../app/web/js/state/downloads.js";

async function loader(mock){
 globalThis.__archiveTest=mock;const url=new URL("../app/web/js/api/extension-archives.js",import.meta.url);
 const source=readFileSync(url,"utf8").replace('import { api, uploadExtensionChunk } from "./client.js";',"const {api,uploadExtensionChunk}=globalThis.__archiveTest;").replace(/from "(\.[^"]+)"/g,(_,path)=>`from ${JSON.stringify(new URL(path,url).href)}`);
 return import("data:text/javascript;base64,"+Buffer.from(source+`\n//${Math.random()}`).toString("base64"));
}
test("file staging hashes incrementally and transfers raw bytes with a resumable task",async()=>{
 const bytes=randomBytes(1048593),file=new Blob([bytes]),id="0123456789ABCDEFGHJKMNPQRSTVWXYZ",chunks=[],calls=[];let received=0,task;
 const mock={api:{post:async(path,body)=>{calls.push({path,body});if(path==="/extension-tasks"){task={...body,id,received:0,state:"receiving",active:false};return{data:task};}assert.equal(path,`/extension-tasks/${id}/seal`);return{data:{...task,state:"ready"}};},get:async()=>({data:{...task,received,state:"receiving"}})},uploadExtensionChunk:async(taskId,offset,chunk)=>{assert.equal(taskId,id);assert(chunk instanceof Uint8Array);chunks.push({offset,chunk});received=offset+chunk.length;return{data:{received}};}};
 try{const{stageExtensionFile}=await loader(mock);const result=await stageExtensionFile(file,{kind:"mcp"});assert.equal(result.state,"ready");assert.equal(calls[0].body.sha256,createHash("sha256").update(bytes).digest("hex"));assert.equal(chunks.length,2);assert.deepEqual(Buffer.concat(chunks.map(c=>Buffer.from(c.chunk))),bytes);assert(!JSON.stringify(calls).includes("base64"));
  received=12345;chunks.length=0;await stageExtensionFile(file,{kind:"mcp",taskId:id});assert.equal(chunks[0].offset,12345);assert.deepEqual(Buffer.concat(chunks.map(c=>Buffer.from(c.chunk))),bytes.subarray(12345));
  await assert.rejects(stageExtensionFile(new Blob(["other"]),{kind:"mcp",taskId:id}),/不符/);
 }finally{delete globalThis.__archiveTest;}
});
test("global task history reflects native byte progress, cancellation and retained publication metadata",()=>{
 const r=createDownloadRegistry(),id="0123456789ABCDEFGHJKMNPQRSTVWXYZ",task={id,kind:"mcp",purpose:"install",state:"receiving",bytes:100,received:25,store:{xid:"work",version_id:"version"}};
 r.observe("/extension-tasks",[task],"home");assert.equal(r.list("home")[0].state,"downloading");assert.equal(r.list("home")[0].done,25);assert.equal(r.list("company").length,0);
 r.observe(`/extension-tasks/${id}`,{...task,state:"cancel_requested",active:true},"home");assert.equal(r.list("home")[0].state,"cancelling");
 r.observe("/extension-tasks",[{...task,state:"ready",active:true,phase:"submitting",sent:100,publication:{upload:id,nonce:id,publish:true}}],"home");assert.equal(r.list("home")[0].state,"uploading");assert.equal(r.list("home")[0].done,100);
 r.observe("/extension-tasks",[{...task,state:"ready",active:false}],"home");assert.equal(r.list("home")[0].state,"ready");r.hide(r.list("home")[0].key);assert.equal(r.list("home").length,0);
});
