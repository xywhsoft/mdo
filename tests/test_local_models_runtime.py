"""Current product installation/import manager in place; tiny synthetic files."""
import argparse, hashlib, json, os, subprocess, tempfile, time
from pathlib import Path
from current_product_host import APP, product_http_config
from test_api_runtime import ROOT, free_port, request, wait_ready
from test_interrupt_runtime import stop_host

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--host',type=Path,default=ROOT/'.build/host/xs.exe');args=parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='local-model-runtime-',dir=ROOT/'.build') as raw, product_http_config(free_port()) as config:
        base=Path(raw);home=base/'home';home.mkdir();port=json.loads(config.read_text(encoding='utf-8'))['services'][0]['port'];process=None;checks=0;log=base/'host.log'
        payload=b'GGUF\x03\x00\x00\x00'+b'owned model resource fixture\n'*4096;source=base/'model.gguf';source.write_bytes(payload);digest=hashlib.sha256(payload).hexdigest()
        def check(value,detail):
            nonlocal checks
            assert value,detail;checks+=1
        def call(body=None,status=200):
            code,_,raw=request(port,'GET' if body is None else 'POST','/api/v1/local-models',body=None if body is None else json.dumps(body).encode(),headers={'Content-Type':'application/json'})
            data=json.loads(raw);check(code==status,(code,data));return data.get('data')
        def wait(id):
            end=time.monotonic()+25
            while time.monotonic()<end:
                item=call()['installations'][id]
                if item['state'] not in ('queued','installing'):return item
                time.sleep(.05)
            raise AssertionError(('operation timed out',item))
        def start():
            nonlocal process
            with log.open('ab') as output:process=subprocess.Popen([str(args.host.resolve()),str(config),'--','--home',str(home)],cwd=APP,env={**os.environ,'MDO_HOME':str(home),'USERPROFILE':str(base),'USE_WEBVIEW':'0'},stdout=output,stderr=output,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
            wait_ready(port,process)
        try:
            start();check(not call()['installations'],'unexpected preexisting models')
            store=base/'weights';store.mkdir();root=call(dict(action='storage',path=str(store)))
            check((store/'mdo-model-store.json').is_file(),root)
            first=call(dict(action='import',path=str(source),mode='copy',name='First copy'));item=wait(first['id']);check(item['state']=='installed',item)
            blob=store/'blobs'/digest;check(blob.read_bytes()==payload,'copied resource mismatch')
            second=call(dict(action='import',path=str(source),mode='copy',name='Shared copy'));item=wait(second['id']);check(item['state']=='installed',item)
            check(len(list((store/'blobs').iterdir()))==1,'duplicate physical weight')
            check(call(dict(action='prune_cache',id=root['id']))['files']==0 and blob.exists(),'cache sweep removed installed resources')
            orphan=store/'blobs'/('f'*64);orphan.write_bytes(b'orphan');foreign=store/'blobs'/'user-not-a-hash.txt';foreign.write_text('keep');check(call(dict(action='cache',id=root['id']))['files']==1,'cache preview missed orphan');check(orphan.exists(),'cache preview mutated files');call(dict(action='prune_cache',id=root['id']));check(not orphan.exists() and blob.exists() and foreign.exists(),'cache sweep removed referenced/unknown content')
            call(dict(action='uninstall',id=first['id'],delete_files=True));check(blob.exists(),'shared resource removed')
            stop_host(process);process=None;start();check(second['id'] in call()['installations'],'installation lost across restart')
            call(dict(action='uninstall',id=second['id'],delete_files=True));check(not blob.exists() and source.exists(),'wrong copy ownership')
            reference=call(dict(action='import',path=str(source),mode='reference',name='Reference'));item=wait(reference['id']);check(item['state']=='installed',item)
            stop_host(process);process=None;start();check(call()['installations'][reference['id']]['state']=='installed' and not call()['read_only'],'external reference did not survive restart')
            call(dict(action='uninstall',id=reference['id'],delete_files=True));check(source.read_bytes()==payload,'reference original was changed')
            other=base/'other';other.mkdir();(other/'user.txt').write_text('keep');call(dict(action='storage',path=str(other)),status=422);check((other/'user.txt').read_text()=='keep','foreign directory was modified')
            bad=base/'bad.gguf';bad.write_bytes(b'not model weights');invalid=call(dict(action='import',path=str(bad),mode='copy'));item=wait(invalid['id']);check(item['state']=='failed','invalid GGUF accepted')
            call(dict(action='uninstall',id=invalid['id'],delete_files=True))
            marker=store/'mdo-model-store.json';original=marker.read_bytes();marker.write_text('{"id":"foreign"}')
            missing=call(dict(action='import',path=str(source),mode='copy'));item=wait(missing['id']);check(item['state']=='failed','store ownership change ignored');check(marker.read_text()=='{"id":"foreign"}','ownership marker overwritten')
            marker.write_bytes(original);call(dict(action='retry',id=missing['id']));item=wait(missing['id']);check(item['state']=='installed',item)
            newstore=base/'other-drive';newstore.mkdir();call(dict(action='storage',path=str(newstore)))
            linked=call(dict(action='import',path=str(blob),mode='reference'));item=wait(linked['id']);check(item['state']=='installed' and item['store']==missing['store'],item)
            call(dict(action='uninstall',id=missing['id'],delete_files=True));check(blob.exists(),'managed blob referenced from a different default store was removed')
            call(dict(action='uninstall',id=linked['id'],delete_files=True));check(blob.exists(),'reference uninstall deleted its original')
            third=call(dict(action='import',path=str(source),mode='copy'));item=wait(third['id']);check(item['state']=='installed',item)
            check((newstore/'blobs'/digest).exists(),'uninstall crossed resource roots')
            call(dict(action='cancel',id=third['id']),status=422);check(call()['installations'][third['id']]['state']=='installed','cancel changed a completed installation')
            record=home/'data/local-models/state.json';saved=record.read_bytes();record.unlink();record.mkdir()
            call(dict(action='import',path=str(source),mode='copy'),status=422);check(len(call()['installations'])==1,'failed state publication left a phantom queued job')
            record.rmdir();record.write_bytes(saved)
            stop_host(process);process=None;record=home/'data/local-models/state.json';saved=record.read_bytes();record.write_text('{broken');start()
            check(call()['read_only'],'corrupt registry is writable');call(dict(action='import',path=str(source),mode='copy'),status=422);check(record.read_text()=='{broken','corrupt registry was overwritten')
            stop_host(process);process=None;record.write_bytes(saved)
            malformed=json.loads(saved);malformed['installations'][third['id']]['assets'][0]['sha256']='../outside';record.write_text(json.dumps(malformed));start()
            check(call()['read_only'],'semantically invalid resource registry is writable');check(json.loads(record.read_bytes())==malformed,'invalid registry evidence overwritten')
            stop_host(process);process=None;record.write_bytes(saved)
        except BaseException as e:raise AssertionError(f'{e}\n{log.read_text(encoding="utf-8",errors="replace")[-5000:]}') from e
        finally:
            if process:stop_host(process)
        print(f'PASS {checks} local model import, resource ownership, restart and failure checks')

if __name__=='__main__':main()
