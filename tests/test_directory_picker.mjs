import assert from "node:assert/strict";
import test from "node:test";
import { createDirectoryNavigation, directoryChild, directoryBreadcrumbs, directoryCompletion, directoryPlaces } from "../app/web/js/features/sessions/directory-navigation.js";

const data = (path, directories = ["alpha", "two words", "中文"]) => ({ path,
  separator: "/", parent: path === "/" ? "" : "/", directories, total: directories.length, has_more: false, next_cursor: "" });
const tick = async () => { await Promise.resolve(); await Promise.resolve(); };
function setup() {
  const calls = [], writes = [], changes = [];
  const view = createDirectoryNavigation({
    read(path, signal, query) { return new Promise((resolve, reject) => calls.push({ path, signal, query, resolve, reject })); },
    create(path, name, signal) { return new Promise((resolve, reject) => writes.push({ path, name, signal, resolve, reject })); },
    onChange: state => changes.push(state),
  });
  return { view, calls, writes, changes, async load(path, value = data(path)) {
    const result = view.navigate(path); calls.at(-1).resolve(value); await result;
  } };
}

test("native paths and breadcrumbs preserve roots, UNC shares and literal POSIX backslashes", () => {
  assert.equal(directoryChild(data("/"), "a"), "/a");
  assert.equal(directoryChild(data("/has\\"), "a"), "/has\\/a");
  assert.equal(directoryChild({path:"D:\\",separator:"\\"}, "two words"), "D:\\two words");
  assert.deepEqual(directoryBreadcrumbs("\\\\server\\share\\one\\two", "\\").map(row => row.path),
    ["\\\\server\\share\\", "\\\\server\\share\\one", "\\\\server\\share\\one\\two"]);
  assert.deepEqual(directoryBreadcrumbs("D:\\work\\one", "\\").map(row => row.path), ["D:\\", "D:\\work", "D:\\work\\one"]);
  assert.deepEqual(directoryBreadcrumbs("/has\\/a", "/").map(row => row.path), ["/", "/has\\", "/has\\/a"]);
  assert.deepEqual(directoryCompletion("D:\\two w", "\\"), {path:"D:\\",prefix:"two w",limit:20});
  assert.deepEqual(directoryCompletion("/has\\/a", "/"), {path:"/has\\/",prefix:"a",limit:20});
});

test("late navigation cannot replace a newer directory or alter its history", async () => {
  const {view,calls} = setup();
  try {
    const first = view.navigate("/first"), second = view.navigate("/second");
    assert.equal(calls[0].signal.aborted, true);
    calls[1].resolve(data("/second")); await second;
    calls[0].resolve(data("/first")); await first;
    assert.equal(view.get().current.path, "/second");
    assert.deepEqual(view.get().history, ["/second"]);
  } finally { view.dispose(); }
});

test("a failed navigation retains the displayed page, requested path and safe retry", async () => {
  const ctx = setup();
  try {
    await ctx.load("/work"); const previous = ctx.view.get().current;
    const next = ctx.view.navigate("/denied");
    assert.equal(ctx.view.get().current, previous);
    assert.equal(await ctx.view.confirm(), null);
    await ctx.view.mkdir("must-not-create-in-old-directory"); assert.equal(ctx.writes.length, 0);
    ctx.calls.at(-1).reject(new Error("permission denied")); await next;
    assert.equal(ctx.view.get().requestedPath, "/denied");
    assert.equal(ctx.view.get().current, previous);
    assert.equal(await ctx.view.confirm(), null);
    const retry = ctx.view.retry(); assert.equal(ctx.calls.at(-1).path, "/denied");
    ctx.calls.at(-1).resolve(data("/denied")); await retry;
    assert.deepEqual(ctx.view.get().history, ["/work", "/denied"]);
  } finally { ctx.view.dispose(); }
});

test("back/forward positions commit only after a successful read and a new branch drops forward history", async () => {
  const ctx = setup();
  try {
    await ctx.load("/a"); await ctx.load("/b"); await ctx.load("/c");
    let pending = ctx.view.back(); ctx.calls.at(-1).reject(new Error("offline")); await pending;
    assert.equal(ctx.view.get().index, 2);
    pending = ctx.view.back(); ctx.calls.at(-1).resolve(data("/b")); await pending;
    assert.equal(ctx.view.get().index, 1);
    pending = ctx.view.forward(); ctx.calls.at(-1).resolve(data("/c")); await pending;
    assert.equal(ctx.view.get().index, 2);
    pending = ctx.view.back(); ctx.calls.at(-1).resolve(data("/b")); await pending;
    await ctx.load("/different");
    assert.deepEqual(ctx.view.get().history, ["/a", "/b", "/different"]);
  } finally { ctx.view.dispose(); }
});

