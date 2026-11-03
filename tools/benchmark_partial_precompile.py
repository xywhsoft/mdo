#!/usr/bin/env python3
"""Candidate-driven partial AOT experiment; original app is read-only."""
import argparse,contextlib,hashlib,json,os,re,shutil,subprocess,sys,time
from pathlib import Path
import build_mdo,application_sdk
from benchmark_precompiled import bounded_remove,elf_info,free_port,stats

ROOT=Path(__file__).resolve().parents[1]
SDK=ROOT/'.build/sdk-overlays/3173f94f8307f94899f04ceaccd88695f128f14bb68b333dbc3a8e1b8dae1b5e'

def tree_hash(root):
    h=hashlib.sha256()
    for p in sorted(root.rglob('*')):
        if p.is_file():h.update(p.relative_to(root).as_posix().encode()+b'\0'+hashlib.sha256(p.read_bytes()).digest())
    return h.hexdigest()

def closing_brace(source,start):
    depth=0;i=start;quote=None
    while i<len(source):
        c=source[i]
        if quote:
            if c=='\\':i+=2;continue
            if c==quote:quote=None
        elif source.startswith('//',i):
            end=source.find('\n',i);i=len(source) if end<0 else end;continue
        elif source.startswith('/*',i):
            end=source.find('*/',i+2)
            if end<0:raise ValueError('Unterminated C comment')
            i=end+2;continue
        elif c in ('"',"'"):quote=c
        elif c=='{':depth+=1
        elif c=='}':
            depth-=1
            if depth==0:return i+1
        i+=1
    raise ValueError('Unterminated C body')

def extract_header(text):
    """Bounded to the reviewed candidate headers; fail closed on leftovers."""
    pattern=re.compile(r'^[ \t]*static[ \t]+(?:inline[ \t]+)?(?P<sig>[^;{}#=]+?\b(?P<name>Mdo\w+)\s*\([^;{}#]*\))\s*\{',re.M)
    edits=[];bodies=[];names=[];end=0
    for m in pattern.finditer(text):
        if m.start()<end:continue
        begin=m.end()-1;end=closing_brace(text,begin);sig=m.group('sig').strip()
        edits.append((m.start(),end,sig+';'));bodies.append(sig+text[begin:end]);names.append(m.group('name'))
    # Only the Unicode table is private to the extracted path function. Keep
    # the small kind array in the header: business managers index it directly.
    data=[]
    for name in ('MdoExtensionFolds',):
        m=re.search(r'^static\s+const[^;{}]*\b'+name+r'\s*\[\s*\]\s*=\s*\{',text,re.M)
        if m:
            end=closing_brace(text,m.end()-1)
            while end<len(text) and text[end].isspace():end+=1
            assert text[end]==';';end+=1
            data.append(text[m.start():end]);edits.append((m.start(),end,''))
    thin=text
    for start,end,replacement in sorted(edits,reverse=True):thin=thin[:start]+replacement+thin[end:]
    assert not pattern.search(thin),'Unextracted candidate function'
    return thin,data,bodies,names

@contextlib.contextmanager
def staged_build_globals(project):
    app=project/'app';gen=app/'generated'
    values={'ROOT':project,'APP':app,'LOCK_PATH':project/'deps.lock','SOURCES_PATH':app/'sources.json','GENERATED':gen,
        'UNITY_PATH':gen/'mdo_unity.c','GENERATED_LOCK_PATH':gen/'deps.lock','MODULE_HEADER_PATH':project/'include/mdo/module.h',
        'GENERATED_MODULE_HEADER_PATH':gen/'module-sdk/mdo/module.h','BUILTIN_KEY_PATH':app/'default-home/config/secrets/builtin-model.key'}
    before={key:getattr(build_mdo,key) for key in values}
    try:
        for key,value in values.items():setattr(build_mdo,key,value)
        yield
    finally:
        for key,value in before.items():setattr(build_mdo,key,value)

def prepare_copy(project,lock,sdk):
    project.mkdir(parents=True);shutil.copytree(ROOT/'app',project/'app');shutil.copytree(ROOT/'include',project/'include');shutil.copyfile(ROOT/'deps.lock',project/'deps.lock')
    with staged_build_globals(project):build_mdo.prepare(lock)
    import build_cli_forwarder
    build_cli_forwarder.build(sdk,project/'app/generated/native/windows/cli-forwarder.exe')
    unity=project/'app/generated/mdo_unity.c';unity.write_text('#define MDO_PRODUCT_EDITION "desktop"\n'+unity.read_text(encoding='utf-8'),encoding='utf-8')

