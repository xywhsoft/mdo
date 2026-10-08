import assert from "node:assert/strict";
import test from "node:test";

test("hash navigation loads once while in-place refresh leaves route-owned controls intact", async () => {
  const previous={window:globalThis.window,location:globalThis.location,history:globalThis.history};
  const events=new Map(); globalThis.window={addEventListener:(type,fn)=>events.set(type,fn)};
  globalThis.location={hash:"#/",pathname:"/",search:""};
  globalThis.history={state:null,replaceState(state,_title,url){this.state=state;
    if(url?.includes("#"))globalThis.location.hash=url.slice(url.indexOf("#"));}};
  try {
    const {navigation}=await import("../app/web/js/state/navigation.js?refresh-test");
    let changes=0,loads=0;
    const stop=navigation.subscribe(()=>changes++);
    navigation.subscribe(()=>loads++,{refresh:true});
    const original=navigation.get(); navigation.revalidate();
    assert.equal(navigation.get(),original); assert.deepEqual([changes,loads],[1,2]);
    navigation.select("p","s"); assert.deepEqual([changes,loads],[2,3]);
    events.get("hashchange")(new Event("hashchange")); assert.deepEqual([changes,loads],[2,3]);
    navigation.select("p","s"); assert.deepEqual([changes,loads],[2,4]);
    navigation.revalidate(); assert.deepEqual([changes,loads],[2,5]);
    navigation.openSettings("general"); assert.deepEqual([changes,loads],[3,6]);
    assert.deepEqual(history.state.mdoWorkspace,{projectId:"p",sessionId:"s"});
    events.get("hashchange")(new Event("hashchange")); assert.deepEqual([changes,loads],[3,6]);
    navigation.backToWorkspace(); assert.deepEqual([changes,loads],[4,7]);
    stop(); navigation.newTask("other"); assert.deepEqual([changes,loads],[4,8]);
  } finally {Object.assign(globalThis,previous);}
});

test("scheduled tasks retain the exact conversation when refreshed", async () => {
  const previous = { window: globalThis.window, location: globalThis.location,
    history: globalThis.history };
  globalThis.window = { addEventListener() {} };
  globalThis.location = { hash: "#/projects/work/sessions/original", pathname: "/", search: "" };
  globalThis.history = { state: null, replaceState(state, _title, url) {
    this.state = state;
    if (url?.includes("#")) location.hash = url.slice(url.indexOf("#"));
  } };
  try {
    const { navigation } = await import("../app/web/js/state/navigation.js?schedules-open-test");
    navigation.openSchedules();
    assert.equal(navigation.get().view, "schedules");
    assert.equal(location.hash, "#/schedules");
    assert.deepEqual(history.state.mdoWorkspace, { projectId: "work", sessionId: "original" });
    const { navigation: refreshed } = await import(
      "../app/web/js/state/navigation.js?schedules-reload-test");
    assert.equal(refreshed.get().view, "schedules");
    refreshed.backToWorkspace();
    assert.equal(location.hash, "#/projects/work/sessions/original");
    assert.equal(refreshed.get().sessionId, "original");
  } finally { Object.assign(globalThis, previous); }
});

test("utility pages retain the composer owner while a real workspace selection changes it", async () => {
  const previous = { window: globalThis.window, location: globalThis.location,
    history: globalThis.history };
  globalThis.window = { addEventListener() {} };
  globalThis.location = { hash: "#/projects/work/sessions/first", pathname: "/", search: "" };
  globalThis.history = { state: null, replaceState(state, _title, url) {
    this.state = state;
    if (url?.includes("#")) location.hash = url.slice(url.indexOf("#"));
  } };
  try {
    const { navigation } = await import("../app/web/js/state/navigation.js?composer-owner-test");
    const first = { projectId: "work", sessionId: "first" };
    assert.deepEqual(navigation.workspace(), first);
    navigation.openSettings("models");
    assert.equal(navigation.get().sessionId, "");
    assert.deepEqual(navigation.workspace(), first);
    navigation.openSchedules();
    assert.deepEqual(navigation.workspace(), first);
    const { navigation: refreshed } = await import(
      "../app/web/js/state/navigation.js?composer-owner-reload-test");
    assert.deepEqual(refreshed.workspace(), first);
    refreshed.backToWorkspace();
    refreshed.select("other", "second");
    assert.deepEqual(refreshed.workspace(), { projectId: "other", sessionId: "second" });
    refreshed.openSettings();
    assert.deepEqual(refreshed.workspace(), { projectId: "other", sessionId: "second" });
    refreshed.newTask("third");
    assert.deepEqual(refreshed.workspace(), { projectId: "third", sessionId: "" });
    refreshed.clear();
    assert.deepEqual(refreshed.workspace(), { projectId: "", sessionId: "" });
    assert.equal(Object.isFrozen(refreshed.workspace()), true);
  } finally { Object.assign(globalThis, previous); }
});
