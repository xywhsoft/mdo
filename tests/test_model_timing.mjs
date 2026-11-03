import test from "node:test";
import assert from "node:assert/strict";
import { createModelTimingTracker } from "../app/web/js/features/chat/model-timing.js";
import { eventsToTimeline } from "../app/web/js/features/chat/timeline.js";

test("first visible text includes initial waiting and retry; heartbeat is not a text token",()=>{
  const track=createModelTimingTracker(),event=(kind,time,extra={})=>track({kind,time:time*1000,...extra},"call");
  event("model_start",1000);event("model_retry",2000,{model_retry_after_ms:10000,model_attempts:1});
  event("model_text_delta",3000,{text:""});event("model_retry",12000,{model_retry_after_ms:0,model_attempts:1});
  event("model_reasoning_delta",15000,{text:"thinking"});event("model_text_delta",60000,{text:"answer"});
  const result=event("model_done",61000,{model_attempts:2,model_request_id:"join-me",model_timing:{first_byte_ms:500}});
  assert.equal(result.firstTextMs,59000);assert.equal(result.firstReasoningMs,14000);assert.equal(result.retryWaitMs,10000);
  assert.equal(result.transport.first_byte_ms,500);assert.equal(result.requestId,"join-me");assert.equal(result.attempts,2);
});
test("final reply retains timing for earlier tool-only calls in the same run",()=>{
  const event=(kind,id,turn,fields={})=>({kind,event_id:id,run_id:1,agent_depth:0,agent_turn:turn,time:id*1000000,...fields});
  const items=eventsToTimeline([event("agent_start",1,0,{text:"go",user_message_sequence:1}),event("model_start",2,1),
    event("model_done",3,1,{success:true,model_request_id:"tool-request"}),event("model_start",4,2),
    event("model_text_delta",5,2,{text:"Done"}),event("model_done",6,2,{success:true,model_request_id:"final-request"}),
    event("agent_done",7,2,{success:true,text:"Done"})]);
  const reply=items.find(item=>item.kind==="assistant");
  assert.deepEqual(reply.modelTimings.map(call=>call.requestId),["tool-request","final-request"]);
  assert.equal(reply.modelTimings[0].firstTextMs,undefined);assert.equal(reply.modelTimings[1].firstTextMs,1000);
});
test("missing history does not invent timing and concurrent calls remain isolated",()=>{
  const track=createModelTimingTracker();assert.equal(track({kind:"model_done",time:99},"missing"),null);
  track({kind:"model_start",time:1000000},"A");track({kind:"model_start",time:2000000},"B");
  track({kind:"model_text_delta",time:3000000,text:"B"},"B");
  assert.equal(track({kind:"model_done",time:4000000},"A").firstTextMs,undefined);
  assert.equal(track({kind:"model_done",time:5000000},"B").firstTextMs,1000);
});
