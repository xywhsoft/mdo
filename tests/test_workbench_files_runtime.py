"""File workbench behavior through the real xs/TCC SDK, with private files."""
import argparse,base64,json,os,subprocess,tempfile
from pathlib import Path
from test_api_runtime import ROOT,write_site,free_port,request,wait_ready

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--host',type=Path,required=True);args=parser.parse_args()
    with tempfile.TemporaryDirectory(dir=ROOT/'.build',prefix='workbench-files-') as raw:
        base=Path(raw);port=free_port();config=write_site(base,port);directory=base/'fixture';directory.mkdir()
        (directory/'nested').mkdir();(directory/'.git').mkdir();(directory/'.hidden').mkdir()
        (directory/'nested'/'小说.txt').write_text('第一行\n第二行\n',encoding='utf-8')
        (directory/'.git'/'internal.txt').write_text('metadata',encoding='utf-8')
        (directory/'.hidden'/'private.txt').write_text('hidden',encoding='utf-8')
        (directory/'bytes.bin').write_bytes(bytes(range(256)))
        (directory/'text.txt').write_text('original',encoding='utf-8')
        try:os.symlink(directory,directory/'nested'/'loop',target_is_directory=True)
        except OSError:pass
        with (base/'host.log').open('wb') as log:
            process=subprocess.Popen([str(args.host.resolve()),str(config),'--','--home',str(base/'home')],cwd=base,env={**os.environ,'USE_WEBVIEW':'0'},stdout=log,stderr=log)
            def call(method,params,status=200):
                code,_,body=request(port,'POST','/api/v1/application/call',body=json.dumps({'method':method,'params':params,'context':{'project':'default','extension':'qa.files'}}).encode(),headers={'Content-Type':'application/json'})
                value=json.loads(body);assert code==status,(method,code,value)
                return value['data'] if status==200 else value['error']
            try:
                wait_ready(port,process)
                path={'path':str(directory),'space':'native'}
                page=call('files.directory',{**path,'limit':1});revision=page['revision'];names=[];offset=0
                for _ in range(20):
                    page=call('files.directory',{**path,'limit':1,'offset':offset,'revision':revision});names.extend(row['name'] for row in page['items'])
                    assert page['next']>offset or not page['more'];offset=page['next']
                    if not page['more']:break
                assert {'nested','text.txt','bytes.bin'}<=set(names) and '.hidden' not in names
                hidden=call('files.directory',{**path,'hidden':True});assert '.hidden' in [row['name'] for row in hidden['items']]
                found=call('files.search',{**path,'query':'小说'});assert [row['path'] for row in found['items']]==['nested/小说.txt'];assert not found['incomplete'],found
                found=call('files.search',{**path,'query':'.txt','hidden':True});assert '.hidden/private.txt' in [row['path'] for row in found['items']]
                assert not any(row['path'].startswith('.git/') for row in found['items'])
                limited=call('files.search',{**path,'query':'.txt','limit':1});assert limited['incomplete'] and len(limited['items'])==1
                call('files.search',{**path,'query':'','limit':1},400)
                call('files.directory',{'path':str(directory/'bytes.bin')},400)
                binary=call('files.read',{'path':str(directory/'bytes.bin'),'offset':250,'limit':6});assert base64.b64decode(binary['base64'])==bytes(range(250,256))
                assert 'text' not in binary
                file={'path':str(directory/'text.txt')};before=call('files.read',file)
                (directory/'text.txt').write_text('external change',encoding='utf-8')
                call('files.write',{**file,'revision':before['revision'],'text':'must not overwrite'},412)
                assert (directory/'text.txt').read_text(encoding='utf-8')=='external change'
                current=call('files.stat',file);saved=call('files.write',{**file,'revision':current['revision'],'text':'saved ✓'})
                assert saved['revision']!=current['revision'] and (directory/'text.txt').read_text(encoding='utf-8')=='saved ✓'
                (directory/'new.txt').write_text('new',encoding='utf-8')
                call('files.directory',{**path,'revision':revision},409)
                print('File workbench pagination, hidden files, bounded search, link cycle, byte ranges and stale-save protection: PASS')
            except Exception:
                log.flush();print((base/'host.log').read_text(encoding='utf-8',errors='replace')[-2500:]);raise
            finally:process.terminate();process.wait(timeout=10)
if __name__=='__main__':main()
