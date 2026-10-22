import test from "node:test";
import assert from "node:assert/strict";
import { expandMcpCommand,expandMcpArguments } from "../app/web/js/features/chat/mcp-context.js";

test("legacy and typed MCP commands preserve prompt arguments and fill an unsent draft",async context=>{
  const seen=[];
  context.mock.method(globalThis,"fetch",async(url,options)=>{
    if(options.method==="GET"){assert.equal(url,"/api/v1/mcp/project?project=work");return Response.json({ok:true,data:{items:[]}});}
    assert.equal(url,"/api/v1/mcp/server/context");seen.push(JSON.parse(options.body));
    return Response.json({ok:true,data:{id:"op-prompt",terminal:true,state:"succeeded",result:{content:{messages:[{role:"user",content:{type:"text",text:"Prompt fixture"}}]}}}});
  });
  assert.equal(await expandMcpCommand('/mcp server review {"path":"C:\\\\work","empty":""}',"work"),"Prompt fixture");
  assert.equal(await expandMcpArguments(["server","review",{path:"C:\\work",empty:""}],"work"),"Prompt fixture");
  assert.deepEqual(seen[0],seen[1]);assert.equal(seen[0].params.arguments.path,"C:\\work");
});
test("invalid typed MCP prompt parameters fail before contacting a server",async context=>{
  let calls=0;context.mock.method(globalThis,"fetch",async()=>{calls++;throw Error("unexpected fetch");});
  for(const args of [null,false,0,[],{x:1}])await assert.rejects(()=>expandMcpArguments(["server","prompt",args],"work"));
  assert.equal(calls,0);
});
test("disabled project MCP shadows never fall through to a global server",async context=>{
  let calls=0;context.mock.method(globalThis,"fetch",async()=>{calls++;return Response.json({ok:true,data:{items:[{id:"server",enabled:false,valid:true}]}});});
  await assert.rejects(()=>expandMcpArguments(["server","prompt"],"work"),/已停用/);assert.equal(calls,1);
});
