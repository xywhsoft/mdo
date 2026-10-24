import assert from "node:assert/strict";
import test from "node:test";
import { createSkillWorkbench } from "../app/web/js/features/settings/skill-workbench.js";

import { TestElement as Node, testDocument } from "./helpers/extension-dom.mjs";
const flush = () => new Promise(resolve => setImmediate(resolve));
const descendants = node => [node, ...node.children.flatMap(child => typeof child === "object" ? descendants(child) : [])];

test("Skill workbench uses project-scoped reads, protects edits and emits an unsent authoring intent", async context => {
  const original = { document: globalThis.document, window: globalThis.window };
  const body = new Node("body"), focus = new Node("button");
  globalThis.document = { ...testDocument(), body, activeElement: focus };
  const page = new EventTarget(); let discard = false;
  page.confirm = () => discard; globalThis.window = page;
  const requests = [], intents = [];
  page.addEventListener("mdo-create-skill", event => intents.push(event.detail));
  context.mock.method(globalThis, "fetch", async (url, options) => {
    requests.push(url);
    if (options.method === "POST") {
      if (url === "/api/v1/skills/validate") return Response.json({ok:true,data:{valid:false,resources_checked:false,errors:["Add name and description"],warnings:[]}});
      assert.equal(url,"/api/v1/skills/invoke?project=default");
      assert.equal(JSON.parse(options.body).id,"skill-creator");
      assert.ok(JSON.parse(options.body).arguments.includes("D:/home/skills"));
      return Response.json({ok:true,data:{text:"Expanded creator for D:/home/skills"}});
    }
    assert.equal(options.method, "GET");
    const data = url.endsWith("/projects") ? { items: [{ id: "work", name: "Work" }] } :
      url.includes("/skills/sources") ? { items: [], diagnostics: [], user_directory: "D:/home/skills", project_directory: "D:/work/.mdo/skills" } : { items: [] };
    return Response.json({ ok: true, data });
  });
  const workbench = createSkillWorkbench({ onChange() {} });
  try {
    await workbench.open();
    assert.ok(requests.includes("/api/v1/skills/sources?project=default"));
    assert.ok(requests.includes("/api/v1/skills/project?project=default"));
    const dialog = body.children[0];
    assert(dialog.classList.contains("mdo-layer"));
    const footer=dialog.querySelector("footer"),content=dialog.querySelector(".mdo-layer-body");
    assert(!content.contains(footer));assert.equal(footer.lastChild.textContent,"关闭");
    const buttons = () => descendants(dialog).filter(node => node.tagName === "BUTTON");
    buttons().find(node => node.textContent === "新建项目 Skill").dispatchEvent(new Event("click")); await flush();
    const editor = descendants(dialog).find(node => node.tagName === "TEXTAREA"); editor.value = "Unsent edits";
    assert(footer.textContent.includes("保存到项目"));assert(!descendants(content).some(n=>n.tagName==="BUTTON"&&n.textContent==="保存到项目"));
    buttons().find(node=>node.textContent==="检查格式").dispatchEvent(new Event("click"));await flush();
    assert.equal(editor.value,"Unsent edits");assert.ok(descendants(dialog).some(node=>node.textContent?.includes("Add name and description")));
    const project = descendants(dialog).find(node => node.attributes["aria-label"] === "项目");
    project.value = "work"; project.dispatchEvent(new Event("change")); await flush();
    assert.equal(project.value, "default"); assert.equal(editor.value, "Unsent edits");
    assert.equal(workbench.hasPendingChanges(), true);
    buttons().find(node => node.textContent === "用 AI 编写 Skill").dispatchEvent(new Event("click")); await flush();
    assert.equal(intents.length, 0); discard = true;
    buttons().find(node => node.textContent === "用 AI 编写 Skill").dispatchEvent(new Event("click")); await flush();
    assert.equal(intents.length, 0);
    const requirements = descendants(dialog).find(node => node.attributes["aria-label"] === "构建需求");
    assert(footer.textContent.includes("打开构建对话"));assert(!content.textContent.includes("打开构建对话"));
    requirements.value = "Generate a reusable review checklist";
    buttons().find(node => node.textContent === "打开构建对话").dispatchEvent(new Event("click")); await flush();
    assert.equal(intents.length, 1); assert.equal(intents[0].projectId, "default");
    assert.equal(intents[0].title, "创建 Skill");
    assert.ok(intents[0].text.includes("D:/home/skills")); assert.equal(body.children.length, 0);
  } finally { workbench.destroy(); globalThis.document = original.document; globalThis.window = original.window; }
});

