"""Actual WebView2 content environment and main-document bridge regression."""
import argparse,json,os,subprocess,tempfile,threading,time
from pathlib import Path
from http.server import BaseHTTPRequestHandler,ThreadingHTTPServer
from test_api_runtime import ROOT,write_site,free_port,wait_ready

SCRIPT=r'''
import {createNativeBrowserBridge} from '/js/features/workbench/native-browser.js';
const bridge=createNativeBrowserBridge(),id='qa-native-browser';
const pause=ms=>new Promise(resolve=>setTimeout(resolve,ms));
const check=(value,message)=>{if(!value)throw Error(message)};
try{
 const capabilities=await bridge.call('capabilities');check(capabilities.native,'native backend unavailable');
 await bridge.call('create',{browser:id,url:TARGET+'/page',bounds:{x:280,y:60,width:600,height:600,scale:devicePixelRatio}});
 let snapshot;
 for(let i=0;i<100;i++){snapshot=await bridge.call('snapshot',{browser:id});if(snapshot.value?.text?.includes('Native browser fixture'))break;await pause(100);}
 check(snapshot.value.text.includes('Native browser fixture'),'navigation did not load fixture');
 await bridge.call('type',{browser:id,selector:'#text',text:'C and JS'});
 await bridge.call('click',{browser:id,selector:'#apply'});
 snapshot=await bridge.call('snapshot',{browser:id});check(snapshot.value.text.includes('Result: C and JS'),'input and click failed');
 const screenshot=await bridge.call('screenshot',{browser:id});check(screenshot.data.length>1000,'screenshot is empty');
 await bridge.call('takeover',{browser:id});
 let denied=false;try{await bridge.call('click',{browser:id,selector:'#apply',agent:true})}catch{denied=true}
 check(denied,'manual takeover did not block agent input');await bridge.call('release',{browser:id});
 await bridge.call('navigate',{browser:id,url:TARGET+'/second'});
 for(let i=0;i<100;i++){const state=await bridge.call('state',{browser:id});if(state.url.endsWith('/second'))break;await pause(100);}
 await bridge.call('back',{browser:id});
 for(let i=0;i<100;i++){const state=await bridge.call('state',{browser:id});if(state.url.endsWith('/page'))break;await pause(100);}
 const state=await bridge.call('state',{browser:id});check(state.url.endsWith('/page'),'back navigation failed');
 check(state.viewport_attached===true&&state.viewport_topmost===true,'content is not hosted above the main WebView in the UI widget');
 check(state.controller_bounds.x===0&&state.controller_bounds.y===0&&state.controller_bounds.width===Math.round(600*devicePixelRatio),'content controller is not local to its viewport');
 const isolated=await bridge.call('evaluate',{browser:id,script:'typeof window.mdo+":"+typeof window.XsBrowser'});
 check(isolated.value==='undefined:undefined','host SDK leaked into content view');
 await bridge.call('bounds',{browser:id,bounds:{x:280,y:60,width:600,height:600,scale:devicePixelRatio},visible:false});
 const hidden=await bridge.call('state',{browser:id});check(hidden.visible===false,'native visibility was not applied');
 await bridge.call('create',{browser:id,url:TARGET+'/page',bounds:{x:290,y:70,width:500,height:400,scale:devicePixelRatio},visible:true});
 const attached=await bridge.call('state',{browser:id});check(attached.visible===true&&attached.bounds.width===Math.round(500*devicePixelRatio),'reattaching did not restore native bounds and visibility');
 check(attached.viewport_attached&&attached.viewport_topmost&&attached.controller_bounds.x===0&&attached.controller_bounds.y===0&&attached.controller_bounds.width===attached.bounds.width,'reattaching lost the native viewport hierarchy');
 await bridge.call('navigate',{browser:id,url:TARGET+'/disconnect'});
 let failure;for(let i=0;i<100;i++){await pause(100);failure=await bridge.call('state',{browser:id});if(failure.error)break;}
 check(failure.error&&typeof failure.error_code==='number'&&!failure.loading,'navigation failure lost its concrete native error');
 await bridge.call('close',{browser:id});
 await fetch(TARGET+'/result',{method:'POST',mode:'no-cors',body:JSON.stringify({passed:true,screenshot_bytes:screenshot.data.length,capabilities})});
}catch(error){await fetch(TARGET+'/result',{method:'POST',mode:'no-cors',body:JSON.stringify({passed:false,error:String(error.stack||error)})})}
finally{bridge.dispose()}
'''
APPLICATION_SCRIPT=r'''
import {allowsApiWrite} from '/js/api/client.js';
const pause=ms=>new Promise(resolve=>setTimeout(resolve,ms));
try{
 for(let i=0;i<300&&!window.mdo;i++)await pause(100);
 if(!window.mdo)throw Error('Application SDK did not initialize');
 for(let i=0;i<300&&!allowsApiWrite('/application/call',{method:'POST'});i++)await pause(100);
 const client=mdo.clientId;
 await mdo.call('client.hello',{client,context:mdo.context()});
 await mdo.ui.newTab();
 const browser=await mdo.browser.create({client,url:TARGET+'/page'});
 const perform=async(command,params={})=>{
  const receipt=await mdo.browser.command({id:browser.id,command,...params});
  for(let i=0;i<300;i++){
   const result=await mdo.browser.result({id:receipt.id});
   if(result.state==='succeeded'){await mdo.browser.release({id:receipt.id});return result.result;}
   if(['failed','cancelled'].includes(result.state))throw Error(result.error||JSON.stringify(result));
   await pause(100);
  }throw Error('UI receipt timed out');
 };
 await perform('wait',{selector:'#apply',timeout_ms:10000});
 await perform('type',{selector:'#text',text:'SDK to native browser'});await perform('click',{selector:'#apply'});
 const snapshot=await perform('snapshot');if(!snapshot.value.text.includes('SDK to native browser'))throw Error('SDK command did not reach native browser');
 const panel=await mdo.panel.open({client,type:'mdo.browser',key:'native-sdk',resource:{id:browser.id},reveal:'activate'});
 let mounted=false;for(let i=0;i<100;i++){const state=await mdo.panel.get({id:panel.id});if(state.runtime?.visible){mounted=true;break;}await pause(100);}
 if(!mounted)throw Error('Native browser did not attach to the right panel');
 const {createNativeBrowserBridge}=await import('/js/features/workbench/native-browser.js');const bridge=createNativeBrowserBridge();
 const nativeState=()=>bridge.call('state',{browser:browser.id});
 for(let i=0;i<50&&!(await nativeState()).visible;i++)await pause(100);
 if(!(await nativeState()).visible)throw Error('Background browser stayed invisible after opening its panel');
 const placed=await nativeState();if(!placed.viewport_attached||!placed.viewport_topmost||placed.controller_bounds.x!==0||placed.controller_bounds.y!==0||placed.controller_bounds.width!==placed.bounds.width)throw Error('Loaded content is behind the main WebView or outside its viewport');
 const panelIds=async()=>JSON.stringify((await mdo.panel.list()).map(row=>row.id).sort()),beforeToggle=await panelIds();
 const opener=document.querySelector('#new-workbench-tab');opener.click();
 for(let i=0;i<50&&(await nativeState()).visible;i++)await pause(100);
 if((await nativeState()).visible||opener.getAttribute('aria-expanded')!=='false')throw Error('Sidebar toggle did not hide native content');
 if(await panelIds()!==beforeToggle)throw Error('Hiding the sidebar removed or created a panel');
 opener.click();for(let i=0;i<50&&!(await nativeState()).visible;i++)await pause(100);
 const restored=await nativeState();if(!restored.visible||!restored.viewport_attached||!restored.viewport_topmost)throw Error('Sidebar toggle did not restore the same native viewport');
 if(await panelIds()!==beforeToggle)throw Error('Restoring the sidebar replaced its panels');
 const retainedToggle=await perform('snapshot');if(!retainedToggle.value.text.includes('SDK to native browser'))throw Error('Sidebar toggle lost loaded browser content');
 document.querySelector('.wb-top [aria-label="工作区更多操作"]').click();await pause(150);
 if((await nativeState()).visible)throw Error('Native browser covered the workspace menu');
 document.dispatchEvent(new KeyboardEvent('keydown',{key:'Escape',bubbles:true}));await pause(150);
 if(!(await nativeState()).visible)throw Error('Closing the menu did not restore native content');
 await perform('navigate',{url:TARGET+'/disconnect'});
 let failed=false;for(let i=0;i<100;i++){await pause(100);const row=await mdo.browser.get({id:browser.id});if(row.runtime?.state==='error'){failed=true;break;}}
 if(!failed||!document.querySelector('[data-browser-phase="error"] .wb-browser-message:not([hidden])'))throw Error('Failure was not reported and displayed');
 if((await nativeState()).visible)throw Error('Native error surface covered the readable error page');
 const address=document.querySelector('.wb-browser-toolbar input');address.value=TARGET+'/page';address.form.requestSubmit();
 for(let i=0;i<100;i++){await pause(100);const state=await nativeState();if(state.url===TARGET+'/page'&&!state.loading&&!state.error&&state.visible)break;}
 if(!(await nativeState()).visible)throw Error('Navigation after an error did not restore the page');
 await perform('type',{selector:'#text',text:'SDK to native browser'});await perform('click',{selector:'#apply'});bridge.dispose();
 const image=await perform('screenshot');if(image.data.length<1000)throw Error('SDK screenshot result was empty');
 await mdo.panel.close({id:panel.id});await pause(2500);
 const retained=await perform('snapshot');if(!retained.value.text.includes('SDK to native browser'))throw Error('Closing a panel destroyed the background browser');
 await mdo.browser.close({id:browser.id});
 const path=(await mdo.files.resolve({path:'native-file-qa.txt'})).path;
 await mdo.files.write({path,text:'Native file editor fixture\r\n'});await mdo.ui.openEditor({path,space:'native'});
 let input;for(let i=0;i<100;i++){input=document.querySelector('.wb-file-editor');if(input)break;await pause(100);}
 if(!input)throw Error('Native file editor did not mount');input.value='Native file editor saved\n';input.dispatchEvent(new Event('input',{bubbles:true}));
 document.querySelector('[aria-label="保存文件"]').click();let saved;
 for(let i=0;i<100;i++){await pause(100);saved=await mdo.files.read({path});if(saved.text==='Native file editor saved\r\n')break;}
 if(saved.text!=='Native file editor saved\r\n')throw Error('Native file editing did not preserve CRLF');
 const view=()=>document.querySelector('.wb-files-view:not([hidden])');
 const menu=label=>{view().querySelector('[aria-label="文件更多操作"]').click();const item=[...document.querySelectorAll('[role="menuitem"]')].find(item=>item.textContent===label);if(!item)throw Error('Missing native file action: '+label);item.click();};
 const prompt=window.prompt,confirm=window.confirm;
 try{
  window.prompt=()=> 'native-created.txt';menu('新建文件…');
  for(let i=0;i<100;i++){await pause(100);if(view().querySelector('[aria-label="编辑 native-created.txt"]'))break;}
  input=view().querySelector('[aria-label="编辑 native-created.txt"]');if(!input)throw Error('Native create did not open the new editor');
  input.value='Native renamed draft 🌙\n';input.dispatchEvent(new Event('input',{bubbles:true}));
  window.prompt=()=> 'native-renamed.txt';menu('重命名文件…');
  for(let i=0;i<100;i++){await pause(100);if(view().querySelector('[aria-label="编辑 native-renamed.txt"]'))break;}
  input=view().querySelector('[aria-label="编辑 native-renamed.txt"]');if(input?.value!=='Native renamed draft 🌙\n')throw Error('Native rename lost the working copy');
  view().querySelector('[aria-label="保存文件"]').click();const renamed=path.replace(/[^\\/]+$/,'native-renamed.txt');
  for(let i=0;i<100;i++){await pause(100);saved=await mdo.files.read({path:renamed});if(saved.text==='Native renamed draft 🌙\n')break;}
  if(saved.text!=='Native renamed draft 🌙\n')throw Error('Native renamed editor did not save to its new path');
  window.confirm=()=>true;menu('删除文件…');
  for(let i=0;i<100;i++){await pause(100);if(view().textContent.includes('文件已删除或移动'))break;}
  if(!view().textContent.includes('文件已删除或移动'))throw Error('Native deletion lost its visible missing-file state');
 }finally{window.prompt=prompt;window.confirm=confirm;}
 await fetch(TARGET+'/result',{method:'POST',mode:'no-cors',body:JSON.stringify({passed:true,application:true,file_actions:true,sidebar_toggle:true,screenshot_bytes:image.data.length})});
}catch(error){await fetch(TARGET+'/result',{method:'POST',mode:'no-cors',body:JSON.stringify({passed:false,error:String(error.stack||error)})})}
'''
CHANGES_SCRIPT=r'''
import {allowsApiWrite} from '/js/api/client.js';
const pause=ms=>new Promise(resolve=>setTimeout(resolve,ms));
const until=async predicate=>{for(let i=0;i<200;i++){if(await predicate())return;await pause(100);}throw Error('Native changes UI timed out');};
try{
 await until(()=>window.mdo&&allowsApiWrite('/application/call',{method:'POST'}));
 await mdo.ui.newTab();
 const repository=(await mdo.files.resolve({path:'.',prepare_default:true})).path;
 await mdo.files.mkdir({path:repository});
 const git=async(...args)=>{const result=await mdo.call('process.run',{program:'git',cwd:repository,args:['-c','user.name=QA','-c','user.email=qa@example.invalid','-c','core.hooksPath=',...args],timeout_ms:10000});if(result.exit_code!==0)throw Error(JSON.stringify(result));};
 await git('init','-b','master');await mdo.files.write({path:repository+'/native.js',text:'const answer = 1;\n'});
 await git('add','--all');await git('commit','-m','baseline');
 await mdo.files.write({path:repository+'/native.js',text:'const answer = 2;\n'});await git('add','--all');
 await mdo.files.write({path:repository+'/native.js',text:'const answer = 3;\n'});
 const {api}=await import('/js/api/client.js');
 const session=(await api.post('/sessions',{project_id:'default',title:'Native baseline QA'})).data.id;
 location.hash=`#/projects/default/sessions/${session}`;await until(async()=>(await mdo.call('sdk.context')).session===session);
 const before=(await mdo.files.read({path:repository+'/.git/index'})).base64;
 const panel=await mdo.panel.open({client:mdo.clientId,type:'mdo.changes',key:'native-changes',params:{repository},reveal:'activate'});
 await until(()=>document.querySelector('.wb-changes-view .wb-change-added')?.textContent.includes('answer = 3'));
 const view=document.querySelector('.wb-changes-view'),scope=view.querySelector('.wb-compare-scope');
 scope.value='staged';scope.dispatchEvent(new Event('change'));
 await until(()=>view.querySelector('.wb-change-added')?.textContent.includes('answer = 2'));
 scope.value='unstaged';scope.dispatchEvent(new Event('change'));
 await until(()=>view.querySelector('.wb-change-removed')?.textContent.includes('answer = 2')&&view.querySelector('.wb-change-added')?.textContent.includes('answer = 3'));
 view.querySelector('[aria-label="打开工作区文件"]').click();
 await until(()=>document.querySelector('.wb-files-view:not([hidden]) .wb-source-text')?.textContent.includes('answer = 3'));
 document.querySelector(`.wb-tab[data-panel="${panel.id}"] [role="tab"]`).click();
 await until(()=>!view.hidden);
 if(view.querySelector('.wb-file-tree').hidden)throw Error('Switching tabs unexpectedly hid the file tree');
 if(view.getBoundingClientRect().width<300)throw Error('Changes layout collapsed');
 view.querySelector('[aria-label="变更更多操作"]').click();
 [...document.querySelectorAll('[role="menuitemcheckbox"]')].find(node=>node.textContent==='连续浏览文件').click();
 await until(()=>view.querySelector('.wb-continuous-file .wb-change-added')?.textContent.includes('answer = 3'));
 if(view.querySelectorAll('.wb-continuous-file').length!==1)throw Error('Native continuous comparison did not render its file');
 if(before!==(await mdo.files.read({path:repository+'/.git/index'})).base64)throw Error('Read-only changes modified the index');
 scope.value='session';scope.dispatchEvent(new Event('change'));await until(()=>view.textContent.includes('此比较范围没有变更。'));
 await mdo.files.write({path:repository+'/native.js',text:'const answer = 4;\n'});view.querySelector('[aria-label="刷新变更"]').click();
 await until(()=>view.querySelector('.wb-change-removed')?.textContent.includes('answer = 3')&&view.querySelector('.wb-change-added')?.textContent.includes('answer = 4'));
 if(!view.querySelector('.wb-changes-summary').title.includes('原始工作区字节'))throw Error('Native baseline provenance missing');
 await fetch(TARGET+'/result',{method:'POST',mode:'no-cors',body:JSON.stringify({passed:true,scope:'native WebView2 changes UI',continuous:true,session_baseline:true,width:view.getBoundingClientRect().width,index_unchanged:true})});
}catch(error){await fetch(TARGET+'/result',{method:'POST',mode:'no-cors',body:JSON.stringify({passed:false,error:String(error.stack||error)})})}
'''

