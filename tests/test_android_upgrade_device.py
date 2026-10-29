"""Reinstall one independent QA APK and verify Home plus command relocation."""
import argparse
import hashlib
import json
from pathlib import Path

from android_release_client import AndroidReleaseClient


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--adb',type=Path,required=True)
    parser.add_argument('--serial',required=True)
    parser.add_argument('--package',required=True)
    parser.add_argument('--apk',type=Path,required=True)
    parser.add_argument('--evidence',type=Path,required=True)
    args=parser.parse_args();args.evidence.mkdir(parents=True,exist_ok=False)
    client=AndroidReleaseClient(args.adb,args.serial,args.package)
    result=dict(package=args.package,passed=False,apk_sha256=hashlib.sha256(args.apk.read_bytes()).hexdigest())
    def files():
        listing=client.shell('run-as',client.package,'find','files/mdo-home/config','files/mdo-home/sessions','-type','f')
        return {name:hashlib.sha256(client.private_file(name)).hexdigest() for name in listing.splitlines() if name and not name.endswith('.lock')}
    try:
        client.shell('am','force-stop',client.package)
        before=files();old=client.shell('pm','path',client.package)
        (args.evidence/'before.json').write_text(json.dumps(before,indent=2)+'\n',encoding='utf-8')
        installed=client.adb('install','-r',str(args.apk.resolve()),timeout=240).decode()
        assert 'Success' in installed,installed
        after=files();result['files_checked']=len(before)
        result['changed_before_launch']=[name for name,sha in before.items() if after.get(name)!=sha]
        assert not result['changed_before_launch'],result
        current=client.shell('pm','path',client.package);result['native_directory_relocated']=old!=current
        assert result['native_directory_relocated'],'PackageManager reused the path; relocation was not exercised'
        client.launch();snapshot=client.data('GET','/api/v1/cli-tools')
        result['tool_count']=len(snapshot['items']);result['binding_error']=snapshot['binding_error']
        assert not result['binding_error'] and result['tool_count']==13 and all(row['valid'] for row in snapshot['items']),result
        command=client.call('process.run',dict(program=snapshot['bin_path']+'/python',args=['-I','-c','import ssl,sqlite3; print("upgrade-preserved")'],timeout_ms=10000))
        assert command['exit_code']==0 and 'upgrade-preserved' in command['stdout']['text'],command
        after=files();result['changed_after_launch']=[name for name,sha in before.items() if after.get(name)!=sha]
        assert not result['changed_after_launch'],result
        result['diagnostics']=client.data('GET','/api/v1/diagnostics')
        assert not result['diagnostics']['total'],result
        result['passed']=True
        print(json.dumps(result,ensure_ascii=False),flush=True)
    finally:
        client.close();(args.evidence/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')


if __name__=='__main__':main()
