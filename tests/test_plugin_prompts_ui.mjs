import assert from "node:assert/strict";
import test from "node:test";
import { promptPolicy, promptChosen, updatePromptPolicy, createPluginPromptPicker, promptContributionDetails } from "../app/web/js/features/settings/plugin-prompts.js";
import { TestElement, testDocument, descendants } from "./helpers/extension-dom.mjs";

const item = {plugin:"plugin:qa.browser",key:"plugin:qa.browser/basic",id:"basic",alias:"Browser",version:"1",bytes:25,text:"Browser guidance",activation:"with-tools"};
test("role prompt choices preserve other sources, selections and explicit opt-in",()=>{
  const original={runtime:{max_turns:5},context:{"memory.global":false,"plugins.instructions":{include:["plugin:qa.browser","plugin:missing/guide"],exclude:[]}}};
  assert(promptChosen(promptPolicy(original),item));
  const off=updatePromptPolicy(original,"item",{item,checked:false});
  assert(!promptChosen(promptPolicy(off),item));assert.deepEqual(off.context["memory.global"],false);
  assert.deepEqual(off.runtime,original.runtime);assert(off.context["plugins.instructions"].include.includes("plugin:missing/guide"));
  assert(promptChosen(promptPolicy(updatePromptPolicy(off,"item",{item,checked:true})),item));
  assert(!promptChosen({}, {...item,activation:"explicit"}));
  assert(promptChosen({include:[item.key]}, {...item,activation:"explicit"}));
  assert(!promptChosen(promptPolicy(updatePromptPolicy(original,"mode","off")),item));
  assert(promptChosen(promptPolicy(updatePromptPolicy(original,"mode","automatic")),item));
  assert.deepEqual(original.context["plugins.instructions"].exclude,[]);
});

test("picker never changes configuration on load or on failed/invalid reads",async t=>{
  const previous=globalThis.document;globalThis.document=Object.assign(new EventTarget(),testDocument());t.after(()=>globalThis.document=previous);
  const input=new TestElement("textarea"),controller=new AbortController();t.after(()=>controller.abort());
  input.value=JSON.stringify({context:{"plugins.instructions":false},history:{mode:"current"}});const before=input.value;
  const calls=[];t.mock.method(globalThis,"fetch",async(url,options)=>{calls.push(JSON.parse(options.body));return Response.json({ok:true,data:{items:[item]}});});
  const picker=createPluginPromptPicker({input,signal:controller.signal});await new Promise(resolve=>setImmediate(resolve));
  assert.equal(input.value,before);assert.equal(calls[0].method,"plugin.prompts");assert(picker.textContent.includes("Browser"));
  const mode=picker.querySelector("select");mode.value="selected";mode.dispatchEvent(new Event("change"));
  const check=picker.querySelector("input");check.checked=true;check.dispatchEvent(new Event("change"));
  assert(promptChosen(promptPolicy(JSON.parse(input.value)),item));assert.equal(JSON.parse(input.value).history.mode,"current");
  input.value="{broken";input.dispatchEvent(new Event("input"));assert(mode.disabled);assert.equal(input.value,"{broken");
});

test("preview distinguishes candidates and shows source hashes as plain text",t=>{
  const previous=globalThis.document;globalThis.document=testDocument();t.after(()=>globalThis.document=previous);
  const view=promptContributionDetails({items:[{...item,text:"<script>untrusted()</script>",reason:"runtime_pending",revision:"REV",hash:"HASH"}]});
  assert(view.textContent.includes("候选"));assert(view.textContent.includes("REV / HASH"));
  assert(descendants(view).some(node=>node.tagName==="PRE"&&node.textContent==="<script>untrusted()</script>"));
  assert(!descendants(view).some(node=>node.tagName==="SCRIPT"));
});