DISPLAY_SCRIPT=r'''
import {allowsApiWrite} from '/js/api/client.js';
import {createNativeBrowserBridge} from '/js/features/workbench/native-browser.js';
const pause=ms=>new Promise(resolve=>setTimeout(resolve,ms));
const until=async check=>{for(let i=0;i<300;i++){const value=await check();if(value)return value;await pause(100);}throw Error('Visible browser display timed out');};
try{
 await until(()=>window.mdo&&allowsApiWrite('/application/call',{method:'POST'}));await mdo.ui.newTab();
 const browser=await mdo.browser.create({client:mdo.clientId,url:TARGET+'/paint'});
 await mdo.panel.open({client:mdo.clientId,type:'mdo.browser',key:'visible-viewport',resource:{id:browser.id},reveal:'activate'});
 const bridge=createNativeBrowserBridge();
 const state=await until(async()=>{try{const row=await bridge.call('state',{browser:browser.id});return row.visible&&!row.loading&&row.title==='Visible browser QA'?row:false;}catch{return false;}});
 if(!state.viewport_attached||!state.viewport_topmost||state.controller_bounds.x!==0||state.controller_bounds.y!==0)throw Error('Rendered browser is not above the main UI widget');
 const surface=document.querySelector('.wb-browser-surface').getBoundingClientRect();
 if(state.bounds.width!==Math.round(surface.width*devicePixelRatio)||state.bounds.height!==Math.round(surface.height*devicePixelRatio))throw Error('Native viewport differs from actual sidebar geometry');
 const snapshot=await bridge.call('snapshot',{browser:browser.id});if(!snapshot.value.text.includes('VISIBLE BROWSER QA'))throw Error('Paint fixture was not loaded');
 await fetch(TARGET+'/result',{method:'POST',mode:'no-cors',body:JSON.stringify({passed:true,scope:'native visible sidebar viewport',state,dom:{x:surface.x,y:surface.y,width:surface.width,height:surface.height,scale:devicePixelRatio}})});
 bridge.dispose();
}catch(error){await fetch(TARGET+'/result',{method:'POST',mode:'no-cors',body:JSON.stringify({passed:false,error:String(error.stack||error)})})}
'''