test("builder expansion failures preserve requirements and target changes cancel pending drafts", async context => {
  const original = { document: globalThis.document, window: globalThis.window };
  const body = new Node("body");
  globalThis.document = { ...testDocument(), body, activeElement: null };
  globalThis.window = new EventTarget(); window.confirm = () => true;
  const intents=[];window.addEventListener("mdo-create-skill",event=>intents.push(event.detail));
  let release, delayed=false;
  context.mock.method(globalThis,"fetch",async (url,options)=>{
    if (options.method==="POST") {
      if (delayed) return new Promise(resolve=>{release=()=>resolve(Response.json({ok:true,data:{text:"Late draft"}}));});
      return Response.json({ok:false,error:{message:"Creator disabled"}},{status:403});
    }
    const data=url.endsWith("/projects")?{items:[]}:url.includes("/skills/sources")?
      {items:[],user_directory:"D:/home/skills",project_directory:"D:/work/.mdo/skills"}:{items:[]};
    return Response.json({ok:true,data});
  });
  const workbench=createSkillWorkbench({onChange(){}});
  try {
    await workbench.open({builder:true,mode:"review",id:"existing"});
    const dialog=body.children[0];
    const fields=()=>descendants(dialog);
    const requirements=fields().find(node=>node.attributes["aria-label"]==="构建需求");
    requirements.value="Review this safely";
    const launch=fields().find(node=>node.textContent==="打开构建对话");
    launch.dispatchEvent(new Event("click"));await flush();
    assert.equal(intents.length,0);assert.equal(requirements.value,"Review this safely");
    assert.ok(fields().some(node=>node.textContent?.includes("Creator disabled")));
    assert.equal(workbench.hasPendingChanges(),true);
    delayed=true;launch.dispatchEvent(new Event("click"));await flush();
    workbench.destroy();release();await flush();
    assert.equal(intents.length,0);assert.equal(body.children.length,0);
  } finally {workbench.destroy();globalThis.document=original.document;globalThis.window=original.window;}
});

test("a failed project lookup cannot reuse another project's builder directory", async context => {
  const original={document:globalThis.document,window:globalThis.window};
  const body=new Node("body");globalThis.document={...testDocument(),body,activeElement:null};
  globalThis.window=new EventTarget();window.confirm=()=>true;
  let posts=0,intents=0;window.addEventListener("mdo-create-skill",()=>intents++);
  context.mock.method(globalThis,"fetch",async (url,options)=>{
    if (options.method==="POST") {posts++;return Response.json({ok:true,data:{text:"Wrong target"}});}
    if (url.includes("project=work")) return Response.json({ok:false,error:{message:"Project unavailable"}},{status:404});
    const data=url.endsWith("/projects")?{items:[{id:"work",name:"Work"}]}:url.includes("/skills/sources")?
      {items:[],user_directory:"D:/home/skills",project_directory:"D:/default/.mdo/skills"}:{items:[]};
    return Response.json({ok:true,data});
  });
  const workbench=createSkillWorkbench({onChange(){}});
  try {
    await workbench.open({builder:true,scope:"project"});
    const dialog=body.children[0],fields=()=>descendants(dialog);
    const requirements=fields().find(node=>node.attributes["aria-label"]==="构建需求");requirements.value="Keep my request";
    const project=fields().find(node=>node.attributes["aria-label"]==="项目");
    project.value="work";project.dispatchEvent(new Event("change"));await flush();
    fields().find(node=>node.textContent==="打开构建对话").dispatchEvent(new Event("click"));await flush();
    assert.equal(posts,0);assert.equal(intents,0);assert.equal(requirements.value,"Keep my request");
    assert.ok(fields().some(node=>node.textContent?.includes("directory is unavailable")));
  } finally {workbench.destroy();globalThis.document=original.document;globalThis.window=original.window;}
});

test("project saves keep bottom actions and drafts on failure; busy guards and teardown isolate late responses",async context=>{
  const original={document:globalThis.document,window:globalThis.window};
  globalThis.document=testDocument();globalThis.window=new EventTarget();window.confirm=()=>true;
  let fail=true,gate,changes=0;const writes=[];
  context.mock.method(globalThis,"fetch",async(url,options)=>{
    if(options.method==="PUT") {writes.push(JSON.parse(options.body));if(gate)await gate;return fail?Response.json({ok:false,error:{message:"Project write unavailable"}},{status:403}):Response.json({ok:true,data:{revision:"saved"}});}
    return Response.json({ok:true,data:url.includes("/skills/sources")?{items:[],user_directory:"D:/home/skills",project_directory:"D:/default/.mdo/skills"}:{items:[]}});
  });
  const workbench=createSkillWorkbench({onChange(){changes++;}});
  try {
    await workbench.open();let dialog=document.body.querySelector("dialog");
    const button=(root,text)=>descendants(root).find(n=>n.tagName==="BUTTON"&&n.textContent===text);
    button(dialog,"新建项目 Skill").click();await flush();
    dialog.querySelector("input").value="project-fixture";const content=dialog.querySelector("textarea");content.value="Retain this source";
    const footer=dialog.querySelector("footer");button(footer,"保存到项目").click();await flush();
    assert.equal(content.value,"Retain this source");assert(dialog.textContent.includes("Project write unavailable"));assert.equal(changes,0);
    fail=false;let release;gate=new Promise(resolve=>{release=resolve;});button(footer,"保存到项目").click();button(footer,"保存到项目").click();await flush();
    assert.equal(writes.length,2);dialog.dispatchEvent(new Event("cancel",{cancelable:true}));await flush();assert(dialog.open);
    workbench.destroy();const next=workbench.open();const replacement=document.body.querySelector("dialog");release();gate=null;await next;await flush();
    assert.equal(document.body.querySelector("dialog"),replacement);assert.equal(changes,0);assert(!replacement.textContent.includes("已保存"));
  }finally{workbench.destroy();Object.assign(globalThis,original);}
});
