import assert from "node:assert/strict";
import test from "node:test";
import { readFileSync } from "node:fs";
import { mergeConversationNotices, insertConversationNotices } from "../app/web/js/features/chat/conversation-notices.js";
import { conversationGroups } from "../app/web/js/features/chat/conversation-history.js";
import { eventsToTimeline } from "../app/web/js/features/chat/timeline.js";
import { loadLocale, t, supportedLocales } from "../app/web/js/i18n.js";

test("read warnings stay outside collapsed execution and cannot change task status or actions", () => {
  const events=[
    {event_id:1,kind:"agent_start",schema_version:5,agent_depth:0,run_id:1,user_message_sequence:1,text:"question"},
    {event_id:2,kind:"model_reasoning_delta",agent_depth:0,run_id:1,text:"thought"},
    {event_id:3,kind:"model_text_delta",agent_depth:0,run_id:1,text:"healthy answer"},
    {event_id:4,kind:"agent_done",agent_depth:0,run_id:1,success:true},
  ];
  const original=structuredClone(events);
  const items=insertConversationNotices(eventsToTimeline(events),[
    {id:"bad",code:"invalid_record",before_event_id:3}],()=>"Unreadable record");
  const groups=conversationGroups(items);
  const warning=groups.flatMap(g=>g.entries).find(item=>item.kind==="history-notice");
  assert.ok(warning);
  assert.equal(warning.retryPrompt,undefined);
  assert.equal(warning.sourceEvent,undefined);
  assert.equal(groups[0].state,"done");
  assert.ok(groups[0].entries.some(item=>item.text==="healthy answer"));
  assert.ok(!groups[0].entries.filter(item=>item.kind==="process")
    .flatMap(item=>item.items).some(item=>item.kind==="history-notice"));
  assert.deepEqual(events,original);
  const tail=conversationGroups(insertConversationNotices(eventsToTimeline(events),[
    {id:"tail",code:"invalid_record",before_event_id:0}],()=>"Unreadable tail"));
  assert.equal(tail.at(-1).state,"done");
  assert.equal(conversationGroups(insertConversationNotices([], [
    {id:"only",code:"invalid_record",before_event_id:0}],()=>"Unreadable"))[0].state,"done");
});

test("warnings are deduplicated across refreshes and bounded in the session cache", () => {
  const notices=Array.from({length:200},(_,id)=>({id:`${id}`}));
  assert.equal(mergeConversationNotices(notices,notices).length,128);
  assert.equal(mergeConversationNotices([notices[0]],[notices[0]]).length,1);
});

test("readable text on either side of damage stays separated by its warning", () => {
  const events=[2,3].map(event_id=>({event_id,kind:"model_text_delta",run_id:1,agent_turn:1,
    agent_depth:0,text:`healthy ${event_id}`}));
  const historyNotices=[{id:"bad",code:"invalid_record",before_event_id:3}];
  const projected=eventsToTimeline(events,true,{historyNotices});
  const result=insertConversationNotices(projected,historyNotices,()=>"unreadable");
  assert.deepEqual(result.map(item=>item.text),["healthy 2","unreadable","healthy 3"]);
  assert.equal(new Set(result.map(item=>item.key)).size,3);
  // Normal exports and undamaged conversations keep their merged stream.
  assert.equal(eventsToTimeline(events).length,1);
});

test("history recovery messages use all supported interface languages", async context => {
  context.mock.method(globalThis,"fetch",async url=>({ok:true,async json(){
    const locale=/\/lang\/(.+)\.json$/.exec(url)[1];
    return JSON.parse(readFileSync(new URL(`../app/web/lang/${locale}.json`,import.meta.url),"utf8"));
  }}));
  for(const locale of supportedLocales) {
    await loadLocale(locale);
    for(const key of ["historyScanLimited","historyTextLimited","historySectionLimited","historySectionUnreadable"]) {
      assert.notEqual(t(`timeline.${key}`),`timeline.${key}`);
      assert.ok(t(`timeline.${key}`).length>10);
    }
  }
});
