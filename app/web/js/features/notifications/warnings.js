const locales = {
  "zh-CN": { title:"警告",summary:n=>`有 ${n} 项需要处理的警告，点击查看详情。`,check:"重新检查",close:"关闭",later:"稍后提醒",settings:"前往设置",path:"位置",suggestion:"处理建议",stale:"暂时无法检查当前状态，已保留上次的警告信息。",retry:"请检查设备连接后重新检查。",truncated:"警告信息较多，当前仅显示部分内容；处理后请重新检查。",root:"知识库目录不可用",metadata:"知识库配置无效",index:"知识库索引不可用",config:"知识库启停配置不可用",scan:"知识库检查未完成",limit:"部分知识库提示词未注入",impact:"受影响的知识库本次不会注入提示词，普通对话可以继续。",repair:"请检查对应路径、读取权限、knowledge.json 和索引文件，修复后重新检查。",reasons:{root_not_directory:"knowledge 路径被普通文件或链接占用，原文件已保留。",root_unavailable:"无法读取知识库目录，请检查路径和权限。",invalid_metadata:"knowledge.json 缺失、无法读取或格式无效。",index_unavailable:"索引文件缺失、无法读取、为空或超过 4 MiB。",config_unavailable:"无法读取扩展启停配置，知识库提示词已跳过。",scan_failed:"目录读取中断，尚未检查的知识库已跳过。",scan_limit:"本次最多检查 256 个知识库目录。",hint_limit:"知识库提示词超过 128 KiB 总预算，超出部分已跳过。"} },
  "en-US": { title:"Warnings",summary:n=>`${n} warning(s) need attention. Select to view details.`,check:"Check again",close:"Close",later:"Remind later",settings:"Open settings",path:"Location",suggestion:"Suggested action",stale:"Current checks are unavailable; previous warnings have been retained.",retry:"Check the device connection and try again.",truncated:"Only part of the diagnostic list is shown. Check again after resolving the listed issues.",root:"Knowledge directory unavailable",metadata:"Invalid knowledge metadata",index:"Knowledge index unavailable",config:"Knowledge preferences unavailable",scan:"Knowledge check incomplete",limit:"Some knowledge hints were omitted",impact:"Affected knowledge hints are skipped for this run. Ordinary conversations can continue.",repair:"Check the path, read permissions, knowledge.json and index, then check again.",reasons:{} },
  "ru-RU": { title:"Предупреждения",summary:n=>`Требуют внимания: ${n}. Нажмите для подробностей.`,check:"Проверить снова",close:"Закрыть",later:"Напомнить позже",settings:"Открыть настройки",path:"Расположение",suggestion:"Рекомендация",stale:"Проверка недоступна; предыдущие предупреждения сохранены.",retry:"Проверьте соединение с устройством и повторите проверку.",truncated:"Показана только часть предупреждений. После исправления повторите проверку.",root:"Каталог базы знаний недоступен",metadata:"Неверные метаданные базы знаний",index:"Индекс базы знаний недоступен",config:"Настройки базы знаний недоступны",scan:"Проверка базы знаний не завершена",limit:"Часть подсказок базы знаний пропущена",impact:"Затронутые подсказки пропущены. Обычные диалоги могут продолжаться.",repair:"Проверьте путь, права чтения, knowledge.json и индекс, затем повторите проверку.",reasons:{} },
};
export const warningCopy = locale => locales[locale] || locales["en-US"];
// UI fingerprint only; never used as an integrity or authorization check.
export function warningRevision(value) {
  const text=JSON.stringify(value);let a=2166136261,b=5381;
  for(let i=0;i<text.length;i++){a=Math.imul(a^text.charCodeAt(i),16777619);b=Math.imul(b,33)^text.charCodeAt(i);}
  return (a>>>0).toString(16).padStart(8,"0")+(b>>>0).toString(16).padStart(8,"0");
}
const sections={knowledge:"knowledge",skills:"skills",mcp:"mcp",modules:"extensions",plugins:"extensions",extensions:"extensions",sessions:"diagnostics",schedules:"diagnostics"};
export function warningNotice({diagnostics,failed=false,online=[],locale}) {
  const copy=warningCopy(locale),rows=new Map();
  for(const row of Array.isArray(diagnostics?.items)?diagnostics.items:[]){
    if(!row||typeof row.domain!=="string"||typeof row.message!=="string")continue;
    const id="diagnostic:"+JSON.stringify([row.domain,row.code||row.stage||"check",row.subject_id||"",row.path||"",row.code?"":row.message]);
    const code=typeof row.code==="string"?row.code:"",knowledge=row.domain==="knowledge";
    const kind=code?.startsWith("root_")?"root":code==="invalid_metadata"?"metadata":code==="index_unavailable"?"index":code==="config_unavailable"?"config":code==="hint_limit"?"limit":"scan";
    rows.set(id,{id,revision:warningRevision(row),title:knowledge?copy[kind]+(row.subject_id?` · ${row.subject_id}`:""):`${row.domain}${row.subject_id?` · ${row.subject_id}`:""}`,
      body:knowledge?`${copy.reasons[code]||row.message}\n${copy.impact}`:row.message,path:typeof row.path==="string"?row.path:"",remedy:knowledge?copy.repair:typeof row.remedy==="string"?row.remedy:"",section:sections[row.domain]||"diagnostics"});
  }
  if(diagnostics?.truncated)rows.set("diagnostic:truncated",{id:"diagnostic:truncated",revision:1,title:copy.title,body:copy.truncated,section:"diagnostics"});
  if(failed)rows.set("diagnostic:unavailable",{id:"diagnostic:unavailable",revision:1,title:copy.title,body:copy.stale,remedy:copy.retry});
  for(const n of online)rows.set(`online:${n.id}`,{...n,original:n,id:`online:${n.id}`,online:true});
  const items=[...rows.values()].sort((a,b)=>a.id.localeCompare(b.id));
  return items.length?{id:"system-warnings",revision:warningRevision(items.map(({id,revision,title,body,path,remedy})=>({id,revision,title,body,path,remedy}))),title:`${copy.title} (${items.length})`,body:copy.summary(items.length),icon:"warning",rank:2,action:"warnings",persistent:true,items}:null;
}
