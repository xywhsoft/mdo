const {chromium}=require(process.env.MDO_PLAYWRIGHT_PATH || 'playwright');
const assert=require('node:assert/strict'),fs=require('node:fs');
(async()=>{
 const origin=process.argv[2],evidence=process.argv[3],browser=await chromium.launch({headless:true,channel:'msedge'});let page;const errors=[];
 const until=async check=>{const deadline=Date.now()+25000;while(Date.now()<deadline){if(await check())return;await new Promise(resolve=>setTimeout(resolve,100));}throw Error('Baseline UI condition timed out');};
 try{
  page=await browser.newPage({viewport:{width:1440,height:1000},locale:'zh-CN'});page.on('pageerror',error=>errors.push(String(error)));
  await page.route('**/*',route=>new URL(route.request().url()).hostname==='127.0.0.1'?route.continue():route.abort());
  await page.goto(origin+'/');await page.waitForFunction(()=>Boolean(window.mdo));await page.locator('#new-workbench-tab').click();await page.getByRole('heading',{name:'新标签页',exact:true}).waitFor();
  const create=()=>page.evaluate(async()=>{const {api}=await import('/js/api/client.js');return(await api.post('/sessions',{project_id:'default',title:'Baseline UI fixture'})).data.id;});
  const select=async id=>{await page.evaluate(id=>{location.hash=`#/projects/default/sessions/${id}`},id);await until(()=>page.evaluate(async id=>(await mdo.call('sdk.context')).session===id,id));};
  const first=await create();await select(first);await page.evaluate(()=>mdo.ui.newTab());
  assert.equal((await page.evaluate(()=>mdo.changes.baseline())).available,true,'generated JS baseline method failed');
  await page.locator('.wb-start-view:not([hidden])').getByRole('button',{name:'打开变更',exact:true}).click();
  const view=page.locator('.wb-changes-view:not([hidden])');await view.getByRole('combobox',{name:'变更比较范围',exact:true}).selectOption('session');await view.getByText('此比较范围没有变更。',{exact:true}).waitFor();
  await page.evaluate(async()=>{await mdo.files.write({path:'tracked.txt',text:'after session\n'});await mdo.files.write({path:'new.txt',text:'new file\n'});});await view.getByRole('button',{name:'刷新变更',exact:true}).click();
  await view.getByRole('textbox',{name:'筛选变更文件',exact:true}).fill('tracked.txt');await view.getByRole('treeitem').filter({hasText:'tracked.txt'}).click();await view.locator('.wb-change-removed').filter({hasText:'preexisting dirty'}).waitFor();await view.locator('.wb-change-added').filter({hasText:'after session'}).waitFor();
  const rows=await page.evaluate(()=>mdo.panel.list());const firstPanel=rows.find(row=>row.type==='mdo.changes'&&row.context.session===first);assert.ok(firstPanel);
  assert.ok((await view.locator('.wb-changes-summary').getAttribute('title')).includes('原始工作区字节'));
  await view.getByRole('button',{name:'选择差异第 7 行',exact:true}).click();await view.getByRole('button',{name:'引用所选差异',exact:true}).click();assert.ok((await page.locator('.prompt-context-open').getAttribute('title')).includes('会话基线树：'));
  const second=await create();await select(second);await view.getByRole('button',{name:'刷新变更',exact:true}).click();await view.locator('.wb-change-removed').filter({hasText:'preexisting dirty'}).waitFor();
  assert.equal((await page.evaluate(()=>mdo.changes.list({scope:'session'}))).files.length,0,'new navigation session used an older baseline');
  await page.evaluate(()=>mdo.ui.newTab());await page.locator('.wb-start-view:not([hidden])').getByRole('button',{name:'打开变更',exact:true}).click();await view.getByRole('combobox',{name:'变更比较范围',exact:true}).selectOption('session');await view.getByText('此比较范围没有变更。',{exact:true}).waitFor();
  const panels=await page.evaluate(()=>mdo.panel.list());assert.equal(panels.filter(row=>row.type==='mdo.changes').length,2,'different session scopes reused one changes tab');
  await page.locator(`.wb-tab[data-panel="${firstPanel.id}"] [role="tab"]`).click();await view.locator('.wb-change-removed').filter({hasText:'preexisting dirty'}).waitFor();await page.screenshot({path:evidence+'/session-baseline.png'});
  await page.waitForTimeout(500);await page.reload();await page.waitForFunction(()=>Boolean(window.mdo));await view.locator('.wb-change-removed').filter({hasText:'preexisting dirty'}).waitFor();assert.equal(await view.getByRole('combobox',{name:'变更比较范围',exact:true}).inputValue(),'session');assert.deepEqual(errors,[]);
  fs.writeFileSync(evidence+'/result.json',JSON.stringify({passed:true,checks:['real session creation','startup dirty state excluded','raw baseline labels and quote','panel retains original session after navigation','separate session panels','scope survives reload']},null,2));console.log('Actual baseline UI, quote provenance, frozen session context and reload: PASS');
 }catch(error){if(page){await page.screenshot({path:evidence+'/failure.png'}).catch(()=>{});fs.writeFileSync(evidence+'/failure.json',JSON.stringify({error:String(error.stack||error),errors,body:await page.locator('body').innerText().catch(()=>'')},null,2));}throw error;}
 finally{await browser.close();}
})().catch(error=>{console.error(error);process.exitCode=1;});
