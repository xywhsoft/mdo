const {chromium}=require(process.env.MDO_PLAYWRIGHT_PATH||'playwright');
const assert=require('node:assert/strict');const fs=require('node:fs');
(async()=>{
 const origin=process.argv[2],dir=process.argv[3],session='/api/v1/projects/edit-qa/sessions/stopped-edit';
 const browser=await chromium.launch({headless:true,channel:'msedge'}),page=await browser.newPage({viewport:{width:1440,height:1000},locale:'zh-CN'}),errors=[];
 page.setDefaultTimeout(10000);page.on('pageerror',e=>errors.push(String(e)));
 let release,blocked;const entered=new Promise(r=>blocked=r),gate=new Promise(r=>release=r);
 try{
  await page.route('**/*',route=>new URL(route.request().url()).hostname==='127.0.0.1'?route.continue():route.abort());
  await page.goto(origin+'/#/projects/edit-qa/sessions/stopped-edit');await page.waitForFunction(()=>Boolean(window.mdo));
  const dismiss=page.getByRole('button',{name:'不再提示',exact:true});if(await dismiss.isVisible())await dismiss.click();
  await page.locator('#prompt').fill('original prompt');await page.locator('#send').click();
  const original=page.locator('#timeline .timeline-item[data-kind="user"]').filter({hasText:'original prompt'});
  await original.waitFor();await page.locator('#send[data-action="stop"]').waitFor();
  for(let attempt=0;attempt<100&&!fs.readFileSync(dir+'/runtime.log','utf8').includes('EDIT_MODEL_CALL=1');attempt++)await page.waitForTimeout(50);
  assert.ok(fs.readFileSync(dir+'/runtime.log','utf8').includes('EDIT_MODEL_CALL=1'),'the original model call must be running before Stop');
  await page.locator('#send').click();
  await original.getByRole('button',{name:'编辑此消息并重新发送',exact:true}).click();
  await page.locator('#message-edit-input').fill('edited prompt one');
  let editBody,editEtag;
  await page.route('**/truncate',async route=>{editBody=route.request().postDataJSON();editEtag=route.request().headers()['if-match'];blocked();await gate;
    const committed=await route.fetch();assert.equal(committed.status(),200,await committed.text());
    await route.abort('connectionreset'); /* Lose the successful cut acknowledgement once. */
  });
  await page.locator('#message-edit-form button[type="submit"]').click();
  const provisional=page.locator('.submission-user').filter({hasText:'edited prompt one'});
  await provisional.waitFor();assert.equal(await provisional.count(),1);
  assert.equal(await page.locator('#message-edit-input').isVisible(),false,'editor yields immediately to the pending bubble');
  await entered;assert.match(editBody.history_token,/^[0-9a-f]{64}$/);
  await page.screenshot({path:dir+'/pending.png',fullPage:true});
  const boot=await page.request.get(origin+'/api/v1/bootstrap'),token=boot.headers()['x-mdo-write-token'];
  const current=await page.request.get(origin+session);
  const renamed=await page.request.patch(origin+session,{headers:{'If-Match':current.headers().etag,'X-Mdo-Write-Token':token},data:{title:'Renamed during edit'}});
  assert.equal(renamed.status(),200,await renamed.text());
  const legacy=await page.request.post(origin+session+'/truncate',{headers:{'If-Match':current.headers().etag,'X-Mdo-Write-Token':token},data:{through_sequence:editBody.through_sequence,source_event_id:editBody.source_event_id,client_edit_id:'e'.repeat(32)}});
  assert.equal(legacy.status(),412,'legacy clients keep strict revision fencing');
  for(const [history_token,status] of [['z'.repeat(64),422],['f'.repeat(64),409]]){
    const refused=await page.request.post(origin+session+'/truncate',{headers:{'If-Match':current.headers().etag,'X-Mdo-Write-Token':token},data:{...editBody,client_edit_id:'d'.repeat(32),history_token}});
    assert.equal(refused.status(),status,await refused.text());
  }
  release();await page.unroute('**/truncate');
  await page.locator('#timeline .timeline-item[data-kind="user"]').filter({hasText:'edited prompt one'}).waitFor({timeout:25000});
  await page.locator('#timeline .timeline-item[data-kind="assistant"]').filter({hasText:'Edited reply accepted.'}).waitFor({timeout:25000});
  await page.waitForFunction(()=>document.querySelectorAll('.submission-user').length===0);
  assert.equal(await page.locator('#timeline .timeline-item[data-kind="user"]').count(),1);
  assert.equal(await page.locator('#message-edit-error').isVisible(),false);
  // A genuinely new tail while the next editor is open must not be discarded.
  const edited=page.locator('#timeline .timeline-item[data-kind="user"]');
  await page.waitForFunction(()=>!document.querySelector('#send[data-action="stop"]'));
  let staleReplay;
  for(let attempt=0;attempt<30;attempt++){
    staleReplay=await page.request.post(origin+session+'/truncate',{headers:{'If-Match':editEtag,'X-Mdo-Write-Token':token},data:editBody});
    if(staleReplay.status()!==409||(await staleReplay.json()).error?.code!=='session_runtime_busy')break;
    await page.waitForTimeout(50);
  }
  assert.equal(staleReplay.status(),409,await staleReplay.text());
  assert.equal((await staleReplay.json()).error.code,'session_history_changed','a cached cut cannot replay after new history');
  const different=await page.request.post(origin+session+'/truncate',{headers:{'If-Match':editEtag,'X-Mdo-Write-Token':token},data:{...editBody,replacement:{...editBody.replacement,text:'different body under the same edit ID'}}});
  assert.equal(different.status(),409);assert.equal((await different.json()).error.code,'session_edit_conflict');
  let releaseRead,readEntered;
  const readGate=new Promise(resolve=>releaseRead=resolve),reading=new Promise(resolve=>readEntered=resolve);
  await page.route('**/history',async route=>{readEntered();await readGate;await route.continue();});
  const baseReady=page.waitForResponse(response=>response.url().endsWith('/history')&&response.status()===200);
  void baseReady.catch(()=>{});
  await edited.getByRole('button',{name:'编辑此消息并重新发送',exact:true}).click();
  await reading;await page.locator('#message-edit-input').fill('cancelled pending edit');
  await page.locator('#message-edit-form button[type="submit"]').click();
  await page.locator('.submission-user').filter({hasText:'cancelled pending edit'}).waitFor();
  assert.equal(await page.locator('.submission-reply button').count(),0);
  await page.locator('#send[data-action="stop"]').click();
  await page.locator('#message-edit-input').waitFor();assert.equal(await page.locator('#message-edit-input').inputValue(),'cancelled pending edit');
  assert.equal(await page.locator('#message-edit-error').isVisible(),false);
  await page.waitForFunction(()=>document.querySelectorAll('.submission-user').length===0);
  assert.equal(await page.locator('.submission-user').count(),0);
  releaseRead();await page.unroute('**/history');
  await page.locator('#message-edit-input').fill('preserve this conflicted edit');
  await baseReady;
  let third;
  for(let attempt=0;attempt<30;attempt++){
    third=await page.request.post(origin+session+'/runs',{headers:{'X-Mdo-Write-Token':token},data:{prompt:'new message from another window'}});
    if(third.status()!==409||(await third.json()).error?.code!=='session_busy')break;
    await page.waitForTimeout(50);
  }
  assert.equal(third.status(),202,await third.text());
  await page.locator('#timeline .timeline-item[data-kind="user"]').filter({hasText:'new message from another window'}).waitFor();
  await page.waitForFunction(()=>!document.querySelector('#send[data-action="stop"]'));
  await page.locator('#message-edit-form button[type="submit"]').click();
  await page.locator('#message-edit-error').waitFor();
  assert.equal(await page.locator('#message-edit-input').inputValue(),'preserve this conflicted edit');
  assert.ok((await page.locator('#message-edit-error').innerText()).includes('内容已保留'));
  assert.equal(await page.locator('#timeline .timeline-item[data-kind="user"]').count(),2);
  await page.screenshot({path:dir+'/conflict.png',fullPage:true});
  assert.deepEqual(errors,[]);fs.writeFileSync(dir+'/browser.json',JSON.stringify({passed:true,immediate:true,oneBubble:true,metadataRebase:true,historyConflictRetained:true,cancelPendingRetainsInput:true,invalidTokensRejected:true,lostCutAcknowledgementRecovered:true,legacyRevisionFence:true,staleReceiptRejected:true,errors},null,2));
  console.log('PASS actual stop/edit/resend, pending before gated cut, metadata rebase, queue/event deduplication, genuine history conflict retained');
 }catch(error){release?.();console.error(error);console.error((await page.locator('body').innerText()).slice(-5000));await page.screenshot({path:dir+'/failure.png',fullPage:true});throw error;}
 finally{await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1;});
