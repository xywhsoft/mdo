import test from 'node:test';
import assert from 'node:assert/strict';
import {extensionPresentation,extensionTags,compareExtensions} from '../app/web/js/components/extension-presentation.js';
import {createExtensionBrowser} from '../app/web/js/components/extension-browser.js';
import {testDocument} from './helpers/extension-dom.mjs';

test('presentation uses server metadata, preserves explicit defaults and bounds tags',()=>{
  assert.deepEqual(extensionPresentation({sort_order:0,tags:[],serial:4,installation:{sort_order:8,tags:['old']}}),{sort_order:0,sort_id:4,tags:[]});
  assert.deepEqual(extensionPresentation({installation:{sort_order:8,tags:['推荐'],serial:7}}),{sort_order:8,sort_id:7,tags:['推荐']});
  assert.deepEqual(extensionTags([' 推荐 ','推荐','',42,'a\nb','中'.repeat(22)]),['推荐']);
  assert.equal(extensionTags(Array.from({length:30},(_,n)=>String(n))).length,16);
  assert.equal(extensionPresentation({sort_order:2147483648}).sort_order,0);
});
test('rank precedes descending numeric ID; local IDs use a deterministic descending order',()=>{
  const rows=[{sort_order:0,sort_id:999},{sort_order:1000,sort_id:1},{sort_order:900,sort_id:3},{sort_order:900,sort_id:12}];
  assert.deepEqual(rows.sort(compareExtensions).map(r=>r.sort_id),[1,12,3,999]);
  assert.deepEqual(['a','c','b'].map(sort_id=>({sort_id})).sort(compareExtensions).map(r=>r.sort_id),['c','b','a']);
});
test('shared extension list sorts before paging and renders safe capsules after the byline',()=>{
  const previous=globalThis.document;globalThis.document=testDocument();
  try{
    const browser=createExtensionBrowser();
    const rows=Array.from({length:15},(_,n)=>({key:String(n),title:'Tool '+n,byline:'Creator',...extensionPresentation({serial:n,sort_order:n===0?1000:0,tags:n===0?['推荐','<img src=x>']:[]})}));
    browser.setItems(rows,{sort:true,pageKey:'all'});
    const first=browser.root.querySelector('.eb-row');
    assert.equal(first.querySelector('.eb-name').textContent,'Tool 0');
    assert.deepEqual(first.querySelectorAll('.eb-tag').map(t=>t.textContent),['推荐','<img src=x>']);
    assert.equal(first.querySelectorAll('img').length,0);
    const title=first.querySelector('.eb-title');assert(title.children.indexOf(first.querySelector('.eb-tag'))>title.children.indexOf(first.querySelector('.eb-byline')));
    assert.equal(browser.root.querySelectorAll('.eb-name')[1].textContent,'Tool 14');
    browser.destroy();
  }finally{globalThis.document=previous;}
});
