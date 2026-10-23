const {chromium}=require(process.env.MDO_PLAYWRIGHT_PATH || 'playwright');
const assert=require('node:assert/strict'),fs=require('node:fs');
(async()=>{
 const origin=process.argv[2],evidence=process.argv[3],browser=await chromium.launch({headless:true,channel:'msedge'});
 const until=async check=>{const deadline=Date.now()+20000;while(Date.now()<deadline){if(await check())return;await new Promise(resolve=>setTimeout(resolve,100));}throw Error('Terminal condition did not become true');};
 let page;const errors=[],diagnostics=[];
 try{
  page=await browser.newPage({viewport:{width:1440,height:1000},locale:'zh-CN'});
  page.on('pageerror',error=>errors.push(String(error)));page.on('dialog',dialog=>dialog.accept());
  page.on('console',message=>{if(['error','warning'].includes(message.type()))diagnostics.push(message.text());});
  page.on('response',async response=>{try{const req=response.request().postDataJSON();if(req?.method==='terminal.read'){const data=(await response.json()).data;diagnostics.push({read:req.params.cursor,status:response.status(),offset:data?.offset,next:data?.next,bytes:data?.bytes});if(diagnostics.length>20)diagnostics.shift();}}catch{}});
  await page.route('**/*',route=>new URL(route.request().url()).hostname==='127.0.0.1'?route.continue():route.abort());
  await page.goto(origin+'/');await page.waitForFunction(()=>Boolean(window.mdo));await page.locator('#new-workbench-tab').click();
  await page.getByRole('heading',{name:'新标签页',exact:true}).waitFor();
  const panel=await page.evaluate(()=>mdo.panel.open({client:mdo.clientId,type:'mdo.terminal',key:'terminal-qa',title:'终端',reveal:'activate'}));
  const view=page.locator('.wb-terminal-view:not([hidden])'),bar=view.locator('.wb-terminal-toolbar');
  await bar.getByRole('button',{name:'新建终端',exact:true}).click();
  await view.locator('.xterm-rows').waitFor();
  await page.waitForFunction(()=>document.querySelector('.wb-terminal-view')?.dataset.terminalInput==='connected');
  const terminal=await page.evaluate(async()=> (await mdo.terminal.list())[0]);assert.equal(terminal.state,'running');
  assert.ok(terminal.cwd.includes('workbench-ui-'),'shell escaped private QA Home');
  const typing=view.getByRole('textbox',{name:'交互终端输入',exact:true});
  const shell=async text=>{await typing.focus();await page.keyboard.insertText(text);await page.keyboard.press('Enter');};
  const file=async(name,contents)=>{
   await until(()=>page.evaluate(async({name,contents})=>{try{return (await mdo.files.read({path:name})).text===contents;}catch{return false;}},{name,contents}));
  };
  await shell("[IO.File]::WriteAllText('ui-unicode.txt','界面🌙'); Write-Output '终端界面正确'");
  await file('ui-unicode.txt','界面🌙');
  await page.waitForFunction(()=>[...document.querySelectorAll('.wb-terminal-surface .xterm-rows > div')].some(row=>row.textContent.trim()==='终端界面正确'));
  // Exercise the browser paste path with isolated data. Never replace/read the
  // user's OS clipboard or risk executing concurrently copied user content.
  await typing.focus();await typing.evaluate((node,text)=>{const data=new DataTransfer();data.setData('text/plain',text);node.dispatchEvent(new ClipboardEvent('paste',{clipboardData:data,bubbles:true}));},"[IO.File]::WriteAllText('clipboard.txt','粘贴成功')");
  await page.keyboard.press('Enter');await file('clipboard.txt','粘贴成功');
  await typing.focus();await page.keyboard.press('Control+f');await view.getByRole('textbox',{name:'查找终端内容',exact:true}).fill('终端界面正确');await view.locator('.wb-terminal-find > span').filter({hasText:'1 /'}).waitFor();await page.keyboard.press('Escape');
  const admitted=(await page.evaluate(id=>mdo.terminal.get({id}),terminal.id)).input_admitted;
  await typing.evaluate(node=>{const data=new DataTransfer();data.setData('text/plain','x'.repeat(65537));node.dispatchEvent(new ClipboardEvent('paste',{clipboardData:data,bubbles:true}));});
  await view.locator('.wb-terminal-message').filter({hasText:'超出 64 KiB'}).waitFor();
  assert.equal((await page.evaluate(id=>mdo.terminal.get({id}),terminal.id)).input_admitted,admitted,'oversized paste sent a partial command');
  // Real PowerShell cancellation, then prove a following command runs promptly.
  await shell("Start-Sleep -Seconds 30; [IO.File]::WriteAllText('must-not-run.txt','bad')");await page.waitForTimeout(500);await typing.focus();await page.keyboard.press('Control+c');
  await shell("[IO.File]::WriteAllText('interrupted.txt','ok')");await file('interrupted.txt','ok');
  await assert.rejects(()=>page.evaluate(()=>mdo.files.read({path:'must-not-run.txt'})));
  const before=await page.evaluate(id=>mdo.terminal.get({id}),terminal.id);
  await shell("Start-Sleep -Milliseconds 800; [IO.File]::WriteAllText('hidden.txt','ok'); Write-Output 'HIDDEN-COMPLETED'");
  await page.waitForFunction(()=>!document.querySelector('.wb-terminal-status').textContent.includes('待确认'));
  await page.locator('.wb-top').getByRole('button',{name:'新建标签页',exact:true}).click();await file('hidden.txt','ok');
  const hidden=await page.evaluate(id=>mdo.terminal.get({id}),terminal.id);assert.equal(hidden.state,'running');assert.equal(hidden.pid,before.pid);
  await page.getByRole('tab',{name:'终端',exact:true}).click();await view.locator('.xterm-rows').filter({hasText:'HIDDEN-COMPLETED'}).waitFor();
  await page.waitForFunction(()=>document.querySelector('.wb-terminal-view')?.dataset.terminalInput==='connected');
  // Resize is real: the backend reports the PTY dimensions chosen by the view.
  await page.setViewportSize({width:760,height:700});await page.waitForTimeout(300);
  const narrow=await page.evaluate(id=>mdo.terminal.get({id}),terminal.id);assert.ok(narrow.columns>=2&&narrow.columns<=500&&narrow.rows<before.rows);
  await page.screenshot({path:evidence+'/terminal-narrow.png'});await page.setViewportSize({width:1440,height:1000});
  await page.waitForTimeout(300);await page.screenshot({path:evidence+'/terminal-shell.png'});
  let lost=false,writesAfterLoss=0;
  const loseAck=async route=>{const request=route.request().postDataJSON();if(request?.method==='terminal.write'){
   if(lost)writesAfterLoss++;
   if(!lost&&Buffer.from(request.params.base64||'','base64').toString()==='\r'){lost=true;await route.fetch();await route.abort();return;}
  }await route.continue();};
  await page.route('**/api/v1/application/call',loseAck);
  await shell("[IO.File]::AppendAllText('once.txt','1')");await view.getByText(/最近输入是否收到尚不确定/).waitFor();await file('once.txt','1');
  await page.waitForTimeout(800);assert.equal(writesAfterLoss,0,'uncertain input was retried');
  await page.unroute('**/api/v1/application/call',loseAck);
  await bar.getByRole('button',{name:'连接终端输入',exact:true}).click();await page.waitForFunction(()=>document.querySelector('.wb-terminal-view')?.dataset.terminalInput==='connected');
  // Closing only the panel detaches; another panel attaches to the same process.
  await page.evaluate(id=>mdo.panel.close({id}),panel.id);await page.waitForTimeout(2500);
  assert.equal((await page.evaluate(id=>mdo.terminal.get({id}),terminal.id)).pid,before.pid);
  const other=await page.evaluate(id=>mdo.terminal.attach({id,client:'qa-other-view',control:true}),terminal.id);
  await page.evaluate(id=>mdo.panel.open({client:mdo.clientId,type:'mdo.terminal',key:'reattach',title:'终端',params:{terminal:id},reveal:'activate'}),terminal.id);
  await view.locator('.xterm-rows').filter({hasText:'HIDDEN-COMPLETED'}).waitFor();
  await view.getByText('另一个页面正在输入；这里可以继续只读查看。',{exact:true}).waitFor();
  await page.evaluate(other=>mdo.terminal.detach({...other,client:'qa-other-view'}),other);
  await bar.getByRole('button',{name:'连接终端输入',exact:true}).click();
  await page.waitForFunction(()=>document.querySelector('.wb-terminal-view')?.dataset.terminalInput==='connected');
  await shell("[IO.File]::WriteAllText('reattached.txt','ok')");await file('reattached.txt','ok');
  await bar.getByRole('button',{name:'结束终端',exact:true}).click();
  await until(()=>page.evaluate(async id=>(await mdo.terminal.get({id})).drained,terminal.id));
  await view.locator('.wb-terminal-status > span:first-child').filter({hasText:/^已结束$/}).waitFor();assert.equal(await page.evaluate(()=>mdo.terminal.list().then(rows=>rows.length)),1);
  assert.deepEqual(errors,[]);
  fs.writeFileSync(evidence+'/result.json',JSON.stringify({passed:true,time:Date.now(),checks:['real Windows PowerShell','Unicode input and render','isolated clipboard paste event','search','Ctrl+C interrupts','hidden panel continues','real resize','lost input ACK never replays','competing writer stays readonly','panel close/reattach without spawn','explicit end'],terminal,narrow},null,2));
  console.log('Terminal frontend, real PowerShell Unicode/interrupt, hide/reattach, resize and explicit end: PASS');
 }catch(error){if(page){await page.screenshot({path:evidence+'/failure.png'}).catch(()=>{});fs.writeFileSync(evidence+'/failure-diagnostics.json',JSON.stringify({error:String(error.stack||error),errors,diagnostics,body:await page.locator('body').innerText().catch(()=>''),rows:await page.locator('.xterm-rows').allTextContents().catch(()=>[])},null,2));}throw error;}
 finally{
  if(page)await page.evaluate(async()=>{try{for(const row of await mdo.terminal.list())if(['starting','running','closing'].includes(row.state))await mdo.terminal.close({id:row.id,epoch:row.epoch});}catch{}}).catch(()=>{});
  await browser.close();
 }
})().catch(error=>{console.error(error);process.exitCode=1;});
