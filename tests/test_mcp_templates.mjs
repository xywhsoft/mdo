import assert from "node:assert/strict";
import {readFileSync} from "node:fs";
import test from "node:test";
import {testDocument,descendants} from "./helpers/extension-dom.mjs";
import {mcpVariantInputs,mcpTemplateDiff,parseMcpTemplate} from "../app/web/js/features/settings/mcp-template.js";
const flush=()=>new Promise(resolve=>setImmediate(resolve));
const recipe={format:"mdo.mcp-template.v1",id:"service",inputs:[{id:"key",label:"Private key",type:"text",secret:true,required:true},{id:"other",label:"Other variant",type:"text",secret:false,required:true}],variants:[{id:"remote",label:"Remote",config:{transport:{type:"streamable-http",endpoint:"https://example.invalid/mcp",headers:[{name:"Authorization",secret_ref:"input:key"}]}}}]};
test("MCP schemas isolate variant parameters, disallow private defaults and describe target changes",()=>{
 assert.deepEqual(mcpVariantInputs(recipe,recipe.variants[0]).map(i=>i.id),["key"]);
 const bad=structuredClone(recipe);bad.inputs[0].default="private";assert.throws(()=>parseMcpTemplate(JSON.stringify(bad)),/默认值/);
 const changed=structuredClone(recipe);changed.variants[0].config.transport.endpoint="https://other.invalid/mcp";
 assert.deepEqual(mcpTemplateDiff(recipe,changed),[{path:"variants.0.config.transport.endpoint",before:"https://example.invalid/mcp",after:"https://other.invalid/mcp"}]);
});
