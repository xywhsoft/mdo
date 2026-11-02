import assert from "node:assert/strict";
import test from "node:test";
globalThis.window = new EventTarget();
const { loadCommands, runCommand, cancelCommand, parseCommand } = await import("../app/web/js/features/chat/extension-commands.js");
const { withSessionRuntime } = await import("../app/web/js/state/session-runtime.js");
const { errorMessage } = await import("../app/web/js/utils/dom.js");
const flush = () => new Promise(resolve => setImmediate(resolve));
const deferred = () => { let resolve; const promise = new Promise(done => { resolve = done; }); return { promise, resolve }; };

test("compact waits for the same session's recovery inspection before submission", async context => {
  const gate = deferred(), scope = { projectId: "work", sessionId: "compact-inspection" };
  const inspection = withSessionRuntime(scope.projectId, scope.sessionId, () => gate.promise);
  await flush();
  let posts = 0;
  context.mock.method(globalThis, "fetch", async (_url, options) => {
    posts++;
    return Response.json({ ok: true, data: { id: JSON.parse(options.body).invocation_id, terminal: true, state: "succeeded" } });
  });
  const command = runCommand({ id: "compact", requires_session: true }, "", scope);
  try { await flush(); assert.equal(posts, 0, "recovery still owns the exclusive server runtime"); }
  finally { gate.resolve(); await Promise.all([inspection, command]); }
  assert.equal(posts, 1);
});

test("compact owns the session until its asynchronous receipt is terminal", async context => {
  const polling = deferred(), receipt = deferred(); let id;
  context.mock.method(globalThis, "fetch", async (_url, options) => {
    if (options.method === "POST") {
      id = JSON.parse(options.body).invocation_id;
      return Response.json({ ok: true, data: { id, terminal: false, state: "running" } });
    }
    polling.resolve(); await receipt.promise;
    return Response.json({ ok: true, data: { id, terminal: true, state: "succeeded" } });
  });
  const scope = { projectId: "work", sessionId: "compact-polling" };
  const command = runCommand({ id: "compact", requires_session: true }, "", scope);
  await polling.promise;
  let inspected = false;
  const inspection = withSessionRuntime(scope.projectId, scope.sessionId, async () => { inspected = true; });
  try { await flush(); assert.equal(inspected, false, "receipt polling must retain the runtime lane"); }
  finally { receipt.resolve(); await Promise.all([command, inspection]); }
  assert.equal(inspected, true);
});

test("a cancelled compact waiting for inspection never submits or bypasses its owner", async context => {
  const gate = deferred(), scope = { projectId: "work", sessionId: "compact-cancel" }, controller = new AbortController();
  const inspection = withSessionRuntime(scope.projectId, scope.sessionId, () => gate.promise);
  await flush();
  let posts = 0, followerEntered = false;
  context.mock.method(globalThis, "fetch", async () => { posts++; throw Error("unexpected POST"); });
  const command = runCommand({ id: "compact", requires_session: true }, "", scope, { signal: controller.signal });
  const rejected = assert.rejects(command, { name: "AbortError" });
  const follower = withSessionRuntime(scope.projectId, scope.sessionId, async () => { followerEntered = true; });
  try {
    controller.abort(); await rejected; await flush();
    assert.equal(posts, 0); assert.equal(followerEntered, false);
  } finally { gate.resolve(); await Promise.all([inspection, follower]); }
});

test("stop and a compact in another session do not wait for the held session", async context => {
  const gate = deferred();
  const inspection = withSessionRuntime("work", "compact-other", () => gate.promise);
  await flush();
  let posts = 0;
  context.mock.method(globalThis, "fetch", async (_url, options) => {
    posts++;
    return Response.json({ ok: true, data: { id: JSON.parse(options.body).invocation_id, terminal: true, state: "succeeded" } });
  });
  try {
    await runCommand({ id: "stop", requires_session: false }, "", { projectId: "work", sessionId: "compact-other" });
    await runCommand({ id: "compact", requires_session: true }, "", { projectId: "work", sessionId: "independent" });
    assert.equal(posts, 2);
  } finally { gate.resolve(); await inspection; }
});

