import assert from "node:assert/strict";
import test from "node:test";
globalThis.window = new EventTarget();
const { loadCommands, runCommand, cancelCommand, parseCommand } = await import("../app/web/js/features/chat/extension-commands.js");

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
