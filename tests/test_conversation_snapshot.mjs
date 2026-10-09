import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import test, { beforeEach, after } from "node:test";
const previous = { window: globalThis.window, document: globalThis.document, fetch: globalThis.fetch };
const timers = new Map(); let timerId = 0;
const flush = () => new Promise(resolve => setImmediate(resolve));
globalThis.window = Object.assign(new EventTarget(), {
  setTimeout(fn, delay) { timers.set(++timerId, {fn, delay}); return timerId; },
  clearTimeout(id) { timers.delete(id); },
});
globalThis.document = Object.assign(new EventTarget(), {hidden:false});
const { decodeConversationPage } = await import("../app/web/js/features/chat/conversation-page.js");
const { timelineStore, selectTimeline, clearTimeline, clearTimelineCache, loadOlderTimeline,
  refreshSelectedTimeline, applyLiveTimeline } = await import("../app/web/js/features/chat/timeline-store.js");
const sha = text => createHash("sha256").update(text).digest("hex");
const epoch = "a".repeat(64);
const owner = {projectId:"qa",sessionId:"a"};
function page(ids, fields={}) {
  const e = fields.epoch ?? epoch, s = fields.session_id ?? "a";
  const items = ids.map(id => ({event_id:id,aggregate_end_id:0,kind:"model_text_delta",text:`text ${id}`,
    projection_epoch:e,node_id:`${s}:${e}:${id}`,content_hash:sha(`text ${id}`)}));
  const items_json = JSON.stringify(items);
  return { project_id:"qa",session_id:s,epoch:e,next_cursor:ids.at(-1) ?? 0,
    latest_event_id:ids.at(-1) ?? 0,next_before:ids[0] ?? 0,delta:false,has_more:false,history_lost:false,
    ...fields,items_json,items_hash:sha(items_json) };
}
const reply = p => Response.json({ok:true,data:p});
let fetcher;
globalThis.fetch = (url, options) => fetcher(url, options);
beforeEach(() => { clearTimeline(); clearTimelineCache(); timers.clear(); });
after(() => { clearTimeline(); Object.assign(globalThis, previous); });
test("snapshot hashes, order, ownership and aggregate ranges are checked before use", () => {
  assert.equal(decodeConversationPage(page([1,2]),owner).items.length,2);
  for (const broken of [page([2,1]),page([1],{session_id:"b"}),{...page([1]),items_hash:"f".repeat(64)},
    page([1],{next_cursor:2,latest_event_id:1})]) assert.throws(() => decodeConversationPage(broken,owner),{code:"conversation_invalid"});
  const p=page([1]); const items=JSON.parse(p.items_json); items[0].aggregate_end_id=3;
  p.items_json=JSON.stringify(items); p.items_hash=sha(p.items_json);
  assert.throws(() => decodeConversationPage(p,owner),{code:"conversation_invalid"});
});
test("a complete reply can exceed the old byte and event preview limits", () => {
  const p = page(Array.from({length: 110}, (_, i) => i + 1));
  const items = JSON.parse(p.items_json);
  items[0].text = "完整段落。".repeat(20_000);
  p.items_json = JSON.stringify(items); p.items_hash = sha(p.items_json);
  assert.ok(Buffer.byteLength(p.items_json) > 256 * 1024);
  assert.equal(decodeConversationPage(p, owner).items[0].text, items[0].text);
  const tooMany = page(Array.from({length: 4097}, (_, i) => i + 1));
  assert.throws(() => decodeConversationPage(tooMany, owner), {code: "conversation_invalid"});
});
test("cold history uses one compact snapshot and cursor-only revalidation", async () => {
  const calls=[];
  fetcher = async url => { calls.push(url); return reply(url.includes("after=")
    ? page([],{delta:true,next_cursor:40,latest_event_id:40}) : page([38,39,40])); };
  await selectTimeline("qa","a");
  assert.equal(calls.length,2); assert.ok(calls.every(url=>url.includes("/conversation?")));
  assert.ok(calls[1].includes(`after=40&epoch=${epoch}`));
  assert.deepEqual(timelineStore.get().data.events.map(e=>e.event_id),[38,39,40]);
});
test("switching back paints cached content while a single delta request is pending", async () => {
  fetcher=async url=>reply(page(url.includes("after=")?[]:[1],{session_id:url.includes("/b/")?"b":"a",
    ...(url.includes("after=")?{delta:true,next_cursor:1,latest_event_id:1}:{})}));
  await selectTimeline("qa","a"); await selectTimeline("qa","b");
  let resolve;
  fetcher=()=>new Promise(done=>resolve=done);
  const pending=selectTimeline("qa","a");
  assert.equal(timelineStore.get().data.cached,true); assert.equal(timelineStore.get().data.events[0].text,"text 1");
  await flush();
  resolve(reply(page([2],{delta:true}))); await pending;
  assert.deepEqual(timelineStore.get().data.events.map(e=>e.event_id),[1,2]);
  assert.equal(timelineStore.get().data.syncing,false);
});
test("a replaced journal discards cached nodes instead of combining two histories", async () => {
  fetcher=async url=>reply(page(url.includes("after=")?[]:[1],url.includes("after=")?{delta:true,next_cursor:1,latest_event_id:1}:{}));
  await selectTimeline("qa","a");
  fetcher=async()=>reply(page([7],{epoch:"b".repeat(64)}));
  await refreshSelectedTimeline();
  assert.deepEqual(timelineStore.get().data.events.map(e=>e.event_id),[7]);
});
test("an older history response preserves pushes that arrived while it was in flight", async () => {
  fetcher=async url=>reply(url.includes("after=")?page([],{delta:true,next_cursor:8,latest_event_id:8}):page([7,8],{has_more:true}));
  await selectTimeline("qa","a"); let resolve;
  fetcher=()=>new Promise(done=>resolve=done);
  const pending=loadOlderTimeline();
  await flush();
  applyLiveTimeline({type:"events",project_id:"qa",session_id:"a",epoch,items:[{event_id:9,kind:"model_text_delta",text:"live"}],next_cursor:9,latest_event_id:9});
  resolve(reply(page([5,6],{next_cursor:8,latest_event_id:8}))); await pending;
  assert.equal(timelineStore.get().data.cursor,9);
  assert.deepEqual(timelineStore.get().data.events.map(e=>e.event_id),[5,6,7,8,9]);
});
test("a superseded response cannot overwrite the newly selected session", async () => {
  let resolve;
  fetcher=()=>new Promise(done=>resolve=done);
  const old=selectTimeline("qa","a"); await Promise.resolve(); await Promise.resolve();
  fetcher=async url=>reply(page(url.includes("after=")?[]:[3],{session_id:"b",...(url.includes("after=")?{delta:true,next_cursor:3,latest_event_id:3}:{})}));
  await selectTimeline("qa","b"); resolve(reply(page([1]))); await old;
  assert.equal(timelineStore.get().data.sessionId,"b"); assert.equal(timelineStore.get().data.events[0].event_id,3);
});