test("a removed Agent produces the localized recovery guidance from its command receipt", async context => {
  context.mock.method(globalThis, "fetch", async (_url, options) => Response.json({ ok: true, data: {
    id: JSON.parse(options.body).invocation_id, terminal: true, state: "failed",
    error_code: "agent_unavailable", error: "agent_unavailable: Agent definition unavailable: agent.removed-fixture",
  } }));
  await assert.rejects(runCommand({ id: "compact", requires_session: true }, "", { projectId: "work", sessionId: "compact-orphan" }), error => {
    assert.equal(error.code, "agent_unavailable");
    assert.match(errorMessage(error), /输入框.*Agent.*插件/);
    return true;
  });
});

test("command parsing keeps raw arguments and leaves both Skill forms separate",()=>{
  assert.deepEqual(parseCommand('/model ["model-id"]'),{id:"model",raw:'["model-id"]'});
  assert.equal(parseCommand('/skill:review hi'),null);assert.equal(parseCommand('$review hi'),null);
  assert.deepEqual(parseCommand('/review "" C:\\path'),{id:"review",raw:'"" C:\\path'});
});
test("scoped catalog has no builtin fallback for invalid or disabled shadows and refresh sees edits",async context=>{
  let calls=0;
  context.mock.method(globalThis,"fetch",async url=>{
    assert.equal(url,"/api/v1/commands?project=work");calls++;
    return Response.json({ok:true,data:{help:{id:"help",valid:false,enabled:true},stop:{id:"stop",valid:true,enabled:false},
      custom:{id:"custom",valid:true,enabled:true,revision:String(calls)}}});
  });
  assert.deepEqual((await loadCommands("work")).map(c=>c.id),["custom"]);
  assert.equal((await loadCommands("work"))[0].revision,"1");
  assert.equal((await loadCommands("work",{refresh:true}))[0].revision,"2");
});
test("lost POST acknowledgement recovers the same identity without replay",async context=>{
  const requests=[];let id;
  context.mock.method(globalThis,"fetch",async(url,options)=>{
    requests.push([url,options.method]);
    if(options.method==="POST"){
      const body=JSON.parse(options.body);id=body.invocation_id;
      assert.equal(body.raw_arguments,'["",true]');assert.equal(body.session_id,"session");
      throw Error("connection lost after admission");
    }
    assert.equal(url,"/api/v1/command-invocations/"+id);
    return Response.json({ok:true,data:{id,state:"succeeded",terminal:true,actions:[]}});
  });
  const result=await runCommand({id:"custom",revision:"v1"},'["",true]',{projectId:"work",sessionId:"session"});
  assert.equal(result.id,id);assert.match(id,/^cmd-[a-f0-9]{32}$/);
  assert.deepEqual(requests.map(r=>r[1]),["POST","GET"]);
});
test("an uncertain invocation is re-read when the user retries, never submitted again",async context=>{
  let posts=0,id,available=false;
  context.mock.method(globalThis,"fetch",async(url,options)=>{
    if(options.method==="POST"){posts++;id=JSON.parse(options.body).invocation_id;throw Error("offline");}
    if(!available)throw Error("offline");
    assert.equal(url,"/api/v1/command-invocations/"+id);
    return Response.json({ok:true,data:{id,state:"succeeded",terminal:true}});
  });
  const command={id:"uncertain"},scope={projectId:"work",sessionId:"session"};
  await assert.rejects(()=>runCommand(command,"",scope),/结果不确定/);
  available=true;assert.equal((await runCommand(command,"",scope)).id,id);assert.equal(posts,1);
});
test("explicit cancellation uses the invocation endpoint",async context=>{
  context.mock.method(globalThis,"fetch",async(url,options)=>{
    assert.equal(url,"/api/v1/command-invocations/cmd-"+"a".repeat(32));assert.equal(options.method,"DELETE");
    return Response.json({ok:true,data:{state:"cancelled"}});
  });
  assert.equal(await cancelCommand("cmd-"+"a".repeat(32),"local"),true);
});
