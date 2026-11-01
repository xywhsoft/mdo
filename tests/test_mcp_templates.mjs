import assert from "node:assert/strict";
import {readFileSync} from "node:fs";
import test from "node:test";
import {testDocument,descendants} from "./helpers/extension-dom.mjs";
import {mcpVariantInputs,mcpDefaultVariant,mcpTemplateDiff,parseMcpTemplate} from "../app/web/js/features/settings/mcp-template.js";
const flush=()=>new Promise(resolve=>setImmediate(resolve));
const recipe={format:"mdo.mcp-template.v1",id:"service",inputs:[{id:"key",label:"Private key",type:"text",secret:true,required:true},{id:"other",label:"Other variant",type:"text",secret:false,required:true}],variants:[{id:"remote",label:"Remote",config:{transport:{type:"streamable-http",endpoint:"https://example.invalid/mcp",headers:[{name:"Authorization",secret_ref:"input:key"}]}}}]};
test("MCP schemas isolate variant parameters, disallow private defaults and describe target changes",()=>{
 assert.deepEqual(mcpVariantInputs(recipe,recipe.variants[0]).map(i=>i.id),["key"]);
 const bad=structuredClone(recipe);bad.inputs[0].default="private";assert.throws(()=>parseMcpTemplate(JSON.stringify(bad)),/默认值/);
 const changed=structuredClone(recipe);changed.variants[0].config.transport.endpoint="https://other.invalid/mcp";
 assert.deepEqual(mcpTemplateDiff(recipe,changed),[{path:"variants.0.config.transport.endpoint",before:"https://example.invalid/mcp",after:"https://other.invalid/mcp"}]);
});

test("default MCP variants follow platform declarations and never select an advanced-only grant",()=>{
 const t={...recipe,inputs:[],variants:[{id:"linux",default:true,platforms:["linux-x86_64"]},{id:"desktop",default:true,platforms:["windows-x86_64"]},{id:"browser-profile",default:false,platforms:["windows-x86_64"]}]};
 assert.equal(mcpDefaultVariant(t,"windows-x86_64").id,"desktop");assert.equal(mcpDefaultVariant(t,"linux-x86_64").id,"linux");assert.equal(mcpDefaultVariant(t,"android-arm64-v8a"),null);
 assert.equal(parseMcpTemplate(JSON.stringify(t)).variants.length,3);
 const duplicate=structuredClone(t);duplicate.variants[2].default=true;assert.throws(()=>parseMcpTemplate(JSON.stringify(duplicate)),/默认/);
 const invalid=structuredClone(t);invalid.variants[0].default="true";assert.throws(()=>parseMcpTemplate(JSON.stringify(invalid)),/布尔/);
 const old={...recipe,inputs:[],variants:[{id:"ordinary",platforms:["any"]},{id:"extra",default:false,platforms:["any"]}]};
 assert.equal(mcpDefaultVariant(old,"windows-x86_64").id,"ordinary");
});