def make_partial(project,inventory):
    app=project/'app';generated=app/'generated';work=generated/'partial-build';work.mkdir(exist_ok=True)
    libraries=[];exports={};removed=[]
    for unit in inventory['first_batch']:
        includes=[];data=[];bodies=[];names=[]
        for item in unit['files']:
            relative=item['source'];p=app/relative;text=p.read_text(encoding='utf-8')
            if hashlib.sha256(p.read_bytes()).hexdigest()!=item['sha256']:
                raise ValueError('Candidate changed since interface audit: '+relative)
            if p.suffix=='.h':
                thin,constants,functions,symbols=extract_header(text)
                p.write_text(thin,encoding='utf-8');includes.append('#include "/app/'+relative+'"')
                data+=constants;bodies+=functions;names+=symbols
            else:
                # Preserve exact preprocessor branches in platform C sources.
                def absolute(m):
                    target=(p.parent/m.group(1)).resolve();assert target.is_relative_to(app.resolve())
                    return '#include "/app/'+target.relative_to(app.resolve()).as_posix()+'"'
                bodies.append(re.sub(r'^\s*#include\s+"([^"\n]+)"',absolute,text,flags=re.M));removed.append(relative)
        source='#include <xsbase.h>\n'+'\n'.join(includes+data+bodies)+'\n'
        file=work/(unit['id']+'.c');file.write_text(source,encoding='utf-8')
        libraries.append(unit['id']);exports[unit['id']]=names
    for relative in removed:(app/relative).unlink()
    manifest=json.loads((app/'sources.json').read_text());manifest['sources']=[x for x in manifest['sources'] if x not in removed]
    (app/'sources.json').write_text(json.dumps(manifest,indent=2)+'\n')
    # Rebuild mirrors and native bindings exclusively inside the copied tree.
    with staged_build_globals(project):
        source=build_mdo.generated_unity(build_mdo.load_object(project/'deps.lock'),build_mdo.source_list())
    (generated/'mdo_core.c').write_text('#define MDO_PRODUCT_EDITION "desktop"\n'+source,encoding='utf-8')
    application_sdk.generate(project)
    calls='\n'.join(f' if(tcc_add_file(T,"/app/generated/precompiled/{name}.o")<0)return false;' for name in libraries)
    (generated/'partial_objects.inc.h').write_text('static bool MdoLoadCommonObjects(TCCState* T){\n'+calls+'\n return true;\n}\n')
    boot=app/'src/bootstrap/plugin_bootstrap.inc.c';text=boot.read_text(encoding='utf-8')
    text='#include "../../generated/partial_objects.inc.h"\n'+text.replace('"/app/generated/mdo_unity.c"','"/app/generated/mdo_core.c"')
    text=text.replace('bool Ok=Tcc&&Source&&!memchr(Source,0,Size);','bool Ok=Tcc&&Source&&!memchr(Source,0,Size)&&MdoLoadCommonObjects(Tcc);')
    boot.write_text(text,encoding='utf-8')
    entry=r'''#include <xsbase.h>
#include <libtcc.h>
#include <stdio.h>
#include "partial_objects.inc.h"
static TCCState* Core;
static XS_ServiceInitProc Init;
static XS_ServiceUnitProc Unit;
static XS_ServiceSwapProc Swap;
static XS_RequestProc Request;
void ServiceInit(XS_HostInfo* Host){
 Core=xsCreateTCC();
 if(!Core||!MdoLoadCommonObjects(Core)||tcc_add_file(Core,"/app/generated/mdo_core.c")<0||tcc_relocate(Core)<0){fprintf(stderr,"partial core compile failed\n");return;}
 Init=(XS_ServiceInitProc)tcc_get_symbol(Core,"ServiceInit");Unit=(XS_ServiceUnitProc)tcc_get_symbol(Core,"ServiceUnit");
 Swap=(XS_ServiceSwapProc)tcc_get_symbol(Core,"ServiceSwap");Request=(XS_RequestProc)tcc_get_symbol(Core,"RequestProc");
 if(Init&&Unit&&Request)Init(Host);
}
void ServiceUnit(XS_HostInfo* Host){if(Unit)Unit(Host);if(Core)xsDestroyTCC(Core);Core=NULL;}
bool ServiceSwap(XS_HostInfo* Host,xvalue** Shared){return Swap&&Swap(Host,Shared);}
XS_RequestResult RequestProc(XS_HttpReq* Req){return Request?Request(Req):XS_FALLBACK;}
'''
    (generated/'mdo_unity.c').write_text(entry,encoding='utf-8')
    return {'libraries':libraries,'exports':exports,'removed_c':removed}

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--directory',type=Path,default=ROOT/'.build/partial-precompile-20261104');p.add_argument('--samples',type=int,default=9);p.add_argument('--prepare-only',action='store_true');a=p.parse_args()
    out=a.directory.resolve();assert out.is_relative_to(ROOT/'.build') and os.name=='nt';out.mkdir(parents=True,exist_ok=True)
    before_files={str(base):{p.relative_to(base).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in base.rglob('*') if p.is_file()} for base in (ROOT/'app',ROOT/'include')}
    before={'app':tree_hash(ROOT/'app'),'include':tree_hash(ROOT/'include')};lock=build_mdo.load_object(ROOT/'deps.lock');host=ROOT/'.build/host'
    build_mdo.verify_dependencies(SDK,lock);build_mdo.verify_host_receipt(host/'xsw.exe',lock,False,icon=True)
    results={'source_commit':build_mdo.git_head(ROOT),'source_inputs':before,'variants':{},'compiler':{},'samples':a.samples}
    def save():(out/'results.json').write_text(json.dumps(results,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    try:
        project=out/'project';bounded_remove(project,out);prepare_copy(project,lock,SDK)
        def pack(name):
            start=time.perf_counter()
            with (out/(name+'-pack.log')).open('wb') as log:subprocess.run([str(host/'xsw.exe'),'pack',str(project/'app'),'-o',str(out/(name+'.exe'))],stdout=log,stderr=log,check=True,timeout=60,creationflags=subprocess.CREATE_NO_WINDOW)
            exe=out/(name+'.exe');results['variants'][name]={'bytes':exe.stat().st_size,'sha256':hashlib.sha256(exe.read_bytes()).hexdigest(),'pack_ms':(time.perf_counter()-start)*1000};save();print('pack',name,exe.stat().st_size,flush=True)
        pack('source')
        # Compile the seven library objects with the same embedded TCC/SDK.
        driver=out/'compiler';driver.mkdir(exist_ok=True);shutil.copyfile(ROOT/'tests/fixtures/precompiled-benchmark.c',driver/'main.c')
        wanted=json.loads((host/'xs.exe.build.json').read_text());headers=[h for h in (host/'objects').rglob('xs_build_extensions.h') if all(e in h.parent.name for e in wanted['extensions']) and wanted['profile_sha256'][:12] in h.parent.name];assert len(headers)==1
        defines=re.findall(r'tcc_define_symbol\(pTcc,\s*("[^"]+"),\s*("[^"]+")\);',headers[0].read_text())
        (driver/'compiler-defines.inc').write_text('\n'.join(f'tcc_define_symbol(T,{name},{value});' for name,value in defines)+'\n')
        config={'services':[{'enabled':True,'class':'http','name':'partial-compiler','ip':'127.0.0.1','port':free_port(),'host_default':{'enabled':True,'name':'compiler','path':'web','devlang':'c','devfile':'main.c'}}]}
        (driver/'xs.json').write_text(json.dumps(config));objects=project/'app/generated/precompiled';objects.mkdir(exist_ok=True)
        def compile_probe(name,input,emit,object_path=None,manifest=False,iterations=1):
            report=out/(name+'-compiler.json');fields={'BENCH_APP_ROOT':(project/'app').as_posix(),'BENCH_RESULT_PATH':report.as_posix(),'BENCH_OBJECT_PATH':(object_path or objects/'unused.o').as_posix(),'BENCH_INPUT':input}
            text='\n'.join('#define '+key+' '+json.dumps(value) for key,value in fields.items())+f'\n#define BENCH_EMIT {int(emit)}\n#define BENCH_ITERATIONS {iterations}\n'
            if manifest:
                shutil.copyfile(project/'app/generated/partial_objects.inc.h',driver/'partial_objects.inc.h')
                text+='\n#define BENCH_OBJECT_MANIFEST "partial_objects.inc.h"\n'
            (driver/'benchmark-config.h').write_text(text)
            with (out/(name+'-compiler.log')).open('wb') as log:subprocess.run([str(host/'xs.exe'),str(driver/'xs.json')],cwd=driver,stdout=log,stderr=log,check=True,timeout=60,creationflags=subprocess.CREATE_NO_WINDOW)
            rows=json.loads(report.read_text());assert all(row['ok'] for row in rows);return rows
        rows=compile_probe('source-core','/app/generated/mdo_unity.c',False,iterations=a.samples)
        results['compiler']['source-core']={'raw':rows,'summary':stats([row['total_ms'] for row in rows])};save()
        inventory=json.loads((ROOT/'docs/precompile-candidates.json').read_text(encoding='utf-8'))
        results['extraction']=make_partial(project,inventory);save()
        for name in results['extraction']['libraries']:
            rows=compile_probe(name,'/app/generated/partial-build/'+name+'.c',True,objects/(name+'.o'))
            results['compiler'][name]={'raw':rows,'object':elf_info(objects/(name+'.o'))};save();print('emit',name,flush=True)
        rows=compile_probe('partial-core','/app/generated/mdo_core.c',False,manifest=True,iterations=a.samples)
        results['compiler']['partial-core']={'raw':rows,'summary':stats([row['total_ms'] for row in rows])};save()
        bounded_remove(project/'app/generated/partial-build',project/'app/generated')
        pack('partial')
        if not a.prepare_only:
            print('Prepared source and partial packs; benchmark with benchmark_partial_startup.py',flush=True)
    finally:
        after={'app':tree_hash(ROOT/'app'),'include':tree_hash(ROOT/'include')};results['original_unchanged']=before==after
        if before!=after:
            results['changed_original_paths']=[]
            for base in (ROOT/'app',ROOT/'include'):
                current={p.relative_to(base).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in base.rglob('*') if p.is_file()}
                results['changed_original_paths'] += [str(base/path) for path in sorted(set(current)|set(before_files[str(base)])) if current.get(path)!=before_files[str(base)].get(path)]
            print('Original files changed during trial:',results['changed_original_paths'],flush=True)
        save()
        if sys.exc_info()[0] is None:assert before==after,'Original app/include changed during trial'

if __name__=='__main__':main()
