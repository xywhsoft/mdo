"""Package the bounded, verified Windows llama.cpp/Qwen3 starter catalogue.

Weights stay upstream. Runtime ZIPs retain pinned provenance and licenses;
model recipes bind the exact runtime publication IDs and archive hashes.
"""
import argparse, hashlib, json, sys, urllib.request, uuid, zipfile
from pathlib import Path
ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT/'tools'))
from extension_archives import digest,zip_info,tool_documents
from prepare_local_models import REVISION,WEIGHT_SHA,WEIGHT_SIZE
PINNED = {'cpu':'ca48f78bcf17c614375d8f4f8625eafc99e353b5b1f4c6797953dca67214b189',
          'vulkan':'846c47a25198dea5721056867cd047013a2d84b60ab844daa65f74076ac4e22f'}
def xid(text):return uuid.uuid5(uuid.NAMESPACE_URL,'https://mdo.xywhsoft.com/local-models/'+text).hex
def labels(zh,en,ru):return dict(zip(('zh-CN','en-US','ru-RU'),(zh,en,ru)))
def pack(path,files):
    with zipfile.ZipFile(path,'w',compression=zipfile.ZIP_DEFLATED,compresslevel=6) as z:
        for name,data in sorted(files.items()):
            if isinstance(data,dict):data=json.dumps(data,ensure_ascii=False,indent=2)+'\n'
            z.writestr(zip_info(name,0o100755 if name.endswith('.exe') else 0o100644),data)
