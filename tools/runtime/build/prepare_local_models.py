"""Fetch a bounded, pinned real-model acceptance set without copying app/SDK."""
import argparse,hashlib,json,shutil,urllib.request,zipfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3]
REVISION='23749fefcc72300e3a2ad315e1317431b06b590a'
WEIGHT_SHA='9465e63a22add5354d9bb4b99e90117043c7124007664907259bd16d043bb031'
WEIGHT_SIZE=639446688
def digest(path):
    h=hashlib.sha256()
    with path.open('rb') as f:
        for chunk in iter(lambda:f.read(1024*1024),b''):h.update(chunk)
    return h.hexdigest()
def fetch(url,path,limit,expected=None,size=None):
    if path.exists() and (size is None or path.stat().st_size==size) and (expected is None or digest(path)==expected):return
    if shutil.disk_usage(path.parent).free<2*1024**3:raise RuntimeError('Keep at least 2 GiB free for this bounded acceptance set')
    # Public ModelScope mirror is directly reachable on this machine. No
    # credentials, model gateway or user proxy configuration is changed.
    opener=urllib.request.build_opener(urllib.request.ProxyHandler({})) if 'modelscope.cn/' in url else urllib.request.build_opener()
    part=path.with_suffix(path.suffix+'.part');have=part.stat().st_size if part.exists() else 0
    req=urllib.request.Request(url,headers={'User-Agent':'mdo-local-model-acceptance/1','Range':f'bytes={have}-'} if have else {'User-Agent':'mdo-local-model-acceptance/1'})
    with opener.open(req,timeout=40) as r:
        if r.status!=206:have=0
        reported=int(r.headers.get('Content-Length','0'));assert not reported or reported+have<=limit
        with part.open('ab' if have else 'wb') as out:
            while True:
                chunk=r.read(1024*1024)
                if not chunk:break
                have+=len(chunk)
                if have>limit:raise ValueError('Source exceeded the acceptance size bound')
                out.write(chunk)
                if have%(16*1024**2)<len(chunk):print(path.name,round(have/1024**2,1),'MiB',flush=True)
    if size is not None and have!=size:raise ValueError('Incomplete model weight; retained resumable partial')
    if expected and digest(part)!=expected:raise ValueError('Weight SHA-256 differs from the official HF LFS pointer')
    part.replace(path)
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,default=ROOT/'.build/local-model-real');p.add_argument('--download',action='store_true');a=p.parse_args();base=a.output.resolve();base.mkdir(parents=True,exist_ok=True)
    sources=[dict(name=f'llama-b12477-bin-win-{backend}-x64.zip',url=f'https://github.com/ggml-org/llama.cpp/releases/download/b12477/llama-b12477-bin-win-{backend}-x64.zip',limit=64*1024**2,backend=backend) for backend in ('cpu','vulkan')]
    sources.append(dict(name='Qwen3-0.6B-Q8_0.gguf',url='https://modelscope.cn/models/Qwen/Qwen3-0.6B-GGUF/resolve/master/Qwen3-0.6B-Q8_0.gguf',limit=WEIGHT_SIZE,size=WEIGHT_SIZE,sha256=WEIGHT_SHA,upstream=f'https://huggingface.co/Qwen/Qwen3-0.6B-GGUF/resolve/{REVISION}/Qwen3-0.6B-Q8_0.gguf'))
    if not a.download:print(json.dumps(sources,indent=2));return
    for item in sources:
        target=base/item['name'];fetch(item['url'],target,item['limit'],item.get('sha256'),item.get('size'));item.update(bytes=target.stat().st_size,sha256=digest(target))
        if target.suffix=='.zip':
            with zipfile.ZipFile(target) as archive:
                assert archive.testzip() is None
                item['archive_files']=len(archive.infolist());item['unpacked_bytes']=sum(f.file_size for f in archive.infolist())
        print('VERIFIED',item['name'],item['bytes'],item['sha256'],flush=True)
    (base/'sources.json').write_text(json.dumps(dict(schema='mdo.local-model-acceptance.v1',upstream_revision=REVISION,sources=sources),indent=2)+'\n',encoding='utf-8')
if __name__=='__main__':main()
