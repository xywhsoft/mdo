import test from "node:test";
import assert from "node:assert/strict";
import { testDocument, TestElement, descendants } from "./helpers/extension-dom.mjs";
import { createExtensionPanels } from "../app/web/js/features/settings/extension-panels.js";
import { resourceCredits, customizableResource } from "../app/web/js/features/settings/resource-credits.js";
import { expandSkillAuthoringDraft } from "../app/web/js/features/settings/skill-authoring.js";
const flush=()=>new Promise(r=>setImmediate(r));

test("Skill and Agent-tool local lists restrict installed works, retain user edits and show distinct credits",async t=>{
  const old={document:globalThis.document,window:globalThis.window,fetch:globalThis.fetch};
  const roots=new Map(["skills","tools","commands"].map(k=>[k,new TestElement("div")])),doc=testDocument();
  doc.querySelector=s=>roots.get(s.match(/^\[data-extension-manager="([^"]+)"\]$/)?.[1])||null;
  globalThis.document=doc;globalThis.window=new EventTarget();
  const item=(kind,id,managed)=>({id,name:`${managed?"Store":"Local"} ${kind}`,description:"User description",path:kind==="skills"?`skills/${id}/SKILL.md`:`${kind}/${id}.c`,enabled:true,valid:true,external:true,management_known:true,store_managed:managed});
  const local=Object.fromEntries(["skills","tools","commands"].map(kind=>[kind,[item(kind,"published",true),item(kind,"mine",false)]]));
  const receipts=Object.fromEntries(["skills","tools","commands"].map((kind,i)=>[String(i+1),{id:i+1,source:"store",submitter:"publisher_username",author:"Account nickname",version:"1.2.0",manifest:{author:"Original Developers",copyright:"Copyright Original Developers",license:"MIT"},files:[{path:local[kind][0].path}]}]));
  globalThis.fetch=async url=>Response.json({ok:true,data:url==="/api/v1/ecosystem"?receipts:url==="/api/v1/tools"?{items:[]}:{items:local[url.split("/").at(-1)]||[]}});
  const panels=createExtensionPanels();t.after(()=>{panels.destroy();Object.assign(globalThis,old);});
  for(const kind of ["skills","tools","commands"]){
    panels.selectSection(kind);await flush();await flush();
    const root=roots.get(kind),rows=root.querySelectorAll(".eb-row");assert.equal(rows.length,2);
    const published=rows.find(r=>r.dataset.key==="published"),mine=rows.find(r=>r.dataset.key==="mine");
    assert(published.textContent.includes("publisher_username")&&published.textContent.includes("Original Developers")&&published.textContent.includes("Copyright Original Developers"));
    const labels=descendants(published).filter(n=>n.tagName==="BUTTON").map(n=>n.textContent);
    for(const forbidden of ["编辑","导出","分享到创作中心","用 AI 改进","删除"])assert(!labels.includes(forbidden),`${kind}: ${forbidden}`);
    assert(labels.includes("检查更新")&&labels.includes("卸载商店资源"));assert(published.querySelector(".eb-switch"));
    const own=descendants(mine).filter(n=>n.tagName==="BUTTON").map(n=>n.textContent);assert(own.includes("编辑")&&own.includes("导出")&&own.includes("分享到创作中心"));
    published.querySelector(".eb-open").click();const pane=document.body.querySelector(".eb-detail");assert(pane.textContent.includes("Original Developers")&&pane.textContent.includes("MIT"));
  }
});

test("provenance stays authoritative after edits and legacy display names are never presented as usernames",()=>{
  assert(!customizableResource({store_managed:true}));assert(!customizableResource({management_known:false}));assert(customizableResource({}, {source:"local"}));
  const credits=resourceCredits({store_managed:true},{source:"store",author:"Old nickname",copyright:"Full attribution"});
  assert.notEqual(credits.submitter,"Old nickname");assert.equal(credits.author,"Old nickname");assert.equal(credits.copyright,"Full attribution");
});

test("typing an installed Skill ID into the builder cannot open a write-oriented authoring draft",async t=>{
  const previous=globalThis.fetch;t.after(()=>{globalThis.fetch=previous;});let invocations=0;
  globalThis.fetch=async url=>{if(url.includes("/extensions/skills/"))return Response.json({ok:true,data:{store_managed:true}});invocations++;return Response.json({ok:true,data:{text:"Review only"}});};
  const settings={directory:"D:/home/skills",scope:"user",mode:"improve",id:"published"};
  await assert.rejects(expandSkillAuthoringDraft(settings));assert.equal(invocations,0);
  assert.equal(await expandSkillAuthoringDraft({...settings,mode:"review"}),"Review only");assert.equal(invocations,1);
});
