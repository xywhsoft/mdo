#!/usr/bin/env python3
"""Measure same-host TCC objects without changing the product build default.

Windows x64 only. Uses one disposable package staging tree, the existing host
and SDK, no checkout/SDK copies. Outputs are confined to the project's .build.
"""
import argparse, hashlib, http.client, json, os, re, shutil, socket, statistics
import struct, subprocess, sys, time
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]

def free_port():
    with socket.socket() as sock:
        sock.bind(('127.0.0.1',0));return sock.getsockname()[1]

def stats(values):
    return {'median_ms':statistics.median(values),'min_ms':min(values),'max_ms':max(values),
            'p90_ms':sorted(values)[min(len(values)-1,int(len(values)*.9))],'samples':len(values)}

def bounded_remove(path,base):
    path=path.resolve();base=base.resolve()
    if path==base or not path.is_relative_to(base):raise ValueError('Removal escaped benchmark directory: '+str(path))
    if path.exists():shutil.rmtree(path)

def elf_info(path):
    data=path.read_bytes();head=struct.unpack_from('<16sHHIQQQIHHHHHH',data)
    assert head[0][:6]==b'\x7fELF\x02\x01' and head[1]==1 and head[2]==62,'Expected ELF64 x86-64 relocatable object'
    sections=[struct.unpack_from('<IIQQQQIIQQ',data,head[6]+i*head[11]) for i in range(head[12])]
    absolute=[]
    for section in sections:
        if section[1]!=2:continue
        strings=sections[section[6]];names=data[strings[4]:strings[4]+strings[5]]
        for at in range(section[4],section[4]+section[5],section[9]):
            name,info,other,index,value,size=struct.unpack_from('<IBBHQQ',data,at)
            if index==0xfff1 and value:absolute.append(names[name:].split(b'\0',1)[0].decode(errors='replace'))
    assert not absolute,('Process-specific absolute symbols in object',absolute[:20])
    return {'format':'ELF64 x86-64, Windows x64 calling convention','bytes':len(data),
            'sha256':hashlib.sha256(data).hexdigest(),'nonzero_absolute_symbols':absolute}

