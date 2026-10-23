const {chromium}=require(process.env.MDO_PLAYWRIGHT_PATH || 'playwright');
const assert=require('node:assert/strict'),fs=require('node:fs');
(async()=>{
 const origin=process.argv[2],evidence=process.argv[3],browser=await chromium.launch({headless:true,channel:'msedge'});let page;const errors=[],reads=[];
 const until=async check=>{const deadline=Date.now()+25000;while(Date.now()<deadline){if(await check())return;await new Promise(resolve=>setTimeout(resolve,100));}throw Error('Restart condition timed out');};
 try{
  page=await browser.newPage({viewport:{width:1440,height:1000},locale:'zh-CN'});page.on('pageerror',error=>errors.push(String(error)));page.on('dialog',dialog=>dialog.accept());
  page.on('response',async response=>{try{const method=response.request().postDataJSON()?.method;if(['terminal.read','terminal.get'].includes(method)){const value=await response.json();reads.push({method,status:response.status(),code:value.error?.code,state:value.data?.state});if(reads.length>20)reads.shift();}}catch{}});
  await page.route('**/*',route=>new URL(route.request().url()).hostname==='127.0.0.1'?route.continue():route.abort());
  await page.goto(origin+'/');await page.waitForFunction(()=>Boolean(window.mdo));await page.locator('#new-workbench-tab').click();await page.getByRole('heading',{name:'新标签页',exact:true}).waitFor();
  const old=await page.evaluate(async()=>{const caps=await mdo.terminal.capabilities();const row=await mdo.terminal.create({key:'before-crash',args:caps.default_program.toLowerCase().includes('powershell')?['-NoLogo','-NoProfile']:[]});await mdo.panel.open({client:mdo.clientId,type:'mdo.terminal',key:'crash-view',params:{terminal:row.id},reveal:'activate'});return row;});
  const view=page.locator('.wb-terminal-view:not([hidden])');await until(async()=>await view.getAttribute('data-terminal-input')==='connected');
  const input=view.getByRole('textbox',{name:'交互终端输入',exact:true});await input.focus();await page.keyboard.insertText("[IO.File]::AppendAllText('once-before-crash.txt','1'); Start-Sleep -Seconds 30; [IO.File]::WriteAllText('never-after-crash.txt','bad')");await page.keyboard.press('Enter');
  await until(()=>page.evaluate(async()=>{try{return(await mdo.files.read({path:'once-before-crash.txt'})).text==='1'}catch{return false}}));
  fs.writeFileSync(evidence+'/restart-request','restart owned host');await until(()=>fs.existsSync(evidence+'/restart-done'));
  await view.locator('.wb-terminal-message').filter({hasText:'终端状态尚未确认。'}).waitFor({timeout:20000});
  assert.equal(await view.getAttribute('data-terminal-input'),'readonly');assert.equal(await view.getAttribute('data-terminal-state'),'unknown');assert.equal(await view.getByRole('button',{name:'新建终端',exact:true}).isDisabled(),true);await page.screenshot({path:evidence+'/terminal-service-restarted.png'});
  await page.reload();await page.waitForFunction(()=>Boolean(window.mdo));await view.locator('.xterm-rows').waitFor();
  await until(async()=>await view.getAttribute('data-terminal-state')==='ended');
  const rows=await page.evaluate(()=>mdo.terminal.list());assert.equal(rows.length,1);assert.equal(rows[0].id,old.id);assert.notEqual(rows[0].epoch,old.epoch);assert.equal(rows[0].reason,'service_restarted');
  assert.equal((await page.evaluate(()=>mdo.files.read({path:'once-before-crash.txt'}))).text,'1');await assert.rejects(()=>page.evaluate(()=>mdo.files.read({path:'never-after-crash.txt'})));
  await view.getByRole('button',{name:'新建终端',exact:true}).click();await until(async()=>await view.getAttribute('data-terminal-input')==='connected');
  const now=await page.evaluate(()=>mdo.terminal.list());assert.equal(now.length,2);assert.equal(now.filter(row=>row.state==='running').length,1);assert.deepEqual(errors,[]);
  fs.writeFileSync(evidence+'/result.json',JSON.stringify({passed:true,time:Date.now(),checks:['actual page across abrupt restart','specific restart state','old terminal ended after reload','no process or input replay','explicit new terminal only'],old_terminal:old.id,new_terminal:now.find(row=>row.state==='running').id},null,2));console.log('Terminal actual UI abrupt restart, ended restore and no input replay: PASS');
 }catch(error){if(page){await page.screenshot({path:evidence+'/failure.png'}).catch(()=>{});fs.writeFileSync(evidence+'/failure.json',JSON.stringify({error:String(error.stack||error),errors,reads,visible:await page.evaluate(()=>({hidden:document.hidden,view:document.querySelector('.wb-terminal-view')?.getBoundingClientRect().toJSON()})).catch(()=>null),observation:await page.evaluate(async()=>{const api=await import('/js/api/client.js');return api.allowsApiWrite('/application/call',{method:'POST',body:{method:'terminal.read'}})}).catch(e=>e.message),body:await page.locator('body').innerText().catch(()=>''),terminal:await page.locator('.wb-terminal-view').innerText().catch(()=>'')},null,2));}throw error;}
 finally{if(page)await page.evaluate(async()=>{try{for(const row of await mdo.terminal.list())if(['running','starting','closing'].includes(row.state))await mdo.terminal.close({id:row.id,epoch:row.epoch});}catch{}}).catch(()=>{});await browser.close();}
})().catch(error=>{console.error(error);process.exitCode=1;});
