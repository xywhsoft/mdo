import test from "node:test";
import assert from "node:assert/strict";
import { commandResultContent } from "../app/web/js/features/chat/command-results.js";
test("command results retain data and files separately from unsent drafts",()=>{
  const value={message:"done",draft:"review this",data:{count:2},files:[{path:"report.txt"}]};
  const result=commandResultContent(value);assert.equal(result.message,"done");assert.equal(result.draft,"review this");
  assert.deepEqual(JSON.parse(result.detail),{data:{count:2},files:[{path:"report.txt"}]});assert.equal(value.draft,"review this");
});
test("invalid or oversized draft fields become readable results and cannot overwrite input",()=>{
  for(const draft of [false,{},"x".repeat(65537)]){
    const result=commandResultContent({draft});assert.equal(result.draft,null);assert.equal(JSON.parse(result.detail).draft.length,draft.length);
  }
  assert.equal(commandResultContent({draft:"中".repeat(22000)}).draft,null);
});
test("primitive and empty command results remain visible without inventing model context",()=>{
  assert.equal(commandResultContent({}).detail,"");assert.equal(commandResultContent(null).draft,null);
  assert.equal(commandResultContent([1,true]).detail,"[\n  1,\n  true\n]");
  assert.equal(commandResultContent({draft:"<script>alert(1)</script>"}).draft,"<script>alert(1)</script>");
});
