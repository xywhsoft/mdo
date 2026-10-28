"""Ordinary whole-directory ZIP publishing for build-time official tools."""
import hashlib
import json
from pathlib import Path
import re
import shutil
import stat
import unicodedata
import uuid
import zipfile

MAX_ARCHIVE=512*1048576
MAX_DIRECTORY=1024*1048576

def valid_path(path):
    if not isinstance(path,str) or not path or len(path.encode('utf-8'))>4096 or path.startswith('/') or '\\' in path or ':' in path:return False
    parts=path.rstrip('/').split('/')
    return all(p and p not in ('.','..') and len(p.encode('utf-8'))<=255 and not p.endswith(('.',' ')) and not re.match(r'^(con|prn|aux|nul|com[1-9]|lpt[1-9])(?:\.|$)',p,re.I) and not any(ord(c)<32 or ord(c)==127 or c in '<>"|?*' for c in p) for p in parts)

def digest(path):
    sha=hashlib.sha256()
    with Path(path).open('rb') as stream:
        for data in iter(lambda:stream.read(1048576),b''):sha.update(data)
    return sha.hexdigest()

def tool_documents(metadata):
    if metadata.get('platform') not in ('windows-x86_64','linux-x86_64','linux-x86_64-glibc','linux-x86_64-musl','linux-arm64','android-arm64-v8a','macos-arm64'):raise ValueError('Invalid tool platform')
    alias=metadata['id']
    if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9._-]{0,63}',alias):raise ValueError('Invalid tool alias')
    meta=dict(kind='external-tools',alias=alias,name=metadata['name'],entry='tool.json',platforms=[metadata['platform']])
    for key,limit in [('version',128),('description',2048),('copyright',4096),('license',1024),('author',240)]:
        if key in metadata:
            value=metadata[key]
            if not isinstance(value,str) or '\0' in value or len(value.encode())>limit:raise ValueError('Invalid '+key)
            meta[key]=value
    prompt=metadata.get('prompt','')
    if not isinstance(prompt,str) or not prompt or '\0' in prompt or len(prompt.encode())>16384:raise ValueError('Invalid tool usage prompt')
    config=dict(program=metadata['entry'],prompt=prompt,commands=metadata.get('commands',[]),links=metadata.get('links',[]))
    if not valid_path(config['program']):raise ValueError('Invalid tool program')
    if not isinstance(config['links'],list) or len(config['links'])>16 or any(not isinstance(link,str) or not link.startswith(('https://','http://')) or len(link.encode())>2048 or any(ord(c)<32 for c in link) for link in config['links']):raise ValueError('Invalid tool source links')
    names=set()
    for command in config['commands']:
        name=command.get('name');args=command.get('args',[])
        if not isinstance(name,str) or not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9._+-]{0,63}',name) or name.lower() in names or not valid_path(command.get('entry')) or not isinstance(args,list) or len(args)>64 or any(not isinstance(a,str) or '\0' in a or len(a.encode())>32768 for a in args):raise ValueError('Invalid tool command')
        names.add(name.lower())
    if len(names)>64:raise ValueError('Too many command entries')
    return meta,config

def zip_info(path,mode):
    info=zipfile.ZipInfo(path,(1980,1,1,0,0,0));info.create_system=3;info.external_attr=mode<<16|(16 if path.endswith('/') else 0);info.compress_type=zipfile.ZIP_DEFLATED
    return info

def package_tool(stage,metadata,output,extra_directories=None):
    stage=Path(stage);output=Path(output);meta,config=tool_documents(metadata);files=[];seen=set();total=0
    sources=[(path.relative_to(stage).as_posix(),path) for path in stage.rglob('*')]
    for prefix,directory in (extra_directories or {}).items():
        directory=Path(directory);sources.append((prefix,directory))
        sources.extend((prefix+'/'+p.relative_to(directory).as_posix(),p) for p in directory.rglob('*'))
    for relative,path in sorted(sources):
        mode=path.lstat().st_mode
        if stat.S_ISLNK(mode) or not (stat.S_ISDIR(mode) or stat.S_ISREG(mode)):raise ValueError('Unsupported directory entry: '+relative)
        if relative in ('extension.json','tool.json'):raise ValueError('Tool build source already contains reserved publication documents')
        name=relative+('/' if path.is_dir() else '');key=unicodedata.normalize('NFC',relative).casefold()
        if not valid_path(name) or key in seen:raise ValueError('Unsafe or aliased path: '+name)
        seen.add(key);files.append((name,path));total+=path.stat().st_size if path.is_file() else 0
        if len(files)>49998 or total>MAX_DIRECTORY:raise ValueError('Directory exceeds limits')
    available={name for name,_ in files if not name.endswith('/')}
    if config['program'] not in available or any(row['entry'] not in available for row in config['commands']):raise ValueError('Program or command dependency is missing')
    output.mkdir(parents=True,exist_ok=True);target=output/f"{meta['alias']}-{meta.get('version','unversioned')}-{metadata['platform']}.zip"
    if target.parent.resolve()!=output.resolve() or not re.fullmatch(r'[A-Za-z0-9._+-]+',target.name):raise ValueError('Invalid output name')
    pending=target.with_name(target.name+'.'+uuid.uuid4().hex+'.pending')
    try:
        with zipfile.ZipFile(pending,'w',compression=zipfile.ZIP_DEFLATED,compresslevel=6,allowZip64=True) as archive:
            archive.writestr(zip_info('extension.json',0o100644),json.dumps(meta,ensure_ascii=False,indent=2)+'\n')
            archive.writestr(zip_info('tool.json',0o100644),json.dumps(config,ensure_ascii=False,indent=2)+'\n')
            for name,path in files:
                directory=name.endswith('/');executable=bool(path.stat().st_mode&0o111)
                if not directory:
                    with path.open('rb') as stream:magic=stream.read(4)
                    executable=executable or magic.startswith((b'#!',b'\x7fELF',b'MZ'))
                info=zip_info(name,0o40755 if directory else 0o100755 if executable else 0o100644)
                with archive.open(info,'w') as sink:
                    if not directory:
                        with path.open('rb') as stream:shutil.copyfileobj(stream,sink,1048576)
        if pending.stat().st_size>MAX_ARCHIVE:raise ValueError('ZIP exceeds 512 MiB')
        with zipfile.ZipFile(pending) as check:
            if check.testzip() is not None:raise ValueError('ZIP verification failed')
        pending.replace(target)
    finally:
        pending.unlink(missing_ok=True)
    return {**meta,'manifest':meta,'platform':metadata['platform'],'sha256':digest(target),'size':target.stat().st_size,'file':target.name,'recommended':bool(metadata.get('recommended',False)),'upstream_version':metadata.get('upstream_version',metadata.get('version',''))}
