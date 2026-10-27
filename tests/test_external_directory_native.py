"""External tool discovery, portable bindings, ZIP and complete removal."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import uuid
import zipfile
from native_product_runner import product

ROOT = Path(__file__).resolve().parents[1]
ID = '0123456789ABCDEFGHJKMNPQRSTVWXYZ'
CONTRACT = r'''
static bool SdkPutBool(xvalue* R,cstr K,bool B){return xrtValueObjectSetNew(R,xrtStrView(K),xrtValueBool(B));}
xvalue* QaProductContract(const xvalue* P,xvfs V){
 xvalue* R=xrtValueObject();char Error[512]={0};bool Home=QaProductHome(SdkField(P,"root"),SdkField(P,"app"),V);
 bool Ok=Home&&MdoExtensionsInit()&&MdoCliInit(NULL);SdkPutBool(R,"initialized",Ok);
 xvalue* Snapshot=Ok?MdoCliSnapshot():NULL;const xvalue* Items=SdkGet(Snapshot,"items"),*Tool=xrtValueArrayGet(Items,0);
 bool Found=xrtValueCount(Items)==1&&CliBool(Tool,"valid",false)&&!strcmp(CliText(Tool,"id",96),"0123456789ABCDEFGHJKMNPQRSTVWXYZ");
 SdkPutBool(R,"directory_discovered",Found);SdkPut(R,"binding_error",CliText(Snapshot,"binding_error",256));
 char* Prompt=xrtStrDup("system");bool Injected=MdoCliToolPrompt(&Prompt)&&strstr(Prompt,"author model usage")&&!strstr(Prompt,"user description text");
 SdkPutBool(R,"prompt_is_separate",Injected);xrtFree(Prompt);
 if(SdkField(P,"action")&&!strcmp(SdkField(P,"action"),"remove")){
  Ok=Ok&&MdoExtensionsSetEnabled("external-tools","0123456789ABCDEFGHJKMNPQRSTVWXYZ",false,Error)&&MdoCliReload();
  bool Exists=false;xfileinfo Info;Ok=Ok&&MdoHomeExternalStat("data/external-tools/bin/qa-probe.exe",&Exists,&Info)&&!Exists;
  SdkPutBool(R,"disabled_bindings_removed",Ok);
  xvalue* Gone=NULL;Ok=Ok&&MdoExtensionsRemove("external-tools","0123456789ABCDEFGHJKMNPQRSTVWXYZ",&Gone,Error)&&MdoCliReload();xrtValueRelease(Gone);
  SdkPutBool(R,"whole_directory_removed",Ok);MdoCliUnit();Ok=Ok&&MdoCliInit(NULL);
  xvalue* Empty=Ok?MdoCliSnapshot():NULL;SdkPutBool(R,"restart_no_resurrection",Ok&&!xrtValueCount(SdkGet(Empty,"items")));xrtValueRelease(Empty);
 }else{
  char Hash[65];uint64 Bytes;bool Packed=MdoExtensionsPack("external-tools","0123456789ABCDEFGHJKMNPQRSTVWXYZ","data/extension-tasks/export/tool.zip",NULL,Hash,&Bytes,Error);
  SdkPutBool(R,"whole_directory_zip",Packed);
 }
 SdkPut(R,"error",Error);xrtValueRelease(Snapshot);MdoCliUnit();MdoExtensionsUnit();if(Home)MdoHomeUnit();return R;
}
'''


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--url', required=True); p.add_argument('--home', type=Path, required=True); p.add_argument('--project', default='default')
    a = p.parse_args(); out = ROOT / '.build/external-directory-native'; out.mkdir(parents=True, exist_ok=True)
    fixture = out / ('home-' + uuid.uuid4().hex[:12]); directory = fixture / 'external-tools' / ID
    (directory / 'bin').mkdir(parents=True)
    (directory / 'extension.json').write_text(json.dumps(dict(kind='external-tools', name='QA Program', alias='qa-probe', entry='tool.json', platforms=['windows-x86_64'], description='user description text')), encoding='utf-8')
    (directory / 'tool.json').write_text(json.dumps(dict(program='bin/probe.exe', command='qa-probe', aliases=['qa-alias'], prompt='author model usage', commands=[dict(name='qa-probe', entry='bin/probe.exe', args=['prefix'])])), encoding='utf-8')
    source = directory / 'probe.c'; source.write_text('#include <stdio.h>\n#include <windows.h>\nint wmain(int n,wchar_t** v){for(int i=1;i<n;i++){char s[4096];if(!WideCharToMultiByte(CP_UTF8,0,v[i],-1,s,sizeof(s),0,0))return 90;puts(s);}return 37;}\n')
    subprocess.run(['E:/software/w64devkit/bin/gcc.exe', '-Os', '-s', '-municode', str(source), '-o', str(directory / 'bin/probe.exe')], check=True)
    try:
        result = product(a, fixture, CONTRACT, out / 'initial.json')
        for key in ('initialized', 'directory_discovered', 'prompt_is_separate', 'whole_directory_zip'): assert result.get(key), (key, result)
        assert not result['binding_error'], result
        entry = fixture / 'data/external-tools/bin/qa-probe.exe'
        def run(path, expected):
            r = subprocess.run([str(path), 'unicode-中文', 'a b'], capture_output=True, timeout=10)
            assert r.returncode == 37 and r.stdout.decode().splitlines() == expected, (r.returncode, r.stdout, r.stderr)
        run(entry, ['prefix', 'unicode-中文', 'a b']); run(entry.with_name('qa-alias.exe'), ['unicode-中文', 'a b'])
        with zipfile.ZipFile(fixture / 'data/extension-tasks/export/tool.zip') as z:
            assert z.testzip() is None and set(z.namelist()) == {'extension.json', 'tool.json', 'probe.c', 'bin/', 'bin/probe.exe'}
        moved = fixture.with_name(fixture.name + '-moved'); fixture.rename(moved); fixture = moved
        run(fixture / 'data/external-tools/bin/qa-probe.exe', ['prefix', 'unicode-中文', 'a b'])
        contract = CONTRACT.replace('if(SdkField(P,"action")&&!strcmp(SdkField(P,"action"),"remove"))', 'if(true)')
        removed = product(a, fixture, contract, out / 'removed.json')
        for key in ('disabled_bindings_removed', 'whole_directory_removed', 'restart_no_resurrection'): assert removed.get(key), (key, removed)
        assert not (fixture / 'external-tools' / ID).exists()
        assert not (fixture / 'data/external-tools/bin/qa-alias.exe').exists()
        print(json.dumps(dict(passed=True, checks=['directory discovery', 'prompt vs description', 'real executable and prefix/alias', 'ordinary ZIP', 'move Home', 'disable/uninstall command cleanup', 'restart no resurrection']), ensure_ascii=False))
    finally:
        if fixture.resolve().parent == out.resolve() and fixture.name.startswith('home-'): shutil.rmtree(fixture)


if __name__ == '__main__': main()
