import assert from "node:assert/strict";
import test from "node:test";
import { createRecoveryDecisions, recoveryMatchesWorkspace } from "../app/web/js/features/approvals/recovery-decisions.js";

const pending = (session="a", token="a") => ({ project_id:"default",session_id:session,
  recovery_token:token.repeat(64),resume_required:true,items:[{tool_call_id:"same-id",tool_available:true}] });
const choose = (state, data, action="retry") => state.choose(data,"same-id",action);

test("same-token refresh preserves a decision but a new token with the same call ID needs another choice",()=>{
  const state=createRecoveryDecisions(), old=pending(), fresh=pending("a","c");
  state.select(old); assert.equal(choose(state,old),true);
  state.select(structuredClone(old)); assert.equal(state.ready(old),true);
  state.select(fresh); assert.equal(state.choice("same-id"),undefined); assert.equal(state.ready(fresh),false);
  assert.equal(choose(state,old),false); assert.equal(choose(state,fresh),true);
});

test("projects and sessions independently bind choices even when providers reuse their IDs",()=>{
  const state=createRecoveryDecisions(), a=pending(), b=pending("b"), other={...a,project_id:"other"};
  state.select(a); choose(state,a); state.select(b); assert.equal(state.ready(b),false);
  choose(state,b); state.select(other); assert.equal(state.ready(other),false);
  assert.equal(choose(state,b),false);
});

test("an unavailable tool cannot inherit retry while recording uncertainty remains valid",()=>{
  const state=createRecoveryDecisions(), data=pending(), unavailable={...data,items:[{tool_call_id:"same-id",tool_available:false}]};
  state.select(data); choose(state,data); state.select(unavailable);
  assert.equal(state.ready(unavailable),false); assert.equal(choose(state,unavailable),false);
  assert.equal(choose(state,unavailable,"record_uncertain"),true);
  state.select(structuredClone(unavailable)); assert.equal(state.ready(unavailable),true);
});

test("in-flight A does not block B, and a late A completion preserves B's reviewed choices",()=>{
  const state=createRecoveryDecisions(), a=pending(), b=pending("b","b");
  state.select(a); choose(state,a); const first=state.begin(a);
  assert.equal(state.begin(a),null); assert.equal(state.isBusy(a),true);
  state.select(b); assert.equal(state.isBusy(b),false); choose(state,b,"record_uncertain");
  const second=state.begin(b); assert.ok(second);
  assert.equal(first.choices.get("same-id"),"retry");
  assert.equal(second.choices.get("same-id"),"record_uncertain");
  state.finish(first,true); assert.equal(state.choice("same-id"),"record_uncertain");
  assert.equal(state.isBusy(b),true); state.finish(second,true); assert.equal(state.ready(b),false);
});

test("a new token for an in-flight session cannot submit until its original operation settles",()=>{
  const state=createRecoveryDecisions(), old=pending(), fresh=pending("a","c");
  state.select(old); choose(state,old); const first=state.begin(old);
  state.select(fresh); assert.equal(choose(state,fresh),false); assert.equal(state.begin(fresh),null);
  state.finish(first,true); assert.equal(choose(state,fresh),true); const second=state.begin(fresh);
  assert.ok(second); assert.equal(state.finish(first,true),false); assert.equal(state.isBusy(fresh),true);
  state.finish(second,false); assert.equal(state.ready(fresh),true);
});

test("failed submission preserves its current choices and an unreviewed tool cannot be submitted",()=>{
  const state=createRecoveryDecisions(), data=pending(); state.select(data);
  assert.equal(state.begin(data),null); assert.equal(choose(state,data,"allow"),false);
  choose(state,data); const operation=state.begin(data); state.finish(operation,false);
  assert.equal(state.ready(data),true); assert.equal(state.isBusy(data),false);
});

test("late recovery callbacks belong only to the original workspace, including after settings navigation",()=>{
  const data=pending(), route={view:"workspace",projectId:"default",sessionId:"a"};
  assert.equal(recoveryMatchesWorkspace(data,route),true);
  for(const patch of [{projectId:"other"},{sessionId:"b"},{view:"settings"},{sessionId:""}])
    assert.equal(recoveryMatchesWorkspace(data,{...route,...patch}),false);
  assert.equal(recoveryMatchesWorkspace(null,route),false);
});
