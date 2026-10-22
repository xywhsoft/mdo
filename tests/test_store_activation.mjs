import test from "node:test";
import assert from "node:assert/strict";
import { reloadStorePluginRuntime } from "../app/web/js/features/settings/store-activation.js";
test("plugin activation survives a generation reconnect and requires a successful reload receipt",async()=>{
  let count=0,waits=0;
  const result=await reloadStorePluginRuntime(async(method,params)=>{if(method==='runtime.reload')return {id:41};assert.equal(params.id,41);count++;if(count===1)throw Error('reconnecting');return {state:count===2?2:3};},{wait:async()=>{waits++;}});
  assert.equal(result.state,3);assert.equal(count,3);assert.equal(waits,2);
});
test("plugin reload failure and exhausted receipts cannot be presented as active",async()=>{
  await assert.rejects(()=>reloadStorePluginRuntime(async method=>method==='runtime.reload'?{id:42}:{state:4,message:'bad source'},{wait:async()=>{}}),/bad source/);
  await assert.rejects(()=>reloadStorePluginRuntime(async method=>method==='runtime.reload'?{id:42}:{state:2},{attempts:2,wait:async()=>{}}),/not completed/);
});
