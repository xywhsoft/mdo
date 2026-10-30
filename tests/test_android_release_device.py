"""Bounded real-phone acceptance of process output and directory extensions.

Requires an independently installed debuggable QA APK. It never opens the
daily package, calls a paid model, clears application data, or uninstalls apps.
"""
from __future__ import annotations

import argparse
import base64
import hashlib
import json
from pathlib import Path
import shlex
import time
import traceback
import uuid

from android_release_client import AndroidReleaseClient
from test_application_sdk_runtime import COMMAND


def decoded_tool(value):
    assert value['success'], value
    text=value['text'];start=text.find('{')
    assert start>=0,text
    return json.JSONDecoder().raw_decode(text[start:])[0]


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--adb',type=Path,required=True);p.add_argument('--serial',required=True)
    p.add_argument('--package',required=True);p.add_argument('--edition',choices=('lite','full'),required=True)
    p.add_argument('--version',default=json.loads((Path(__file__).resolve().parents[1]/'app/release.json').read_text(encoding='utf-8'))['version_name'])
    p.add_argument('--evidence',type=Path,required=True)
    p.add_argument('--only',nargs='+',choices=('sdk-processes','managed-tools','directories','packaged-tools','command-ownership'))
    a=p.parse_args()
    if a.edition=='lite' and a.only and set(a.only)&{'packaged-tools','command-ownership'}:p.error('Packaged command checks require the full APK')
    a.evidence.mkdir(parents=True,exist_ok=False)
    c=AndroidReleaseClient(a.adb,a.serial,a.package)
    report=dict(package=a.package,edition=a.edition,paid_model_calls=0,checks=[],groups=[],passed=False)
    def check(value,name):
        assert value,name
        report['checks'].append(name)
    def read(path,offset=0,limit=1048576):
        data=bytearray()
        while len(data)<limit:
            count=min(65536,limit-len(data))
            value=c.call('fs.read',dict(path=path,offset=offset+len(data),limit=count))
            part=base64.b64decode(value['base64']);data.extend(part)
            if len(part)<count:break
        return bytes(data)
    def run(script,**options):
        return c.call('process.run',dict(program='/system/bin/sh',args=['-c',script],**options))
    def output(value):
        if value.get('native_path'):return read(value['native_path'])
        if 'base64' in value:return base64.b64decode(value['base64'])
        return value.get('text','').encode('utf-8')
    def group(name,body):
        if a.only and name not in a.only:return
        start=time.monotonic()
        try:body();status='passed'
        except Exception as error:
            status='failed';(a.evidence/(name+'.error.txt')).write_text(traceback.format_exc(),encoding='utf-8')
            print('FAIL',name,str(error)[:2000],flush=True)
        report['groups'].append(dict(name=name,status=status,seconds=round(time.monotonic()-start,3)))
        (a.evidence/'result.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
        print(status.upper(),name,flush=True)
    long_script="printf 'QA_BEGIN\\n'; /system/bin/toybox seq 1 20000; printf 'QA_END\\n'; /system/bin/toybox seq 1 4000 >&2"
    expected=b'QA_BEGIN\n'+b''.join(f'{i}\n'.encode() for i in range(1,20001))+b'QA_END\n'
    expected_err=b''.join(f'{i}\n'.encode() for i in range(1,4001))
    def processes():
        short=c.call('process.run',dict(program='/system/bin/sh',args=['-c',"printf '<%s>' \"$1\"; printf shorterr >&2; exit 7",'qa','']))
        check(short['exit_code']==7 and output(short['stdout'])==b'<>' and output(short['stderr'])==b'shorterr','SDK preserves empty argv, stdout, stderr and exit status')
        large=run(long_script)
        check(large['output_complete'] and not large['truncated'] and 'base64' not in large['stdout'],'Long SDK output uses files')
        check(output(large['stdout'])==expected and output(large['stderr'])==expected_err,'Long stdout and stderr retain every byte')
        check(read(large['stdout']['native_path'],len(expected)-7,7)==b'QA_END\n','SDK file output supports byte paging')
        report['persistent_output']=dict(path=large['stdout']['native_path'],sha256=hashlib.sha256(expected).hexdigest())
        chosen='qa-exclusive-'+uuid.uuid4().hex[:10]+'.log';first=run('printf chosen',stdout_path=chosen)
        denied=c.call('process.run',dict(program='/system/bin/sh',args=['-c','printf must-not-replace'],stdout_path=chosen),expected=409)
        check(not denied['details']['may_have_completed'] and output(first['stdout'])==b'chosen','Existing explicit output is never overwritten')
        limited=run('/system/bin/toybox head -c 20000 /dev/zero',max_output_bytes=1024)
        check(limited['truncated'] and not limited['output_complete'] and limited['output_error']=='output_limit' and limited['stdout']['bytes']==20000 and limited['stdout']['saved_bytes']==1024,'Output cap reports saved and observed byte counts')
        timed=c.call('process.run',dict(program='/system/bin/sh',args=['-c',"printf before-timeout; /system/bin/toybox sleep 5"],timeout_ms=300),expected=504)
        check(timed['details']['may_have_completed'] and output(timed['details']['result']['stdout'])==b'before-timeout','Timeout retains partial output')
        missing=c.call('process.run',dict(program='mdo-android-no-such-program'),expected=404)
        check(missing['code']=='sdk_native_failed' and missing['details']['cause'] and not missing['details']['may_have_completed'],'Missing process retains its actual failure cause')
        for fields in ({'max_output_bytes':0},{'merge_stderr':True,'stderr_path':'bad.log'},{'args':['\0']}):
            c.call('process.run',{'program':'/system/bin/sh',**fields},expected=400)
        check(True,'Invalid process configurations are rejected before execution')
        merged=run('printf out; printf err >&2',merge_stderr=True)
        merged_bytes=output(merged['stdout'])
        check(merged['stderr_merged'] and len(merged_bytes)==6 and merged_bytes.count(b'out')==1 and merged_bytes.count(b'err')==1,'Explicit stderr merge retains both streams')
        run('printf streaming-output',stream=True)
        check('process.stdout' in json.dumps(c.data('GET','/api/v1/application/events')),'Streaming events remain available')
    def managed_tools():
        session=c.data('POST','/api/v1/sessions',dict(project_id='default',title='Android process tool QA',model_id='android-fixture',reasoning_effort='none',permission_profile='full-access',max_output_tokens=1024))
        def tool(name,args):return c.call('tool.call',dict(project='default',session=session['id'],tool=name,arguments=args))
        result=decoded_tool(tool('exec',dict(argv=['/system/bin/sh','-c',long_script],timeout_ms=10000)))
        check(result['output_complete'] and output(result['stdout'])==expected,'Managed exec retains full stdout')
        locator=result['stdout']['path'];page=tool('read',dict(path=locator,start_line=1,max_lines=3,max_bytes=512))
        check(page['success'] and 'QA_BEGIN' in page['text'],'Managed read resolves @results logs')
        bad=tool('write',dict(path=locator,content='must-not-modify'))
        check(not bad['success'] and output(result['stdout'])==expected,'Result aliases are read only')
        spawned=decoded_tool(tool('spawn',dict(argv=['/system/bin/sh','-c',long_script+'; /system/bin/toybox sleep 0.1'])))
        identity=spawned['task_id'];time.sleep(.6)
        state=decoded_tool(tool('status',dict(task_id=identity)))
        check(state['finished'] and state['state']=='succeeded' and state['output_complete'],'Background task completes without output collection')
        check('text' not in state['stdout'] and output(state['stdout'])==expected,'Status exposes paths without consuming output')
        waiting=decoded_tool(tool('wait',dict(task_ids=[identity],timeout_ms=5000)))
        check(waiting['ready'] and output(state['stdout'])==expected,'Wait preserves the complete output file')
        live=decoded_tool(tool('spawn',dict(argv=['/system/bin/sh','-c','printf running; /system/bin/toybox sleep 20'])))
        stopped=decoded_tool(tool('stop',dict(task_id=live['task_id'],mode='kill_tree',wait_ms=5000)))
        check(stopped['finished'],'Stop drains the managed process tree')
        tools=c.data('GET','/api/v1/tools')['items'];names={row['id'] for row in tools}
        check({'status','ask'}<=names and not {'poll','ask_user'}&names,'Only compact tool names are advertised')
    def directories():
        for kind,prefix in dict(agents='agent',subagents='subagent',tools='tool',mcp='mcp',skills='skill',commands='command',plugins='plugin',knowledge='knowledge',**{'external-tools':'external-tool'}).items():
            for row in c.data('GET','/api/v1/extensions/'+kind)['items']:
                check(row['id'].startswith(prefix+':') and row['directory_id'],'Typed '+kind+' directory identities')
        ident='qa-device-knowledge';path='/api/v1/extensions/knowledge/'+ident
        existing=c.request('GET',path)[2];revision=existing.get('data',{}).get('revision','')
        manifest=dict(kind='knowledge',id='knowledge:qa.android-release',name='Android QA',alias=ident,entry='knowledge.json',platforms=['any'])
        index='refs/a/b/c/d/e/f/g/h/i/index.md'
        meta=dict(format='mdo.knowledge.v1',index=index,prompt='Consult {$path}/'+index,inject_prompt=True)
        c.data('PUT',path,dict(manifest=manifest,expected_revision=revision,edits=[dict(path='knowledge.json',text=json.dumps(meta)),dict(path=index,text='# Android QA\n')],trust_code=True))
        item=c.data('GET',path);before=item['revision']
        check(item['valid'] and c.data('GET',path+'/file?path='+index)['text']=='# Android QA\n','Deep knowledge directory saves and reads on Android')
        status,_,error=c.request('PUT',path,dict(manifest=manifest,expected_revision='0'*64,edits=[dict(path=index,text='must-not-replace')],trust_code=True))
        check(status in (409,412,422) and c.data('GET',path)['revision']==before,'Stale directory save preserves the current revision')
        for enabled in (False,True):
            c.data('POST',path+'/enabled',dict(enabled=enabled,expected_revision=before))
            check(c.data('GET',path)['desired_enabled']==enabled,'Knowledge activation '+str(enabled))
        c.data('DELETE',path,dict(expected_revision=before))
        check(c.request('GET',path)[0]==404,'Whole knowledge directory uninstall')
        ident='qa-device-sdk';path='/api/v1/extensions/commands/'+ident
        manifest=dict(kind='commands',id='command:qa.android-release',name='Android SDK QA',alias=ident,entry='command.c',platforms=['any'])
        source='/* mdo-command '+json.dumps(dict(id=ident,description='Android SDK regression',effects=['read'],requires_session=False,timeout_ms=10000))+' */\n#include <string.h>\n'+COMMAND
        current=c.request('GET',path)[2].get('data',{}).get('revision','')
        c.data('PUT',path,dict(manifest=manifest,expected_revision=current,edits=[dict(path='command.c',text=source)],trust_code=True))
        c.data('POST',path+'/enabled',dict(enabled=True))
        invocation='cmd-'+uuid.uuid4().hex
        c.data('POST','/api/v1/command-invocations?project=default',dict(invocation_id=invocation,command_id=ident,raw_arguments=json.dumps(['runtime.info',{}])))
        for _ in range(200):
            status=c.data('GET','/api/v1/command-invocations/'+invocation)
            if status['terminal']:break
            time.sleep(.1)
        expected_version=json.loads((Path(__file__).resolve().parents[1]/'app/release.json').read_text(encoding='utf-8'))['version_name']
        check(status['state']=='succeeded' and status['result']['version']==expected_version,'Real ARM64 TCC command calls the Application SDK')
        c.data('DELETE',path,dict(expected_revision=c.data('GET',path)['revision']))
    def full_tools():
        snapshot=c.data('GET','/api/v1/cli-tools');items=snapshot['items']
        expected={'busybox','curl','jq','openssh','aria2','ripgrep','7zip','python'}
        check(not snapshot.get('binding_error') and {row['alias'] for row in items}==expected and all(row['valid'] for row in items),'All eight original packaged tools initialize and bind')
        cases={'busybox':['echo','qa-busybox'],'curl':['--version'],'jq':['-n','--arg','s','中文 a b','$s'],'openssh':['-V'],'aria2':['--version'],'ripgrep':['--version'],'7zip':['i'],'python':['-I','-c','import sys,ssl,sqlite3; print("QA_PYTHON",repr(sys.argv[1]))','']}
        commands={'7zip':'7zz','aria2':'aria2c','openssh':'ssh','ripgrep':'rg'}
        report['tools']=[]
        for item in items:
            alias=item['alias'];args=cases[alias]
            result=c.call('process.run',dict(program=snapshot['bin_path']+'/'+commands.get(alias,alias),args=args,timeout_ms=20000))
            text=(output(result['stdout'])+output(result['stderr'])).decode('utf-8',errors='replace')
            check(result['exit_code']==0 and text.strip(),'APK UID executes '+alias+' through the command forwarder')
            report['tools'].append(dict(alias=alias,exit_code=result['exit_code'],preview=text[:200]))
        def execute(command,args):
            result=c.call('process.run',dict(program=snapshot['bin_path']+'/'+command,args=args,timeout_ms=20000))
            check(result['exit_code']==0,'Packaged '+command+' workflow exits successfully')
            return output(result['stdout'])
        python=next(item for item in items if item['alias']=='python')
        result=c.call('process.run',dict(program=snapshot['bin_path']+'/'+python['command'],args=['-I','-c','import sys;sys.stdout.buffer.write(b"x"*200000);sys.stderr.buffer.write(b"err"*6000)'],timeout_ms=10000))
        check(output(result['stdout'])==b'x'*200000 and output(result['stderr'])==b'err'*6000,'Packaged Python retains long stdout/stderr')
        path='/api/v1/extensions/external-tools/'+python['directory_id']
        for enabled in (False,True):
            c.data('POST',path+'/enabled',dict(enabled=enabled))
            environment=output(run('printf "%s|%s" "$MDO_PYTHON_HOME" "$PYTHONHOME"')['stdout'])
            expected=c.home+'/data/external-tools/android/python'
            check(environment==((expected+'|'+expected).encode() if enabled else b'|'),'Python environment follows its enabled state '+str(enabled))
        execute('python',['-I','-c','import ssl,sqlite3; print("python-restored")'])
        ripgrep=next(item for item in items if item['alias']=='ripgrep');identity=ripgrep['directory_id'];path='/api/v1/extensions/external-tools/'+identity
        c.data('POST',path+'/enabled',dict(enabled=False))
        disabled=c.data('GET','/api/v1/cli-tools')
        check(not next(row for row in disabled['items'] if row['id']==ripgrep['id'])['enabled'],'Packaged tool disable updates the catalogue')
        c.data('POST',path+'/enabled',dict(enabled=True))
        check(next(row for row in c.data('GET','/api/v1/cli-tools')['items'] if row['id']==ripgrep['id'])['enabled'],'Packaged tool enable restores the binding')
    def command_ownership():
        snapshot=c.data('GET','/api/v1/cli-tools')
        alias=snapshot['bin_path']+'/7zz';backup=alias+'.qa-'+uuid.uuid4().hex[:8];marker=b'QA user-owned entry\n'
        c.shell('run-as',c.package,'mv',alias,backup)
        try:
            c.shell('run-as',c.package,'sh','-c','printf %s '+shlex.quote(marker.decode())+' > '+shlex.quote(alias))
            status,_,error=c.request('POST','/api/v1/cli-tools',dict(action='refresh'))
            check(status==422 and 'unrecognized file' in error['error']['message'] and c.private_file(alias)==marker,'Unknown user files are preserved during command-link repair')
        finally:
            assert c.private_file(alias)==marker,'Refusing to replace a changed QA ownership fixture'
            c.shell('run-as',c.package,'rm',alias);c.shell('run-as',c.package,'mv',backup,alias)
        check(not c.data('POST','/api/v1/cli-tools',dict(action='refresh'))['binding_error'],'Command index recovers after its conflicting QA file is removed')
        native=c.shell('run-as',c.package,'readlink',alias)
        for target in ('/data/app/~~QA/other.package-QA/lib/arm64/libmdo_cli_forwarder.so',native.replace('libmdo_cli_forwarder.so','libxs.so'),native.replace('/data/app/','/data/app/../app/',1)):
            c.shell('run-as',c.package,'mv',alias,backup)
            try:
                c.shell('run-as',c.package,'ln','-s',target,alias)
                status,_,error=c.request('POST','/api/v1/cli-tools',dict(action='refresh'))
                check(status==422 and 'unrecognized file' in error['error']['message'] and c.shell('run-as',c.package,'readlink',alias)==target,'Command-link repair refuses another package, another library or an unnormalized path')
            finally:
                assert c.shell('run-as',c.package,'readlink',alias)==target,'Refusing to replace a changed QA link fixture'
                c.shell('run-as',c.package,'rm',alias);c.shell('run-as',c.package,'mv',backup,alias)
            check(not c.data('POST','/api/v1/cli-tools',dict(action='refresh'))['binding_error'],'Command index recovers after its conflicting QA link is removed')
    try:
        c.launch();info=c.call('runtime.info');snapshot=c.data('GET','/api/v1/cli-tools')
        check(info['version']==a.version and snapshot['edition']==a.edition,'Correct candidate version and Android edition')
        marker=c.home+'/workspace/qa-device-marker.txt';marker_bytes=b'Android release QA: preserve across lite/full update\n'
        if a.edition=='lite':c.call('fs.write',dict(path=marker,text=marker_bytes.decode()))
        else:check(read(marker)==marker_bytes,'Lite-to-full upgrade preserved the private Home marker')
        group('sdk-processes',processes);group('managed-tools',managed_tools);group('directories',directories)
        if a.edition=='full':group('packaged-tools',full_tools);group('command-ownership',command_ownership)
        old_pid=c.shell('pidof',c.package);c.launch()
        check(c.shell('pidof',c.package)!=old_pid and read(marker)==marker_bytes,'Cold restart preserves the Home marker')
        if report.get('persistent_output'):
            saved=report['persistent_output'];check(hashlib.sha256(read(saved['path'])).hexdigest()==saved['sha256'],'Cold restart preserves complete process output')
        report['passed']=all(row['status']=='passed' for row in report['groups'])
    finally:
        c.close();(a.evidence/'result.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    raise SystemExit(0 if report['passed'] else 1)


if __name__=='__main__':main()
