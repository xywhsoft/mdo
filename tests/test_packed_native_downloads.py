"""Smoke the embedded download API/assets without copying the app or executable."""
import argparse,json,os,subprocess,tempfile
from pathlib import Path
from test_api_runtime import ROOT,free_port,request,wait_ready

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--packed',type=Path,required=True);args=parser.parse_args()
    packed=args.packed.resolve();assert packed.is_file()
    with tempfile.TemporaryDirectory(dir=ROOT/'.build',prefix='packed-download-') as raw:
        base=Path(raw);home=base/'home';index=home/'data/downloads/tasks.json';index.parent.mkdir(parents=True)
        identity='P'*32
        index.write_text(json.dumps([dict(id=identity,source='transfer',identity='/qa/packed',name='Packed fixture',sha256='a'*64,
            bytes=100,received=40,state='downloading',context=dict(path='/qa/packed',relative='data/downloads/fixture.bin'))]),encoding='utf-8')
        port=free_port();config=base/'xs.json'
        config.write_text(json.dumps(dict(services=[dict(enabled=True,name='packed-download',ip='127.0.0.1',port=port,
            **{'class':'http'},host_default=dict(enabled=True,name='mdo',path='web',devlang='c',devfile='generated/mdo_unity.c'))])),encoding='utf-8')
        # The pack host locates its virtual root beside the executable. A hard
        # link provides that root without copying a second executable to disk.
        launcher=base/packed.name;os.link(packed,launcher)
        with (base/'host.log').open('wb') as log:
            process=subprocess.Popen([str(launcher),'--','--home',str(home)],cwd=base,
                env=dict(os.environ,MDO_HOME=str(home)),stdout=log,stderr=log,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
            try:
                wait_ready(port,process)
                def api(method,path):
                    status,_,body=request(port,method,'/api/v1'+path,body=b'{}' if method=='POST' else None,headers={'Content-Type':'application/json'})
                    assert status==200,(status,body);return json.loads(body)['data']
                rows=api('GET','/download-tasks');assert len(rows)==1,rows
                assert rows[0]['state']=='paused' and rows[0]['received']==40 and 'context' not in rows[0],rows
                assert api('POST',f'/download-tasks/{identity}/cancel')[0]['state']=='cancelled'
                assert api('POST',f'/download-tasks/{identity}/forget')==[]
                assert json.loads(index.read_text(encoding='utf-8'))==[]
                for relative in ('js/features/downloads/center.js','js/state/downloads.js','lang/zh-CN.json','lang/en-US.json','lang/ru-RU.json'):
                    status,_,body=request(port,'GET','/'+relative)
                    assert status==200 and body==(ROOT/'app/web'/relative).read_bytes(),relative
                print('PASS packed native task recovery, cancel/forget persistence and exact embedded UI/language assets')
            except Exception:
                print((base/'host.log').read_text(encoding='utf-8',errors='replace'));raise
            finally:
                process.terminate()
                try:process.wait(timeout=10)
                except subprocess.TimeoutExpired:process.kill();process.wait(timeout=5)

if __name__=='__main__':main()
