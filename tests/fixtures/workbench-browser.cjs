const {chromium}=require(process.env.MDO_PLAYWRIGHT_PATH || 'playwright');
const assert=require('node:assert/strict');
(async()=>{
 const origin=process.argv[2],evidence=process.argv[3],browser=await chromium.launch({headless:true,channel:'msedge'});
 try{
  const page=await browser.newPage({viewport:{width:1440,height:1000},locale:'zh-CN'}),errors=[];
  page.on('pageerror',error=>errors.push(String(error)));
  await page.route('**/*',route=>new URL(route.request().url()).hostname==='127.0.0.1'?route.continue():route.abort());
  await page.goto(origin+'/');await page.waitForFunction(()=>Boolean(window.mdo));
  await page.locator('#new-workbench-tab').click();await page.locator('.workbench:not([hidden])').waitFor();
  await page.getByRole('heading',{name:'新标签页',exact:true}).waitFor();
  const created=await page.evaluate(async()=>{
    const resource=await mdo.call('resource.put',{title:'Plan from SDK',kind:'plan',revision:0,value:{items:[{text:'Native and JS share this plan',done:true}]}});
    const first=await mdo.ui.newTab();
    mdo.ui.registerPanel('qa.document',{placement:'right',title:'Proposal',renderer:'document'});
    const document=await mdo.call('resource.put',{title:'方案',kind:'document',revision:0,value:'# Architecture\n\n<script>window.PWNED=true</script>'});
    const handle=await mdo.ui.openPanel('qa.document',{key:'main',resource:{id:document.id}});
    const repeat=await mdo.ui.openPanel('qa.document',{key:'main',resource:{id:document.id}});
    if(handle.id!==repeat.id)throw Error('Duplicate resource opened another tab');
    window.workbenchFixture={resource:document,handle,plan:resource};return {id:handle.id};
  });
  await page.getByRole('heading',{name:'Architecture',exact:true}).waitFor();
  assert.equal(await page.evaluate(()=>window.PWNED),undefined);
  await page.evaluate(async()=>{
    const {resource}=window.workbenchFixture;
    await mdo.call('resource.put',{...resource,revision:resource.revision,value:'# Updated from C-compatible service'});
  });
  await page.getByRole('heading',{name:'Updated from C-compatible service',exact:true}).waitFor({timeout:10000});
  const scope=await page.evaluate(async()=>{
    const captures=[];
    mdo.ui.registerPanel('qa.custom',{placement:'right',title:'Custom',mount(root,{context,sdk,signal}){
      root.textContent='Custom view';captures.push(context);window.boundPanelSdk=sdk;
      return ()=>{window.disposedPanels=(window.disposedPanels||0)+1;};
    }});
    const handle=await mdo.ui.openPanel('qa.custom',{key:'main'});window.customHandle=handle;
    return captures[0];
  });
  await page.getByText('Custom view',{exact:true}).waitFor();
  assert.equal(scope.project,'default');
  assert.equal(await page.getByRole('button',{name:'并排',exact:true}).count(),0);
  await page.getByRole('button',{name:'工作区更多操作',exact:true}).click();
  await page.getByRole('menuitemcheckbox',{name:'并排',exact:true}).click();assert.equal(await page.locator('.wb-content.wb-split').count(),1);
  await page.getByRole('button',{name:'工作区更多操作',exact:true}).click();
  await page.keyboard.press('End');assert.equal(await page.evaluate(()=>document.activeElement.textContent),'收起');
  await page.keyboard.press('Escape');assert.equal(await page.getByRole('menu').count(),0);
  await page.getByRole('button',{name:'工作区更多操作',exact:true}).click();
  await page.getByRole('menuitem',{name:'收起',exact:true}).click();assert.equal(await page.locator('.workbench').isVisible(),false);
  await page.evaluate(()=>window.customHandle.reveal());await page.locator('.workbench:not([hidden])').waitFor();
  await page.evaluate(()=>window.customHandle.close());assert.equal(await page.evaluate(()=>window.disposedPanels),1);
  await page.evaluate(async()=>{
    const browser=await mdo.browser.create({client:mdo.clientId});
    window.browserFixture=await mdo.panel.open({client:mdo.clientId,type:'mdo.browser',resource:{id:browser.id},reveal:'activate'});
  });
  await page.locator('.wb-browser-toolbar').waitFor();
  await page.getByText('完整浏览需要在 mdo 客户端打开，当前窗口没有原生浏览器后端。',{exact:true}).waitFor();
  const address=await page.locator('.wb-browser-toolbar input').boundingBox();assert.ok(address.width>120);
  await page.screenshot({path:evidence+'/browser-controls.png'});
  await page.evaluate(()=>mdo.panel.close({id:window.browserFixture.id}));
  await page.evaluate(async()=>{
    const frame=await mdo.call('stream.open',{capacity:65536}),input=await mdo.call('stream.open',{capacity:65536});
    const image=document.createElement('canvas');image.width=2;image.height=2;image.getContext('2d').fillStyle='rgb(255,0,0)';image.getContext('2d').fillRect(0,0,2,2);
    const png=Uint8Array.from(atob(image.toDataURL('image/png').split(',')[1]),c=>c.charCodeAt(0)),packet=new Uint8Array(png.length+4);
    new DataView(packet.buffer).setUint32(0,png.length,true);packet.set(png,4);
    await mdo.call('stream.write',{handle:frame.handle,base64:btoa(String.fromCharCode(...packet))});await mdo.call('stream.close',{handle:frame.handle});
    mdo.ui.registerPanel('qa.surface',{placement:'right',title:'Surface',renderer:'surface'});
    window.surfaceFixture={frame,input,handle:await mdo.ui.openPanel('qa.surface',{params:{stream:frame.handle,input_stream:input.handle,width:2,height:2}})};
  });
  await page.waitForFunction(()=>[...document.querySelectorAll('.wb-surface')].some(canvas=>canvas.getContext('2d').getImageData(0,0,1,1).data[0]===255));
  await page.getByRole('button',{name:'开始控制',exact:true}).click();await page.locator('.wb-surface').click();await page.keyboard.press('KeyA');
  await page.getByRole('button',{name:'退出控制',exact:true}).click();
  await page.evaluate(async()=>{
    const {frame,input,handle}=window.surfaceFixture;let text='';
    for(let i=0;i<40;i++){
      try{text+=(await mdo.call('stream.read',{handle:input.handle,limit:16384})).text||'';}catch(error){if(error.code!=='sdk_stream_would_block')throw error;}
      if(text.includes('release_all'))break;await new Promise(resolve=>setTimeout(resolve,50));
    }
    const events=text.trim().split('\n').map(JSON.parse);
    for(const kind of ['acquire','pointerdown','pointerup','keydown','keyup','release_all'])if(!events.some(event=>event.kind===kind))throw Error('Missing surface input '+kind);
    if(!events.every((event,index)=>!index||event.sequence>events[index-1].sequence))throw Error('Input sequence is not monotonic');
    await handle.close();await mdo.call('stream.release',{handle:frame.handle});await mdo.call('stream.release',{handle:input.handle});
  });
  await page.waitForTimeout(500);await page.screenshot({path:evidence+'/desktop.png'});
  await page.reload();await page.waitForFunction(()=>Boolean(window.mdo));
  await page.locator('#new-workbench-tab').click();await page.locator('.workbench:not([hidden])').waitFor();
  await page.setViewportSize({width:390,height:844});await page.screenshot({path:evidence+'/mobile.png'});
  const box=await page.locator('.workbench').boundingBox();assert.ok(box.width>300&&box.width<=391&&box.x>=-1&&box.x<90,JSON.stringify(box));
  await page.getByRole('button',{name:'工作区更多操作',exact:true}).click();
  await page.getByRole('menuitem',{name:'收起',exact:true}).click();assert.equal(await page.locator('#prompt').isVisible(),true);
  assert.deepEqual(errors,[]);console.log('Workbench real browser desktop/mobile, plugin lifecycle, resource refresh and restore: PASS');
 }finally{await browser.close();}
})().catch(error=>{console.error(error);process.exitCode=1;});
