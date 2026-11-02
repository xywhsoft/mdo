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
const notice = (code="invalid_record", offset=100, before=0) => ({
  code, offset, before_event_id:before, id:`${epoch}:${offset}:${code}`,
});
function withItems(p, mutate) {
  const items=JSON.parse(p.items_json); mutate(items);
  const items_json=JSON.stringify(items);
  return {...p,items_json,items_hash:sha(items_json)};
}
let fetcher;
globalThis.fetch = (url, options) => fetcher(url, options);
beforeEach(() => { clearTimeline(); clearTimelineCache(); timers.clear(); });
after(() => { clearTimeline(); Object.assign(globalThis, previous); });
test("snapshot hashes and ownership remain strict while bad rows are quarantined", () => {
  assert.equal(decodeConversationPage(page([1,2]),owner).items.length,2);
  for (const broken of [page([1],{session_id:"b"}),{...page([1]),items_hash:"f".repeat(64)},
    page([1],{next_cursor:2,latest_event_id:1})]) assert.throws(() => decodeConversationPage(broken,owner),{code:"conversation_invalid"});
  const p=page([1]); const items=JSON.parse(p.items_json); items[0].aggregate_end_id=3;
  p.items_json=JSON.stringify(items); p.items_hash=sha(p.items_json);
  const decoded=decodeConversationPage(p,owner);
  assert.equal(decoded.items.length,0);
  assert.equal(decoded.notices[0].code,"unreadable_event");
  assert.deepEqual(decodeConversationPage(page([2,1],{latest_event_id:2,next_cursor:2}),owner)
    .items.map(e=>e.event_id),[2]);
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

test("a damaged row or action anchor preserves neighboring replies", () => {
  const input=withItems(page([1,2,3]),items=>{
    items[1].text=null;
    items[2].action_prompt={kind:"agent_start",event_id:99};
  });
  const result=decodeConversationPage(input,owner);
  assert.deepEqual(result.items.map(e=>e.text),["text 1","text 3"]);
  assert.equal(result.items[1].action_prompt,undefined);
  assert.equal(result.notices.length,2);
  // Display recovery neither repairs nor overwrites the received journal data.
  assert.equal(JSON.parse(input.items_json)[1].text,null);
  const annotation=decodeConversationPage(page([1,2],{notices:[{code:"future_unknown"},null]}),owner);
  assert.equal(annotation.items.length,2);
  assert.equal(annotation.notices.length,1);
});

test("byte continuation crosses an empty corrupt suffix without losing healthy history", async () => {
  const suffix=page([],{has_more:true,next_scan_before:1000,notices:[notice()]});
  fetcher=async()=>reply(suffix);
  await selectTimeline("qa","a");
  assert.equal(timelineStore.get().status,"ready");
  assert.equal(timelineStore.get().data.hasOlder,true);
  let query;
  fetcher=async url=>{query=url; return reply(page([1,2],{next_scan_before:0,notices:[notice("invalid_record",200,2)]}));};
  await loadOlderTimeline();
  assert.ok(query.includes(`scan_before=1000&epoch=${epoch}`));
  const recovered=timelineStore.get().data;
  assert.equal(recovered.cursor,2);
  assert.equal(recovered.hasOlder,false);
  assert.equal(recovered.olderScanBefore,0);
  assert.deepEqual(recovered.events.map(e=>e.event_id),[1,2]);
  fetcher=async()=>reply(suffix);
  await refreshSelectedTimeline();
  assert.deepEqual(timelineStore.get().data.events.map(e=>e.event_id),[1,2]);
  assert.equal(timelineStore.get().data.cursor,2);
  assert.equal(timelineStore.get().data.hasOlder,false);
});

test("delta refresh and cached selection retain the byte cursor and local notices", async () => {
  fetcher=async url=>reply(url.includes("after=") ? page([],{delta:true,next_cursor:8,latest_event_id:8}) :
    page([7,8],{has_more:true,next_scan_before:500,notices:[notice("scan_limit",500,7)]}));
  await selectTimeline("qa","a");
  await refreshSelectedTimeline();
  assert.equal(timelineStore.get().data.olderScanBefore,500);
  assert.equal(timelineStore.get().data.historyNotices.length,1);
  clearTimeline();
  await selectTimeline("qa","a");
  assert.equal(timelineStore.get().data.olderScanBefore,500);
  let query;
  fetcher=async url=>{query=url;return reply(page([5,6],{next_cursor:8,latest_event_id:8,
    next_scan_before:0,notices:[notice("invalid_record",300,6)]}));};
  await loadOlderTimeline();
  assert.ok(query.includes("scan_before=500"));
  assert.equal(timelineStore.get().data.historyNotices.length,2);
  assert.equal(timelineStore.get().data.hasOlder,false);
  fetcher=async()=>reply(page([9],{epoch:"b".repeat(64),next_scan_before:0}));
  await refreshSelectedTimeline();
  assert.equal(timelineStore.get().data.historyNotices.length,0);
});

test("intentional history clearing drops obsolete display warnings and continuation", async () => {
  fetcher=async url=>reply(url.includes("after=") ? page([],{delta:true,next_cursor:8,latest_event_id:8}) :
    page([7,8],{has_more:true,next_scan_before:500,notices:[notice()]}));
  await selectTimeline("qa","a");
  applyLiveTimeline({project_id:"qa",session_id:"a",epoch,items:[
    {event_id:9,kind:"history_truncated",source_event_id:1}],next_cursor:9,latest_event_id:9});
  assert.equal(timelineStore.get().data.historyNotices.length,0);
  assert.equal(timelineStore.get().data.olderScanBefore,undefined);
});

test("an older GET observing an append cannot skip the new live events", async () => {
  fetcher=async url=>reply(url.includes("after=") ? page([],{delta:true,next_cursor:8,latest_event_id:8}) :
    page([7,8],{has_more:true,next_scan_before:500}));
  await selectTimeline("qa","a");
  fetcher=async()=>reply(page([5,6],{next_cursor:10,latest_event_id:10,next_scan_before:0}));
  await loadOlderTimeline();
  assert.equal(timelineStore.get().data.cursor,8);
  let query;
  fetcher=async url=>{query=url;return reply(page([9,10],{delta:true}));};
  await refreshSelectedTimeline();
  assert.ok(query.includes("after=8"));
  assert.deepEqual(timelineStore.get().data.events.map(e=>e.event_id),[5,6,7,8,9,10]);
});
