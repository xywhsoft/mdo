"""Inspect actual products: the forwarder belongs only in Windows VFS, and
Android lite/full retain their distinct PackageManager-owned native payloads."""
import argparse,hashlib,json,subprocess,tempfile,zipfile
from pathlib import Path
from test_android_distribution import inspect,ROOT

def run(products,host):
    image=None;release=json.loads((ROOT/'app/release.json').read_text());rows=[]
    targets=[('windows',products/'windows/mdo.exe'),('windows',products/'windows/mdo-server.exe')]
    targets += [('linux',products/f'linux/linux-x86_64-{libc}-{edition}'/('mdo' if edition=='gui' else 'mdo-server')) for libc in ('glibc','musl') for edition in ('gui','server')]
    targets += [(edition,products/f'android/mdo-{edition}.apk') for edition in ('lite','full')]
    for platform,path in targets:
        assert path.is_file(),path
        with tempfile.TemporaryDirectory(prefix='cli-pack-',dir=ROOT/'.build') as temp:
            folder=Path(temp);packed=path
            if platform in ('lite','full'):
                row=inspect(platform,release,path)
                with zipfile.ZipFile(path) as apk:
                    packed=folder/'app.xrtpack';packed.write_bytes(apk.read('assets/app.xrtpack'))
                    assert not any(n.endswith('.exe') for n in apk.namelist())
                    if platform=='full':assert {'assets/mdo-extensions/aria2.zip','assets/mdo-extensions/ripgrep.zip','assets/mdo-extensions/7zip.zip'}<=set(apk.namelist())
            else:row={}
            unpack=folder/'unpacked';subprocess.run([str(host),'pack','--extract',str(packed),'-d',str(unpack)],check=True,capture_output=True)
            helper=unpack/'generated/native/windows/cli-forwarder.exe';assert helper.is_file()==(platform=='windows'),path
            if helper.is_file():
                if image is None:image=helper.read_bytes()
                else:assert helper.read_bytes()==image,path
            for relative in ('src/distribution/cli_bindings.inc.c','src/distribution/cli.inc.c','src/distribution/android_tools.c','include/mdo/extension_zip.h'):
                assert (unpack/relative).read_bytes()==(ROOT/'app'/relative).read_bytes(),(path,'stale source',relative)
            for retired in ('src/distribution/cli_archive.inc.c','include/mdo/cli_package.h','include/mdo/ecosystem_package.h'):assert not (unpack/retired).exists(),(path,'retired protocol',retired)
            rows.append({**row,'platform':platform,'file':str(path.relative_to(products)),'bytes':path.stat().st_size,'sha256':hashlib.sha256(path.read_bytes()).hexdigest()})
    result={'passed':True,'forwarder_bytes':len(image),'forwarder_sha256':hashlib.sha256(image).hexdigest(),'products':rows}
    (products/'validation.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8');print(json.dumps(result))

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--products',type=Path,required=True);p.add_argument('--host',type=Path,default=ROOT/'.build/host/xs.exe');a=p.parse_args();run(a.products.resolve(),a.host.resolve())
