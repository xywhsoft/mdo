const {chromium}=require(process.env.MDO_PLAYWRIGHT_PATH || 'playwright');
const assert=require('node:assert/strict'),fs=require('node:fs');
(async()=>{
 const origin=process.argv[2],evidence=process.argv[3],browser=await chromium.launch({headless:true,channel:'msedge'});let page;const errors=[],answers=[];
 const until=async check=>{const deadline=Date.now()+20000;while(Date.now()<deadline){if(await check())return;await new Promise(resolve=>setTimeout(resolve,80));}throw Error('File operation condition timed out');};
 const api=async(method,params)=>{const bootstrap=await fetch(origin+'/api/v1/bootstrap');const token=bootstrap.headers.get('X-Mdo-Write-Token');const reply=await fetch(origin+'/api/v1/application/call',{method:'POST',headers:{'Content-Type':'application/json','X-Mdo-Write-Token':token},body:JSON.stringify({method,params,context:{project:'default'}})});const value=await reply.json();if(!reply.ok)throw Object.assign(Error(value.error.message),value.error,{status:reply.status});return value.data;};
 try{
  page=await browser.newPage({viewport:{width:1440,height:1000},locale:'zh-CN'});page.on('pageerror',error=>errors.push(String(error)));
  page.on('dialog',async dialog=>{try{if(dialog.type()==='prompt'){const answer=answers.shift();assert.notEqual(answer,undefined,'unexpected prompt');await dialog.accept(typeof answer==='function'?await answer():answer);}else await dialog.accept();}catch(error){errors.push(String(error));await dialog.dismiss().catch(()=>{});}});
  await page.route('**/*',route=>new URL(route.request().url()).hostname==='127.0.0.1'?route.continue():route.abort());
  await page.goto(origin+'/');await page.waitForFunction(()=>Boolean(window.mdo));await page.locator('#new-workbench-tab').click();
  await page.getByRole('heading',{name:'新标签页',exact:true}).waitFor();
  const fixture=await page.evaluate(async()=>{const root=(await mdo.files.resolve({path:'action-fixture'})).path;if(!root.includes('workbench-ui-'))throw Error('fixture escaped private Home');await mdo.files.mkdir({path:root});const panel=await mdo.panel.open({client:mdo.clientId,type:'mdo.files',key:'actions',title:'文件',params:{root},reveal:'activate'});return{root,id:panel.id};});
  const view=page.locator('.wb-files-view:not([hidden])');await view.locator('.wb-tree-body').waitFor();
  const mainMenu=async name=>{await view.getByRole('button',{name:'文件更多操作',exact:true}).click();await page.getByRole('menuitem',{name,exact:true}).click();};
  const rowMenu=async(label,name)=>{const line=view.locator('.wb-tree-line').filter({has:page.getByRole('button',{name:label,exact:true})});await line.hover();await line.getByRole('button',{name:'更多操作：'+label,exact:true}).click();await page.getByRole('menuitem',{name,exact:true}).click();};
  answers.push('子目录');await mainMenu('新建文件夹…');await view.getByRole('button',{name:'子目录',exact:true}).waitFor();
  answers.push('草稿.txt');await rowMenu('子目录','新建文件…');await view.getByRole('textbox',{name:'编辑 草稿.txt',exact:true}).fill('第一份草稿🌙\n');
  answers.push('重命名.txt');await mainMenu('重命名文件…');await view.getByRole('textbox',{name:'编辑 重命名.txt',exact:true}).waitFor();
  const original=fixture.root+'/子目录/草稿.txt',path=fixture.root+'/子目录/重命名.txt';
  assert.equal((await api('files.read',{path})).text,'');await assert.rejects(()=>api('files.stat',{path:original}));
  await until(async()=> (await api('panel.get',{id:fixture.id})).view_state?.files?.selected===path);
  await page.reload();await page.waitForFunction(()=>Boolean(window.mdo));await view.getByRole('textbox',{name:'编辑 重命名.txt',exact:true}).waitFor();assert.equal(await view.locator('textarea.wb-file-editor').inputValue(),'第一份草稿🌙\n');
  await view.getByRole('button',{name:'保存文件',exact:true}).click();await until(async()=>(await api('files.read',{path})).text==='第一份草稿🌙\n');
  answers.push('重命名.txt');await mainMenu('新建文件…');await page.getByText('同名条目已存在或正在使用，没有覆盖它的内容。',{exact:true}).waitFor();assert.equal((await api('files.read',{path})).text,'第一份草稿🌙\n');
  // Modify the disk while the native prompt freezes this page's JS. This makes
  // a genuinely stale metadata decision without racing the polling timer.
  answers.push(async()=>{await api('files.write',{path,text:'external version\n'});return 'must-not-rename.txt';});await mainMenu('重命名文件…');
  await page.getByText('条目已发生变化，已刷新列表；请重新确认后操作。原有编辑仍然保留。',{exact:true}).waitFor();assert.equal((await api('files.read',{path})).text,'external version\n');
  await assert.rejects(()=>api('files.stat',{path:fixture.root+'/子目录/must-not-rename.txt'}));
  await until(async()=>{const text=await view.locator('.wb-source-text').allTextContents();return text.join('').includes('external version');});
  await view.getByRole('button',{name:'编辑文件',exact:true}).click();await view.locator('.wb-file-editor').fill('pane one working\n');
  const second=await page.evaluate(path=>mdo.panel.open({client:mdo.clientId,type:'mdo.editor',key:'second-editor',params:{path,edit:true},reveal:'activate'}),path);
  await until(async()=>await page.locator(`.wb-tab[data-panel="${second.id}"] [role="tab"]`).getAttribute('aria-selected').catch(()=>null)==='true');
  await view.getByRole('textbox',{name:'编辑 重命名.txt',exact:true}).waitFor();await view.locator('.wb-file-editor').fill('pane two saved\n');await view.getByRole('button',{name:'保存文件',exact:true}).click();
  await until(async()=>(await api('files.read',{path})).text==='pane two saved\n');
  await page.getByRole('tab',{name:'文件',exact:true}).click();assert.equal(await view.locator('.wb-file-editor').inputValue(),'pane one working\n');
  const profile=await page.evaluate(()=>localStorage.getItem('mdo.workbench.profile'));await until(async()=>(await api('panel.layout.get',{id:profile})).value.active===fixture.id);
  await page.reload();await page.waitForFunction(()=>Boolean(window.mdo));await view.locator('.wb-file-editor').waitFor();assert.equal(await view.locator('.wb-file-editor').inputValue(),'pane one working\n');
  await view.getByText('文件已被外部修改，编辑内容已保留。',{exact:true}).waitFor();await view.locator('.wb-file-editor').fill('pane one still based on old disk\n');
  await page.reload();await page.waitForFunction(()=>Boolean(window.mdo));await view.locator('.wb-file-editor').waitFor();await view.getByText('文件已被外部修改，编辑内容已保留。',{exact:true}).waitFor();assert.equal(await view.getByRole('button',{name:'保存文件',exact:true}).isDisabled(),true);
  await view.getByRole('button',{name:'重新读取磁盘文件',exact:true}).click();await view.locator('.wb-source-text').filter({hasText:'pane two saved'}).waitFor();
  await view.getByRole('button',{name:'编辑文件',exact:true}).click();await view.locator('.wb-file-editor').fill('folder move draft\n');
  answers.push('改名目录');await rowMenu('子目录','重命名…');const moved=fixture.root+'/改名目录/重命名.txt';
  await until(async()=> (await api('panel.get',{id:second.id})).params.path===moved);
  await view.getByRole('textbox',{name:'编辑 重命名.txt',exact:true}).waitFor();assert.equal(await view.locator('.wb-file-editor').inputValue(),'folder move draft\n');
  await page.getByRole('tab',{name:'重命名.txt',exact:true}).click();await view.locator('.wb-file-editor').waitFor();assert.equal(await view.locator('.wb-file-editor').inputValue(),'pane two saved\n');assert.equal(await view.getByRole('button',{name:'保存文件',exact:true}).isDisabled(),true);
  const before=await page.locator('.wb-tab').count();await page.evaluate(path=>mdo.ui.openEditor({path,space:'native'}),moved);assert.equal(await page.locator('.wb-tab').count(),before,'renamed editor opened a duplicate tab');assert.equal(await page.locator(`.wb-tab[data-panel="${second.id}"] [role="tab"]`).getAttribute('aria-selected'),'true');
  await page.getByRole('tab',{name:'文件',exact:true}).click();await view.locator('.wb-file-editor').waitFor();
  await mainMenu('删除文件…');await view.getByText('文件已删除或移动；当前内容保留，未保存编辑仍是本机草稿。',{exact:true}).waitFor();assert.equal(await view.locator('.wb-file-editor').inputValue(),'folder move draft\n');
  await until(async()=>(await api('panel.layout.get',{id:profile})).value.active===fixture.id);
  await page.reload();await page.waitForFunction(()=>Boolean(window.mdo));await view.locator('.wb-file-editor').waitFor();assert.equal(await view.locator('.wb-file-editor').inputValue(),'folder move draft\n');assert.equal(await view.getByRole('button',{name:'保存文件',exact:true}).isDisabled(),true);
  await api('files.write',{path:fixture.root+'/from-outside.txt',text:'outside\n'});await view.getByRole('button',{name:'from-outside.txt',exact:true}).waitFor({timeout:10000});
  await page.screenshot({path:evidence+'/file-actions.png'});assert.deepEqual(errors,[]);assert.equal(answers.length,0);
  fs.writeFileSync(evidence+'/result.json',JSON.stringify({passed:true,time:Date.now(),checks:['exclusive create','dirty file rename/reload','no overwrite on create','stale rename rejection','independent pane drafts','conflict remains after edit/reload','hidden editor directory remap','reuse renamed editor','deleted draft recovery','automatic external directory refresh']},null,2));console.log('File actions, stale decisions, independent/renamed/deleted drafts, hidden records and live directory refresh: PASS');
 }catch(error){if(page){await page.screenshot({path:evidence+'/failure.png'}).catch(()=>{});fs.writeFileSync(evidence+'/failure.json',JSON.stringify({error:String(error.stack||error),errors,body:await page.locator('body').innerText().catch(()=>''),storage:await page.evaluate(()=>Object.keys(localStorage).filter(key=>key.startsWith('mdo.file.draft')).map(key=>[key,JSON.parse(localStorage.getItem(key))])).catch(()=>[])},null,2));}throw error;}
 finally{await browser.close();}
})().catch(error=>{console.error(error);process.exitCode=1;});
