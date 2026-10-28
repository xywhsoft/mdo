import assert from "node:assert/strict";
import test from "node:test";
import { newRole, roleAdvanced, parseRoleAdvanced, roleProgramTemplate, openRolePlan } from "../app/web/js/features/settings/agent-authoring.js";
import { editPrompt, promptFields } from "../app/web/js/features/settings/extension-formats.js";
import { TestElement, testDocument, descendants } from "./helpers/extension-dom.mjs";

test("role form preserves JSON configuration and explicit empty tool selection",()=>{
  for(const kind of ["agents","subagents"]){
    const doc=promptFields(newRole(kind));assert.equal(doc.schema,"mdo.agent.v1");assert.equal(doc.tools,"inherit");
    assert.equal(doc.mode,kind === "agents"?"primary":"subagent");
  }
  const source=JSON.stringify({schema:"mdo.agent.v1",name:"A",tools:[],context:{"memory.global":false},runtime:{max_turns:9},custom_metadata:{author:"creator"},prompt:"P"});
  assert.deepEqual(roleAdvanced(source),{context:{"memory.global":false},runtime:{max_turns:9}});
  const changed=JSON.parse(editPrompt(source,{name:"B",runtime:undefined,prompt:"Other"}));
  assert.deepEqual(changed.tools,[]);assert.equal(changed.runtime,undefined);assert.deepEqual(changed.custom_metadata,{author:"creator"});
  assert.deepEqual(parseRoleAdvanced('{"skills":[],"mcp_servers":[]}'),{skills:[],mcp_servers:[]});
  assert.throws(()=>parseRoleAdvanced('{"runtim":{}}'));assert.throws(()=>parseRoleAdvanced('[]'));
  for(const kind of ["agents","subagents"]){const text=roleProgramTemplate(kind,"sample");assert(text.includes("MDO_AGENT_MODULE(Role)"));assert(text.includes(`${kind === "agents"?"agent":"subagent"}.sample`));}
});

test("role inspector displays source diagnostics in a shared window without saving or executing code",async t=>{
  const previous={document:globalThis.document,window:globalThis.window};
  globalThis.document=Object.assign(new EventTarget(),testDocument());globalThis.window=new EventTarget();
  t.after(()=>Object.assign(globalThis,previous));
  const calls=[];t.mock.method(globalThis,"fetch",async(url,options)=>{
    calls.push({url,method:options.method,body:JSON.parse(options.body)});
    return Response.json({ok:true,data:{system_prompt:"System fixture",user_context:"User fixture",sources:[{id:"system",enabled:true,role:"system",order:0,text:"System fixture",truncated:false}]}});
  });
  await openRolePlan({kind:"subagents",id:"sample",content:newRole("subagents"),owner:document.body});
  const dialog=document.body.querySelector("dialog");assert(dialog.classList.contains("mdo-layer"));
  assert(dialog.textContent.includes("System fixture"));assert(dialog.textContent.includes("不运行 C 回调"));
  assert.equal(calls[0].method,"POST");assert.equal(calls[0].url,"/api/v1/extensions/subagents/sample/plan?project=default");
  assert.equal(calls.length,1);
  descendants(dialog).find(node=>node.tagName === "BUTTON"&&node.textContent === "关闭").click();
});

test("both role editors save explicit no-tools and expose identical C/configuration controls",async t=>{
  const previous={document:globalThis.document,window:globalThis.window};
  globalThis.document=Object.assign(new EventTarget(),testDocument());globalThis.window=new EventTarget();window.confirm=()=>true;
  const roots=new Map(["agents","subagents"].map(kind=>[kind,new TestElement("div")]));
  for(const root of roots.values())document.body.append(root);
  document.querySelector=selector=>roots.get(selector.match(/data-extension-manager="([^"]+)"/)?.[1])||null;
  const writes=[];t.mock.method(globalThis,"fetch",async(url,options)=>{
    if(options.method==="PUT")writes.push({url,body:JSON.parse(options.body)});
    return Response.json({ok:true,data:{items:[]}});
  });
  const {createExtensionPanels}=await import("../app/web/js/features/settings/extension-panels.js");
  const panel=createExtensionPanels();t.after(()=>{panel.destroy();Object.assign(globalThis,previous);});
  const flush=()=>new Promise(resolve=>setImmediate(resolve));
  const button=(root,text)=>descendants(root).find(n=>n.tagName==="BUTTON"&&n.textContent===text);
  for(const kind of ["agents","subagents"]){
    panel.selectSection(kind);await flush();button(roots.get(kind),"新建").click();await flush();
    const dialog=document.body.querySelector("dialog"),form=dialog.querySelector("form");
    dialog.querySelector("input").value=`config-${kind}`;
    for(const control of form.elements){if(control.name==="name"||control.name==="description")control.value="Role fixture";}
    assert(form.elements.find(n=>n.name==="code"));assert(form.elements.find(n=>n.name==="role_options"));
    const inherit=dialog.querySelector("fieldset").querySelector("input");inherit.checked=false;inherit.dispatchEvent(new Event("change"));
    button(dialog,"保存").click();await flush();
    assert.equal(writes.at(-1).url,`/api/v1/extensions/${kind}/config-${kind}`);
    assert.deepEqual(promptFields(writes.at(-1).body.edits.find(file=>file.path==="agent.md").text).tools,[]);
    assert.equal(document.body.querySelector("dialog"),null);
  }
});

