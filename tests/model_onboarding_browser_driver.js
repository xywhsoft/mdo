// Injected by queue_browser_driver.mjs into the real model settings page.
(async()=>{
  const checks=[];
  const until=async(test,label)=>{for(let n=0;n<300;n++){if(await test())return;await new Promise(r=>setTimeout(r,35));}throw new Error(label);};
  const assert=(value,label)=>{if(!value)throw new Error(label);checks.push(label);};
  const button=label=>[...document.querySelectorAll('button')].find(n=>n.textContent===label);
  const click=label=>{const node=button(label);if(!node||node.disabled)throw new Error('Unavailable: '+label);node.click();};
  const input=(name,value)=>{const node=document.querySelector(`[name="${name}"]`);if(!node)throw new Error('Missing input: '+name);node.value=value;node.dispatchEvent(new Event('input',{bubbles:true}));return node;};
  const current=async()=>(await(await fetch('/api/v1/models/config')).json()).data;
  const layout=()=>assert(document.documentElement.scrollWidth<=innerWidth+1,'No horizontal overflow at '+innerWidth);
  try {
    await until(()=>globalThis.__qaReady,'settings ready');
    click('添加供应商');await until(()=>button('Fixture'),'supplier catalog');click('Fixture');
    assert(!document.querySelector('.supplier-connection-advanced').open,'Advanced connection settings start collapsed');
    const groups=[...document.querySelectorAll('.model-proxy-inline h4')].map(n=>n.textContent);
    assert(groups[0].includes('供应商')&&groups[1].includes('新模型'),'Supplier and model proxy settings share one advanced group in order');
    assert(document.querySelector('[name="model_proxy_kind"]').value==='provider','New models inherit supplier proxy');
    input('api-key','browser-fixture-only');click('下一步');
    assert(document.querySelector('[role="tab"][aria-selected="true"]').textContent==='手动填写'&&document.querySelector('textarea'),'Manual input is selected by default');
    assert((await(await fetch(globalThis.__qaSupplier+'/stats')).json()).models===0,'Entering source step makes no model-list request');
    click('自动获取');assert(document.querySelector('.model-source-spinner'),'Clicking automatic immediately displays spinner');
    assert(getComputedStyle(document.querySelector('.model-source-spinner')).animationName!=='none','Loading indicator rotates');
    await until(()=>document.querySelectorAll('[name="discovered-model"]').length===2,'inline discovery list');
    assert(document.querySelectorAll('[name="selected-model"]').length===0,'Model list appears in source step before parameter confirmation');
    if(new URLSearchParams(location.search).get('preview')==='source'){
      layout();document.querySelector('#result').textContent=JSON.stringify({passed:true,checks});return;
    }
    const first=document.querySelector('[role="tab"][aria-selected="true"]');first.focus();first.dispatchEvent(new KeyboardEvent('keydown',{key:'Home',bubbles:true}));
    assert(document.activeElement.textContent==='手动填写','Source tabs support keyboard selection and focus');
    document.activeElement.dispatchEvent(new KeyboardEvent('keydown',{key:'End',bubbles:true}));
    await until(()=>button('下一步')&&!button('下一步').disabled,'automatic discovery after source switch');
    click('下一步');await until(()=>button('添加并验证（2）')&&!button('添加并验证（2）').disabled,'automatic parameter lookup');
    assert(document.querySelector('[name="parameter-0-context_tokens"]').value==='64000','Supplier context metadata reused');
    assert(document.querySelector('[name="parameter-1-context_tokens"]').value==='32000','Missing model context filled from catalog automatically');
    const layer=document.querySelector('.model-setup-layer');
    assert(layer?.matches(':modal')&&!document.querySelector('#panel').contains(layer),'Wizard uses public modal outside background settings');
    const content=layer.querySelector('.mdo-layer-body'),footer=layer.querySelector('.mdo-layer-footer');
    assert(footer.contains(button('添加并验证（2）')),'Actions live in fixed public layer footer');
    if(innerWidth<=600)assert(layer.classList.contains('is-fullscreen'),'Narrow viewport uses full screen layer');
    const editors=[...content.querySelectorAll('.model-parameter-editor')];for(const node of editors)node.open=true;
    content.scrollTop=content.scrollHeight;
    const bounds=footer.getBoundingClientRect(),windowBounds=layer.getBoundingClientRect();
    assert(content.scrollHeight>content.clientHeight&&bounds.bottom<=windowBounds.bottom+1&&bounds.top>=0,'Long model content scrolls while footer remains visible');
    assert(content.scrollWidth<=content.clientWidth+1,'Modal content has no horizontal overflow');
    for(const node of editors)node.open=false;content.scrollTop=0;
    layout();
    if(new URLSearchParams(location.search).has('preview')){
      document.querySelector('#result').textContent=JSON.stringify({passed:true,checks});return;
    }
    const makeDefault=document.querySelector('[name="make-default"]');makeDefault.checked=true;makeDefault.dispatchEvent(new Event('change',{bubbles:true}));
    click('添加并验证（2）');await until(()=>button('添加并验证（1）'),'partial validation failure');
    let saved=await current();
    assert(saved.items.some(n=>n.wire_model==='alpha')&&!saved.items.some(n=>n.wire_model==='beta'),'Only successful model committed');
    assert(document.querySelectorAll('[name="selected-model"]').length===1&&document.querySelector('[name="selected-model"]').value==='beta','Retry page retains only failed model');
    assert(Boolean(document.querySelector('.resource-error')?.textContent),'Failed model displays its error');
    await fetch(globalThis.__qaSupplier+'/allow-beta');
    click('添加并验证（1）');await until(()=>document.querySelectorAll('.supplier-model').length===2,'successful retry');
    assert(!document.querySelector('.model-setup-layer'),'Successful wizard closes and returns to model cards');
    saved=await current();assert(saved.items.filter(n=>n.provider==='fixture').length===2,'Retry completes remaining model');
    const cards=[...document.querySelectorAll('.supplier-model')];
    assert(cards.every(n=>[...n.querySelectorAll('button')].filter(b=>b.textContent==='验证').length===1),'Each card has one verification action');
    assert(cards.every(n=>n.textContent.includes('上下文')&&n.textContent.includes('思考')&&n.querySelector('.model-parameter-link')),'Cards show parameters and a model parameter link');
    assert(!cards.some(n=>/测试回复|测试工具调用|高级编辑/.test(n.textContent)),'Redundant test and advanced-edit buttons removed');
    layout();
    const stats=await(await fetch(globalThis.__qaSupplier+'/stats')).json();
    assert(stats.models===2,'Exactly one model-list request per automatic tab activation');
    assert(stats.calls.filter(c=>c.model==='alpha').length===2,'Successful model never verified again on retry');
    assert(stats.catalog_credentials===false,'Public parameter catalog receives no supplier credential');
    document.querySelector('#result').textContent=JSON.stringify({passed:true,checks,stats});
  }catch(error){document.querySelector('#result').textContent=JSON.stringify({passed:false,checks,error:String(error),body:document.body.innerText.slice(-2000)});}
})();
