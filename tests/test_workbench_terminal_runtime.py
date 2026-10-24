"""Exercise real xrt PTYs in a private Home, without touching user terminals."""
import argparse,base64,json,os,re,subprocess,sys,tempfile,time,uuid
from pathlib import Path
from test_api_runtime import ROOT,write_site,free_port,request,wait_ready
from test_application_sdk_runtime import COMMAND

CHILD=r'''
import os,sys,time,subprocess
print('READY:%s:%s'%(sys.stdin.isatty(),sys.stdout.isatty()),flush=True)
while True:
 try:
  line=input()
  if line=='EXIT':break
  if line=='SIZE':
   size=os.get_terminal_size();print('SIZE:%sx%s'%(size.columns,size.lines),flush=True)
  elif line=='FLOOD':
   for at in range(2200):sys.stdout.write('x'*1000+'\n');sys.stdout.flush()
   print('FLOOD-END',flush=True)
  elif line=='BLOCK':
   if os.name!='nt':
    import tty;tty.setraw(sys.stdin.fileno())
   print('BLOCKING',flush=True);time.sleep(60)
  elif line=='ORPHAN':
   child=subprocess.Popen([sys.executable,'-c','import time; time.sleep(45)','mdo-terminal-orphan-qa'])
   print('ORPHAN:%s'%child.pid,flush=True);break
  else:print('ECHO:'+line,flush=True)
 except KeyboardInterrupt:print('INTERRUPTED',flush=True)
 except EOFError:
  # ReadConsole may report Ctrl+C cancellation as EOF instead of raising
  # KeyboardInterrupt. The fixture retains its input loop so the transport
  # test can verify the control byte without assuming a child's exit policy.
  if os.name=='nt':print('INTERRUPTED-EOF',flush=True)
  else:break
'''

