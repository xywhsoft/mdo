import test from "node:test";
import assert from "node:assert/strict";
import { buildNotices, matchesNotice } from "../app/web/js/features/notifications/model.js";
import { noticeTone, bubbleText } from "../app/web/js/features/notifications/indicator.js";
import { createToolPanel, toolActions } from "../app/web/js/features/notifications/tool-panel.js";
const device={build_id:30000001,platform:"windows-x86_64",edition:"desktop"};
const notice={id:"old",revision:2,title:"Old versions",body:"Update",icon:"warning",min_build:30000000,max_build:30000002,platforms:[device.platform],editions:[device.edition],languages:["zh-CN"],audience:"all",starts_at:1,ends_at:10};
test("notice filters include lower and exclude upper version/time boundaries",()=>{
 assert(matchesNotice(notice,device,"zh-CN",false,9000));
 assert(!matchesNotice(notice,{...device,build_id:30000002},"zh-CN",false,9000));
 assert(!matchesNotice(notice,device,"zh-CN",false,10000));
 assert(!matchesNotice(notice,device,"en-US",false,9000));
 assert(!matchesNotice({...notice,audience:"signed_in"},device,"zh-CN",false,9000));
});
const copy={update:"Update",toolsUpdate:"Tools update",install:"Install",login:"Sign in"};
test("update/read/resolution, guide snooze, marketing and revision reset",()=>{
 const args={distribution:{...device,tools:[],installed:{core:"old",core_revision:1},toolpacks:[{id:"core",revision:2}],notices:[{...notice,starts_at:0,ends_at:0},{...notice,id:"offer",icon:"coupon",starts_at:0,ends_at:0}]},update:{available:true,sha256:"hash"},account:{state:"signed_out"},locale:"zh-CN",copy};
 assert.deepEqual(buildNotices(args).map(n=>n.id),["app-update","tools-guide","account-guide","offer","old"]);
 const preferences={"app-update":{dismissed:"hash",read:"hash"},"tools-guide":{snooze:Date.now()+60000},old:{dismissed:1},marketing:false};
 const ids=buildNotices({...args,preferences}).map(n=>n.id);assert(ids.includes("app-update"));assert(ids.includes("old"));assert(!ids.includes("tools-guide"));assert(!ids.includes("offer"));
 assert(!buildNotices({...args,update:{available:false},account:{state:"refreshing"}}).some(n=>["account-guide","app-update"].includes(n.id)));
});
test("tool operations respect target admission, dependencies, withdrawal and compatibility",()=>{
 const d={...device,installed:{core:"x",core_revision:1,retired:[{id:"core"}]},toolpacks:[{id:"core",revision:2,min_build:30000001,max_build:30000002}]};
 assert(toolActions(d,"core").install);assert(toolActions(d,"core").repair);assert(toolActions(d,"core").rollback);assert(toolActions(d,"core").uninstall);
 assert(!toolActions({...d,build_id:30000002},"core").install);
 assert(!toolActions({...d,toolpacks:[{...d.toolpacks[0],dependencies:[{id:"python",revision:1}]}]},"core").repair);
 assert(!toolActions({...d,toolpacks:[{...d.toolpacks[0],status:"withdrawn"}]},"core").install);
 assert(!toolActions({...d,installed:{...d.installed,python:"p",python_metadata:{dependencies:[{id:"core",revision:1}]}}},"core").uninstall);
 for(const target of [{selected:{mode:"view"},connected:true},{selected:{mode:"control"},connected:false},{selected:{mode:"control"},connected:true,runtimeChanged:true}])for(const v of Object.values(toolActions(d,"core",target)))assert.equal(v,false);
});
test("Android application and bundled tools produce one update notification",()=>{
 const d={edition:"full",tools:[],toolpack_revision:2,bundled_toolpack_revision:1};
 const result=buildNotices({distribution:d,update:{available:true,sha256:"new",notes:"App notes"},copy:{...copy,android:"Bundled tools"},locale:"zh-CN"});
 assert.equal(result.filter(n=>n.icon==="update").length,1);assert.match(result[0].body,/App notes[\s\S]*Bundled tools/);assert.equal(result[0].action,"update");
});
test("tool page offers APK upgrades on Android and independent packages on Windows",async()=>{
 class Element {
  constructor(tag){this.tag=tag;this.children=[];}
  append(...children){this.children.push(...children);}
  replaceChildren(){this.children=[];}
 }
 const previous=globalThis.document;
 globalThis.document={createElement:tag=>new Element(tag)};
 try {
  const labels={android:"Tools update with the full APK only",androidLite:"Lite edition",androidFull:"Full edition",installFull:"Install full",full:"Full APK updates",desktop:"Independent packages",core:"Core",python:"Python",installPackage:"Install package",updatePackage:"Update package",cleanup:"Cleanup",check:"Check packages"};
  let fullRequests=0,context={},commands=[];
  const panel=createToolPanel({copy:()=>labels,target:()=>context,command:async(...args)=>commands.push(args),openFull:async()=>fullRequests++});
  for(const edition of ["lite","full"]) {
   const body=new Element("div");
   panel.render(body,{platform:"android-arm64-v8a",edition,toolpacks:[{id:"core",revision:2}],installed:{core:"old",core_revision:1}});
   assert(body.children.some(e=>e.textContent===labels.android));
   assert(body.children.some(e=>e.textContent===labels[edition==="lite"?"androidLite":"androidFull"]));
   const buttons=body.children.filter(e=>e.tag==="button");
   assert.deepEqual(buttons.map(e=>e.textContent),[labels[edition==="lite"?"installFull":"full"]]);
   await buttons[0].onclick();
   context={selected:{id:"phone",mode:"view"},connected:true};
   panel.render(body,{edition});
   assert(body.children.find(e=>e.tag==="button").disabled);
   context={};
  }
  assert.equal(fullRequests,2);assert.deepEqual(commands,[]);
  const body=new Element("div");panel.render(body,{...device,toolpacks:[{id:"core",revision:2}]});
  const core=body.children.find(e=>e.tag==="section");
  await core.children.find(e=>e.tag==="div").children[0].onclick();
  assert.deepEqual(commands,[["install","core"]]);
  assert(body.children.some(e=>e.tag==="button"&&e.textContent===labels.check));
 } finally {globalThis.document=previous;}
});
test("tool failures distinguish checksums from functional probes and show retained diagnostics",()=>{
 class Element {constructor(tag){this.tag=tag;this.children=[];}append(...children){this.children.push(...children);}replaceChildren(){this.children=[];}}
 const previous=globalThis.document;globalThis.document={createElement:tag=>new Element(tag)};
 try {
  const labels={stages:{downloading:"Download",probing:"Probe"},errors:{checksum:"SHA mismatch",probe:"Capability failed"},expectedHash:"Expected",actualHash:"Actual",diagnosticSaved:"Saved diagnostic"};
  const panel=createToolPanel({copy:()=>labels,target:()=>({}),command:async()=>{},openFull:async()=>{}}),body=new Element("div");
  panel.render(body,{...device,message:"Generic failure",error:{stage:"downloading",code:"checksum",actual_bytes:12,expected_bytes:20,actual_sha256:"abc",expected_sha256:"def",download:"data/toolpacks/download.failed"}});
  const texts=()=>body.children.map(e=>e.textContent);
  assert(texts().includes("Download · SHA mismatch"));assert(texts().includes("abc"));assert(texts().includes("def"));assert(texts().includes("12 / 20 bytes"));assert(texts().includes("data/toolpacks/last-error.json"));assert(!texts().includes("Generic failure"));
  panel.render(body,{...device,error:{stage:"probing",code:"probe",file:"7zip/7z.exe",detail:"7zip: timed out"}});
  assert(texts().includes("Probe · Capability failed · 7zip/7z.exe"));assert(texts().includes("7zip: timed out"));
 }finally{globalThis.document=previous;}
});
test("indicators keep update priority and bubble content while mapping notification severity",()=>{
 const copy={newVersion:"有一个新版本发布了"};
 const update={rank:0,title:"应用更新",body:"新增 SSH 工具",icon:"update"};
 assert.equal(noticeTone(update),"update");
 assert.deepEqual(bubbleText(update,copy),{title:copy.newVersion,body:"新增 SSH 工具"});
 for(const type of ["error","warning","success","coupon"])assert.equal(noticeTone({icon:type,rank:4}),type);
 for(const type of ["message","tools","account","unknown"])assert.equal(noticeTone({icon:type,rank:4}),"info");
 assert.deepEqual(bubbleText({title:"说明",body:"<script>literal</script>"},copy),{title:"说明",body:"<script>literal</script>"});
});
