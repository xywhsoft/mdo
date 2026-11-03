"""Exercise current directory plugins against the measured standalone packs."""
import argparse,json,os,shutil,subprocess
from pathlib import Path
from test_api_runtime import ROOT,free_port,request,wait_ready

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--directory',type=Path,default=ROOT/'.build/precompiled-bench-20261104');a=p.parse_args()
    out=a.directory.resolve();assert out.is_relative_to(ROOT/'.build')
    proof=out/'plugin-compatibility';proof.mkdir(exist_ok=True);reports=[]
    for variant in ('source','object-full','object-stripped-full','object-pruned'):
        for mode in ('native','core'):
            base=proof/(variant+'-'+mode)
            assert base.resolve().parent==proof.resolve()
            if base.exists():shutil.rmtree(base)
            base.mkdir();home=base/'home';directory=home/'plugins/qa-probe';directory.mkdir(parents=True)
            (directory/'extension.json').write_text(json.dumps({'kind':'plugins','id':'plugin:qa.precompiled','name':'Object QA','alias':'object-qa',
                'entry':'plugin.json','platforms':['any'],'version':'1.0.0'}),encoding='utf-8')
            if mode=='core':
                source=(ROOT/'app/src/api/resources.c').read_text(encoding='utf-8').replace('MdoApiValueSetString(Data, "version", MDO_VERSION_TEXT)',
                    'MdoApiValueSetString(Data, "version", "precompiled-core-qa")')
                assert '"precompiled-core-qa"' in source
                recipe={'overrides':[{'source':'override.c','target':'src/api/resources.c'}]};(directory/'override.c').write_text(source,encoding='utf-8')
            else:
                source=(ROOT/'tests/fixtures/modules/local-tool.c').read_text().replace('"user.tools.fixture"','MDO_PLUGIN_ID')
                recipe={'entry':{'native':'plugin.c'}};(directory/'plugin.c').write_text(source,encoding='utf-8')
            (directory/'plugin.json').write_text(json.dumps(recipe),encoding='utf-8')
            launcher=base/'mdo.exe';os.link(out/(variant+'.exe'),launcher);port=free_port()
            (base/'xs.json').write_text(json.dumps({'engine':{'workers':1},'services':[{'enabled':True,'class':'http','name':'plugin-qa','ip':'127.0.0.1','port':port,
                'host_default':{'enabled':True,'name':'mdo','path':'web','devlang':'c','devfile':'generated/mdo_unity.c' if variant=='source' else 'generated/mdo.o'}}]}))
            with (base/'host.log').open('wb') as log:
                child=subprocess.Popen([str(launcher),'--','--home',str(home)],cwd=base,stdout=log,stderr=log,creationflags=subprocess.CREATE_NO_WINDOW)
                try:
                    wait_ready(port,child)
                    bootstrap=json.loads(request(port,'GET','/api/v1/bootstrap')[2])['data']
                    status,_,body=request(port,'POST','/api/v1/application/call',headers={'Content-Type':'application/json'},
                        body=json.dumps({'method':'plugin.list','params':{}}).encode());assert status==200,body
                    plugins=json.loads(body)['data'];tools=json.loads(request(port,'GET','/api/v1/tools')[2])['data']
                    applied=bootstrap['version']=='precompiled-core-qa' if mode=='core' else 'user.fixture' in json.dumps(tools)
                    expected=not(mode=='core' and variant=='object-pruned')
                    row={'variant':variant,'mode':mode,'applied':applied,'expected':expected,'version':bootstrap['version'],
                        'recovery_mode':plugins['recovery_mode'],'passed':applied==expected}
                    reports.append(row);(out/'plugin-compatibility.json').write_text(json.dumps(reports,indent=2)+'\n')
                    assert row['passed'],row
                    print('PASS',variant,mode,'applied=',applied,'recovery=',plugins['recovery_mode'],flush=True)
                finally:child.terminate();child.wait(timeout=10)
            launcher.unlink()

if __name__=='__main__':main()
