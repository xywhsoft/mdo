"""Real session-bound image/file reads, traversal, type and cross-session checks."""
import argparse,json,os
from pathlib import Path
from urllib.parse import quote
from markdown_runtime_support import markdown_host,markdown_session
from test_api_runtime import request

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--packed',type=Path,required=True);args=parser.parse_args()
    with markdown_host(args.packed) as (port,call,home):
        owner=markdown_session(port,home)
        prefix=f'/api/v1/projects/default/sessions/{owner["sessionId"]}/markdown-resource'
        def get(path,mode='image',expected=200):
            status,head,body=request(port,'GET',prefix+'?path='+quote(path,safe='')+'&mode='+mode)
            assert status==expected,(path,status,body[:400]);return head,body
        assert get('中文 图片.png')[0]['content-type']=='image/png'
        assert get(owner['image'])[1]==Path(owner['image']).read_bytes()
        _,body=get('中文 file.txt','file');assert json.loads(body)['data']['path'].replace('\\','/')==owner['file']
        get('中文 file.txt',expected=415)
        (home/'outside.png').write_bytes(Path(owner['image']).read_bytes())
        get('../outside.png',expected=404);get(str(home/'outside.png'),expected=404)
        get('missing.png',expected=404);get('bad\0.png',expected=400)
        artifact=home/'sessions/default'/owner['sessionId']/'artifacts';artifact.mkdir(exist_ok=True)
        (artifact/'图.png').write_bytes(Path(owner['image']).read_bytes())
        get(str(artifact/'图.png'))
        uploads=artifact.parent/'uploads';uploads.mkdir(exist_ok=True)
        (uploads/'图.png').write_bytes(Path(owner['image']).read_bytes())
        assert get('@uploads/图.png')[1]==Path(owner['image']).read_bytes()
        foreign=home/'sessions/default/another/artifacts';foreign.mkdir(parents=True)
        (foreign/'secret.png').write_bytes(Path(owner['image']).read_bytes())
        get(str(foreign/'secret.png'),expected=404)
        linked=Path(owner['workspace'])/'linked'
        symlink=False
        try:os.symlink(home,linked,target_is_directory=True);symlink=True
        except (OSError,NotImplementedError):
            if os.name=='nt':
                import _winapi
                _winapi.CreateJunction(str(home),str(linked));symlink=True
        if symlink:get('linked/outside.png',expected=404)
        print('PASS scoped images/files, Unicode/space/native paths, artifacts, missing/type/traversal/cross-session checks; symlink=',symlink)

if __name__=='__main__':main()