def measure_build_flow(out,host,sdk,count):
    """Same generation/verification/forwarder/pack steps; compile before pack."""
    import contextlib
    import build_mdo
    from qa_release import release_inputs,verify_release_inputs
    inputs=release_inputs();rows=[];driver=out/'compiler';flow_dir=out/'build-flow';flow_dir.mkdir(exist_ok=True);flow=flow_dir/'flow.exe'
    original_run=build_mdo.run
    try:
        for sample in range(count):
            for mode in (('source','object') if sample%2==0 else ('object','source')):
                row={'sample':sample,'mode':mode};log_path=out/f'flow-{sample}-{mode}.log'
                with log_path.open('w',encoding='utf-8') as log:
                    def run(command,cwd,dry_run=False):
                        if dry_run:raise ValueError('Benchmark cannot use dry-run')
                        is_pack=len(command)>1 and command[1]=='pack'
                        if is_pack and mode=='object':
                            package=Path(command[2]);values={'BENCH_APP_ROOT':package.as_posix(),'BENCH_RESULT_PATH':(out/'flow-compiler.json').as_posix(),
                                'BENCH_OBJECT_PATH':(package/'generated/mdo.o').as_posix(),'BENCH_INPUT':'/app/generated/mdo_unity.c'}
                            (driver/'benchmark-config.h').write_text('\n'.join('#define '+k+' '+json.dumps(v) for k,v in values.items())+'\n#define BENCH_EMIT 1\n#define BENCH_ITERATIONS 1\n')
                            begin=time.perf_counter();subprocess.run([str(host/'xs.exe'),str(driver/'xs.json')],cwd=driver,stdout=log,stderr=log,check=True,timeout=30,creationflags=subprocess.CREATE_NO_WINDOW)
                            row['precompile_ms']=(time.perf_counter()-begin)*1000;elf_info(package/'generated/mdo.o')
                            config=json.loads((package/'xs.json').read_text(encoding='utf-8'))
                            for service in config['services']:service['host_default']['devfile']='generated/mdo.o'
                            (package/'xs.json').write_text(json.dumps(config,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
                        begin=time.perf_counter();subprocess.run(command,cwd=cwd,stdout=log,stderr=log,check=True,timeout=60,creationflags=subprocess.CREATE_NO_WINDOW)
                        if is_pack:row['pack_ms']=(time.perf_counter()-begin)*1000
                    build_mdo.run=run;begin=time.perf_counter()
                    with contextlib.redirect_stdout(log):
                        code=build_mdo.main(['--skip-host-build','--host-dir',str(host),'--xserver-root',str(sdk),'--output',str(flow)])
                    assert code==0
                    row.update(total_ms=(time.perf_counter()-begin)*1000,bytes=flow.stat().st_size,sha256=hashlib.sha256(flow.read_bytes()).hexdigest())
                rows.append(row);print('build flow',sample,mode,round(row['total_ms'],1),'ms',flush=True)
        results=json.loads((out/'results.json').read_text(encoding='utf-8'))
        results['build_flow']={'raw':rows,'summary':{mode:stats([row['total_ms'] for row in rows if row['mode']==mode]) for mode in ('source','object')}}
        verify_release_inputs(inputs);(out/'results.json').write_text(json.dumps(results,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    finally:
        build_mdo.run=original_run;flow.unlink(missing_ok=True)
        bounded_remove(ROOT/'.build/packages/flow-host',ROOT/'.build/packages')

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--directory',type=Path,default=ROOT/'.build/precompiled-bench-20261104')
    p.add_argument('--host-dir',type=Path,default=ROOT/'.build/host')
    p.add_argument('--package',type=Path,default=ROOT/'.build/packages/source-host')
    p.add_argument('--samples',type=int,default=9);p.add_argument('--fresh-samples',type=int,default=3)
    p.add_argument('--build-flow-only',action='store_true',help='After the full benchmark, compare complete cached-host build flows')
    p.add_argument('--sdk-dir',type=Path)
    a=p.parse_args();out=a.directory.resolve();site=a.package.resolve();host=a.host_dir.resolve()
    assert os.name=='nt' and 1<=a.samples<=30 and 1<=a.fresh_samples<=10
    assert out.is_relative_to(ROOT/'.build') and site.is_relative_to(ROOT/'.build/packages')
    out.mkdir(parents=True,exist_ok=True)
    if a.build_flow_only:
        assert a.sdk_dir,'--sdk-dir is required for --build-flow-only'
        measure_build_flow(out,host,a.sdk_dir.resolve(),a.samples);return
    receipt=json.loads((out/'build.json').read_text(encoding='utf-8'))
    assert (out/'source.exe').is_file() and hashlib.sha256((out/'source.exe').read_bytes()).hexdigest()==receipt['sha256']
    assert (site/'generated/mdo_unity.c').is_file(),'Build the source package before running the benchmark'
    from qa_release import release_inputs,verify_release_inputs
    inputs=release_inputs();results={'schema_version':1,'source_commit':receipt['mdo_commit'],
        'source_inputs':inputs,'host_sha256':hashlib.sha256((host/'xsw.exe').read_bytes()).hexdigest(),
        'environment':{'platform':sys.platform,'cpu':os.environ.get('PROCESSOR_IDENTIFIER'),'os_cache':'warm/uncontrolled; not an OS cold-cache benchmark',
                       'startup_boundary':'Popen to first ready=true bootstrap response; excludes WebView and browser rendering',
                       'stop':'terminate after probing; prepared Home is restored from the same snapshot before every sample'},
        'compiler':{},'variants':{},'startup':[]}
    def save():
        (out/'results.json').write_text(json.dumps(results,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    driver=out/'compiler';driver.mkdir(exist_ok=True)
    shutil.copyfile(ROOT/'tests/fixtures/precompiled-benchmark.c',driver/'main.c')
    headers=list((host/'objects').rglob('xs_build_extensions.h'))
    wanted=json.loads((host/'xs.exe.build.json').read_text())
    headers=[h for h in headers if all(e in h.parent.name for e in wanted['extensions']) and wanted['profile_sha256'][:12] in h.parent.name]
    assert len(headers)==1,headers
    defines=re.findall(r'tcc_define_symbol\(pTcc,\s*("[^"]+"),\s*("[^"]+")\);',headers[0].read_text())
    (driver/'compiler-defines.inc').write_text('\n'.join(f'tcc_define_symbol(T,{name},{value});' for name,value in defines)+'\n')
    def config(port,entry):
        return {'engine':{'workers':1},'services':[{'enabled':True,'class':'http','name':'benchmark','ip':'127.0.0.1','port':port,
            'recv_limit':8388608,'body_limit':8388608,'host_default':{'enabled':True,'name':'mdo','path':'web','devlang':'c','devfile':entry}}]}
    (driver/'xs.json').write_text(json.dumps(config(free_port(),'main.c')))
    obj=site/'generated/mdo.o'
    def compiler(mode,entry,emit=False):
        values={'BENCH_APP_ROOT':site.as_posix(),'BENCH_RESULT_PATH':(out/(mode+'-compiler.json')).as_posix(),
            'BENCH_OBJECT_PATH':obj.as_posix(),'BENCH_INPUT':entry}
        (driver/'benchmark-config.h').write_text('\n'.join('#define '+k+' '+json.dumps(v) for k,v in values.items())+
            f'\n#define BENCH_EMIT {int(emit)}\n#define BENCH_ITERATIONS {a.samples}\n')
        with (out/(mode+'-compiler.log')).open('wb') as log:
            subprocess.run([str(host/'xs.exe'),str(driver/'xs.json')],cwd=driver,stdout=log,stderr=log,timeout=60,check=True,creationflags=subprocess.CREATE_NO_WINDOW)
        rows=json.loads((out/(mode+'-compiler.json')).read_text());assert all(row['ok'] for row in rows)
        results['compiler'][mode]={'summary':{key:stats([row[key] for row in rows]) for key in rows[0] if key!='ok'},'raw':rows}
        print('compiler',mode,results['compiler'][mode]['summary']['total_ms'],flush=True);save()
    compiler('emit','/app/generated/mdo_unity.c',True)
    compiler('source','/app/generated/mdo_unity.c')
    compiler('object','/app/generated/mdo.o')
    results['object']=elf_info(obj);shutil.copyfile(obj,out/'mdo-raw.o')
    variants={'source':('generated/mdo_unity.c',out/'source.exe')}
    results['variants']['source']={'bytes':(out/'source.exe').stat().st_size,'sha256':receipt['sha256'],'entry':'generated/mdo_unity.c'}
    original_config=json.loads((site/'xs.json').read_text(encoding='utf-8'))
    for service in original_config['services']:service['host_default']['devfile']='generated/mdo.o'
    (site/'xs.json').write_text(json.dumps(original_config,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    def pack(name):
        exe=out/(name+'.exe');begin=time.perf_counter()
        with (out/(name+'-pack.log')).open('wb') as log:
            subprocess.run([str(host/'xsw.exe'),'pack',str(site),'-o',str(exe)],stdout=log,stderr=log,check=True,timeout=60,creationflags=subprocess.CREATE_NO_WINDOW)
        variants[name]=('generated/mdo.o',exe)
        results['variants'][name]={'bytes':exe.stat().st_size,'sha256':hashlib.sha256(exe.read_bytes()).hexdigest(),'pack_ms':(time.perf_counter()-begin)*1000,'entry':'generated/mdo.o'}
        print('pack',name,exe.stat().st_size,flush=True);save()
    pack('object-full')
    subprocess.run(['strip','--strip-unneeded',*[arg for name in ('ServiceInit','ServiceUnit','ServiceSwap','RequestProc') for arg in ('-K',name)],str(obj)],check=True)
    results['stripped_object']=elf_info(obj);compiler('object-stripped','/app/generated/mdo.o');pack('object-stripped-full')
    # Size experiment only: deleting these sources breaks core override plugins.
    removed=[]
    for folder in ('src','include'):
        removed.extend(p.relative_to(site).as_posix() for p in (site/folder).rglob('*') if p.is_file());bounded_remove(site/folder,ROOT/'.build/packages')
    for file in (site/'generated').glob('*.c'):removed.append(file.relative_to(site).as_posix());file.unlink()
    results['pruned_files']=removed;pack('object-pruned')
    run=out/'run';run.mkdir(exist_ok=True);launcher=run/'mdo.exe';seed=out/'prepared-home'
    def request(port,method,path,body=None,token=None):
        connection=http.client.HTTPConnection('127.0.0.1',port,timeout=.15 if method=='GET' else 5)
        try:
            headers={'Content-Type':'application/json'}
            if token:headers['X-Mdo-Write-Token']=token
            connection.request(method,path,body=json.dumps(body) if body is not None else None,headers=headers)
            response=connection.getresponse();data=response.read();return response.status,dict(response.getheaders()),data
        finally:connection.close()
    def startup(name,home,probe=False):
        entry,exe=variants[name];launcher.unlink(missing_ok=True);os.link(exe,launcher)
        port=free_port();(run/'xs.json').write_text(json.dumps(config(port,entry)))
        log_path=out/(name+'-startup.log')
        with log_path.open('ab') as log:
            begin=time.perf_counter();process=subprocess.Popen([str(launcher),'--','--home',str(home)],cwd=run,stdout=log,stderr=log,
                env=dict(os.environ,USE_WEBVIEW='0'),creationflags=subprocess.CREATE_NO_WINDOW)
            try:
                while time.perf_counter()-begin<15:
                    if process.poll() is not None:raise RuntimeError(log_path.read_text(encoding='utf-8',errors='replace')[-4000:])
                    try:
                        code,headers,body=request(port,'GET','/api/v1/bootstrap')
                        doc=json.loads(body)
                        if code==200 and doc.get('data',{}).get('ready'):break
                    except (OSError,ValueError):pass
                    time.sleep(.005)
                else:raise RuntimeError('Startup deadline: '+name)
                elapsed=(time.perf_counter()-begin)*1000
                if probe:
                    assert request(port,'GET','/')[0]==200
                    assert request(port,'GET','/js/app.js')[0]==200
                    assert request(port,'GET','/api/v1/models')[0]==200
                    assert request(port,'GET','/api/v1/application/services')[0]==200
                    token=next(value for key,value in headers.items() if key.lower()=='x-mdo-write-token')
                    code,_,body=request(port,'POST','/api/v1/application/call',{'method':'markdown.parse','params':{'source':'# Object runtime\n\n- [x] ready'}},token)
                    assert code==200 and json.loads(body)['data']['engine']=='md4c',(name,code,body)
                return elapsed
            finally:
                process.terminate();process.wait(timeout=10)
    # Existing-Home samples restore identical bytes before the measured region.
    bounded_remove(seed,out);startup('source',seed,True)
    names=list(variants)
    for phase,count in (('fresh',a.fresh_samples),('prepared',a.samples)):
        for iteration in range(count):
            order=names[iteration%len(names):]+names[:iteration%len(names)]
            if iteration%2:order.reverse()
            for name in order:
                home=out/'run-home';bounded_remove(home,out)
                if phase=='prepared':shutil.copytree(seed,home)
                elapsed=startup(name,home,iteration==0)
                results['startup'].append({'phase':phase,'variant':name,'sample':iteration,'ready_ms':elapsed});save()
            print('startup',phase,iteration+1,'/',count,flush=True)
    results['startup_summary']={phase:{name:stats([row['ready_ms'] for row in results['startup'] if row['phase']==phase and row['variant']==name]) for name in names} for phase in ('fresh','prepared')}
    verify_release_inputs(inputs);results['source_inputs_unchanged']=True;save()
    print(json.dumps(results['startup_summary'],indent=2));bounded_remove(out/'run-home',out)

if __name__=='__main__':main()
