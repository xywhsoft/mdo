import test from "node:test";
import assert from "node:assert/strict";
import { buildNotices, matchesNotice } from "../app/web/js/features/notifications/model.js";
import { noticeTone, bubbleText } from "../app/web/js/features/notifications/indicator.js";
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
 assert.deepEqual(buildNotices(args).map(n=>n.id),["app-update","tools-update-core","tools-guide","account-guide","offer","old"]);
 const preferences={"app-update":{dismissed:"hash",read:"hash"},"tools-guide":{snooze:Date.now()+60000},old:{dismissed:1},marketing:false};
 const ids=buildNotices({...args,preferences}).map(n=>n.id);assert(ids.includes("app-update"));assert(ids.includes("old"));assert(!ids.includes("tools-guide"));assert(!ids.includes("offer"));
 assert(!buildNotices({...args,update:{available:false},account:{state:"refreshing"}}).some(n=>["account-guide","app-update"].includes(n.id)));
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
