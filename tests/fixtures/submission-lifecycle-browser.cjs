const {chromium}=require(process.env.MDO_PLAYWRIGHT_PATH||'playwright');
const assert=require('node:assert/strict'),fs=require('node:fs');
(async()=>{
 const origin=process.argv[2],dir=process.argv[3],errors=[];
 const browser=await chromium.launch({headless:true,channel:'msedge'});
 const page=await browser.newPage({viewport:{width:1440,height:1000},locale:'zh-CN'});
 page.setDefaultTimeout(15000);page.on('pageerror',error=>errors.push(String(error)));
 let release;
 try{
  await page.route('**/*',route=>new URL(route.request().url()).hostname==='127.0.0.1'?route.continue():route.abort());
  await page.goto(origin+'/#/projects/edit-qa/sessions/stopped-edit');
  await page.waitForFunction(()=>Boolean(window.mdo));
  const dismiss=page.getByRole('button',{name:'不再提示',exact:true});if(await dismiss.isVisible())await dismiss.click();
  await page.locator('#new-session').click();
  let blocked,queuePath,messageId;
  const entered=new Promise(resolve=>blocked=resolve),gate=new Promise(resolve=>release=resolve);
  await page.route(/\/queue\/[a-f0-9]{32}$/,async route=>{
   const req=route.request();
   if(req.method()==='PUT'&&req.postDataJSON()?.state==='pending'&&!messageId){
    const url=new URL(req.url());messageId=url.pathname.split('/').at(-1);queuePath=url.pathname.slice(0,-33);
    blocked();await gate;
   }
   await route.continue();
  });
  await page.locator('#prompt').fill('cancel during initial preparation');await page.locator('#send').click();
  await entered;
  const old=page.locator('.submission-user').filter({hasText:'cancel during initial preparation'});
  await old.waitFor();
  assert.notEqual((await page.locator('#run-status').innerText()).trim(),'就绪','preparing input must not show idle');
  assert.equal(await page.locator('#timeline .submission-user').count(),1,'provisional input belongs to the real timeline');
  await page.locator('#send[data-action="stop"]').click();
  await page.locator('#prompt').fill('newer prompt');
  await page.waitForFunction(()=>document.querySelector('.submission-reply[data-state="cancelled"]'));
  release();await page.unroute(/\/queue\/[a-f0-9]{32}$/);
  await page.waitForTimeout(300);
  assert.equal(await page.locator('#prompt').inputValue(),'newer prompt','a cancelled input cannot overwrite later typing');
  assert.equal(fs.readFileSync(dir+'/runtime.log','utf8').match(/EDIT_MODEL_CALL=/g)?.length||0,0);
  const token=(await page.request.get(origin+'/api/v1/bootstrap')).headers()['x-mdo-write-token'];
  let state=await (await page.request.get(origin+queuePath)).json();
  assert.equal(state.data.items.find(item=>item.id===messageId)?.state,'cancelled');
  await page.locator('#send').click();
  const newer=page.locator('#timeline .timeline-item[data-kind="user"]').filter({hasText:'newer prompt'});
  await newer.waitFor({timeout:30000});
  await page.locator('#timeline .timeline-item[data-kind="assistant"]').filter({hasText:'Edited reply accepted.'}).waitFor();
  await page.waitForFunction(()=>!document.querySelector('#send[data-action="stop"]'));
  assert.equal(await old.count(),1,'the occupied draft leaves one explicit stopped input');
  const order=await page.locator('#timeline>.timeline-item[data-kind="user"]').allTextContents();
  assert(order.findIndex(x=>x.includes('cancel during'))<order.findIndex(x=>x.includes('newer prompt')),'stopped input stays before the later run');
  assert.equal(await page.locator('.submission-reply').getByText('正在准备回复…',{exact:true}).count(),0);
  await page.locator('.submission-reply').getByRole('button',{name:'编辑',exact:true}).click();
  await page.waitForFunction(()=>document.querySelector('#prompt').value==='cancel during initial preparation');
  await page.waitForFunction(()=>document.querySelectorAll('.submission-user').length===0);
  await page.locator('#prompt').fill('replacement after cancelled preparation');await page.locator('#send').click();
  await page.locator('#timeline .timeline-item[data-kind="user"]').filter({hasText:'replacement after cancelled preparation'}).waitFor();
  await page.waitForFunction(()=>document.querySelectorAll('.submission-user').length===0,{},{timeout:30000});
  await page.waitForFunction(()=>!document.querySelector('#send[data-action="stop"]'));
  assert.equal(await page.locator('#timeline .timeline-item[data-kind="user"]').count(),2);
  assert.equal(await page.locator('.submission-user').count(),0);
  const status=await (await page.request.get(origin+queuePath+'/'+messageId)).json();
  assert.equal(status.data.state,'cancelled');assert.equal(status.data.retained_input,false);
  const replay=await page.request.post(origin+queuePath,{headers:{'X-Mdo-Write-Token':token},data:{id:messageId,text:'cancel during initial preparation',first:false}});
  assert.equal(replay.status(),409,'the cancelled identity remains fenced after editing');
  await page.reload();await page.waitForFunction(()=>Boolean(window.mdo));
  await page.locator('#timeline .timeline-item[data-kind="user"]').filter({hasText:'replacement after cancelled preparation'}).waitFor();
  assert.equal(await page.locator('.submission-user').count(),0,'reloading cannot resurrect the cancelled input');
  // Native admission is NOT the history handoff. Hold a real Agent prepare
  // callback after the run ID is published but before any Agent Start event.
  await page.goto(origin+'/#/projects/edit-qa/sessions/accepted-prep');
  await page.locator('#prompt').fill('accepted preflight barrier');await page.locator('#send').click();
  for(let attempt=0;attempt<200&&!fs.readFileSync(dir+'/runtime.log','utf8').includes('PREP_BARRIER_ENTERED=1');attempt++)await page.waitForTimeout(50);
  assert(fs.readFileSync(dir+'/runtime.log','utf8').includes('PREP_BARRIER_ENTERED=1'),'the real prepare callback must be holding');
  await page.locator('#send[data-action="stop"]').click();
  await page.waitForFunction(()=>document.querySelector('#prompt').value==='accepted preflight barrier',{},{timeout:30000});
  await page.waitForFunction(()=>document.querySelectorAll('.submission-user').length===0);
  assert.equal(await page.locator('#timeline .timeline-item[data-kind="user"]').count(),0,'a native run ID alone cannot commit the user message');
  assert.equal(fs.readFileSync(dir+'/runtime.log','utf8').match(/EDIT_MODEL_CALL=/g)?.length||0,2,'the stopped prepare callback never reaches a model');
  const discardId='e'.repeat(32),discardQueue='/api/v1/projects/edit-qa/sessions/accepted-prep/queue';
  const staged=await page.request.post(origin+discardQueue,{headers:{'X-Mdo-Write-Token':token},data:{id:discardId,text:'discard stopped input',first:false,managed:true,stage:true,hold_for_edit:true}});
  assert.equal(staged.status(),201);
  assert.equal((await page.request.post(origin+discardQueue+'/'+discardId+'/cancel',{headers:{'X-Mdo-Write-Token':token}})).status(),200);
  await page.reload();await page.waitForFunction(()=>Boolean(window.mdo));
  await page.locator('.submission-reply').getByRole('button',{name:'移除',exact:true}).click();
  await page.waitForFunction(()=>document.querySelectorAll('.submission-user').length===0);
  assert.equal(await page.locator('#prompt').inputValue(),'accepted preflight barrier','discard must preserve the occupied draft');
  assert.equal((await (await page.request.get(origin+discardQueue)).json()).data.items.length,0);
  await page.setViewportSize({width:390,height:844});
  await page.screenshot({path:dir+'/mobile.png',fullPage:true});
  assert.deepEqual(errors,[]);
  fs.writeFileSync(dir+'/browser.json',JSON.stringify({passed:true,newTask:true,stopBeforeRun:true,stopAfterAdmissionBeforeHistory:true,noCancelledModelCall:true,newTypingPreserved:true,chronological:true,editable:true,discard:true,noReplay:true,noResurrection:true,mobile:true,errors},null,2));
  console.log('PASS native first-message preparation stop, typing preservation, chronological handoff, edit/resend and reload');
 }catch(error){release?.();console.error(error);console.error((await page.locator('body').innerText()).slice(-4500));await page.screenshot({path:dir+'/failure.png',fullPage:true});throw error;}
 finally{await browser.close();}
})();
