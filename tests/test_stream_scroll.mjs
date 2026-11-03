import test from "node:test";
import assert from "node:assert/strict";
import { captureStreamScroll, createStreamScroll } from "../app/web/js/features/chat/stream-scroll.js";
const pane=(top=0,height=1000,viewport=300)=>({dataset:{streamScroll:"thought"},scrollTop:top,scrollLeft:9,scrollHeight:height,clientHeight:viewport});
const root=node=>({querySelectorAll:()=>[node]});

test("a reader at the bottom follows appended content across DOM replacement",()=>{
  const scroll=createStreamScroll();scroll.capture(root(pane(700)));
  const replacement=pane(0,1600);scroll.restore(root(replacement));
  assert.equal(replacement.scrollTop,1600);assert.equal(replacement.scrollLeft,9);
});
test("a reader above the bottom retains the exact position and can resume following",()=>{
  const scroll=createStreamScroll();scroll.captureNode(pane(250));
  const replacement=pane(0,1600);scroll.restore(root(replacement));assert.equal(replacement.scrollTop,250);
  replacement.scrollTop=1300;scroll.captureNode(replacement);
  const next=pane(0,2000);scroll.restore(root(next));assert.equal(next.scrollTop,2000);
});
test("hidden panes never overwrite saved positions; cached sessions are isolated",()=>{
  const scroll=createStreamScroll();scroll.captureNode(pane(250));scroll.captureNode(pane(0,0,0));
  scroll.captureNode({...pane(0),closest:()=>({open:false})});
  const saved=scroll.snapshot();scroll.reset();const other=pane(0,1200);scroll.restore(root(other));assert.equal(other.scrollTop,0);
  scroll.reset(saved);const returned=pane(0,2000);scroll.restore(root(returned));assert.equal(returned.scrollTop,250);
  scroll.retain(new Set());assert.equal(scroll.snapshot().size,0);
});
test("short panes follow and rounding tolerance stays small",()=>{
  assert.equal(captureStreamScroll(pane(0,100,100)).followTail,true);
  assert.equal(captureStreamScroll(pane(698.5)).followTail,true);
  assert.equal(captureStreamScroll(pane(697)).followTail,false);
  assert.equal(captureStreamScroll(pane(0,0,0)),null);
});