def main():
 p=argparse.ArgumentParser();p.add_argument('--host',type=Path,required=True);p.add_argument('--evidence',type=Path);args=p.parse_args()
 with tempfile.TemporaryDirectory(dir=ROOT/'.build',prefix='workbench-terminal-') as raw:
  base=Path(raw);port=free_port();config=write_site(base,port);home=base/'home';process=None;created=[]
  registry=base/'src/application/registry.c';source=registry.read_text(encoding='utf-8')
  hook='static mdo_result SdkInvoke(const mdo_sdk_context_v1* C,cstr Method,const xvalue* P,xvalue** R,mdo_error_v1* E){'
  assert source.count(hook)==1
  registry.write_text(source.replace(hook,hook+'\nif(!strcmp(Method,"qa.terminal-stop")){MdoApplicationTerminalsStop();*R=xrtValueBool(true);return MDO_RESULT_OK;}\nif(!strcmp(Method,"qa.terminal-project-available")){xwork_error X;MdoProjectLease* L=MdoProjectLeaseAcquire(C->ProjectId,MDO_PROJECT_LEASE_EXCLUSIVE,&X);*R=xrtValueBool(L!=NULL);MdoProjectLeaseRelease(L);return MDO_RESULT_OK;}'),encoding='utf-8')
  terminal_source=base/'src/application/terminals.c';text=terminal_source.read_text(encoding='utf-8');hook='    const xprocessenv Env[]='
  assert text.count(hook)==1
  terminal_source.write_text(text.replace(hook,'    if(Title&&!strcmp(Title,"delayed-creation"))xrtSleep(350);\n'+hook),encoding='utf-8')
  (home/'commands').mkdir(parents=True)
  csource=COMMAND.replace('mdo_result Code=mdoCall(S,Method,V->At(A,1),R,&E);','mdo_result Code=!strcmp(Method,"terminal.capabilities")?mdoTerminalCapabilities(S,V->At(A,1),R,&E):mdoCall(S,Method,V->At(A,1),R,&E);')
  (home/'commands/sdk.c').write_text('/* mdo-command {"id":"sdk","description":"Terminal fixture"} */\n#include <string.h>\n#include <mdo/workbench.h>\n'+csource,encoding='utf-8')
  log=(base/'host.log').open('wb')
  def start():
   nonlocal process
   environment={key:value for key,value in os.environ.items() if os.name!='nt' or key.lower()!='psmodulepath'}
   process=subprocess.Popen([str(args.host.resolve()),str(config),'--','--home',str(home)],cwd=base,env={**environment,'USE_WEBVIEW':'0'},stdout=log,stderr=log)
   wait_ready(port,process)
  def stop():
   if process and process.poll() is None:
    process.terminate()
    try:process.wait(timeout=10)
    except subprocess.TimeoutExpired:
     process.kill();process.wait(timeout=5);raise AssertionError('Owned test server failed to drain during graceful shutdown; forced cleanup completed')
  def call(method,params=None,expected=200,owner='qa.terminal',session=''):
   code,_,body=request(port,'POST','/api/v1/application/call',body=json.dumps({'method':method,'params':params or {},'context':{'project':'default','session':session,'extension':owner}}).encode(),headers={'Content-Type':'application/json'})
   value=json.loads(body);assert code==expected,(method,code,value)
   return value['data'] if expected==200 else value['error']
  def until(predicate,timeout=12):
   deadline=time.monotonic()+timeout
   while time.monotonic()<deadline:
    value=predicate()
    if value:return value
    time.sleep(.03)
   raise AssertionError('Terminal condition timed out')
  def native(method,params=None):
   identity='cmd-'+uuid.uuid4().hex
   code,_,body=request(port,'POST','/api/v1/command-invocations?project=default',body=json.dumps({'invocation_id':identity,'command_id':'sdk','raw_arguments':json.dumps([method,params])}).encode(),headers={'Content-Type':'application/json'});assert code==202,(code,body)
   def done():
    code,_,body=request(port,'GET','/api/v1/command-invocations/'+identity);value=json.loads(body)['data']
    if value['terminal']:assert value['state']=='succeeded',value;return value['result']
   return until(done)
  try:
   start();cap=call('terminal.capabilities');assert cap['supported'],cap
   assert native('terminal.capabilities')['supported']
   if os.name=='nt':
    defaults=call('terminal.create',{'key':'default-'+uuid.uuid4().hex,'args':['-NoLogo','-NoProfile'] if 'powershell' in cap['default_program'].lower() else ['/d','/q']})
    assert defaults['state']=='running' and Path(defaults['program']).is_absolute(),defaults
    if 'powershell' in defaults['program'].lower():
     shell_path=Path(defaults['cwd']);control=call('terminal.attach',{'id':defaults['id'],'client':'qa-powershell','control':True})
     identity={key:control[key] for key in ['id','epoch','control_token','input_epoch']};identity['client']='qa-powershell';seq=0
     def shell_write(text):
      nonlocal seq
      seq+=1;call('terminal.write',{**identity,'sequence':seq,'text':text})
     # Wait for startup rather than feeding a profile/authenticode prompt.
     deadline=time.monotonic()+10
     while time.monotonic()<deadline:
      value=call('terminal.read',{'id':defaults['id'],'cursor':0,'limit':65536,'wait_ms':100})
      if b'PS ' in base64.b64decode(value['base64']):break
     shell_write("Start-Sleep -Seconds 30; [IO.File]::WriteAllText('ctrl-c-must-not-run.txt','bad')\r");time.sleep(.5);shell_write('\x03');time.sleep(.2)
     shell_write("[IO.File]::WriteAllText('ctrl-c-interrupted.txt','ok')\r")
     until(lambda:(shell_path/'ctrl-c-interrupted.txt').exists())
     assert not (shell_path/'ctrl-c-must-not-run.txt').exists(),'Ctrl+C failed to cancel PowerShell'
    call('terminal.close',{'id':defaults['id'],'epoch':defaults['epoch']});until(lambda:call('terminal.get',{'id':defaults['id']})['drained']);call('terminal.release',{'id':defaults['id']})
   settings={'key':'qa-'+uuid.uuid4().hex,'program':sys.executable,'args':['-u','-c',CHILD],'columns':80,'rows':24}
   ccontext=native('sdk.context');cterminal=native('terminal.create',{**settings,'key':'native-'+uuid.uuid4().hex})
   assert call('terminal.get',{'id':cterminal['id']},owner=ccontext['extension'])['id']==cterminal['id']
   call('terminal.close',{'id':cterminal['id'],'epoch':cterminal['epoch']},owner=ccontext['extension'])
   until(lambda:call('terminal.get',{'id':cterminal['id']},owner=ccontext['extension'])['drained']);call('terminal.release',{'id':cterminal['id']},owner=ccontext['extension'])
   terminal=call('terminal.create',settings);created.append(terminal);identity=terminal['id'];epoch=terminal['epoch'];cursor=0;output=b''
   assert terminal['state']=='running',terminal
   assert terminal['title']=='终端',terminal
   assert call('qa.terminal-project-available') is False,'running PTY must retain its project lease'
   assert call('terminal.create',settings)['id']==identity
   call('terminal.create',{**settings,'rows':25},409)
   call('terminal.get',{'id':identity},403,owner='another.extension')
   call('terminal.get',{'id':identity},409,session='different')
   def read_until(needle,timeout=12):
    nonlocal cursor,output
    def check():
     nonlocal cursor,output
     chunk=call('terminal.read',{'id':identity,'epoch':epoch,'cursor':cursor,'limit':65536,'wait_ms':100});cursor=chunk['next'];output=(output+base64.b64decode(chunk['base64']))[-3000000:]
     return chunk if needle in output else False
    return until(check,timeout)
   read_until(b'READY:True:True')
   lease=call('terminal.attach',{'id':identity,'client':'qa-one','control':True});sequence=0
   first=call('terminal.read',{'id':identity,'cursor':0,'limit':32});second=call('terminal.read',{'id':identity,'cursor':0,'limit':32})
   assert first['base64']==second['base64'] and first['next']==second['next'],'one view consumed another view output'
   pending_read=call('operation.start',{'method':'terminal.read','params':{'id':identity,'epoch':epoch,'cursor':call('terminal.get',{'id':identity})['cursor'],'wait_ms':1000}})
   call('operation.cancel',{'id':pending_read['id']});until(lambda:call('operation.get',{'id':pending_read['id']})['terminal']);call('operation.release',{'id':pending_read['id']})
   assert call('terminal.get',{'id':identity})['state']=='running','cancelling a view read terminated the PTY'
   def control():return {'id':identity,'epoch':epoch,'client':'qa-one','control_token':lease['control_token'],'input_epoch':lease['input_epoch']}
   def send(text):
    nonlocal sequence
    sequence+=1;return call('terminal.write',{**control(),'sequence':sequence,'text':text})
   call('terminal.attach',{'id':identity,'client':'qa-two','control':True},409)
   assert 'control_token' not in call('terminal.attach',{'id':identity,'client':'qa-two'})
   send('once\r');read_until(b'ECHO:once')
   stored=(home/'data/application/terminals.json').read_text(encoding='utf-8')
   assert all(value not in stored for value in ['FLOOD-END','control_token','once','args','base64',lease['control_token']]),'terminal metadata retained process argv or input/control data'
   duplicate=call('terminal.write',{**control(),'sequence':sequence,'text':'once\r'});assert duplicate['duplicate']
   call('terminal.write',{**control(),'sequence':sequence,'text':'different\r'},409)
   call('terminal.write',{**control(),'sequence':sequence+2,'text':'out-of-order\r'},409)
   call('terminal.resize',{**control(),'columns':100,'rows':35});send('SIZE\r');read_until(b'SIZE:100x35')
   send('你好🌙\r');read_until('ECHO:你好🌙'.encode())
   send('\x03');read_until(b'INTERRUPTED')
   send('FLOOD\r');read_until(b'FLOOD-END',45);gap=call('terminal.read',{'id':identity,'epoch':epoch,'cursor':0,'limit':32})
   assert gap['gap'] and gap['oldest']>0 and gap['retained_bytes']<=cap['output_capacity'],gap
   call('terminal.detach',control());assert call('terminal.get',{'id':identity})['state']=='running'
   call('terminal.write',{**control(),'sequence':sequence+1,'text':'lost lease\r'},409)
   lease=call('terminal.attach',{'id':identity,'client':'qa-one','control':True});sequence=0
   send('after-attach\r');read_until(b'ECHO:after-attach')
   send('EXIT\r');ended=until(lambda:(v if (v:=call('terminal.get',{'id':identity}))['state']=='ended' and v['drained'] else False))
   assert ended['exit_code']==0,ended
   call('terminal.release',{'id':identity});call('terminal.get',{'id':identity},404)
   assert call('qa.terminal-project-available') is True,'ended PTY must release its project lease'
   call('terminal.create',settings,410)
   terminal=call('terminal.create',{**settings,'key':'close-'+uuid.uuid4().hex});created.append(terminal);identity=terminal['id'];cursor=0;output=b'';read_until(b'READY:True:True')
   lease=call('terminal.attach',{'id':identity,'client':'qa-one','control':True});sequence=0;send('BLOCK\r');read_until(b'BLOCKING')
   full=False
   for at in range(60):
    code,_,body=request(port,'POST','/api/v1/application/call',body=json.dumps({'method':'terminal.write','params':{**control(),'sequence':sequence+1,'text':'a'*8192},'context':{'project':'default','extension':'qa.terminal'}}).encode(),headers={'Content-Type':'application/json'})
    if code==429:full=True;break
    assert code==200,body;sequence+=1
   assert full,'a non-reading PTY must eventually backpressure input'
   call('terminal.release',{'id':identity},409)
   call('terminal.close',{'id':identity,'epoch':epoch});until(lambda:(v if (v:=call('terminal.get',{'id':identity}))['state']=='ended' and v['drained'] else False))
   until(lambda:call('terminal.get',{'id':identity})['input_queued']==0);call('terminal.release',{'id':identity})
   terminal=call('terminal.create',{**settings,'key':'orphan-'+uuid.uuid4().hex});created.append(terminal);identity=terminal['id'];cursor=0;output=b'';read_until(b'READY:True:True')
   lease=call('terminal.attach',{'id':identity,'client':'qa-one','control':True});sequence=0;send('ORPHAN\r');read_until(b'ORPHAN:')
   orphan_pid=int(re.search(br'ORPHAN:(\d+)',output).group(1))
   until(lambda:call('terminal.get',{'id':identity})['state']=='ended')
   call('terminal.close',{'id':identity,'epoch':epoch});until(lambda:call('terminal.get',{'id':identity})['drained']);call('terminal.release',{'id':identity})
   if os.name=='nt':
    import ctypes
    from ctypes import wintypes
    kernel=ctypes.WinDLL('kernel32',use_last_error=True);kernel.OpenProcess.argtypes=[wintypes.DWORD,wintypes.BOOL,wintypes.DWORD];kernel.OpenProcess.restype=wintypes.HANDLE
    kernel.WaitForSingleObject.argtypes=[wintypes.HANDLE,wintypes.DWORD];kernel.CloseHandle.argtypes=[wintypes.HANDLE]
    handle=kernel.OpenProcess(0x100000,False,orphan_pid)
    if handle:
     try:assert kernel.WaitForSingleObject(handle,1000)==0,'closed PTY left an owned child process running'
     finally:kernel.CloseHandle(handle)
   else:
    def orphan_ended():
     try:return Path(f'/proc/{orphan_pid}/stat').read_text().split(') ',1)[1][0]=='Z' or b'mdo-terminal-orphan-qa' not in Path(f'/proc/{orphan_pid}/cmdline').read_bytes()
     except FileNotFoundError:return True
    until(orphan_ended)
   terminal=call('terminal.create',{**settings,'key':'restart-'+uuid.uuid4().hex});created.append(terminal)
   process.kill();process.wait(timeout=10);start();restored=call('terminal.get',{'id':terminal['id']});assert restored['state']=='ended' and restored['reason']=='service_restarted' and restored['pid']==0,restored
   assert call('terminal.create',{**settings,'key':restored['key']})['id']==restored['id'],'restart replayed a creation intent'
   call('terminal.read',{'id':restored['id'],'epoch':epoch},410)
   call('terminal.release',{'id':restored['id']})
   marker=base/'must-not-start.txt';pending_key='delayed-'+uuid.uuid4().hex
   pending=call('operation.start',{'method':'terminal.create','params':{'key':pending_key,'title':'delayed-creation','program':sys.executable,'args':['-c','import pathlib,sys; pathlib.Path(sys.argv[1]).write_text("started")',str(marker)]}})
   delayed=until(lambda:next((row for row in call('terminal.list') if row['key']==pending_key),False))
   call('terminal.close',{'id':delayed['id'],'epoch':delayed['epoch']})
   receipt=until(lambda:(row if (row:=call('operation.get',{'id':pending['id']}))['terminal'] else False))
   assert receipt['state']=='cancelled',receipt
   final=call('terminal.get',{'id':delayed['id']});assert final['reason']=='cancelled_before_start' and final['drained'] and not marker.exists(),final
   call('operation.release',{'id':pending['id']});call('terminal.release',{'id':delayed['id']})
   stored=(home/'data/application/terminals.json').read_text(encoding='utf-8')
   assert all(value not in stored for value in ['FLOOD-END','control_token','once','args','base64']),'terminal metadata retained process argv or input/control data'
   old_receipt=next(path for path in (home/'data/application/terminal-receipts').glob('*.json') if json.loads(path.read_text())['id']==created[0]['id'])
   receipt_text=old_receipt.read_text();old_receipt.write_text('{bad json')
   call('terminal.create',settings,409);old_receipt.write_text(receipt_text)
   bad=call('terminal.create',{**settings,'key':'bad-program','program':str(base/'missing-program')},404)
   assert bad['code']=='sdk_native_failed',bad
   failed=next(row for row in call('terminal.list') if row['key']=='bad-program');assert failed['state']=='ended' and failed['reason']=='not_started',failed
   assert call('qa.terminal-project-available') is True,'failed spawn must not retain a project lease'
   call('terminal.release',{'id':failed['id']})
   running=call('terminal.create',{**settings,'key':'service-stop-'+uuid.uuid4().hex});created.append(running)
   before=time.monotonic();assert call('qa.terminal-stop') is True;assert time.monotonic()-before<5
   drained=call('terminal.get',{'id':running['id']});assert drained['state']=='ended' and drained['drained'] and drained['reason']=='service_stopped',drained
   stop();(home/'data/application/terminals.json').write_text('{broken terminal metadata');start()
   call('terminal.capabilities',expected=503);assert call('runtime.info')['version']=='0.6.0'
   assert (home/'data/application/terminals.json').read_text()=='{broken terminal metadata'
   if args.evidence:
    args.evidence.mkdir(parents=True,exist_ok=True);(args.evidence/'result.json').write_text(json.dumps({'passed':True,'time':time.time(),'platform':sys.platform,'checks':['C and HTTP shared sessions','real TTY','scope isolation','project lease lifetime','ordered deduplicated input','control lease','resize','bounded output/gap','input backpressure','explicit close/drain','no create replay after release','no command replay after restart','metadata privacy','corrupt receipt','spawn failure','graceful service drain','corrupt metadata retained'],'capabilities':cap,'ended':ended,'restored':restored},indent=2),encoding='utf-8')
   print('Real PTY: TTY IO, ordered deduplicated input, leases, resize, bounded output/gap, exit and no replay after service restart: PASS')
  except Exception:
   if created:
    try:print('Last terminal:',call('terminal.get',{'id':created[-1]['id']}),'Output prefix:',repr(output[:200]),'Output tail:',repr(output[-600:]))
    except Exception:pass
   log.flush();print((base/'host.log').read_text(encoding='utf-8',errors='replace')[-5000:]);raise
  finally:
   stop();log.close()

if __name__=='__main__':main()
