const {chromium}=require(process.env.MDO_PLAYWRIGHT_PATH||'playwright');
const assert=require('node:assert/strict'),fs=require('node:fs');
(async()=>{
 const origin=process.argv[2],dir=process.argv[3],errors=[],gates=[];
 const browser=await chromium.launch({headless:true,channel:'msedge'}),page=await browser.newPage({viewport:{width:1340,height:960},locale:'zh-CN'});
 page.setDefaultTimeout(15000);page.on('pageerror',e=>errors.push(String(e)));
 let phantom=null,advertise=false,missingReads=0;
 async function waitGate(n){for(let i=0;i<200&&gates.length<n;i++)await page.waitForTimeout(40);assert.equal(gates.length,n);return gates[n-1];}
 const edit=()=>page.locator('.submission-user').last().getByRole('button',{name:'编辑此消息并重新发送',exact:true});
 try{
  await page.route('**/*',async route=>{
   const req=route.request(),url=new URL(req.url());if(url.hostname!=='127.0.0.1')return route.abort();
   if(url.pathname==='/api/v1/runs'&&advertise){const base=await route.fetch(),body=await base.json();body.data.items.unshift(phantom);body.data.active_runs++;
    return route.fulfill({response:base,json:body});}
   if(phantom&&url.pathname==='/api/v1/runs/'+phantom.id){
    if(advertise)return route.fulfill({status:200,json:{ok:true,data:phantom}});
    missingReads++;return route.fulfill({status:404,json:{ok:false,error:{code:'run_not_found',message:'The prepared reservation was released'}}});}
   if(req.method()==='PUT'&&/\/queue\/[a-f0-9]{32}$/.test(url.pathname)&&req.postDataJSON()?.state==='pending'){
    let release;const gate=new Promise(resolve=>release=resolve);gates.push({path:url.pathname,release});await gate;
   }
   await route.continue();
  });
  await page.goto(origin+'/#/projects/edit-qa/sessions/stopped-edit');await page.waitForFunction(()=>Boolean(window.mdo));
  const dismiss=page.getByRole('button',{name:'不再提示',exact:true});if(await dismiss.isVisible())await dismiss.click();
  await page.locator('#prompt').fill('same stopped input');await page.locator('#send').click();const first=await waitGate(1);
  await page.locator('#send[data-action="stop"]').click();await edit().waitFor();first.release();
  await edit().click();await page.locator('#message-edit-input').fill('same stopped input');await page.locator('#message-edit-form button[type="submit"]').click();
  const second=await waitGate(2),parts=second.path.split('/');
  phantom={id:'run-released-reservation',project_id:parts[4],session_id:parts[6],state:'created',terminal:false,cancel_requested:false};advertise=true;
  await page.waitForFunction(()=>document.querySelector('#run-status')?.dataset.state==='created');
  await page.locator('#send[data-action="stop"]').click();
  for(let i=0;i<100;i++){const status=(await (await page.request.get(origin+second.path)).json()).data;if(status?.terminal)break;await page.waitForTimeout(40);}
  advertise=false;second.release();
  await page.waitForFunction(()=>document.querySelector('#send')?.dataset.action!=='stop');await edit().waitFor();
  assert(!/正在停止/.test(await page.locator('#run-status').innerText()));
  assert(missingReads>0,'an absent remembered run is confirmed by its exact read endpoint');
  advertise=true;
  await page.waitForTimeout(3000);
  assert.equal(await page.locator('#send').getAttribute('data-action'),'send','a late old list cannot resurrect a confirmed missing run');
  assert.equal(await edit().isVisible(),true);advertise=false;
  assert.equal(await page.locator('.submission-user').count(),1);
  assert.equal(fs.readFileSync(dir+'/runtime.log','utf8').match(/EDIT_MODEL_CALL=/g)?.length||0,0);
  await page.screenshot({path:dir+'/stopped.png',fullPage:true});assert.deepEqual(errors,[]);
  fs.writeFileSync(dir+'/browser.json',JSON.stringify({passed:true,sameText:true,editResendStop:true,missingRunConfirmed:true,noModelCalls:true,errors},null,2));
 }catch(error){for(const gate of gates)gate.release();console.error(error);console.error((await page.locator('body').innerText()).slice(-3500));await page.screenshot({path:dir+'/failure.png',fullPage:true});throw error;}
 finally{await browser.close();}
})();