START_SCRIPT=r'''
import {allowsApiWrite} from '/js/api/client.js';
const pause=ms=>new Promise(resolve=>setTimeout(resolve,ms));
const until=async fn=>{for(let i=0;i<200;i++){const value=await fn();if(value)return value;await pause(100);}throw Error('Native start condition timed out')};
try{
 await until(()=>window.mdo&&allowsApiWrite('/application/call',{method:'POST'}));await mdo.call('client.hello',{client:mdo.clientId,context:mdo.context()});
 const start=async()=>{await mdo.ui.newTab();return until(()=>document.querySelector('.wb-start-view:not([hidden])'))};
 const home=await start();const names=[...home.querySelectorAll('.wb-start-tools button')].map(node=>node.getAttribute('aria-label'));
 if(JSON.stringify(names)!==JSON.stringify(['打开浏览器','打开文件','打开终端','打开变更']))throw Error('Default tools differ from four-entry contract');
 home.querySelector('[data-tool="mdo.browser"]').click();await until(()=>document.querySelector('.wb-browser-view:not([hidden]) .wb-browser-toolbar'));
 if((await mdo.panel.list()).some(row=>row.type==='mdo.new-tab'))throw Error('Launching kept the placeholder');
 (await start()).querySelector('[data-tool="mdo.files"]').click();const files=await until(()=>document.querySelector('.wb-files-view:not([hidden])'));
 await pause(300);if(files.querySelector('.wb-tree-body .wb-error'))throw Error('Default files failed in a fresh Home');
 (await start()).querySelector('[data-tool="mdo.terminal"]').click();await until(()=>document.querySelector('.wb-terminal-view:not([hidden]) .xterm-rows'));
 const terminal=(await mdo.terminal.list())[0];if(!terminal||terminal.state!=='running')throw Error('Terminal launcher did not start a PTY');
 (await start()).querySelector('[data-tool="mdo.changes"]').click();await until(()=>document.querySelector('.wb-changes-view:not([hidden])'));
 await mdo.terminal.close({id:terminal.id,epoch:terminal.epoch});await until(async()=>(await mdo.terminal.get({id:terminal.id})).drained);
 await fetch(TARGET+'/result',{method:'POST',mode:'no-cors',body:JSON.stringify({passed:true,scope:'native WebView2 four launchers',tools:names,empty_home:true,terminal_ended:true})});
}catch(error){await fetch(TARGET+'/result',{method:'POST',mode:'no-cors',body:JSON.stringify({passed:false,error:String(error.stack||error)})})}
'''