def build(assets,output):
    output.mkdir(parents=True,exist_ok=True);license_path=assets/'llama-b12477-LICENSE'
    if not license_path.exists():
        with urllib.request.urlopen('https://raw.githubusercontent.com/ggml-org/llama.cpp/b12477/LICENSE',timeout=20) as r:license_path.write_bytes(r.read(16384))
    license_text=license_path.read_bytes()
    if not license_text.startswith(b'MIT License') or b'The ggml authors' not in license_text:raise ValueError('Unexpected upstream license')
    rows=[];runtimes=[];profiles=[]
    for backend,expected in PINNED.items():
        source=assets/f'llama-b12477-bin-win-{backend}-x64.zip'
        if digest(source)!=expected:raise ValueError('Pinned upstream runtime archive changed')
        proof=json.loads((assets/f'acceptance-{backend}.json').read_text(encoding='utf-8'))
        if not proof.get('passed') or proof.get('weight_sha256')!=WEIGHT_SHA:raise ValueError('Real acceptance has not passed for '+backend)
        alias='llama-b12477-windows-'+backend;ref='external-tool:mdo.'+alias
        url='https://github.com/ggml-org/llama.cpp/releases/download/b12477/'+source.name
        meta,tool=tool_documents(dict(id=alias,extension_id=ref,name='llama.cpp b12477 '+backend.upper(),platform='windows-x86_64',entry='llama-server.exe',version='b12477',author='ggml authors',license='MIT; LLVM OpenMP notices included',description='Pinned local inference runtime; model services are managed at Settings > Local models.',prompt='Runtime for mdo managed local models. Start and stop deployments from Settings > Local models.',commands=[dict(name=alias,entry='llama-server.exe')],links=[url,'https://github.com/ggml-org/llama.cpp/tree/b12477']))
        files={'extension.json':meta,'tool.json':tool,'LICENSE':license_text,'UPSTREAM.json':dict(url=url,sha256=expected,tag='b12477')}
        with zipfile.ZipFile(source) as z:
            for name in z.namelist():
                if name.endswith('.dll') or name in ('llama-server.exe','LICENSE-LLVM-OpenMP'):files[name]=z.read(name)
        target=output/(alias+'.zip');pack(target,files);sha=digest(target);version=xid(ref+'/'+sha)
        rows.append(dict(file=target.name,kind='external-tools',platform='windows-x86_64',size=target.stat().st_size,sha256=sha,work=xid(ref),version_id=version))
        runtimes.append(dict(id=backend,ref=ref,version_id=version,sha256=sha,engine='llama.cpp',build='b12477',platform='windows-x86_64',backend=backend,min_os='Windows 10 x64',cpu_features=['avx2']))
        profiles.append(dict(id=backend+'-16k',label=labels('CPU · 16K' if backend=='cpu' else 'Vulkan · 16K','CPU · 16K' if backend=='cpu' else 'Vulkan · 16K','CPU · 16K' if backend=='cpu' else 'Vulkan · 16K'),weight='q8',runtime=backend,context_tokens=16384,max_output_tokens=4096,output_tokens=1024,parallel=1,gpu_layers=0 if backend=='cpu' else -1,capabilities=['text','tools','reasoning'],reasoning=dict(kind='toggle'),requirements=dict(ram_bytes=(3 if backend=='cpu' else 1)*1024**3,vram_bytes=0 if backend=='cpu' else 3*1024**3,disk_bytes=WEIGHT_SIZE,margin_bytes=512*1024**2,basis='estimate',source_url='https://github.com/ggml-org/llama.cpp/blob/b12477/tools/server/README.md',checked_at='2026-11-18',notes=labels('Windows x64 已完成安装、对话与单次工具闭环测试；资源为预留估算，不是可用显存检测。小模型适合体验，复杂 Agent 任务请选更强模型。','Windows x64 installation, chat and a bounded tool task tested. Resource figures are estimates, not free-VRAM measurements. This small model is for trying local inference; use a stronger model for complex Agent work.','В Windows x64 проверены установка, чат и ограниченная задача с инструментом. Ресурсы оценочные, а не измерение свободной VRAM. Для сложных задач Agent нужна более мощная модель.'))))
    names=labels('Qwen3 0.6B · 本地体验','Qwen3 0.6B · Local starter','Qwen3 0.6B · Локальный старт')
    descriptions=labels('约 610 MiB 权重，适合验证本地运行与轻量对话。支持思考开关和工具格式；复杂任务可靠性有限。','About 610 MiB of weights for local inference and lightweight chat. Supports thinking control and tool format; complex task reliability is limited.','Около 610 MiB весов для локального запуска и лёгкого чата. Есть управление рассуждением и формат инструментов; надёжность сложных задач ограничена.')
    doc=dict(schema='mdo.local-model.v1',default_locale='zh-CN',i18n={l:dict(name=names[l],description=descriptions[l]) for l in names},license=dict(id='Apache-2.0',url=f'https://huggingface.co/Qwen/Qwen3-0.6B-GGUF/blob/{REVISION}/LICENSE'),upstream=dict(url='https://huggingface.co/Qwen/Qwen3-0.6B-GGUF',revision=REVISION),tags=[dict(id='lightweight',label=labels('轻量','Lightweight','Лёгкая')),dict(id='local-starter',label=labels('本地体验','Local starter','Для знакомства'))],weights=[dict(id='q8',format='gguf',quantization='Q8_0',files=[dict(id='weights',path='Qwen3-0.6B-Q8_0.gguf',role='weights',bytes=WEIGHT_SIZE,sha256=WEIGHT_SHA,urls=['https://modelscope.cn/models/Qwen/Qwen3-0.6B-GGUF/resolve/master/Qwen3-0.6B-Q8_0.gguf',f'https://huggingface.co/Qwen/Qwen3-0.6B-GGUF/resolve/{REVISION}/Qwen3-0.6B-Q8_0.gguf'],redirect_hosts=['cdn-lfs-cn-1.modelscope.cn','cas-bridge.xethub.hf.co','cdn-lfs.huggingface.co','cdn-lfs-us-1.hf.co'])])],runtimes=runtimes,profiles=profiles)
    meta=dict(kind='local-models',id='local-model:mdo.qwen3-0.6b-q8',alias='qwen3-0.6b-local',name=names['en-US'],entry='model.json',platforms=['windows-x86_64'],version='1.0.0',author='Qwen / mdo',license='Apache-2.0',description=descriptions['en-US'])
    target=output/'qwen3-0.6b-local.zip';pack(target,{'extension.json':meta,'model.json':doc,'README.md':'# Qwen3 0.6B local starter\n\nWeights are downloaded separately and verified against the official HF SHA-256. The ModelScope mirror is mutable but must match that fixed content. Runtime builds are immutable and shared. Windows CPU and AMD Vulkan accepted; other platforms are not published by this starter recipe.\n'})
    sha=digest(target);rows.append(dict(file=target.name,kind='local-models',platform='windows-x86_64',size=target.stat().st_size,sha256=sha,work=xid(meta['id']),version_id=xid(meta['id']+'/'+sha)))
    (output/'catalog.json').write_text(json.dumps(dict(schema='mdo.local-model-publication.v1',packages=rows),ensure_ascii=False,indent=2)+'\n',encoding='utf-8');return rows
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--assets',type=Path,default=ROOT/'.build/local-model-real');p.add_argument('--output',type=Path,default=ROOT/'.build/local-model-publication');a=p.parse_args();print(json.dumps(build(a.assets,a.output),indent=2))