test("keyset pages append without dropping selection or reordering server results", async () => {
  const ctx = setup();
  try {
    await ctx.load("/work", {...data("/work", ["node2", "node10"]),has_more:true,next_cursor:"node10",total:4});
    ctx.view.select("node2"); const more = ctx.view.more();
    assert.deepEqual(ctx.calls.at(-1).query, {filter:"",after:"node10",limit:128});
    ctx.calls.at(-1).resolve(data("/work", ["node10", "node20", "node100"])); await more;
    assert.deepEqual(ctx.view.get().current.directories, ["node2", "node10", "node20", "node100"]);
    assert.equal(ctx.view.get().selected, "node2");
    const failed = ctx.view.navigate("/work", {replace:true,filter:"missing"});
    ctx.calls.at(-1).reject(new Error("offline")); await failed;
    assert.equal(ctx.view.get().filter, "missing"); assert.equal(ctx.view.get().selected, "");
  } finally { ctx.view.dispose(); }
});

test("confirm validates the selected child on the device and never selects a deleted path", async () => {
  const ctx = setup();
  try {
    await ctx.load("/work"); ctx.view.select("two words");
    let selected = ctx.view.confirm(); assert.equal(ctx.calls.at(-1).path, "/work/two words");
    ctx.calls.at(-1).reject(new Error("deleted")); assert.equal(await selected, null);
    await ctx.load("/work"); ctx.view.select("two words"); selected = ctx.view.confirm();
    ctx.calls.at(-1).resolve(data("/normalized/two words")); assert.equal(await selected, "/normalized/two words");
    selected = ctx.view.confirm(); const late = ctx.calls.at(-1); ctx.view.dispose();
    late.resolve(data("/too-late")); assert.equal(await selected, null);
  } finally { ctx.view.dispose(); }
});

test("new folders remain exclusive writes; their canonical name is selected only after readback", async () => {
  const ctx = setup();
  try {
    await ctx.load("/work"); const create = ctx.view.mkdir("new folder");
    assert.equal(ctx.view.get().creating, true);
    const before = ctx.calls.length; await ctx.view.navigate("/other"); assert.equal(ctx.calls.length, before);
    assert.deepEqual([ctx.writes[0].path,ctx.writes[0].name], ["/work","new folder"]);
    ctx.writes[0].resolve({path:"/work/new folder",name:"new folder"}); await tick();
    assert.equal(ctx.calls.at(-1).query.filter, "new folder");
    ctx.calls.at(-1).resolve(data("/work", ["new folder"])); await create;
    assert.equal(ctx.view.get().selected, "new folder");
    const failed = ctx.view.mkdir("exists"); ctx.writes.at(-1).reject(new Error("already exists")); await failed;
    assert.equal(ctx.writes.length, 2); assert.equal(ctx.view.get().creating, false);
  } finally { ctx.view.dispose(); }
});

test("favorites and confirmed-only recents are bounded and isolated by device", () => {
  const saved = new Map(), storage = {getItem:key=>saved.get(key),setItem:(key,value)=>saved.set(key,value)};
  const first = directoryPlaces(storage,"remote:1:device1"), second = directoryPlaces(storage,"remote:1:device2");
  assert.deepEqual(first.get().recent, []);
  first.favorite("D:\\work"); first.confirm("D:\\work"); first.confirm("/other"); first.confirm("D:\\work");
  assert.deepEqual(directoryPlaces(storage,"remote:1:device1").get().recent, ["D:\\work","/other"]);
  assert.deepEqual(second.get().favorites, []); assert.deepEqual(second.get().recent, []);
  first.favorite("D:\\work"); assert.deepEqual(first.get().favorites, []);
  for(let i=0;i<30;i++)first.confirm(`/work/${i}`);
  assert.equal(first.get().recent.length,16);
  directoryPlaces({getItem(){throw Error("denied");},setItem(){throw Error("denied");}},"local").confirm("/works-without-storage");
});
