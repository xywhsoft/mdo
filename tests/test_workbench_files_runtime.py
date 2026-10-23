"""File workbench behavior through the real xs/TCC SDK, with private files."""
import argparse,base64,json,os,subprocess,tempfile,time,uuid
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
from test_api_runtime import ROOT,write_site,free_port,request,wait_ready
from test_workbench_runtime import COMMAND

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--host',type=Path,required=True);args=parser.parse_args()
    with tempfile.TemporaryDirectory(dir=ROOT/'.build',prefix='workbench-files-') as raw:
        base=Path(raw);port=free_port();config=write_site(base,port);directory=base/'fixture';directory.mkdir()
        commands=base/'home/commands';commands.mkdir(parents=True)
        command=COMMAND.replace('mdo_result Code=mdoCall(S,Method,V->At(A,1),R,&E);','mdo_result Code=!strcmp(Method,"files.create")?mdoFilesCreate(S,V->At(A,1),R,&E):!strcmp(Method,"files.inspect")?mdoFilesInspect(S,V->At(A,1),R,&E):mdoCall(S,Method,V->At(A,1),R,&E);')
        (commands/'sdk.c').write_text('/* mdo-command {"id":"sdk","description":"File workbench fixture"} */\n#include <string.h>\n#include <mdo/workbench.h>\n'+command,encoding='utf-8')
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
            def native(method,params):
                identity='cmd-'+uuid.uuid4().hex
                code,_,body=request(port,'POST','/api/v1/command-invocations?project=default',body=json.dumps({'invocation_id':identity,'command_id':'sdk','raw_arguments':json.dumps([method,params])}).encode(),headers={'Content-Type':'application/json'});assert code==202,(code,body)
                for _ in range(300):
                    code,_,body=request(port,'GET','/api/v1/command-invocations/'+identity);value=json.loads(body)['data']
                    if value['terminal']:
                        assert value['state']=='succeeded',value
                        return value['result']
                    time.sleep(.03)
                raise AssertionError('Native file SDK command did not finish')
            try:
                wait_ready(port,process)
                default=Path(call('files.resolve',{'path':'.'})['path']);assert not default.exists(),'fixture already created the default workspace'
                call('files.resolve',{'path':'nested','prepare_default':True},400);assert not default.exists()
                assert call('files.resolve',{'path':'.','prepare_default':True})['path']==str(default) and default.is_dir()
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
                created=directory/'created.txt';metadata=native('files.create',{'path':str(created)})
                assert metadata['type']==1 and metadata['size']==0 and created.read_bytes()==b''
                created.write_text('retain original',encoding='utf-8')
                error=call('files.create',{'path':str(created)},409);assert error['details']['may_have_completed'] is False
                assert created.read_text()=='retain original'
                invalid=directory/'must-not-exist.txt';call('files.create',{'path':str(invalid),'text':'not an empty create'},400);assert not invalid.exists()
                concurrent=directory/'raced.txt'
                def create_race(_):
                    code,_,body=request(port,'POST','/api/v1/application/call',body=json.dumps({'method':'files.create','params':{'path':str(concurrent)},'context':{'project':'default','extension':'qa.files'}}).encode(),headers={'Content-Type':'application/json'});return code,json.loads(body)
                with ThreadPoolExecutor(max_workers=2) as pool:results=list(pool.map(create_race,range(2)))
                assert sorted(code for code,_ in results)==[200,409],results
                assert concurrent.read_bytes()==b''
                folder=directory/'created-folder';call('files.mkdir',{'path':str(folder),'exclusive':True});(folder/'keep.txt').write_text('keep')
                call('files.mkdir',{'path':str(folder),'exclusive':True},409);assert (folder/'keep.txt').read_text()=='keep'
                snapshot=call('files.stat',{'path':str(created)});created.write_text('changed after selection')
                destination=directory/'renamed.txt';call('files.move',{'path':str(created),'destination':str(destination),'revision':snapshot['revision']},412)
                call('files.remove',{'path':str(created),'revision':snapshot['revision']},412);assert created.read_text()=='changed after selection' and not destination.exists()
                fresh=call('files.stat',{'path':str(created)});destination.write_text('do not replace')
                call('files.move',{'path':str(created),'destination':str(destination),'revision':fresh['revision']},409)
                assert destination.read_text()=='do not replace' and created.exists()
                destination.unlink();call('files.move',{'path':str(created),'destination':str(destination),'revision':fresh['revision']});assert not created.exists() and destination.read_text()=='changed after selection'
                moved=call('files.stat',{'path':str(destination)});call('files.remove',{'path':str(destination),'revision':moved['revision']});assert not destination.exists()
                inspected=native('files.inspect',{'paths':[str(directory/'text.txt'),str(folder),str(destination)]})
                assert inspected[0]['revision']==saved['revision'] and inspected[1]['type']==2 and inspected[2]['status']==404,inspected
                call('files.inspect',{'paths':[]},400);call('files.inspect',{'paths':['.']*65},400);call('files.inspect',{'paths':[42]},400);call('files.inspect',{'paths':['.'],'space':'invalid'},400)
                stale_dir=call('files.stat',{'path':str(folder)});(folder/'late.txt').write_text('late')
                call('files.remove',{'path':str(folder),'revision':stale_dir['revision'],'recursive':True},412);assert (folder/'keep.txt').exists()
                outside=base/'outside';outside.mkdir();(outside/'sentinel.txt').write_text('keep outside')
                try:(folder/'link').symlink_to(outside,target_is_directory=True)
                except OSError:pass
                fresh_dir=call('files.stat',{'path':str(folder)});call('files.remove',{'path':str(folder),'revision':fresh_dir['revision'],'recursive':True})
                assert not folder.exists() and (outside/'sentinel.txt').read_text()=='keep outside','directory removal followed a link'
                (directory/'new.txt').write_text('new',encoding='utf-8')
                call('files.directory',{**path,'revision':revision},409)
                print('File workbench C/HTTP create races, guarded move/remove, no-overwrite, batched inspection, directory links, paging/search and stale-save protection: PASS')
            except Exception:
                log.flush();print((base/'host.log').read_text(encoding='utf-8',errors='replace')[-2500:]);raise
            finally:process.terminate();process.wait(timeout=10)
if __name__=='__main__':main()
