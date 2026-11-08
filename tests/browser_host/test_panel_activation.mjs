import assert from 'node:assert/strict';
import test from 'node:test';
import {panelActivationRequested} from '../../app/web/js/features/workbench/panel-activation.js';

test('an existing keyed panel can be activated again without a content edit',()=>{
  const original={reveal:'activate',reveal_revision:1,revision:7};
  assert.equal(panelActivationRequested({...original,reveal_revision:2},original),true);
});
test('routine runtime reports and content updates do not steal tab selection',()=>{
  const original={reveal:'activate',reveal_revision:2,revision:7};
  assert.equal(panelActivationRequested({...original,runtime:{visible:false}},original),false);
  assert.equal(panelActivationRequested({...original,revision:8},original),false);
});
test('legacy mounted panels accept their first retained activation request',()=>{
  assert.equal(panelActivationRequested({reveal:'activate',reveal_revision:1},{reveal:'activate'}),true);
  assert.equal(panelActivationRequested({reveal:'activate'},{reveal:'activate'}),false);
  assert.equal(panelActivationRequested({reveal:'activate'},undefined),true);
});
test('background reuse and invalid activation metadata cannot activate a mounted tab',()=>{
  assert.equal(panelActivationRequested({reveal:'background',reveal_revision:3},{reveal_revision:2}),false);
  for(const value of [undefined,null,'3',0,-1,1.5,NaN,Infinity,2**53])
    assert.equal(panelActivationRequested({reveal:'activate',reveal_revision:value},{reveal_revision:2}),false);
});
test('an older activation snapshot does not replay a consumed focus request',()=>{
  assert.equal(panelActivationRequested({reveal:'activate',reveal_revision:1},{reveal_revision:2}),false);
});
