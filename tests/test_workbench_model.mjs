import assert from "node:assert/strict";
import test from "node:test";
import { panelIdentity, safeBrowserUrl, reorderPanel, normalizeLayout } from "../app/web/js/features/workbench/model.js";
import { createApplicationClient, applyApplicationAction } from "../app/web/js/api/application.js";
test("panel identities retain owner, device and immutable conversation", () => {
 const base={device:"a",project:"p",session:"s"};
 for(const change of [{device:"b"},{project:"b"},{session:"b"}])assert.notEqual(panelIdentity("plan","main",base),panelIdentity("plan","main",{...base,...change}));
 assert.notEqual(panelIdentity("plan","main",base,"one"),panelIdentity("plan","main",base,"two"));
});
test("URLs reject local files, scripts, credentials and oversized addresses", () => {
 for(const value of ["javascript:alert(1)","file:///C:/private","data:text/html,test","https://name:secret@example.com/","https://example.com/"+"x".repeat(9000)])assert.throws(()=>safeBrowserUrl(value));
 assert.equal(safeBrowserUrl("http://127.0.0.1:3000/"),"http://127.0.0.1:3000/");
});
test("tab ordering preserves identity and clamps restored geometry", () => {
 assert.deepEqual(reorderPanel(["a","b","c"],"c","a"),["c","a","b"]);
 assert.deepEqual(reorderPanel(["a","b"],"unknown","a"),["a","b"]);
 assert.equal(normalizeLayout({width:99999}).width,960);assert.equal(normalizeLayout({width:12}).width,320);
 assert.deepEqual(normalizeLayout({order:["a","a",2]}).order,["a"]);
});
test("SDK binding freezes the project before later navigation", async () => {
 let project="first",body;
 globalThis.fetch=async(_,options)=>{body=JSON.parse(options.body);return Response.json({ok:true,data:{}});};
 const api=createApplicationClient(()=>({project,extension:"plugin.test"})),bound=api.bind({session:"old"});project="next";
 await bound.call("sdk.context");assert.equal(body.context.project,"first");assert.equal(body.context.extension,"plugin.test");assert.equal(body.context.session,"old");
});
test("large UI results keep a compact journal without replaying the action", async () => {
 let text=null,calls=0;const storage={getItem:()=>text,setItem:(_,value)=>{assert.ok(value.length<20000);text=value;}};
 const action={id:"snapshot"};const result=await applyApplicationAction(action,()=>{calls++;return {data:"x".repeat(100000)};},storage,"test");
 assert.equal(result.result.data.length,100000);assert.equal((await applyApplicationAction(action,()=>calls++,storage,"test")).result.ephemeral,true);assert.equal(calls,1);
});
