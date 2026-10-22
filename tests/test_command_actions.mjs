import test from "node:test";
import assert from "node:assert/strict";
import { applyCommandActions } from "../app/web/js/features/chat/command-actions.js";
import { newCommand } from "../app/web/js/features/settings/command-template.js";

test("all old commands dispatch typed host actions once, preserving session actions", async () => {
  const seen=[];
  const types=["ui.new","ui.model","ui.session","ui.settings","ui.theme","ui.help","ui.stop","ui.mcp","skill.invoke"];
  const handlers=Object.fromEntries(types.map(type=>[type,params=>seen.push([type,params])]));
  for(const [i,type] of types.entries()) {
    const receipt={id:`command-actions-${i}`,state:"succeeded",actions:[{type,params:type==="ui.session"?{action:"fork"}:[]}]};
    assert.equal(await applyCommandActions(receipt,handlers),true);
    assert.equal(await applyCommandActions(receipt,handlers),false);
  }
  assert.deepEqual(seen.map(item=>item[0]),types);
  assert.deepEqual(seen[2][1],{action:"fork"});
});
test("stale, invalid and failed receipts cannot apply partial UI actions", async () => {
  let count=0;const handlers={"ui.new":()=>count++};
  const receipt={id:"command-stale",state:"succeeded",actions:[{type:"ui.new"}]};
  assert.equal(await applyCommandActions(receipt,handlers,()=>false),false);
  assert.equal(await applyCommandActions({...receipt,state:"failed"},handlers),false);
  await assert.rejects(()=>applyCommandActions({...receipt,actions:[{type:"ui.new"},{type:"ui.unknown"}]},handlers));
  assert.equal(count,0);
});
test("a mutation followed by an error is not replayed",async()=>{
  let count=0;const receipt={id:"command-handler-failed",state:"succeeded",actions:[{type:"ui.session"}]};
  const handlers={"ui.session":()=>{count++;throw Error("reply lost after fork");}};
  await assert.rejects(()=>applyCommandActions(receipt,handlers));
  assert.equal(await applyCommandActions(receipt,handlers),false);assert.equal(count,1);
});
test("new command editor produces static metadata and a C entry point",()=>{
  const source=newCommand("my-test");
  assert.equal(JSON.parse(source.match(/\/\* mdo-command\s+([\s\S]*?)\*\//)[1]).id,"my-test");
  assert.match(source,/mdoCommandRun/);assert.match(source,/const xvalue\* Arguments/);
});
