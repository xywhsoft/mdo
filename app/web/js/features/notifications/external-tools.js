import { api } from "../../api/client.js";
import { currentLocale } from "../../i18n.js";
import { targetState } from "../../api/target.js";

const labels = {
  "zh-CN": ["本机工具","工具商店","添加自定义工具","搜索","刷新","下载任务","启用","停用","编辑","卸载","分享到平台","下载安装","更新","取消","重试","清除已完成任务","暂无工具","可执行文件已不存在，请修改路径或重新安装。","名称","可执行文件绝对路径","注入提示词","版本（如 1.0.0）","许可","相关链接（每行一个）","分享目录（可选，包含程序和依赖；留空只上传程序文件）","保存","关闭","查看我的投稿","撤回","安装完整版","Android 外部程序随完整版 APK 安装和更新。","这将上传选定的程序文件及明确填写的分享目录，审核通过后供其他用户下载。请确认拥有分发许可。","卸载此工具？自行添加的原始文件会保留。","可安装常用命令行工具，帮助 Agent 完成搜索、下载、远程连接和文件处理。","安装","不再提示","管理下载","查看下一页","文件存在","未找到文件","安装程序位置与使用说明会提供给 Agent；不执行工具探测。","查看详情","仅适用于其他平台","需要登录后分享","存储不可写或记录损坏，原始记录已保留。","投稿已进入下载任务区，可在那里查看结果。"],
  "en-US": ["Local tools","Tool store","Add custom tool","Search","Refresh","Downloads","Enable","Disable","Edit","Uninstall","Share to store","Install","Update","Cancel","Retry","Clear finished tasks","No tools","Executable is missing. Edit its path or reinstall.","Name","Absolute executable path","Prompt for the Agent","Version (e.g. 1.0.0)","License","Related links (one per line)","Share directory (optional, includes dependencies; empty uploads only the executable)","Save","Close","My submissions","Withdraw","Install full edition","Android programs are installed and updated with the full APK.","Upload the selected executable and explicitly selected directory for review and distribution? Confirm you have redistribution rights.","Uninstall this tool? Original custom files are preserved.","Install common command-line tools to help the Agent search, download, connect remotely and process files.","Install","Don't show again","Manage downloads","Next page","File exists","File missing","Listed paths and usage notes are provided to the Agent. No probes are executed.","Details","Different platform","Sign in to share","Storage is unavailable or records are corrupt; original records retained.","Submission queued. Check its result in Downloads."],
  "ru-RU": ["На устройстве","Магазин","Добавить инструмент","Поиск","Обновить","Загрузки","Включить","Отключить","Изменить","Удалить","Поделиться","Установить","Обновить","Отмена","Повторить","Очистить завершённые","Нет инструментов","Файл не найден. Измените путь или переустановите.","Название","Полный путь к программе","Инструкция для Agent","Версия (1.0.0)","Лицензия","Ссылки (по одной на строку)","Каталог публикации (необязательно, с зависимостями)","Сохранить","Закрыть","Мои публикации","Отозвать","Полная версия","В Android программы обновляются вместе с полным APK.","Загрузить выбранные файлы для проверки и распространения? Подтвердите право распространения.","Удалить инструмент? Исходные файлы сохранятся.","Установите программы для поиска, загрузки, удалённого доступа и обработки файлов.","Установить","Не показывать","Управление загрузками","Далее","Файл существует","Файл не найден","Пути и инструкции передаются Agent. Программы не запускаются для проверки.","Подробнее","Другая платформа","Войдите для публикации","Хранилище недоступно; исходные записи сохранены.","Публикация в очереди. Результат появится в загрузках."]
};
labels["zh-CN"].push("命令名（留空使用程序名）","命令别名（每行一个）","复制到 mdo 工具目录","复制选定的程序及已填写的分享目录到 mdo，原文件保留。复制后可以通过统一命令目录调用，并随 mdo 一起迁移。","命令入口","外部位置；可复制到 mdo 以获得可迁移的命令入口。","mdo 子进程的命令目录（不修改系统 PATH）");
labels["en-US"].push("Command name (defaults to executable name)","Command aliases (one per line)","Copy into mdo tools","Copy the executable and explicitly selected share directory into mdo? Originals are preserved. The copy can use the portable command directory and move with mdo.","Commands","External location; copy into mdo to create portable command entries.","Command directory for mdo child processes (system PATH is unchanged)");
labels["ru-RU"].push("Имя команды (по умолчанию имя программы)","Псевдонимы (по одному на строку)","Копировать в mdo","Копировать программу и выбранный каталог в mdo? Исходные файлы сохранятся. Копия переносится вместе с mdo.","Команды","Внешний путь; скопируйте в mdo для переносимых команд.","Каталог команд дочерних процессов mdo (системный PATH не меняется)");
const n = (tag, text, cls) => { const e=document.createElement(tag); if(text!=null)e.textContent=text; if(cls)e.className=cls; return e; };
const active = t => ["queued","running","installing","uploading"].includes(t.state);
export function externalToolAction(row, installed) {
  const local=installed.find(t=>t.id===row.tool_id);
  return !local||!local.present?"install":local.version!==row.version?"update":null;
}
export function createExternalToolsPanel({ refresh, openFull, openTools, signedIn, isMain=()=>true }) {
  let data={}, tab="local", query="", error="", context="", catalogRequested=false, editor=null, editing=null, mine=false;
  const signatures=new WeakMap(), hosts=new Set();
  const c=i=>(labels[currentLocale()]??labels["en-US"])[i];
  const writable=()=>{const t=targetState();return data.writable!==false&&(!t.selected||(t.connected&&!t.runtimeChanged&&t.selected.mode!=="view"));};
  const button=(text,fn,disabled=false)=>{const b=n("button",text,"secondary-button");b.type="button";b.disabled=disabled;b.onclick=fn;return b;};
  const guide=n("aside",null,"cli-first-use"), global=n("button",null,"secondary-button cli-download-entry");
  guide.hidden=true;guide.setAttribute("aria-live","polite");global.type="button";global.hidden=true;global.onclick=()=>openTools();
  document.body.append(guide,global);
  async function command(request) {
    if(!writable())return false;const own=context;
    try { const r=await api.post("/cli-tools",request);if(own!==(targetState().selected?.id??"local"))return false;data=r.data;error="";renderAll();void refresh();return true; }
    catch(e){if(own===(targetState().selected?.id??"local")){error=e.message;renderAll();}return false;}
  }
  async function openBundle(){try{await openFull();}catch(e){error=e.message;renderAll();}}
  const fetchCatalog=(before=0)=>command({action:"catalog",query,before});
  function renderAll(){for(const h of hosts){signatures.delete(h);render(h,data,h.dataset.cliUpdates==="true");}}
  function closeEditor(){editor?.remove();editor=null;editing=null;}
  function edit(tool={}) {
    closeEditor();editing=tool.id??null;const own=context,revision=data.revision??0;
    editor=n("dialog",null,"cli-editor");const form=n("form"), message=n("p",null,"cli-error"), fields={};
    form.append(n("h3",c(tool.id?8:2)));
    for(const [key,label,type] of [["name",18],["path",19],["command",46],["aliases",47,"textarea"],["prompt",20,"textarea"],["version",21],["license",22],["links",23,"textarea"],["package_root",24]]){
      const wrap=n("label",null,"cli-field"),input=n(type??"input");input.name=key;
      input.value=key==="links"||key==="aliases"?(Array.isArray(tool[key])?tool[key]:[]).join("\n"):tool[key]??(key==="version"?"1.0.0":key==="license"?"Unknown":"");
      input.required=["name","path","prompt","version","license"].includes(key);input.maxLength=key==="prompt"?16384:key==="path"||key==="package_root"?4096:key==="name"?120:key==="links"?16384:512;
      if(key==="path"&&(data.platform?.startsWith("android")||tool.managed_root))input.readOnly=true;
      if(key==="command"&&tool.commands?.length)input.readOnly=true;
      wrap.append(n("span",c(label)),input);fields[key]=input;form.append(wrap);
    }
    const save=button(c(25),()=>form.requestSubmit()),close=button(c(26),closeEditor);form.append(message,save,close);editor.append(form);document.body.append(editor);
    form.onsubmit=async e=>{e.preventDefault();if(own!==context)return closeEditor();save.disabled=true;const t={id:tool.id??"custom."+crypto.randomUUID().replaceAll("-",""),platform:data.platform,enabled:tool.enabled??true};
      if(tool.commands)t.commands=tool.commands;
      for(const k of Object.keys(fields))t[k]=k==="links"||k==="aliases"?fields[k].value.split(/\r?\n/).map(v=>v.trim()).filter(Boolean):fields[k].value.trim();
      if(await command({action:"save",tool:t,revision}))closeEditor();else{message.textContent=error;save.disabled=false;}
    };editor.addEventListener("cancel",()=>closeEditor());editor.showModal();
  }
  function share(tool) {
    closeEditor();const own=context;editor=n("dialog",null,"cli-editor");const form=n("form"),fields={},message=n("p",null,"cli-error");form.append(n("h3",currentLocale()==="zh-CN"?"提交到创作中心":"Submit to creator center"),n("p",c(31)));
    const visibility=n("label",null,"cli-field"),publish=n("input");publish.type="checkbox";visibility.append(publish,n("span",currentLocale()==="zh-CN"?"申请公布（需审核）；不勾选则私有保存":"Request publication (review required); otherwise save privately"));form.append(visibility);
    for(const [key,zh,en,multi] of [["source_url","固定版本源码 / 可核验来源（HTTPS）","Versioned source / provenance URL (HTTPS)"],["review_file","审核附件的本机绝对路径（源码归档等，可选）","Absolute local path to review file (optional)"],["review_notes","构建方法、安全说明、权限及网络行为","Build instructions, permissions and network behavior",true]]){const label=n("label",null,"cli-field"),input=n(multi?"textarea":"input");input.maxLength=key==="review_notes"?8000:4096;label.append(n("span",currentLocale()==="zh-CN"?zh:en),input);fields[key]=input;form.append(label);}
    const save=button(currentLocale()==="zh-CN"?"提交作品":"Submit",()=>form.requestSubmit()),close=button(c(26),closeEditor);form.append(message,save,close);editor.append(form);document.body.append(editor);editor.showModal();
    form.onsubmit=async e=>{e.preventDefault();if(own!==context)return closeEditor();const request={action:"publish",id:tool.id,name:tool.name,publish:publish.checked,...Object.fromEntries(Object.entries(fields).map(([k,v])=>[k,v.value.trim()]))};if(request.publish&&(request.review_notes.length<10||!request.source_url&&!request.review_file)){message.textContent=currentLocale()==="zh-CN"?"公布时请提供源码或可核验来源，并填写至少 10 个字符的构建及安全说明。":"Provide source/provenance and at least 10 characters of build/security notes.";return;}save.disabled=true;if(await command(request))closeEditor();else{message.textContent=error;save.disabled=false;}};
  }
  function details(tool) {
    closeEditor();editor=n("dialog",null,"cli-editor");editor.append(n("h3",tool.name),n("p",tool.prompt),n("p",`${tool.version} · ${tool.platform} · ${tool.license}`));
    let links=tool.links;try{if(typeof links==="string")links=JSON.parse(links);}catch{links=[];}
    for(const url of Array.isArray(links)?links:[])if(/^https?:\/\//i.test(url)){const a=n("a",url);a.href=url;a.target="_blank";a.rel="noopener noreferrer";editor.append(a,n("br"));}
    editor.append(button(c(26),closeEditor));document.body.append(editor);editor.showModal();
  }
  function tasks(body){
    const list=data.tasks??[];const box=n("details",null,"cli-tasks");box.open=list.some(active);box.append(n("summary",`${c(5)} (${list.filter(active).length}/${list.length})`));
    for(const t of list){const row=n("div",null,"cli-task"),title=t.name||t.action;row.append(n("strong",title),n("span",t.state));
      if(t.total){const progress=n("progress");progress.max=t.total;progress.value=t.done??0;progress.setAttribute("aria-label",title);row.append(progress,n("small",`${Math.floor(100*(t.done??0)/t.total)}%`));}
      if(t.error)row.append(n("p",t.error,"cli-error"));if(t.submission)row.append(n("p",`#${t.submission.id} · ${t.submission.state}`));
      if(active(t))row.append(button(c(13),()=>command({action:"cancel",id:t.id}),!writable()));else if(t.state!=="complete")row.append(button(c(14),()=>command({action:"retry",id:t.id}),!writable()));box.append(row);
    }if(list.length)box.append(button(c(15),()=>command({action:"clear"}),!writable()));body.append(box);
  }
  function render(body,snapshot={},updatesOnly=false){
    if(!body)return;hosts.add(body);body.dataset.cliUpdates=String(updatesOnly);data=snapshot;
    const next=targetState().selected?.id??"local";if(context!==next){context=next;catalogRequested=false;tab="local";query=error="";mine=false;closeEditor();}
    const sig=JSON.stringify([data,currentLocale(),tab,query,error,mine,next,writable(),signedIn(),updatesOnly,isMain()]);if(signatures.get(body)===sig)return;signatures.set(body,sig);body.replaceChildren();
    if(!data.platform){body.append(n("p","…"));return;}
    const android=data.platform.startsWith("android"), disabled=!writable();
    guide.replaceChildren(n("p",c(33)),button(c(34),async()=>{if(android){if(await command({action:"accept"}))await openBundle();}else if(await command({action:"recommend"}))openTools();},disabled),button(c(35),()=>command({action:"dismiss"}),disabled));
    guide.hidden=data.guide!=="new"||disabled||!!targetState().selected||!isMain();
    global.textContent=`↓ ${c(5)}${(data.tasks??[]).some(active)?" · "+(data.tasks??[]).filter(active).length:""}`;global.hidden=!(data.tasks??[]).length;
    if(updatesOnly){body.append(button(c(36),openTools));tasks(body);return;}
    const nav=n("div",null,"cli-tabs");nav.setAttribute("role","tablist");
    for(const [id,text] of [["local",c(0)],["store",c(1)]]){const b=button(text,()=>{tab=id;mine=false;renderAll();if(id==="store"&&!catalogRequested){catalogRequested=true;void fetchCatalog();}});b.setAttribute("role","tab");b.setAttribute("aria-selected",String(tab===id));nav.append(b);}body.append(nav);const creator=n("a",currentLocale()==="zh-CN"?"创作中心：管理作品、审核资料与配额":"Creator center: works, reviews and capacity");creator.href="https://ai.xywhsoft.com/mdo/creator";creator.target="_blank";creator.rel="noopener noreferrer";body.append(creator);
    if(error)body.append(n("p",error,"cli-error"));if(!data.writable)body.append(n("p",c(44),"cli-error"));
    if(android)body.append(n("p",c(30)),button(c(29),openBundle,disabled));
    const toolbar=n("div",null,"cli-toolbar");
    if(tab==="local"){
      toolbar.append(button(c(2),()=>edit(),disabled||android),button(c(27),()=>{mine=!mine;if(mine)void command({action:"mine"});renderAll();},disabled||!signedIn()));body.append(toolbar,n("p",c(40),"notice-description"));
      if(!android&&data.command_directory)body.append(n("small",`${c(52)}: ${data.command_directory}`));
      if(data.binding_error)body.append(n("p",data.binding_error,"cli-error"));
      for(const t of data.items??[]){const row=n("article",null,"cli-row"),main=n("div",null,"cli-info"),actions=n("div",null,"cli-actions");
        main.append(n("strong",t.name),n("small",`${t.version} · ${t.platform} · ${t.license}`),n("code",t.path),n("small",c(t.present?38:39),t.present?null:"cli-error"));
        if(t.exported_commands?.length)main.append(n("small",`${c(50)}: ${t.exported_commands.join(" · ")}`));
        if(!android&&!t.portable)main.append(n("small",c(51)));
        const prompt=n("details");prompt.append(n("summary",c(20)),n("p",t.prompt));main.append(prompt);
        actions.append(button(c(t.enabled?7:6),()=>command({action:"enable",id:t.id,enabled:!t.enabled}),disabled||(!t.enabled&&!t.present)),button(c(8),()=>edit(t),disabled),button(c(41),()=>details(t)));
        if(t.source!=="bundled")actions.append(button(c(9),()=>{if(window.confirm(c(32)))void command({action:"remove",id:t.id});},disabled));
        if(!android)actions.append(button(c(10),()=>share(t),disabled||!signedIn()||!t.present));
        if(!android&&!t.managed_root)actions.append(button(c(48),()=>{if(window.confirm(c(49)))void command({action:"import",id:t.id,name:t.name});},disabled||!t.present));
        row.append(main,actions);body.append(row);
      }
      if(!(data.items??[]).length)body.append(n("p",c(16)));
      if(mine){const box=n("section");box.append(n("h3",c(27)));for(const t of data.store?.mine??[]){const row=n("div",null,"cli-row");row.append(n("span",`${t.name} ${t.version} · ${t.state}${t.reason?" · "+t.reason:""}`),button(c(28),async()=>{if(await command({action:"withdraw",store_id:t.id,revision:t.revision,name:t.name}))void command({action:"mine"});},disabled||!!t.withdrawn));box.append(row);}body.append(box);}
    }else{
      const search=n("input");search.type="search";search.value=query;search.placeholder=c(3);search.setAttribute("aria-label",c(3));search.oninput=()=>{query=search.value;};search.onkeydown=e=>{if(e.key==="Enter"){e.preventDefault();void fetchCatalog();}};
      toolbar.append(search,button(c(3),()=>fetchCatalog(),disabled),button(c(4),()=>fetchCatalog(),disabled));body.append(toolbar);
      for(const t of data.store?.items??[]){const row=n("article",null,"cli-row"),info=n("div",null,"cli-info"),action=externalToolAction(t,data.items??[]);info.append(n("strong",t.name),n("small",`${t.version} · ${t.platform} · ${t.author} · ${t.license}`),n("p",t.prompt));row.append(info,button(c(41),()=>details(t)));
        if(action)row.append(button(c(android?29:action==="update"?12:11),()=>android?openBundle():command({action:"install",store_id:t.id,name:t.name}),disabled||t.platform!==data.platform));body.append(row);
      }if(!(data.store?.items??[]).length)body.append(n("p",c(16)));
      if(data.store?.next_before)body.append(button(c(37),()=>fetchCatalog(data.store.next_before),disabled));
    }tasks(body);
  }
  return {render,command,reset(){data={};context="";closeEditor();guide.hidden=true;global.hidden=true;},destroy(){closeEditor();guide.remove();global.remove();hosts.clear();}};
}
