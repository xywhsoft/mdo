import assert from "node:assert/strict";
import test from "node:test";
import { testDocument,descendants } from "./helpers/extension-dom.mjs";
import { dependencyList,localDependencyPanel } from "../app/web/js/features/settings/extension-dependencies.js";
import { reviewDependencyPlan } from "../app/web/js/features/settings/dependency-plan.js";
import { extensionRef } from "../app/web/js/features/settings/directory-documents.js";
const flush=async()=>{for(let i=0;i<12;i++)await new Promise(r=>setImmediate(r));};
const button=(root,text)=>descendants(root).find(n=>n.tagName==="BUTTON"&&n.textContent===text);
function dom(t){const old={document:globalThis.document,window:globalThis.window};globalThis.document=testDocument();globalThis.window=new EventTarget();t.after(()=>Object.assign(globalThis,old));}
const plan=()=>({id:"a".repeat(32),revision:"b".repeat(64),ready:true,items:[{id:"skill:qa.consumer",kind:"skills",name:"Consumer",operation:"flags",desired_enabled:true},{id:"mcp:qa.repo",kind:"mcp",name:"Repository",operation:"install"}]});

test("all dependency semantics and reverse users are displayed as inert text",t=>{
 dom(t);const view=dependencyList({dependencies:[{id:"tool:qa.git",reason:"Inspect <script>diff</script>"},{id:"knowledge:qa.xrt",optional:true,available:false}],conflicts:[{id:"skill:qa.old",reason:"Exclusive workflow"}],dependents:[{id:"agent:qa.main",name:"Main"}],dependency_issues:[{code:"dependency_missing",chain:["skill:qa.consumer","tool:qa.git"],message:"Install provider"}]});
 for(const text of ["必需","可选","Inspect <script>diff</script>","尚未安装或不可用","不能同时启用","skill:qa.old","agent:qa.main","skill:qa.consumer → tool:qa.git"])assert(view.textContent.includes(text),text);assert.equal(view.querySelectorAll("script").length,0);
});

test("typed IDs respect kind, case, namespace and path boundaries",()=>{
 assert(extensionRef("external-tool:qa.runtime","external-tools"));assert(extensionRef("knowledge:mdo.xrt","knowledge"));for(const id of ["skill:QA.tool","tool:qa.tool","skill:qa..tool","skill:../tool","skill:qa:tool"])assert(!extensionRef(id,"skills"),id);
});

test("MCP binding shows only eligible private connections and uses a revision",async t=>{
 dom(t);const calls=[];t.mock.method(globalThis,"fetch",async(url,o)=>{if(o.method==="POST"){calls.push(JSON.parse(o.body));return Response.json({ok:true,data:{revision:"c".repeat(64)}});}if(url.endsWith("/mcp/connections"))return Response.json({ok:true,data:[{id:"one",name:"Account one",extension_id:"mcp:qa.repo",enabled:true},{id:"two",name:"Account two",extension_id:"mcp:qa.repo",enabled:true},{id:"wrong",extension_id:"mcp:qa.other",enabled:true},{id:"project",extension_id:"mcp:qa.repo",enabled:true,project_id:"project-one"},{id:"off",extension_id:"mcp:qa.repo",enabled:false}]});return Response.json({ok:true,data:{id:"skill:qa.consumer",directory_id:"consumer",bindings_revision:"b".repeat(64),dependency_details:[{id:"mcp:qa.repo",available:true}]}});});
 const view=localDependencyPanel("skills",{id:"skill:qa.consumer",directory_id:"consumer"});document.body.append(view);await flush();const select=view.querySelector("select");assert(select);assert.equal(select.value,"");assert.deepEqual(select.children.map(row=>row.value),["","one","two"]);select.value="two";button(view,"保存连接绑定").click();await flush();assert.deepEqual(calls[0],{action:"bind",dependency_id:"mcp:qa.repo",connection_id:"two",scope:"global",expected_binding_revision:"b".repeat(64)});assert(view.textContent.includes("绑定已保存"));
});

test("cancelled dependency review discards only the unattempted host plan",async t=>{
 dom(t);const calls=[];t.mock.method(globalThis,"fetch",async(url,o)=>{calls.push(JSON.parse(o.body));return Response.json({ok:true,data:{}});});const input=plan(),pending=reviewDependencyPlan(input);const dialog=document.body.querySelector("dialog");assert(button(dialog,"应用此计划").disabled);button(dialog,"取消").click();assert.equal(await pending,null);await flush();assert.deepEqual(calls,[{action:"discard_plan",plan_id:input.id,revision:input.revision}]);
});

test("lost replies retry the identical pinned plan and require deliberate trust",async t=>{
 dom(t);let lost=true;const calls=[];t.mock.method(globalThis,"fetch",async(url,o)=>{calls.push(JSON.parse(o.body));if(lost){lost=false;throw Error("connection interrupted");}return Response.json({ok:true,data:{runtime_ready:true,plan_id:plan().id}});});const pending=reviewDependencyPlan(plan()),dialog=document.body.querySelector("dialog"),apply=button(dialog,"应用此计划"),trust=dialog.querySelector("input");assert(apply.disabled);trust.checked=true;trust.dispatchEvent(new Event("change"));apply.click();await flush();assert(dialog.textContent.includes("可重试此计划"));assert(!apply.disabled);apply.click();assert((await pending).runtime_ready);assert.deepEqual(calls[0],calls[1]);assert.equal(calls.length,2);await flush();assert(!document.body.querySelector("dialog"));
});

test("committed files with pending runtime configuration retain their receipt and explanation",async t=>{
 dom(t);const calls=[];t.mock.method(globalThis,"fetch",async(url,o)=>{calls.push(JSON.parse(o.body));return Response.json({ok:true,data:{runtime_ready:false,runtime_error:"Choose the private account"}});});const pending=reviewDependencyPlan(plan()),dialog=document.body.querySelector("dialog"),trust=dialog.querySelector("input");let returned=false;void pending.then(()=>returned=true);trust.checked=true;trust.dispatchEvent(new Event("change"));button(dialog,"应用此计划").click();await flush();assert(!returned);assert(dialog.textContent.includes("Choose the private account"));button(dialog,"关闭").click();const receipt=await pending;assert.equal(receipt.runtime_ready,false);await flush();assert.equal(calls.length,1);assert(!document.body.querySelector("dialog"));
});