TERMINAL_SCRIPT=r'''
import {allowsApiWrite} from '/js/api/client.js';
const pause=ms=>new Promise(resolve=>setTimeout(resolve,ms));
const until=async fn=>{for(let i=0;i<200;i++){const value=await fn();if(value)return value;await pause(100);}throw Error('Native terminal condition timed out')};
try{
 await until(()=>window.mdo&&allowsApiWrite('/application/call',{method:'POST'}));
 await mdo.call('client.hello',{client:mdo.clientId,context:mdo.context()});await mdo.ui.newTab();
 const cap=await mdo.terminal.capabilities();
 const terminal=await mdo.terminal.create({key:'native-terminal-qa',args:cap.default_program.toLowerCase().includes('powershell')?['-NoLogo','-NoProfile']:[]});
 if(!terminal.cwd.includes('workbench-native-'))throw Error('Terminal escaped its private Home');
 const panel=await mdo.panel.open({client:mdo.clientId,type:'mdo.terminal',key:'native-terminal',params:{terminal:terminal.id},reveal:'activate'});
 const view=await until(()=>document.querySelector('.wb-terminal-view[data-terminal-input="connected"]'));
 await until(()=>view.querySelector('.xterm-rows')?.textContent.includes('PS '));
 const input=view.querySelector('.xterm-helper-textarea');
 const send=text=>{input.focus();input.value=text;input.dispatchEvent(new InputEvent('input',{data:text,inputType:'insertText',bubbles:true}));input.dispatchEvent(new KeyboardEvent('keydown',{key:'Enter',code:'Enter',keyCode:13,which:13,bubbles:true}));};
 send("[IO.File]::WriteAllText('native-terminal.txt','界面🌙'); Write-Output '原生终端成功'");
 await until(async()=>{try{return(await mdo.files.read({path:'native-terminal.txt'})).text==='界面🌙'}catch{return false}});
 await until(()=>view.querySelector('.xterm-rows')?.textContent.includes('原生终端成功'));
 const row=await mdo.terminal.get({id:terminal.id});
 if(row.columns<2||row.rows<2)throw Error('Native terminal was not sized');
 await mdo.ui.newTab();await pause(200);if((await mdo.terminal.get({id:row.id})).pid!==row.pid)throw Error('Hiding ended the terminal');
 [...document.querySelectorAll('.wb-tab-select')].find(node=>node.textContent==='终端').click();
 await until(()=>!view.hidden&&view.dataset.terminalInput==='connected');
 window.confirm=()=>true;view.querySelector('[aria-label="结束终端"]').click();await until(async()=>(await mdo.terminal.get({id:row.id})).drained);
 await fetch(TARGET+'/result',{method:'POST',mode:'no-cors',body:JSON.stringify({passed:true,scope:'native WebView2 terminal UI',width:view.getBoundingClientRect().width,columns:row.columns,rows:row.rows,unicode:true,ended:true})});
}catch(error){await fetch(TARGET+'/result',{method:'POST',mode:'no-cors',body:JSON.stringify({passed:false,error:String(error.stack||error)})})}
'''

