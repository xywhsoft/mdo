const {chromium}=require(process.env.MDO_PLAYWRIGHT_PATH||'playwright');
const assert=require('node:assert/strict'),fs=require('node:fs');
(async()=>{
 const origin=process.argv[2],dir=process.argv[3],errors=[],prepare=process.env.MDO_REPEAT_PREPARE==='1';
 const browser=await chromium.launch({headless:true,channel:'msedge'});
 const page=await browser.newPage({viewport:{width:1340,height:960},locale:'zh-CN'});
 page.setDefaultTimeout(15000);page.on('pageerror',e=>errors.push(String(e)));
 const modelCount=()=>fs.readFileSync(dir+'/runtime.log','utf8').match(prepare?/PREP_BARRIER_ENTERED=1/g:/EDIT_MODEL_CALL=/g)?.length||0;
 async function model(n){for(let i=0;i<250&&modelCount()<n;i++)await page.waitForTimeout(40);
  if(prepare){assert(modelCount()>=n);await page.waitForFunction(()=>['created','running'].includes(document.querySelector('#run-status')?.dataset.state));}
  else assert.equal(modelCount(),n);}
 async function stopped(){await page.waitForFunction(()=>document.querySelector('#send')?.dataset.action!=='stop');
  await page.locator('#timeline [data-kind="user"]').last().getByRole('button',{name:'编辑此消息并重新发送',exact:true}).waitFor();
  assert(!/正在停止/.test(await page.locator('#run-status').innerText()));}
 try{
  await page.route('**/*',route=>new URL(route.request().url()).hostname==='127.0.0.1'?route.continue():route.abort());
  await page.goto(origin+'/#/projects/edit-qa/sessions/'+(prepare?'accepted-prep':'stopped-edit'));await page.waitForFunction(()=>Boolean(window.mdo));
  const dismiss=page.getByRole('button',{name:'不再提示',exact:true});if(await dismiss.isVisible())await dismiss.click();
  await page.locator('#prompt').fill('repeat stop');await page.locator('#send').click();await model(1);
  await page.locator('#send[data-action="stop"]').click();await stopped();
  await page.locator('#timeline [data-kind="user"]').last().getByRole('button',{name:'编辑此消息并重新发送',exact:true}).click();
  await page.locator('#message-edit-input').fill('repeat stop');await page.locator('#message-edit-form button[type="submit"]').click();await model(2);
  await page.locator('#send[data-action="stop"]').click();await stopped();
  await page.locator('#prompt').fill('repeat stop');await page.locator('#send').click();await model(3);
  await page.locator('#send[data-action="stop"]').click();await stopped();
  assert.equal(await page.locator('#timeline [data-kind="user"]').count(),2);
  assert.equal(await page.locator('#timeline [data-kind="user"] button[aria-label="编辑此消息并重新发送"]').count(),2);
  await page.reload();await page.waitForFunction(()=>Boolean(window.mdo));await stopped();
  if(!prepare)assert.equal(modelCount(),3);else assert.equal(fs.readFileSync(dir+'/runtime.log','utf8').match(/EDIT_MODEL_CALL=/g)?.length||0,0);assert.deepEqual(errors,[]);
  await page.screenshot({path:dir+'/stopped.png',fullPage:true});
  fs.writeFileSync(dir+'/browser.json',JSON.stringify({passed:true,preparation:prepare,editResendStop:true,sequentialStop:true,sameText:true,noDuplicateModelCalls:true,reload:true,errors},null,2));
 }catch(error){console.error(error);console.error((await page.locator('body').innerText()).slice(-3500));await page.screenshot({path:dir+'/failure.png',fullPage:true});throw error;}
 finally{await browser.close();}
})();
