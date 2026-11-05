const {chromium}=require(process.env.MDO_PLAYWRIGHT_PATH || 'playwright');
const assert=require('node:assert/strict');
(async()=>{
 const browser=await chromium.launch({headless:true,channel:'msedge'}),page=await browser.newPage({viewport:{width:1440,height:1000},locale:'zh-CN'}),errors=[];
 page.on('pageerror',e=>errors.push(String(e)));
 try{
  await page.route('**/*',route=>new URL(route.request().url()).hostname==='127.0.0.1'?route.continue():route.abort());
  await page.goto(process.argv[2]+'/#/projects/qa/sessions/turn-review');await page.waitForFunction(()=>Boolean(window.mdo));
  // Open the actual persisted session through ordinary navigation.
  await page.locator('.turn-changes:not([hidden])').first().waitFor({timeout:25000});
  const cards=page.locator('.turn-changes').filter({has:page.locator('.turn-changes-files')});await cards.nth(1).waitFor();assert.equal(await cards.count(),2);
  assert.equal(await page.locator('.turn-changes:not([hidden])').count(),2,'empty and unavailable captures do not add error-like cards to successful replies');
  const first=cards.first();await first.getByRole('button',{name:'已修改 5 个文件',exact:true}).waitFor();
  assert.equal(await first.locator('.turn-changes-file').count(),3);
  await first.getByRole('button',{name:'再显示 2 个文件',exact:true}).click();assert.equal(await first.locator('.turn-changes-file').count(),5);
  await first.locator('.turn-changes-file').filter({hasText:'tracked.txt'}).click();
  const view=page.locator('.wb-changes-view');await view.locator('.wb-change-code').filter({hasText:'first reply'}).waitFor();
  assert.equal(await view.getByRole('combobox',{name:'变更比较范围',exact:true}).inputValue(),'turn');
  await first.locator('.turn-changes-file').filter({hasText:'新增文件.txt'}).click();
  await view.locator('.wb-change-code').filter({hasText:'new text'}).waitFor();
  assert.equal(await page.locator('.wb-tab').count(),1,'file clicks reuse this reply panel');
  await first.getByRole('button',{name:'查看变更',exact:true}).click();await view.locator('.wb-continuous-file').first().waitFor();
  await view.locator('.wb-change-code').filter({hasText:'new text'}).waitFor();
  await view.locator('.wb-change-code').filter({hasText:'first reply'}).waitFor();
  assert.equal(await page.locator('.wb-tab').count(),1);
  const dismiss=page.getByRole('button',{name:'不再提示',exact:true});if(await dismiss.isVisible())await dismiss.click();
  await first.scrollIntoViewIfNeeded();await page.keyboard.press('Escape');
  await page.screenshot({path:process.argv[3]+'/review.png',fullPage:true});
  await page.evaluate(()=>document.documentElement.dataset.theme='dark');await page.screenshot({path:process.argv[3]+'/review-dark.png',fullPage:true});
  await page.reload();await page.waitForFunction(()=>Boolean(window.mdo));
  await cards.nth(1).waitFor();assert.equal(await cards.count(),2);
  const concurrency=await page.evaluate(async()=>{
    const {createTurnChanges}=await import('/js/features/chat/turn-changes.js');
    let reading=0,peak=0,completed=0;
    const review=createTurnChanges({read:async()=>{peak=Math.max(peak,++reading);await new Promise(resolve=>setTimeout(resolve,5));reading--;completed++;return {files:[]};}});
    for(let turn=1;turn<=12;turn++)review.node({turn,key:`qa-${turn}`,time:0},{projectId:'qa',sessionId:'load-test'});
    for(let attempt=0;completed<12&&attempt<100;attempt++)await new Promise(resolve=>setTimeout(resolve,5));
    review.destroy();return {peak,completed};
  });assert.deepEqual(concurrency,{peak:2,completed:12});
  assert.deepEqual(errors,[]);console.log('PASS summary expansion, exact-file right diff, panel reuse, continuous review, persisted reload, bounded history reads; no page errors');
 }catch(error){console.error('API DIAGNOSTIC',await page.evaluate(async()=>{
   try{return await mdo.bind({project:'qa',session:'turn-review'}).call('changes.list',{scope:'turn',turn:1});}catch(e){return {code:e.code,message:e.message};}
 }));console.error((await page.locator('body').innerText()).slice(-4000));await page.screenshot({path:process.argv[3]+'/failure.png',fullPage:true});throw error;}
 finally{await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1;});