class Fixture(BaseHTTPRequestHandler):
 def log_message(self,*args):pass
 def do_GET(self):
  if self.path=='/disconnect':self.close_connection=True;self.connection.close();return
  data=b'''<!doctype html><title>Browser fixture</title><h1>Native browser fixture</h1><input id=text><button id=apply onclick="document.querySelector('#result').textContent='Result: '+document.querySelector('#text').value">Apply</button><p id=result></p>'''
  if self.path=='/second':data=b'<!doctype html><title>Second</title><h1>Second page</h1>'
  if self.path=='/paint':data=b'<!doctype html><title>Visible browser QA</title><style>body{margin:0;background:rgb(22,167,90);color:white;font:32px sans-serif;padding:30px}h1{font-size:32px}</style><h1>VISIBLE BROWSER QA</h1><p>This green content must be visible in the sidebar.</p>'
  self.send_response(200);self.send_header('Content-Type','text/html');self.send_header('Content-Length',str(len(data)));self.end_headers();self.wfile.write(data)
 def do_POST(self):
  self.server.result=json.loads(self.rfile.read(int(self.headers['Content-Length'])));self.send_response(204);self.end_headers()
def main():
 p=argparse.ArgumentParser();target=p.add_mutually_exclusive_group(required=True);target.add_argument('--host',type=Path);target.add_argument('--packed',type=Path);p.add_argument('--evidence',type=Path,required=True);p.add_argument('--hold',type=int,choices=range(0,61),default=0);modes=p.add_mutually_exclusive_group();modes.add_argument('--application',action='store_true');modes.add_argument('--changes',action='store_true');modes.add_argument('--terminal',action='store_true');modes.add_argument('--start',action='store_true');modes.add_argument('--display',action='store_true');args=p.parse_args()
 args.evidence.mkdir(parents=True,exist_ok=True)
 fixture=ThreadingHTTPServer(('127.0.0.1',0),Fixture);fixture.result=None;threading.Thread(target=fixture.serve_forever,daemon=True).start()
 with tempfile.TemporaryDirectory(dir=ROOT/'.build',prefix='workbench-native-') as directory:
  base=Path(directory)
  if args.packed:
   from test_packed_home_lease import site,release_packed_copies
   base,port=site(base,'site',args.packed.resolve());config=base/'xs.json';command=[str(base/args.packed.name),'--']
   (base/'web').mkdir()
  else:
   port=free_port();config=write_site(base,port);command=[str(args.host.resolve()),str(config),'--']
  value=json.loads(config.read_text(encoding='utf-8'));service=value['services'][0];service['class']='app'
  service['window']=json.loads((ROOT/'app/xs.json').read_text(encoding='utf-8'))['services'][0]['window'];service['window']['title']='Mdo native browser QA';service['window']['install_fallback']='headless'
  config.write_text(json.dumps(value),encoding='utf-8')
  target=f'http://127.0.0.1:{fixture.server_port}'
  script='<script type="module">const TARGET='+json.dumps(target)+';'+(DISPLAY_SCRIPT if args.display else START_SCRIPT if args.start else TERMINAL_SCRIPT if args.terminal else CHANGES_SCRIPT if args.changes else APPLICATION_SCRIPT if args.application else SCRIPT)+'</script>'
  page=(ROOT/'app/web/index.html').read_text(encoding='utf-8').replace('</body>',script+'</body>') if args.application or args.changes or args.terminal or args.start or args.display else '<!doctype html><meta charset=utf-8><h1>Mdo browser QA</h1>'+script
  (base/'web/index.html').write_text(page,encoding='utf-8')
  with (args.evidence/'native-host.log').open('wb') as log:
   environment={key:value for key,value in os.environ.items() if not (args.terminal or args.start) or key.lower()!='psmodulepath'}
   process=subprocess.Popen([*command,'--home',str(base/'home')],cwd=base,env=environment,stdout=log,stderr=log)
   try:
    wait_ready(port,process);deadline=time.monotonic()+90
    while fixture.result is None and time.monotonic()<deadline and process.poll() is None:time.sleep(.1)
    result=fixture.result or {'passed':False,'error':'Native browser did not return a result'}
    (args.evidence/'native-result.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
    assert result['passed'],result;print('Actual WebView2 sidebar parent, topmost viewport and local controller geometry: PASS' if args.display else 'Actual WebView2 four launchers and fresh Home: PASS' if args.start else 'Actual WebView2 terminal rendering, Unicode input, dimensions, hide and end: PASS' if args.terminal else 'Actual WebView2 changes scopes, file location and retained tree, real index unchanged: PASS')
    if args.hold:print('Holding owned QA window for visual inspection',flush=True);time.sleep(args.hold)
   finally:
    process.terminate();process.wait(timeout=15)
    if args.packed:release_packed_copies(base/args.packed.name)
    time.sleep(1)
 fixture.shutdown();fixture.server_close()
if __name__=='__main__':main()
