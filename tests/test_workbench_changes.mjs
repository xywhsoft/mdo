import test from "node:test";
import assert from "node:assert/strict";
import { changesTree, pairDiffRows } from "../app/web/js/features/workbench/changes-model.js";
import { readOperation } from "../app/web/js/features/workbench/read-operation.js";

test("paired differences retain exact identities for unequal blocks and mid-block pages", () => {
  const rows = [{kind:"hunk",offset:0,text:"@@ -3,2 +3 @@"}, {kind:"removed",offset:1,old_line:3,text:"-old"}, {kind:"removed",offset:2,old_line:4,text:"-extra"}, {kind:"added",offset:3,new_line:3,text:"+new"}, {kind:"context",offset:4,old_line:5,new_line:4,text:" same"}];
  const pairs = pairDiffRows(rows);
  assert.equal(pairs[1].left.old_line,3); assert.equal(pairs[1].right.new_line,3); assert.equal(pairs[2].right,undefined);
  assert.equal(pairs[3].left,rows[4]); assert.equal(pairs[3].right,rows[4]);
  assert.deepEqual(pairDiffRows(rows.slice(3,4)),[{left:undefined,right:rows[3]}]);
  assert.deepEqual(pairs.flatMap(pair=>pair.wide?[pair.wide]:[pair.left,pair.right]).filter(Boolean).map(row=>row.offset).sort(),[0,1,2,3,4,4]);
});

test("change tree filters by old rename path and can reveal collapsed ancestors", () => {
  const files=[{path:"src/new.js",old_path:"old.js"},{path:"root.txt"},{path:"src/deep/a.txt"}];
  assert.deepEqual(changesTree(files,new Set(["src"])).map(row=>row.path),["src","root.txt"]);
  assert.deepEqual(changesTree(files,new Set(["src"]),"old.js").map(row=>row.path),["src","src/new.js"]);
});

test("read operations release results and cancel only their own admitted job on disposal", async () => {
  const calls=[]; const sdk={
    start:async(method,params,id)=>{calls.push(["start",id]);return {terminal:false};},
    wait:async()=>({terminal:true,state:"succeeded"}),result:async()=>({rows:[1]}),
    call:async(method,params)=>calls.push([method,params.id]),cancel:async id=>calls.push(["cancel",id])
  };
  assert.deepEqual(await readOperation(sdk,"changes.list",{}),{rows:[1]});
  await new Promise(resolve=>setImmediate(resolve));
  assert.equal(calls[1][0],"operation.release");assert.equal(calls[1][1],calls[0][1]);
  const controller=new AbortController();sdk.wait=async()=>{controller.abort();throw new DOMException("cancelled","AbortError");};
  await assert.rejects(readOperation(sdk,"changes.list",{},controller.signal),{name:"AbortError"});
  await new Promise(resolve=>setImmediate(resolve));
  assert.equal(calls[3][0],"cancel");assert.equal(calls[3][1],calls[2][1]);assert.equal(calls[4][1],calls[2][1]);
});
