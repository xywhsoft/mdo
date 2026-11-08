import assert from "node:assert/strict";
import test from "node:test";
globalThis.window = new EventTarget();
const { applyApplicationAction } = await import("../app/web/js/api/application.js");
const store = () => { const data = new Map(); return { getItem: key => data.get(key) || null, setItem: (key,value) => data.set(key,value) }; };

test("browser results stay private even when the observation is small", async () => {
  for (const method of ["workbench.browser", "browseruse.observe"]) {
    const storage=store(), action={id:"private-read",action:method}; let calls=0;
    const result=await applyApplicationAction(action,()=>{calls++;return {text:"private page",image:"private pixels"}},storage,"journal");
    assert.equal(result.result.text,"private page");
    assert.ok(!storage.getItem("journal").includes("private page"));
    const replay=await applyApplicationAction(action,()=>calls++,storage,"journal");
    assert.equal(replay.result.ephemeral,true);assert.equal(calls,1);
  }
});
test("SDK UI acknowledgements survive retries without repeating effects", async () => {
  let calls = 0; const storage = store(), action = { id: "uia-test", action: "ui.new" };
  const execute = async () => { calls++; };
  assert.deepEqual(await applyApplicationAction(action,execute,storage,"journal"),{success:true});
  assert.deepEqual(await applyApplicationAction(action,execute,storage,"journal"),{success:true});
  assert.equal(calls,1);
});
test("a reload during an effect is uncertain and is never replayed", async () => {
  let calls=0;const storage=store();storage.setItem("journal",JSON.stringify({"uia-test":{state:"executing"}}));
  const result=await applyApplicationAction({id:"uia-test"},()=>calls++,storage,"journal");
  assert.equal(result.success,false);assert.equal(calls,0);assert.match(result.error,/restarted/);
});
test("failed effects and unavailable intent storage cannot trigger repeated actions",async()=>{
  let calls=0;const storage=store(),action={id:"uia-test"};
  const execute=()=>{calls++;throw Error("effect failed")};
  assert.equal((await applyApplicationAction(action,execute,storage,"journal")).success,false);
  await applyApplicationAction(action,execute,storage,"journal");assert.equal(calls,1);
  await assert.rejects(()=>applyApplicationAction(action,execute,{getItem:()=>null,setItem:()=>{throw Error("quota")}},"journal"),/quota/);
  assert.equal(calls,1);
});
test("corrupt or unreadable intent journals cannot be mistaken for empty storage",async()=>{
 let calls=0,writes=0;
 for(const text of ['{','null','[]','false','""']){
  const storage={getItem:()=>text,setItem:()=>writes++};
  await assert.rejects(()=>applyApplicationAction({id:'uia-uncertain'},()=>calls++,storage,'journal'),/journal/);
 }
 await assert.rejects(()=>applyApplicationAction({id:'uia-uncertain'},()=>calls++,{getItem(){throw Error('unavailable')},setItem:()=>writes++},'journal'),/unavailable/);
 assert.equal(calls,0);assert.equal(writes,0);
});
