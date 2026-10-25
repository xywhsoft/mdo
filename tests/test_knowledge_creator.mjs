import test from "node:test";
import assert from "node:assert/strict";
import {readFileSync,existsSync} from "node:fs";
import {testDocument,descendants} from "./helpers/extension-dom.mjs";
test("web knowledge creation resets the index and builds a Windows-compatible inert artifact",async t=>{
 const previous=globalThis.document;globalThis.document=testDocument();
 const format=new URL("../app/web/js/features/settings/knowledge-format.js",import.meta.url).href;
 const site=new URL("../../home/host/xywhsoft_ai/plugin/mdo/page/creator/knowledge-builder.js",import.meta.url);
 if(!existsSync(site)){globalThis.document=previous;t.skip("Platform checkout is required for the web-builder integration");return;}
 const source=readFileSync(site,"utf8").replace('"/mdo/knowledge-format.js"',JSON.stringify(format));
 try{const {mountKnowledgeBuilder}=await import("data:text/javascript;base64,"+Buffer.from(source).toString("base64"));
  const form=document.createElement("form"),box=document.createElement("fieldset"),kind=document.createElement("select"),artifact=document.createElement("input");kind.value="knowledge";Object.defineProperty(form,"elements",{value:{kind,artifact}});form.append(box);
  const builder=mountKnowledgeBuilder(form,box);builder.reset();builder.sync();assert.equal(box.hidden,false);assert.equal(artifact.required,false);
  const field=title=>descendants(box).find(n=>n.tagName==="LABEL"&&n.textContent===title).children.at(-1);
  assert.equal(field("版本（1.0.0）").value,"1.0.0");assert.equal(field("索引文档路径").value,"index.md");assert(field("索引正文（使用默认单文档时可直接编辑；导入目录后按原文档保留）").value.includes("知识库索引"));
  for(const [title,value]of[["作品 ID（小写英文，发布后不变）","browser-library"],["知识库名称","Library"],["描述：面向用户说明内容用途","User description"],["使用说明与资料来源","Original test-only documents"]])field(title).value=value;
  const file=builder.artifact(),p=JSON.parse(await file.text());assert.equal(p.resources.length,1);assert.equal(p.resources[0].kind,"knowledge");assert.equal(p.manifest.min_build_id,60000009);assert.equal(JSON.parse(p.resources[0].content).format,"mdo.knowledge.v1");assert.equal(p.resources[0].files[0].path,"index.md");assert(JSON.parse(p.resources[0].content).inject_prompt);
  builder.reset();assert.equal(field("知识库名称").value,"");assert.equal(field("索引文档路径").value,"index.md");
 }finally{globalThis.document=previous;}
});
