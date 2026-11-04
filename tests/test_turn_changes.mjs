import test from "node:test";
import assert from "node:assert/strict";
import { appendTurnChanges } from "../app/web/js/features/chat/turn-changes.js";

test("only a completed main execution gets a summary, after its final record", () => {
  const items = [
    {kind:"assistant",key:"a",runKey:"1",runEpoch:10,turnState:"done"},
    {kind:"tool",key:"child",runKey:"2",runEpoch:20,turnState:"done",agentDepth:1},
    {kind:"assistant",key:"b",runKey:"1",runEpoch:10,turnState:"done"},
    {kind:"assistant",key:"live",runKey:"1",runEpoch:30,turnState:"running"},
    {kind:"assistant",key:"fragment",runKey:"3",runEpoch:0,turnState:"done"},
  ];
  const result=appendTurnChanges(items);
  assert.deepEqual(result.map(item=>item.key),["a","child","b","changes-10","live","fragment"]);
  assert.equal(result[3].turn,10);
});
test("reused native IDs and failed or cancelled replies retain separate review identities", () => {
  const items=[{key:"failure",runKey:"1",runEpoch:3,turnState:"failed"},
    {key:"stop",runKey:"1",runEpoch:9,turnState:"cancelled"}];
  assert.deepEqual(appendTurnChanges(items).filter(item=>item.kind==="changes").map(item=>item.turn),[3,9]);
  assert.deepEqual(appendTurnChanges([{key:"notice",kind:"system"}]),[{key:"notice",kind:"system"}]);
});
