import assert from "node:assert/strict";
import test from "node:test";
import { resourceReferences, resourceOwner, receiptKey } from "../app/web/js/features/settings/store-formats.js";

test("legacy receipt paths still link to their resource managers",()=>{
  const row={id:3,files:[{path:"commands/review.md"},{path:"skills/check/SKILL.md"},{path:"skills/check/references/a.md"},{path:"modules/agents/custom.c"}]};
  assert.deepEqual(resourceReferences(row),[{kind:"commands",id:"review"},{kind:"skills",id:"check"},{kind:"c-agents",id:"custom"}]);
  assert.equal(resourceOwner({3:row},"commands/review.md"),row);assert.equal(receiptKey(row),"3");assert.equal(receiptKey({key:"local:review",id:0}),"local:review");
});
