const {chromium}=require(process.env.MDO_PLAYWRIGHT_PATH || 'playwright');
const assert=require('node:assert/strict'),fs=require('node:fs');
(async()=>{
 const origin=process.argv[2],evidence=process.argv[3],browser=await chromium.launch({headless:true,channel:'msedge'});let page;const errors=[];
 try{
  page=await browser.newPage({viewport:{width:1440,height:1000},locale:'zh-CN'});page.on('pageerror',e=>errors.push(String(e)));
  await page.route('**/*',route=>new URL(route.request().url()).hostname==='127.0.0.1'?route.continue():route.abort());
  await page.goto(origin+'/');await page.waitForFunction(()=>Boolean(window.mdo));await page.locator('#new-workbench-tab').click();
  const start=page.locator('.wb-start-view:not([hidden])');await start.getByRole('heading',{name:'新标签页',exact:true}).waitFor();
  assert.deepEqual(await start.locator('.wb-start-tools button').evaluateAll(nodes=>nodes.map(n=>n.getAttribute('aria-label'))),['打开浏览器','打开文件','打开终端','打开变更']);
  assert.equal(await start.getByRole('combobox').count(),0);assert.equal(await start.getByRole('textbox').count(),0);assert.equal(await start.getByText('会话计划',{exact:true}).count(),0);
  await page.screenshot({path:evidence+'/start-desktop.png'});
  const newTab=async()=>{await page.locator('.wb-top').getByRole('button',{name:'新建标签页',exact:true}).click();await start.getByRole('heading',{name:'新标签页',exact:true}).waitFor();};
  await start.getByRole('button',{name:'打开浏览器',exact:true}).click();await page.locator('.wb-browser-view:not([hidden]) .wb-browser-toolbar').waitFor();
  assert.equal(await page.getByRole('tab',{name:'新标签页',exact:true}).count(),0,'launcher left an extra tab');
  await newTab();await start.getByRole('button',{name:'打开文件',exact:true}).click();
  const files=page.locator('.wb-files-view:not([hidden])');await files.locator('.wb-file-tree').waitFor();
  await page.waitForTimeout(500);assert.equal(await files.locator('.wb-tree-body .wb-error').count(),0,'fresh default workspace cannot be opened');
  assert.equal(await page.getByRole('tab',{name:'新标签页',exact:true}).count(),0);
  const firstFiles=await page.evaluate(async()=> (await mdo.panel.list()).find(row=>row.type==='mdo.files').id);
  await newTab();await start.getByRole('button',{name:'打开文件',exact:true}).click();await files.waitFor();
  assert.deepEqual(await page.evaluate(async()=> (await mdo.panel.list()).filter(row=>row.type==='mdo.files').map(row=>row.id)),[firstFiles],'files launcher duplicated the same context');
  await newTab();await start.getByRole('button',{name:'打开终端',exact:true}).click();await page.locator('.wb-terminal-view:not([hidden]) .xterm-rows').waitFor();
  assert.equal(await page.evaluate(()=>mdo.terminal.list().then(rows=>rows.length)),1);
  assert.equal(await page.getByRole('tab',{name:'新标签页',exact:true}).count(),0);
  await newTab();await start.getByRole('button',{name:'打开变更',exact:true}).click();await page.locator('.wb-changes-view:not([hidden])').waitFor();
  await newTab();await page.evaluate(()=>{window.removeStartPlugin=mdo.ui.registerPanel('qa.start',{title:'测试插件',description:'插件入口',placement:'right',mount(root){root.textContent='插件已打开';}});});
  await start.getByRole('button',{name:'打开测试插件',exact:true}).waitFor();await start.getByRole('button',{name:'打开测试插件',exact:true}).click();await page.getByText('插件已打开',{exact:true}).waitFor();
  await newTab();await page.evaluate(()=>window.removeStartPlugin());await start.getByRole('button',{name:'打开测试插件',exact:true}).waitFor({state:'detached'});
  await page.evaluate(()=>mdo.bind({extension:'qa.publisher'}).panel.register({id:'qa.server-panel',title:'服务插件',renderer:'document'}));
  await start.getByRole('button',{name:'打开服务插件',exact:true}).waitFor();await start.getByRole('button',{name:'打开服务插件',exact:true}).click();
  await page.getByRole('tab',{name:'服务插件',exact:true}).waitFor();
  const serverPanel=await page.evaluate(async()=> (await mdo.bind({extension:'qa.publisher'}).panel.list()).find(row=>row.type==='qa.server-panel'));
  assert.equal(serverPanel.owner,'qa.publisher');assert.equal(serverPanel.context.project,'default');
  await newTab();await page.evaluate(()=>mdo.bind({extension:'qa.publisher'}).panel.unregister({id:'qa.server-panel'}));
  await start.getByRole('button',{name:'打开服务插件',exact:true}).waitFor({state:'detached'});
  await page.setViewportSize({width:480,height:780});await page.screenshot({path:evidence+'/start-narrow.png'});
  assert.ok((await start.locator('[data-tool="mdo.browser"]').boundingBox()).width>200);assert.deepEqual(errors,[]);
  fs.writeFileSync(evidence+'/result.json',JSON.stringify({passed:true,time:Date.now(),checks:['four default launchers','blank browser','empty Home files','no extra new tab','same-context file reuse','real terminal launch','changes launch','registered plugin launch/unregister','narrow layout']},null,2));console.log('Four-entry start page, context reuse, empty Home, plugin and narrow layout: PASS');
 }catch(error){if(page){await page.screenshot({path:evidence+'/failure.png'});fs.writeFileSync(evidence+'/failure.json',JSON.stringify({error:String(error.stack||error),errors,body:await page.locator('body').innerText()},null,2));}throw error;}
 finally{if(page)await page.evaluate(async()=>{try{for(const row of await mdo.terminal.list())await mdo.terminal.close({id:row.id,epoch:row.epoch});}catch{}}).catch(()=>{});await browser.close();}
})().catch(error=>{console.error(error);process.exitCode=1;});
