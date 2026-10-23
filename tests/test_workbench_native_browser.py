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
 const isolated=await bridge.call('evaluate',{browser:id,script:'typeof window.mdo+":"+typeof window.XsBrowser'});
 check(isolated.value==='undefined:undefined','host SDK leaked into content view');
 await bridge.call('bounds',{browser:id,bounds:{x:280,y:60,width:600,height:600,scale:devicePixelRatio},visible:false});
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
 const image=await perform('screenshot');if(image.data.length<1000)throw Error('SDK screenshot result was empty');
 await mdo.panel.close({id:panel.id});await pause(2500);
 const retained=await perform('snapshot');if(!retained.value.text.includes('SDK to native browser'))throw Error('Closing a panel destroyed the background browser');
 await mdo.browser.close({id:browser.id});
 await fetch(TARGET+'/result',{method:'POST',mode:'no-cors',body:JSON.stringify({passed:true,application:true,screenshot_bytes:image.data.length})});
}catch(error){await fetch(TARGET+'/result',{method:'POST',mode:'no-cors',body:JSON.stringify({passed:false,error:String(error.stack||error)})})}
'''
class Fixture(BaseHTTPRequestHandler):
 def log_message(self,*args):pass
 def do_GET(self):
  data=b'''<!doctype html><title>Browser fixture</title><h1>Native browser fixture</h1><input id=text><button id=apply onclick="document.querySelector('#result').textContent='Result: '+document.querySelector('#text').value">Apply</button><p id=result></p>'''
  if self.path=='/second':data=b'<!doctype html><title>Second</title><h1>Second page</h1>'
  self.send_response(200);self.send_header('Content-Type','text/html');self.send_header('Content-Length',str(len(data)));self.end_headers();self.wfile.write(data)
 def do_POST(self):
  self.server.result=json.loads(self.rfile.read(int(self.headers['Content-Length'])));self.send_response(204);self.end_headers()
def main():
 p=argparse.ArgumentParser();p.add_argument('--host',type=Path,required=True);p.add_argument('--evidence',type=Path,required=True);p.add_argument('--application',action='store_true');args=p.parse_args()
 args.evidence.mkdir(parents=True,exist_ok=True)
 fixture=ThreadingHTTPServer(('127.0.0.1',0),Fixture);fixture.result=None;threading.Thread(target=fixture.serve_forever,daemon=True).start()
 with tempfile.TemporaryDirectory(dir=ROOT/'.build',prefix='workbench-native-') as directory:
  base=Path(directory);port=free_port();config=write_site(base,port)
  value=json.loads(config.read_text(encoding='utf-8'));service=value['services'][0];service['class']='app'
  service['window']=json.loads((ROOT/'app/xs.json').read_text(encoding='utf-8'))['services'][0]['window'];service['window']['title']='Mdo native browser QA';service['window']['install_fallback']='headless'
  config.write_text(json.dumps(value),encoding='utf-8')
  target=f'http://127.0.0.1:{fixture.server_port}'
  script='<script type="module">const TARGET='+json.dumps(target)+';'+(APPLICATION_SCRIPT if args.application else SCRIPT)+'</script>'
  page=(ROOT/'app/web/index.html').read_text(encoding='utf-8').replace('</body>',script+'</body>') if args.application else '<!doctype html><meta charset=utf-8><h1>Mdo browser QA</h1>'+script
  (base/'web/index.html').write_text(page,encoding='utf-8')
  with (args.evidence/'native-host.log').open('wb') as log:
   process=subprocess.Popen([str(args.host.resolve()),str(config),'--','--home',str(base/'home')],cwd=base,stdout=log,stderr=log)
   try:
    wait_ready(port,process);deadline=time.monotonic()+90
    while fixture.result is None and time.monotonic()<deadline and process.poll() is None:time.sleep(.1)
    result=fixture.result or {'passed':False,'error':'Native browser did not return a result'}
    (args.evidence/'native-result.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
    assert result['passed'],result;print('Actual WebView2 navigation, input, screenshot, takeover, back and SDK isolation: PASS')
   finally:
    process.terminate();process.wait(timeout=15);time.sleep(1)
 fixture.shutdown();fixture.server_close()
if __name__=='__main__':main()
