import test from "node:test";
import assert from "node:assert/strict";
import { conversationGroups, conversationProcessSummary } from "../app/web/js/features/chat/conversation-history.js";
import { eventsToTimeline } from "../app/web/js/features/chat/timeline.js";
const e=(id,kind,fields={})=>({event_id:id,kind,schema_version:9,run_id:1,agent_depth:0,agent_turn:1,time:id*1e6,...fields});

test("consecutive calls include intervening progress and artifacts without folding replies",()=>{
  const events=[e(1,"agent_start",{text:"Fix the project",user_message_sequence:1}),
    e(2,"model_text_delta",{text:"Inspecting files"}),
    e(3,"tool_start",{tool_name:"read",tool_call_id:"one"}),e(4,"tool_done",{tool_call_id:"one",success:true,text:"file"}),
    e(5,"artifact_created",{artifact_path:"report.txt"}),e(6,"compaction_done"),
    e(7,"model_reasoning_delta",{text:"Next check"}),e(8,"tool_start",{tool_name:"exec",tool_call_id:"two"}),
    e(9,"tool_done",{tool_call_id:"two",success:false,text:"Exit 1"}),e(10,"model_text_delta",{agent_turn:2,text:"Found the cause"}),
    e(11,"agent_done",{success:true})];
  const [group]=conversationGroups(eventsToTimeline(events));
  assert.deepEqual(group.entries.map(item=>item.kind),["assistant","process","assistant"]);
  const block=group.entries[1],summary=conversationProcessSummary(block);
  assert.equal(summary.count,2);assert.equal(summary.failed,1);assert.deepEqual(summary.names,["read","exec"]);
  assert.deepEqual(block.items.map(item=>item.kind),["tool","system","system","reasoning","tool"]);
  assert.equal(group.entries[0].text,"Inspecting files");assert.equal(group.entries[2].text,"Found the cause");
});
test("retry stays in the current call group and resolves without creating another group",()=>{
  const first=[e(1,"agent_start",{user_message_sequence:1}),e(2,"tool_start",{tool_name:"read",tool_call_id:"a"}),
    e(3,"tool_done",{tool_call_id:"a",success:true}),e(4,"model_retry",{model_http_status:429,model_retry_after_ms:8000})];
  const before=conversationGroups(eventsToTimeline(first))[0].entries[0];
  assert.equal(before.items.at(-1).kind,"retry");assert.equal(conversationProcessSummary(before).retry.httpStatus,429);
  const after=conversationGroups(eventsToTimeline([...first,e(5,"model_reasoning_delta",{text:"Continue"}),
    e(6,"tool_start",{tool_call_id:"b",tool_name:"read"})]))[0].entries;
  assert.equal(after.length,1);assert.equal(after[0].key,before.key);
  assert.equal(conversationProcessSummary(after[0]).retry,undefined);assert.equal(conversationProcessSummary(after[0]).count,2);
});
test("unreadable sections, actionable recovery and final failures remain visible boundaries",()=>{
  const tool=id=>({key:String(id),kind:"tool",sourceEventId:id,state:"done",role:"read"});
  const entries=conversationGroups([tool(1),{key:"notice",kind:"history-notice",text:"Unreadable record"},tool(3),
    {key:"recover",kind:"system",text:"Action required",state:"failed"},tool(5),{key:"error",kind:"error",text:"Failed"}])[0].entries;
  assert.deepEqual(entries.map(item=>item.kind),["process","history-notice","process","system","process","error"]);
});
