"""Alternating source/partial-AOT startup and process CPU measurements."""
import argparse,ctypes,json,os,shutil,subprocess,sys,time
from ctypes import wintypes
from pathlib import Path
from test_api_runtime import ROOT,free_port,request,wait_ready
sys.path.insert(0,str(ROOT/'tools'))
from benchmark_precompiled import bounded_remove,stats

def process_cpu_ms(process):
    created=wintypes.FILETIME();exited=wintypes.FILETIME();kernel=wintypes.FILETIME();user=wintypes.FILETIME()
    get=ctypes.WinDLL('kernel32',use_last_error=True).GetProcessTimes
    get.argtypes=[wintypes.HANDLE,*([ctypes.POINTER(wintypes.FILETIME)]*4)];get.restype=wintypes.BOOL
    if not get(int(process._handle),ctypes.byref(created),ctypes.byref(exited),ctypes.byref(kernel),ctypes.byref(user)):raise ctypes.WinError(ctypes.get_last_error())
    return sum((x.dwHighDateTime<<32)|x.dwLowDateTime for x in (kernel,user))/10000

def main():
    p=argparse.ArgumentParser();p.add_argument('--directory',type=Path,default=ROOT/'.build/partial-precompile-20261104');p.add_argument('--samples',type=int,default=15);p.add_argument('--fresh-samples',type=int,default=3)
    p.add_argument('--variants',default='source,partial');a=p.parse_args();names=a.variants.split(',')
    assert set(names)<=set(('source','partial','partial-direct')) and len(names)==len(set(names))
    out=a.directory.resolve();assert os.name=='nt' and out.is_relative_to(ROOT/'.build') and 1<=a.samples<=30
    run=out/'startup';run.mkdir(exist_ok=True);launcher=run/'mdo.exe';seed=out/'startup-seed';rows=[]
    def start(name,home,probe=False):
        launcher.unlink(missing_ok=True);os.link(out/(name+'.exe'),launcher);port=free_port()
        config={'engine':{'workers':1},'services':[{'enabled':True,'class':'http','name':'partial-qa','ip':'127.0.0.1','port':port,
            'host_default':{'enabled':True,'name':'mdo','path':'web','devlang':'c','devfile':'generated/mdo_unity.c'}}]}
        if name=='partial-direct':config['services'][0]['host_default']['dev_lib']='generated/precompiled'
        (run/'xs.json').write_text(json.dumps(config))
        with (out/(name+'-startup.log')).open('ab') as log:
            begin=time.perf_counter();child=subprocess.Popen([str(launcher),'--','--home',str(home)],cwd=run,stdout=log,stderr=log,creationflags=subprocess.CREATE_NO_WINDOW)
            try:
                # Nonblocking, short TCP checks avoid a 150ms connection timeout
                # becoming the measurement's polling granularity on Windows.
                import socket
                while time.perf_counter()-begin<20:
                    if child.poll() is not None:raise RuntimeError(name+' exited during startup')
                    try:
                        with socket.create_connection(('127.0.0.1',port),timeout=.005):pass
                        code,_,body=request(port,'GET','/api/v1/bootstrap')
                        if code==200 and json.loads(body)['data']['ready']:break
                    except (OSError,ValueError,KeyError):pass
                    time.sleep(.005)
                else:raise RuntimeError('Ready timeout: '+name)
                value={'ready_ms':(time.perf_counter()-begin)*1000,'process_cpu_ms':process_cpu_ms(child)}
                if probe:
                    for path in ('/','/js/app.js','/api/v1/models','/api/v1/tools','/api/v1/application/services'):assert request(port,'GET',path)[0]==200,path
                    code,_,body=request(port,'POST','/api/v1/application/call',headers={'Content-Type':'application/json'},body=json.dumps({'method':'markdown.parse','params':{'source':'# ready\n\n- [x] libraries'}}).encode())
                    assert code==200 and json.loads(body)['data']['engine']=='md4c'
                return value
            finally:child.terminate();child.wait(timeout=10)
    bounded_remove(seed,out);start('source',seed,True)
    for phase,count in (('fresh',a.fresh_samples),('prepared',a.samples)):
        for i in range(count):
            order=names[i%len(names):]+names[:i%len(names)]
            if i%2:order.reverse()
            for name in order:
                home=out/'startup-home';bounded_remove(home,out)
                if phase=='prepared':shutil.copytree(seed,home)
                value=start(name,home,i==0);rows.append({'phase':phase,'variant':name,'sample':i,**value})
                (out/'startup-raw.json').write_text(json.dumps(rows,indent=2)+'\n')
            print('PASS startup',phase,i+1,'/',count,flush=True)
    results=json.loads((out/'results.json').read_text(encoding='utf-8'))
    results['startup']={'raw':rows,'boundary':'Popen to ready=true API response, no WebView; OS cache not cleared; one worker; prepared Home reset to same snapshot before each sample',
        'summary':{phase:{name:{key:stats([r[key] for r in rows if r['phase']==phase and r['variant']==name]) for key in ('ready_ms','process_cpu_ms')} for name in names} for phase in ('fresh','prepared')}}
    (out/'results.json').write_text(json.dumps(results,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    bounded_remove(out/'startup-home',out);launcher.unlink(missing_ok=True)
    print(json.dumps(results['startup']['summary'],indent=2),flush=True)

if __name__=='__main__':main()
