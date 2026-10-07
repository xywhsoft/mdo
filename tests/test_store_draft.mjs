import assert from "node:assert/strict";
import test from "node:test";
import { createStoreDraft } from "../app/web/js/features/settings/store-draft.js";
import { resourceReferences, resourceOwner, receiptKey } from "../app/web/js/features/settings/store-formats.js";

test("draft edits during save are persisted with the returned revision",async()=>{
  let value="first",release;const calls=[];
  const draft=createStoreDraft({id:"draft-test",snapshot:()=>({fields:{name:value},references:[]}),save:async input=>{calls.push(input);if(calls.length===1)await new Promise(done=>{release=done;});return {revision:`r${calls.length}`};}});
  assert.equal(draft.pending,false);
  const saving=draft.flush();value="second";draft.schedule();release();await saving;
  assert.equal(calls.length,2);assert.equal(calls[1].revision,"r1");assert.equal(calls[1].draft.fields.name,"second");assert.equal(draft.revision,"r2");
  draft.dispose();assert.equal(draft.pending,false);
});
test("save conflict preserves dirty fields and retries with the original revision",async()=>{
  let calls=0;const draft=createStoreDraft({id:"draft-test",revision:"original",snapshot:()=>({fields:{name:"kept"},references:[]}),save:async input=>{assert.equal(input.revision,"original");if(++calls===1)throw Object.assign(Error("changed"),{status:412});return {revision:"updated"};}});
  await assert.rejects(draft.flush());assert.equal(draft.pending,true);await draft.flush();assert.equal(draft.revision,"updated");draft.dispose();
});
test("legacy receipt paths still link to their resource managers",()=>{
  const row={id:3,files:[{path:"commands/review.md"},{path:"skills/check/SKILL.md"},{path:"skills/check/references/a.md"},{path:"modules/agents/custom.c"}]};
  assert.deepEqual(resourceReferences(row),[{kind:"commands",id:"review"},{kind:"skills",id:"check"},{kind:"c-agents",id:"custom"}]);
  assert.equal(resourceOwner({3:row},"commands/review.md"),row);assert.equal(receiptKey(row),"3");assert.equal(receiptKey({key:"local:review",id:0}),"local:review");
});
