import assert from "node:assert/strict";
import test from "node:test";
import { createRecoveryDecisions, recoveryMatchesWorkspace } from "../app/web/js/features/approvals/recovery-decisions.js";

const pending = (session="a", token="a") => ({ project_id:"default",session_id:session,
  recovery_token:token.repeat(64),resume_required:true,items:[{tool_call_id:"same-id",tool_available:true}] });
const choose = (state, data, action="retry") => state.choose(data,"same-id",action);

test("same-token refresh preserves a choice; a new token never inherits permission to repeat",()=>{
  const state=createRecoveryDecisions(), old=pending(), fresh=pending("a","c");
  state.select(old); assert.equal(choose(state,old),true);
  state.select(structuredClone(old)); assert.equal(state.ready(old),true);
  state.select(fresh); assert.equal(state.choice("same-id"),"record_uncertain"); assert.equal(state.ready(fresh),true);
  assert.equal(choose(state,old),false); assert.equal(choose(state,fresh),true);
});

test("projects and sessions independently bind choices even when providers reuse their IDs",()=>{
  const state=createRecoveryDecisions(), a=pending(), b=pending("b"), other={...a,project_id:"other"};
  state.select(a); choose(state,a); state.select(b); assert.equal(state.choice("same-id"),"record_uncertain");
  choose(state,b); state.select(other); assert.equal(state.choice("same-id"),"record_uncertain");
  assert.equal(choose(state,b),false);
});

test("an unavailable tool cannot inherit retry while recording uncertainty remains valid",()=>{
  const state=createRecoveryDecisions(), data=pending(), unavailable={...data,items:[{tool_call_id:"same-id",tool_available:false}]};
  state.select(data); choose(state,data); state.select(unavailable);
  assert.equal(state.ready(unavailable),true); assert.equal(choose(state,unavailable),false);
  assert.equal(state.choice("same-id"),"record_uncertain");
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

test("unknown effects default to not repeating; a failed submission preserves an explicit retry",()=>{
  const state=createRecoveryDecisions(), data=pending(); state.select(data);
  assert.equal(state.choice("same-id"),"record_uncertain");
  assert.equal(choose(state,data,"allow"),false);
  choose(state,data); const operation=state.begin(data); state.finish(operation,false);
  assert.equal(state.ready(data),true); assert.equal(state.isBusy(data),false);
});

test("only an available tool explicitly marked read-only is retried by default",()=>{
  for (const [tool_available, automatic_retry_safe, expected] of [
    [true, true, "retry"], [true, false, "record_uncertain"],
    [false, true, "record_uncertain"], [true, "true", "record_uncertain"],
    [true, undefined, "record_uncertain"],
  ]) {
    const state = createRecoveryDecisions(), data = pending();
    data.items[0] = { ...data.items[0], tool_available, automatic_retry_safe };
    state.select(data);
    assert.equal(state.begin(data).choices.get("same-id"), expected);
  }
});

test("accepted recovery stays locked through refresh and a session round trip",()=>{
  const state = createRecoveryDecisions(), a = pending(), b = pending("b", "b");
  state.select(a); const operation = state.begin(a); state.finish(operation, true);
  state.select(structuredClone(a)); assert.equal(state.begin(a), null);
  assert.equal(choose(state, a), false); assert.equal(state.isSubmitted(a), true);
  state.select(b); assert.ok(state.begin(b));
  state.select(a); assert.equal(state.begin(a), null);
  const fresh = pending("a", "c"); state.select(fresh);
  assert.equal(state.isSubmitted(fresh), false);
  assert.equal(state.begin(fresh).choices.get("same-id"), "record_uncertain");
});

test("late recovery callbacks belong only to the original workspace, including after settings navigation",()=>{
  const data=pending(), route={view:"workspace",projectId:"default",sessionId:"a"};
  assert.equal(recoveryMatchesWorkspace(data,route),true);
  for(const patch of [{projectId:"other"},{sessionId:"b"},{view:"settings"},{sessionId:""}])
    assert.equal(recoveryMatchesWorkspace(data,{...route,...patch}),false);
  assert.equal(recoveryMatchesWorkspace(null,route),false);
});
