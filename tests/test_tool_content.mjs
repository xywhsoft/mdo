import assert from "node:assert/strict";
import test from "node:test";
import { formatToolArguments, toolCallSummary, toolInputPreview,
  resolveToolSectionText } from "../app/web/js/features/chat/tool-content.js";
import { eventsToTimeline } from "../app/web/js/features/chat/timeline.js";

test("file/search/process/task summaries preserve the old tool-specific reading mode", () => {
  assert.equal(toolCallSummary("read", '{"path":"notes/QA notes.txt"}'), "notes/QA notes.txt");
  assert.equal(toolCallSummary("grep", '{"path":"src","pattern":"token"}'), "/token/ · src");
  assert.equal(toolCallSummary("poll", '{"task_id":17,"wait_ms":1000}'), "#17");
  assert.equal(toolCallSummary("wait", '{"task_ids":[17,23]}'), "#17, #23");
  assert.equal(toolCallSummary("exec", '{"argv":["python","-c","print(42)","two words"]}'),
    'python -c print(42) "two words"');
  assert.equal(toolCallSummary("custom", '{"url":"https://example.com"}'), "https://example.com");
  assert.equal(toolCallSummary("custom", "plain\noutput"), "plain output");
  assert.equal(toolCallSummary("read", '{"path":'), '{"path":');
  assert.equal(toolCallSummary("write", '{"path":"two words.txt","content":"clipped',true), "two words.txt");
  for (const text of ['{"path":"clipped', '{"content":"contains \\"path\\": \\"wrong\\",',
    '{"path":"bad\\q","content":"clipped'])
    assert.equal(toolCallSummary("write",text,true), "调用参数有截断");
});

test("call formatting changes only the display, malformed and non-object text stays literal", () => {
  const raw = '{"path":"a","edits":[{"old_text":"before\\n","new_text":"after\\n"}]}';
  const formatted = formatToolArguments(raw);
  assert.deepEqual(JSON.parse(formatted), JSON.parse(raw));
  assert.ok(formatted.includes('\n  "path"'));
  for (const value of ['{"path":', "not JSON", '"string"', "null", "17"])
    assert.equal(formatToolArguments(value), value);
});

test("write/edit previews show requested text safely without claiming a whole-file diff", () => {
  const content = '<script>alert(1)</script>\nsecond line';
  assert.deepEqual(toolInputPreview("write", JSON.stringify({path:"a", content})), {kind:"write", text:content});
  const edits = [{old_text:"before\n",new_text:"after\n"}, {old_text:"second",new_text:""}];
  const preview = toolInputPreview("edit", JSON.stringify({path:"a", edits}));
  assert.deepEqual(preview.lines.filter(line => line.text).map(line => [line.index,line.kind,line.text]),
    [[1,"removed","before"],[1,"added","after"],[2,"removed","second"]]);
  assert.equal(preview.omitted, false);
  for (const [name, text, truncated] of [["edit", '{"edits":[]}', false],
    ["edit", '{"edits":[{"old_text":17,"new_text":"b"}]}', false],
    ["write", JSON.stringify({content}), true], ["custom", JSON.stringify({content}), false]])
    assert.equal(toolInputPreview(name,text,truncated), null);
  const large = toolInputPreview("edit", JSON.stringify({edits:[{old_text:"x\n".repeat(300),new_text:"y"}]}));
  assert.equal(large.lines.length, 200);
  assert.equal(large.omitted, true);
});

test("tool projection retains independent full-text event identities and truncation flags", () => {
  const [tool] = eventsToTimeline([
    {kind:"tool_start",event_id:10,run_id:1,tool_call_id:"c",tool_name:"write",text:"input",text_truncated:true},
    {kind:"tool_done",event_id:11,run_id:1,tool_call_id:"c",tool_name:"write",text:"output",text_truncated:false,success:true},
  ]);
  assert.deepEqual([tool.inputEventId,tool.inputTruncated,tool.outputEventId,tool.outputTruncated], [10,true,11,false]);
  const [orphan] = eventsToTimeline([{kind:"tool_done",event_id:21,text:"prefix",text_truncated:true,success:false}]);
  assert.deepEqual([orphan.outputEventId,orphan.outputTruncated,orphan.state], [21,true,"failed"]);
});

test("section copy reads only its original event, preserving raw arguments and visible-prefix agreement", async () => {
  const owner={projectId:"p",sessionId:"s"};
  const item={inputText:'{"path":',inputEventId:10,inputTruncated:true,
    outputText:"output",outputEventId:11,outputTruncated:false};
  const raw='{"path":"two words","content":"full\\ntext"}';
  const full=await resolveToolSectionText(item,"input",owner, async (...args) => {
    assert.deepEqual(args.slice(0,4),["p","s",10,"tool_start"]);
    return raw;
  });
  assert.deepEqual(full,{text:raw,complete:true});
  assert.deepEqual(await resolveToolSectionText(item,"output",owner,()=>{throw new Error("must not fetch");}),
    {text:"output",complete:true});
  assert.equal(item.inputText,'{"path":');
  for (const read of [async()=>null, async()=>"different",async()=>{throw new Error("offline");},
    async()=>item.inputText+"x".repeat(65536)])
    assert.deepEqual(await resolveToolSectionText(item,"input",owner,read),{text:item.inputText,complete:false});
  assert.deepEqual(await resolveToolSectionText({...item,inputEventId:0},"input",owner),
    {text:item.inputText,complete:false});
});

test("spilled tool output reads its session artifact on demand, not the model-facing summary", async () => {
  const owner={projectId:"p",sessionId:"s"};
  const item={outputText:"output saved as artifact",outputEventId:12,outputTruncated:false,artifactId:3};
  let calls=0;
  assert.deepEqual(await resolveToolSectionText(item,"output",owner,
    ()=>{throw new Error("must not use event summary");}, async (actualOwner,eventId)=>{
      assert.deepEqual(actualOwner,owner);
      assert.equal(eventId,12);
      calls++;
      return "full original output\n";
    }),{text:"full original output\n",complete:true});
  assert.equal(calls,1);
  for (const read of [async()=>null,async()=>"x".repeat(65537),async()=>{throw new Error("missing");}])
    assert.deepEqual(await resolveToolSectionText(item,"output",owner,()=>null,read),
      {text:item.outputText,complete:false});
});
