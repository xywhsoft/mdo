import test from "node:test";
import assert from "node:assert/strict";
import { createModelTimingTracker } from "../app/web/js/features/chat/model-timing.js";

test("first visible text includes initial waiting and retry; heartbeat is not a text token",()=>{
  const track=createModelTimingTracker(),event=(kind,time,extra={})=>track({kind,time:time*1000,...extra},"call");
  event("model_start",1000);event("model_retry",2000,{model_retry_after_ms:10000,model_attempts:1});
  event("model_text_delta",3000,{text:""});event("model_retry",12000,{model_retry_after_ms:0,model_attempts:1});
  event("model_reasoning_delta",15000,{text:"thinking"});event("model_text_delta",60000,{text:"answer"});
  const result=event("model_done",61000,{model_attempts:2,model_request_id:"join-me",model_timing:{first_byte_ms:500}});
  assert.equal(result.firstTextMs,59000);assert.equal(result.firstReasoningMs,14000);assert.equal(result.retryWaitMs,10000);
  assert.equal(result.transport.first_byte_ms,500);assert.equal(result.requestId,"join-me");assert.equal(result.attempts,2);
});
test("missing history does not invent timing and concurrent calls remain isolated",()=>{
  const track=createModelTimingTracker();assert.equal(track({kind:"model_done",time:99},"missing"),null);
  track({kind:"model_start",time:1000000},"A");track({kind:"model_start",time:2000000},"B");
  track({kind:"model_text_delta",time:3000000,text:"B"},"B");
  assert.equal(track({kind:"model_done",time:4000000},"A").firstTextMs,undefined);
  assert.equal(track({kind:"model_done",time:5000000},"B").firstTextMs,1000);
});
