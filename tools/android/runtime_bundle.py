"""Full APK: whole-directory ZIPs plus Android OS-installed native images.

JSON is only the signed APK index. Files remain real ZIP entries. Android
forbids exec/dlopen from writable Home, so ELF images additionally occupy their
required lib/*.so install entries. Execution links are a disposable host cache.
"""
import json
import re
import sys
import tempfile
from pathlib import Path
import zipfile

ROOT=Path(__file__).resolve().parents[2]
if str(ROOT/'tools') not in sys.path:sys.path.insert(0,str(ROOT/'tools'))
from extension_archives import package_tool,digest,valid_path

APPLETS=('sh','ash','awk','sed','grep','find','cat','head','tail','sort','wc','cp','mv','rm','mkdir','rmdir','touch','tar','gzip','gunzip','unzip','xargs','tee','tr','which','basename','dirname','ls','pwd','echo')

def add_runtime(archive,root:Path,edition:str,build_id:int,revision:int):
    if edition not in ('full','lite'):raise ValueError('Invalid APK edition')
    definitions={r['id']:r for r in json.loads((ROOT/'tools/runtime/official.json').read_text(encoding='utf-8'))['tools']}
    records={r['id']:r for r in json.loads((ROOT/'tools/runtime/catalog.json').read_text(encoding='utf-8'))['tools'] if r['platform']=='android-arm64-v8a'}
    extensions=[];files=[];names={};executables={}
    if edition=='full':
        for directory in sorted(root.iterdir()):
            if not directory.is_dir() or directory.name=='fonts':continue
            tool=directory.name
            if tool not in definitions:raise ValueError('No official metadata for '+tool)
            definition=definitions[tool];record=records.get(tool,{})
            entries=[p for p in record.get('entrypoints',[]) if (directory/p).is_file()]
            if not entries:
                for p in sorted(directory.rglob('*')):
                    if not p.is_file():continue
                    with p.open('rb') as stream:elf=stream.read(4)==b'\x7fELF'
                    if elf:entries=[p.relative_to(directory).as_posix()];break
            if not entries:raise ValueError('No native program for '+tool)
            native={};existing=set()
            for file in sorted(directory.rglob('*')):
                if file.is_symlink():raise ValueError('Runtime source contains a link')
                if not file.is_file():continue
                path=file.relative_to(root).as_posix();existing.add(path)
                if not valid_path(path):raise ValueError('Unsafe native path')
                with file.open('rb') as source:elf=source.read(4)==b'\x7fELF'
                if not elf:continue
                library=file.name.startswith('lib') and file.suffix=='.so'
                name=file.name if library else 'libmdo_'+path.replace('/','_').replace('.','_')+'.so'
                if not re.fullmatch(r'lib[A-Za-z0-9_.+-]+\.so',name):raise ValueError('invalid native runtime name: '+name)
                sha=digest(file)
                if not library and sha in executables:name=executables[sha]
                if name in names and names[name]!=sha:raise ValueError('conflicting native runtime name: '+name)
                if name not in names:
                    names[name]=sha;archive.write(file,'lib/arm64-v8a/'+name,compress_type=zipfile.ZIP_STORED)
                if not library:executables[sha]=name
                native[path]=(name,sha);files.append(dict(tool=tool,path=path[len(tool)+1:],native=name,sha256=sha))
            aliases=directory/'native-aliases.json'
            if aliases.is_file():
                values=json.loads(aliases.read_text(encoding='utf-8'))
                if not isinstance(values,dict) or len(values)>1024:raise ValueError('invalid native aliases')
                for path,target in sorted(values.items()):
                    if not isinstance(path,str) or not valid_path(path) or not path.startswith(tool+'/') or path in existing or target not in native:raise ValueError('invalid native alias target')
                    name,sha=native[target];files.append(dict(tool=tool,path=path[len(tool)+1:],native=name,sha256=sha,alias=True));existing.add(path)
            commands=[dict(name=Path(p).name,entry=p,args=[]) for p in entries]
            if tool=='busybox':commands.extend(dict(name=p,entry=entries[0],args=[p]) for p in APPLETS)
            if tool=='python':
                for name in ('python','python3'):
                    if name not in {c['name'] for c in commands}:commands.append(dict(name=name,entry=entries[0],args=[]))
            metadata=dict(id=tool,name=definition['name'],description=definition['description'],prompt=definition['prompt'],license=record.get('license',definition['license']),links=definition['links'],version=record.get('version',definition.get('versions',{}).get('android-arm64-v8a','1')),platform='android-arm64-v8a',entry=entries[0],commands=commands)
            extra={'fonts':root/'fonts'} if tool in ('poppler','imagemagick') and (root/'fonts').is_dir() else None
            with tempfile.TemporaryDirectory(prefix='mdo-apk-zip-') as temporary:
                row=package_tool(directory,metadata,Path(temporary),extra)
                asset='mdo-extensions/'+tool+'.zip'
                archive.write(Path(temporary)/row['file'],'assets/'+asset,compress_type=zipfile.ZIP_STORED)
                extensions.append(dict(id=tool,asset='assets/'+asset,sha256=row['sha256'],bytes=row['size']))
    release=json.loads((ROOT/'app/release.json').read_text(encoding='utf-8'))
    archive.writestr('assets/mdo-runtime.json',json.dumps(dict(edition=edition,build_id=str(build_id),version_code=release['version_code'].replace('.','').upper(),extensions=extensions,files=files),separators=(',',':')))
