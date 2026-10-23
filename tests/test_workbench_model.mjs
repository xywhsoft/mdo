import assert from "node:assert/strict";
import test from "node:test";
import { panelIdentity, safeBrowserUrl, browserLocation, browserPhase, reorderPanel, normalizeLayout, validateImageFrame } from "../app/web/js/features/workbench/model.js";
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
test("home and browser share URL/search rules including local ports and IPv6", () => {
 for (const [input, output] of [["baidu.com", "https://baidu.com/"], ["localhost:3000/a?q=b", "http://localhost:3000/a?q=b"],
   ["127.0.0.1:9000", "http://127.0.0.1:9000/"], ["[::1]:3000/", "http://[::1]:3000/"],
   ["[2001:db8::1]/", "https://[2001:db8::1]/"], [" https://example.com/a ", "https://example.com/a"]])assert.equal(browserLocation(input), output);
 assert.equal(new URL(browserLocation("帮助我查资料")).searchParams.get("q"), "帮助我查资料");
 for(const input of ["", "file:///tmp/secret", "javascript:alert(1)", "https://user:password@example.com", "a".repeat(9000)])assert.throws(()=>browserLocation(input));
});
test("browser states never turn an error or a pending navigation into ready", () => {
 assert.equal(browserPhase({url:"about:blank",loading:false}),"empty");
 assert.equal(browserPhase({url:"https://example.com",loading:true}),"loading");
 assert.equal(browserPhase({url:"https://example.com",loading:true,error:"DNS failed"}),"error");
 assert.equal(browserPhase({url:"https://example.com",loading:false,error:"Connection refused"}),"error");
 assert.equal(browserPhase({url:"https://example.com",loading:false}),"ready");
});

test("surface frames reject decompression-sized images before decoding", () => {
 const bytes=new Uint8Array(24);bytes.set([137,80,78,71,13,10,26,10]);const data=new DataView(bytes.buffer);
 data.setUint32(16,1920);data.setUint32(20,1080);assert.deepEqual(validateImageFrame(bytes),{width:1920,height:1080});
 data.setUint32(16,8192);data.setUint32(20,8192);assert.throws(()=>validateImageFrame(bytes),/1600/);
 assert.throws(()=>validateImageFrame(new Uint8Array([255,216,255,192])),/1600/);
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
