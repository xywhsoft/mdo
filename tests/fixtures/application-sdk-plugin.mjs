export async function activate(sdk) {
  const fixture=globalThis.applicationSdkFixture;fixture.sdk=sdk;
  sdk.ui.registerPanel('qa.fixture.panel',{render:async(root,{sdk})=>{await sdk.call('storage.write',{path:'test.json',text:'panel'});}});
  sdk.ui.registerMenu('qa.fixture.menu',{execute:({sdk})=>sdk.call('storage.write',{path:'test.json',text:'menu'})});
  sdk.ui.registerRenderer('qa.fixture.renderer',async(root,value,{sdk})=>{await sdk.call('storage.write',{path:'test.json',text:'renderer'});});
  sdk.ui.registerKeybinding('qa.fixture.key',{code:'KeyK',mod:true,execute:({sdk})=>sdk.call('storage.write',{path:'test.json',text:'keybinding'})});
  fixture.attempted=true;
  if(fixture.fail)throw Error('Expected activation failure');
  await sdk.call('storage.write',{path:'test.json',text:'activate'});fixture.ready=true;
}
