import { createExtensionBrowser, actionMenu, extensionToggle, detailTabs, detailFacts, browserText } from "../../components/extension-browser.js";
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
labels["zh-CN"].push("描述","面向用户，说明这个工具能完成什么功能。列表将显示这段描述。","面向模型，说明何时使用工具、如何调用及注意事项；不会作为列表描述展示。","暂无描述");
labels["en-US"].push("Description","For users: explain what this tool can do. This text appears in the list.","For the model: explain when and how to use this tool, including caveats. This is not the list description.","No description");
labels["ru-RU"].push("Описание","Для пользователя: какие задачи решает инструмент. Этот текст отображается в списке.","Для модели: когда и как применять инструмент и какие ограничения учитывать. Не используется как описание списка.","Нет описания");
labels["zh-CN"].push("提交者","版权声明","未提供版权声明，请参阅许可文件及上游项目链接。","本机添加","提交者未提供");
labels["en-US"].push("Submitted by","Copyright notice","No copyright notice supplied. See the license files and upstream project links.","Added locally","Submitter not provided");
labels["ru-RU"].push("Опубликовал","Авторские права","Сведения не предоставлены. См. файлы лицензий и ссылки на исходный проект.","Добавлено локально","Издатель не указан");
const n = (tag, text, cls) => { const e=document.createElement(tag); if(text!=null)e.textContent=text; if(cls)e.className=cls; return e; };
const active = t => ["queued","running","installing","uploading"].includes(t.state);
export function externalToolAction(row, installed) {
  const local=installed.find(t=>t.id===row.tool_id);
  return !local||!local.present?"install":local.version!==row.version?"update":null;
}
export function createExternalToolsPanel({ refresh, openFull, signedIn, openDownloads=()=>{}, isMain=()=>true }) {
  let data={}, tab="local", query="", error="", context="", catalogRequested=false, editor=null, editing=null, mine=false, localQuery="", stateFilter="all";
  const browsers = new Map(), pending = new Set();
  let catalogRequest=null, catalogHistory=[], catalogBefore=0, catalogOffset=0, catalogEpoch=0, catalogInitial=0;
  const signatures=new WeakMap(), hosts=new Set();
  const c=i=>(labels[currentLocale()]??labels["en-US"])[i];
  const userDefined=tool=>["manual","local"].includes(tool?.source);
  const submitter=tool=>tool.submitter||tool.author||(userDefined(tool)?c(60):c(61));
  const writable=()=>{const t=targetState();return data.writable!==false&&(!t.selected||(t.connected&&!t.runtimeChanged&&t.selected.mode!=="view"));};
  const button=(text,fn,disabled=false)=>{const b=n("button",text,"secondary-button");b.type="button";b.disabled=disabled;b.onclick=fn;return b;};
  const guide=n("aside",null,"cli-first-use");
  guide.hidden=true;guide.setAttribute("aria-live","polite");document.body.append(guide);
  async function command(request) {
    if(!writable())return false;const own=context,key=JSON.stringify(request);if(pending.has(key))return false;pending.add(key);
    try { const r=await api.post("/cli-tools",request);if(own!==(targetState().selected?.id??"local"))return false;data=r.data;error="";renderAll();void refresh();return true; }
    catch(e){if(own===(targetState().selected?.id??"local")){error=e.message;renderAll();}return false;}finally{pending.delete(key);if(own===context)renderAll();}
  }
  async function openBundle(){try{await openFull();}catch(e){error=e.message;renderAll();}}
  async function fetchCatalog(before=0, direction="reset", pages=0) {
    if(catalogRequest)return false;
    catalogRequest={before,query,direction,pages,previousId:data.store?.request_id,previousTasks:new Set((data.tasks||[]).map(t=>t.id))};renderAll();
    if(!await command({action:"catalog",query,before})){catalogRequest=null;renderAll();return false;}
    return true;
  }
  function settleCatalog() {
    if(!catalogRequest)return;
    const r=catalogRequest, result=data.store||{};
    if(result.request_id&&result.request_id!==r.previousId&&result.query===r.query&&result.before===r.before){
      if(r.direction==="next"){catalogHistory.push({before:catalogBefore,offset:catalogOffset});catalogOffset+=r.pages;}
      else if(r.direction==="previous"){const previous=catalogHistory.pop();catalogOffset=previous?.offset||0;}
      else {catalogHistory=[];catalogOffset=0;}
      catalogBefore=r.before;catalogInitial=r.direction==="previous"?"last":0;catalogEpoch++;catalogRequest=null;
    }else{
      const failed=[...(data.tasks||[])].reverse().find(t=>!r.previousTasks.has(t.id)&&t.action==="catalog"&&t.request?.before===r.before&&t.request?.query===r.query&&["failed","cancelled","interrupted"].includes(t.state));
      if(failed){error=failed.error||failed.state;catalogRequest=null;}
    }
  }
  function renderAll(){for(const h of hosts){signatures.delete(h);render(h,data,h.dataset.cliUpdates==="true");}}
  function closeEditor(){editor?.remove();editor=null;editing=null;}
  function edit(tool={}) {
    if(tool.id&&!userDefined(tool))return;
    closeEditor();editing=tool.id??null;const own=context,revision=data.revision??0;
    editor=n("dialog",null,"cli-editor");const form=n("form"), message=n("p",null,"cli-error"), fields={};
    form.append(n("h3",c(tool.id?8:2)));
    for(const [key,label,type] of [["name",18],["description",53,"textarea"],["path",19],["command",46],["aliases",47,"textarea"],["prompt",20,"textarea"],["version",21],["copyright",58,"textarea"],["license",22],["links",23,"textarea"],["package_root",24]]){
      const wrap=n("label",null,"cli-field"),input=n(type??"input");input.name=key;
      input.value=key==="links"||key==="aliases"?(Array.isArray(tool[key])?tool[key]:[]).join("\n"):tool[key]??(key==="version"?"1.0.0":key==="license"?"Unknown":"");
      input.required=["name","description","path","prompt","version","license"].includes(key);input.maxLength=key==="copyright"?1024:key==="description"?512:key==="prompt"?16384:key==="path"||key==="package_root"?4096:key==="name"?120:key==="links"?16384:512;
      if(key==="path"&&(data.platform?.startsWith("android")||tool.managed_root))input.readOnly=true;
      if(key==="command"&&tool.commands?.length)input.readOnly=true;
      wrap.append(n("span",c(label)),input);if(key==="description"||key==="prompt")wrap.append(n("small",c(key==="description"?54:55)));fields[key]=input;form.append(wrap);
    }
    const save=button(c(25),()=>form.requestSubmit()),close=button(c(26),closeEditor);form.append(message,save,close);editor.append(form);document.body.append(editor);
    form.onsubmit=async e=>{e.preventDefault();if(own!==context)return closeEditor();save.disabled=true;const t={id:tool.id??"custom."+crypto.randomUUID().replaceAll("-",""),platform:data.platform,enabled:tool.enabled??true};
      if(tool.commands)t.commands=tool.commands;
      for(const k of Object.keys(fields))t[k]=k==="links"||k==="aliases"?fields[k].value.split(/\r?\n/).map(v=>v.trim()).filter(Boolean):fields[k].value.trim();
      if(await command({action:"save",tool:t,revision}))closeEditor();else{message.textContent=error;save.disabled=false;}
    };editor.addEventListener("cancel",()=>closeEditor());editor.showModal();
  }
  function share(tool) {
    if(!userDefined(tool))return;
    closeEditor();const own=context;editor=n("dialog",null,"cli-editor");const form=n("form"),fields={},message=n("p",null,"cli-error");form.append(n("h3",currentLocale()==="zh-CN"?"提交到创作中心":"Submit to creator center"),n("p",c(31)));
    const visibility=n("label",null,"cli-field"),publish=n("input");publish.type="checkbox";visibility.append(publish,n("span",currentLocale()==="zh-CN"?"申请公布（需审核）；不勾选则私有保存":"Request publication (review required); otherwise save privately"));form.append(visibility);
    for(const [key,zh,en,multi] of [["source_url","固定版本源码 / 可核验来源（HTTPS）","Versioned source / provenance URL (HTTPS)"],["review_file","审核附件的本机绝对路径（源码归档等，可选）","Absolute local path to review file (optional)"],["review_notes","构建方法、安全说明、权限及网络行为","Build instructions, permissions and network behavior",true]]){const label=n("label",null,"cli-field"),input=n(multi?"textarea":"input");input.maxLength=key==="review_notes"?8000:4096;label.append(n("span",currentLocale()==="zh-CN"?zh:en),input);fields[key]=input;form.append(label);}
    const save=button(currentLocale()==="zh-CN"?"提交作品":"Submit",()=>form.requestSubmit()),close=button(c(26),closeEditor);form.append(message,save,close);editor.append(form);document.body.append(editor);editor.showModal();
    form.onsubmit=async e=>{e.preventDefault();if(own!==context)return closeEditor();const request={action:"publish",id:tool.id,name:tool.name,publish:publish.checked,...Object.fromEntries(Object.entries(fields).map(([k,v])=>[k,v.value.trim()]))};if(request.publish&&(request.review_notes.length<10||!request.source_url&&!request.review_file)){message.textContent=currentLocale()==="zh-CN"?"公布时请提供源码或可核验来源，并填写至少 10 个字符的构建及安全说明。":"Provide source/provenance and at least 10 characters of build/security notes.";return;}save.disabled=true;if(await command(request))closeEditor();else{message.textContent=error;save.disabled=false;}};
  }
  function toolDetail(tool, install) {
    const description=n("div");description.append(n("p",tool.description||c(56)),detailFacts([[c(57),submitter(tool)],[c(58),tool.copyright||c(59)],[c(22),tool.license]]));
    const usage=n("div");usage.append(detailFacts([[c(19),tool.path],[c(50),(tool.exported_commands||[]).join(" · ")],[c(52),data.command_directory]]),n("h4",c(20)),n("p",tool.prompt));
    const source=n("div");source.append(detailFacts([[c(21),tool.version],[browserText("platform","支持平台"),tool.platform],[c(22),tool.license]]));
    let links=tool.links;try{if(typeof links==="string")links=JSON.parse(links);}catch{links=[];}
    for(const url of Array.isArray(links)?links:[])if(/^https?:\/\//i.test(url)){const a=n("a",url);a.href=url;a.target="_blank";a.rel="noopener noreferrer";source.append(a,n("br"));}
    const root=n("div");root.append(detailTabs([{id:"description",label:browserText("description","说明"),content:description},{id:"usage",label:browserText("usage","使用方式"),content:usage},{id:"source",label:browserText("sourceFiles","来源与文件"),content:source}]));
    if(install)root.append(install());return root;
  }
  function render(body,snapshot={},updatesOnly=false){
    if(!body)return;hosts.add(body);body.dataset.cliUpdates=String(updatesOnly);data=snapshot;
    const next=targetState().selected?.id??"local";if(context!==next){context=next;catalogRequested=false;catalogRequest=null;catalogHistory=[];catalogBefore=catalogOffset=catalogEpoch=0;tab="local";query=error="";mine=false;localQuery="";stateFilter="all";for(const browser of browsers.values())browser.reset();closeEditor();}
    settleCatalog();
    const sig=JSON.stringify([!!catalogRequest,catalogEpoch,data,currentLocale(),tab,query,localQuery,stateFilter,error,mine,next,writable(),signedIn(),updatesOnly,isMain()]);if(signatures.get(body)===sig)return;signatures.set(body,sig);const focusedSearch=body.contains(document.activeElement)&&document.activeElement?.type==="search",selection=focusedSearch?[document.activeElement.selectionStart,document.activeElement.selectionEnd]:null;browsers.get(body)?.rememberFocus();body.replaceChildren();
    if(!data.platform){body.append(n("p","…"));return;}
    const android=data.platform.startsWith("android"), disabled=!writable();
    guide.replaceChildren(n("p",c(33)),button(c(34),async()=>{if(android){if(await command({action:"accept"}))await openBundle();}else if(await command({action:"recommend"}))openDownloads();},disabled),button(c(35),()=>command({action:"dismiss"}),disabled));
    guide.hidden=data.guide!=="new"||disabled||!!targetState().selected||!isMain();
    if(updatesOnly){body.append(button(c(36),openDownloads));return;}
    const nav=n("div",null,"store-tabs");nav.setAttribute("role","tablist");
    for(const [id,text] of [["local",c(0)],["store",c(1)]]){const b=button(text,()=>{tab=id;mine=false;stateFilter="all";for(const browser of browsers.values())browser.reset();renderAll();if(id==="store"&&!catalogRequested){catalogRequested=true;void fetchCatalog();}});b.setAttribute("role","tab");b.setAttribute("aria-selected",String(tab===id));nav.append(b);}body.append(nav);const creator=n("a",browserText("creator","创作中心"),"secondary-button");creator.href="https://ai.xywhsoft.com/mdo/creator";creator.target="_blank";creator.rel="noopener noreferrer";
    if(error)body.append(n("p",error,"cli-error"));if(!data.writable)body.append(n("p",c(44),"cli-error"));
    if(android)body.append(n("p",c(30)),button(c(29),openBundle,disabled));
    const toolbar=n("div",null,"cli-toolbar eb-toolbar");
    let browser=browsers.get(body);if(!browser){browser=createExtensionBrowser({kind:"toolpacks"});browsers.set(body,browser);}
    const filter=n("select");filter.setAttribute("aria-label",browserText("status","状态"));
    for(const [id,text] of (tab==="local"?[["all",browserText("all","全部状态")],["enabled",c(6)],["disabled",c(7)],["error",browserText("needsAttention","需要处理")]]:[["all",browserText("all","全部状态")],["installed",browserText("installed","已安装")],["updates",browserText("updates","可更新")],["new",browserText("notInstalled","未安装")]])){const o=n("option",text);o.value=id;filter.append(o);}filter.value=stateFilter;
    const search=n("input");search.type="search";search.value=tab==="local"?localQuery:query;search.placeholder=c(3);search.setAttribute("aria-label",c(3));
    const taskLabel=job=>`${c(5)} · ${browserText(`task.${job.state}`,job.state)}${job.total?` · ${Math.min(100,Math.floor(100*(job.done||0)/job.total))}%`:""}`;
    const taskFor=t=>(data.tasks||[]).find(job=>active(job)&&(job.request?.id&&job.request.id===t.id||job.request?.store_id&&(job.request.store_id===t.store_id||job.request.store_id===t.id)));
    const renderList=()=>{
      const needle=localQuery.toLocaleLowerCase();
      const rows=tab==="local"?(data.items||[]).filter(t=>`${t.name} ${t.description||""} ${t.command||""}`.toLocaleLowerCase().includes(needle)).filter(t=>stateFilter==="all"||(stateFilter==="error"?!t.present:stateFilter==="enabled"?t.enabled:!t.enabled)).map(t=>{
        const job=taskFor(t), custom=!android&&userDefined(t), actions=[];
        if(custom)actions.push(button(c(8),()=>edit(t),disabled));
        if(t.source!=="bundled"&&!android)actions.push(button(c(9),()=>{if(window.confirm(c(32)))void command({action:"remove",id:t.id});},disabled));
        if(custom)actions.push(button(c(10),()=>share(t),disabled||!signedIn()||!t.present));
        if(custom&&!t.managed_root)actions.push(button(c(48),()=>{if(window.confirm(c(49)))void command({action:"import",id:t.id,name:t.name});},disabled||!t.present));
        const toggle=extensionToggle(button(c(t.enabled?7:6),()=>command({action:"enable",id:t.id,enabled:!t.enabled}),disabled||(!t.enabled&&!t.present)),t.enabled,t.name);
        return {key:`local:${t.id}`,title:t.name,byline:submitter(t),description:t.description||c(56),meta:[t.version,t.license],toggle,actions,badge:{text:job?taskLabel(job):!t.present?c(39):"",tone:!t.present?"error":""},detail:()=>toolDetail(t)};
      }):(data.store?.items||[]).filter(t=>stateFilter==="all"||(stateFilter==="installed"?!externalToolAction(t,data.items||[]):stateFilter==="updates"?externalToolAction(t,data.items||[])==="update":externalToolAction(t,data.items||[])==="install")).map(t=>{
        const action=externalToolAction(t,data.items||[]),job=taskFor(t);
        const install=()=>button(c(android?29:action==="update"?12:11),()=>android?openBundle():command({action:"install",store_id:t.id,name:t.name}),disabled||t.platform!==data.platform||!!job);
        const primary=action?install():null;
        return {key:`store:${t.id}`,title:t.name,byline:submitter(t),description:t.description||c(56),meta:[t.version,t.platform],badge:{text:job?taskLabel(job):t.platform!==data.platform?c(42):!action?browserText("installed","已安装"):""},primary,detail:()=>toolDetail(t,action?install:null)};
      });
      browser.setItems(rows,{empty:c(16),busy:!!catalogRequest,
        pageKey:JSON.stringify([tab,localQuery,stateFilter,tab==="store"?catalogEpoch:0]),initialPage:catalogInitial,pageOffset:tab==="store"?catalogOffset:0,
        onNext:tab==="store"&&data.store?.next_before?pages=>{void fetchCatalog(data.store.next_before,"next",pages);}:null,
        onPrevious:tab==="store"&&catalogHistory.length?()=>{void fetchCatalog(catalogHistory.at(-1).before,"previous");}:null});
    };
    search.oninput=()=>{if(tab==="local"){localQuery=search.value;renderList();}else query=search.value;};
    search.onkeydown=e=>{if(tab==="store"&&e.key==="Enter"){e.preventDefault();void fetchCatalog();}};
    filter.onchange=()=>{stateFilter=filter.value;if(tab==="store"&&catalogHistory.length)void fetchCatalog();else renderList();};
    toolbar.append(search,filter);if(tab==="store")toolbar.append(button(c(3),()=>fetchCatalog(),disabled));
    toolbar.append(button(c(4),()=>tab==="store"?fetchCatalog():refresh(),disabled),creator);
    body.append(toolbar);
    if(tab==="local"){
      toolbar.append(actionMenu([...(android?[]:[button(c(2),()=>edit(),disabled)]),button(c(27),()=>{mine=!mine;if(mine)void command({action:"mine"});renderAll();},disabled||!signedIn())],browserText("createImport","创建 / 导入")));
      if(data.binding_error)body.append(n("p",data.binding_error,"cli-error"));
      renderList();body.append(browser.root);
      if(mine){const box=n("section");box.append(n("h3",c(27)));for(const t of data.store?.mine??[]){const row=n("div",null,"cli-row");row.append(n("span",`${t.name} ${t.version} · ${t.state}${t.reason?" · "+t.reason:""}`),button(c(28),async()=>{if(await command({action:"withdraw",store_id:t.id,revision:t.revision,name:t.name}))void command({action:"mine"});},disabled||!!t.withdrawn));box.append(row);}body.append(box);}
    }else{
      renderList();body.append(browser.root);

    }if(focusedSearch){search.focus({preventScroll:true});if(selection&&search.setSelectionRange)search.setSelectionRange(...selection);}
  }
  return {render,command,reset(){data={};context="";for(const browser of browsers.values())browser.reset();closeEditor();guide.hidden=true;},destroy(){for(const browser of browsers.values())browser.destroy();browsers.clear();closeEditor();guide.remove();hosts.clear();}};
}
