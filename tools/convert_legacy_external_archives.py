"""One-time operator conversion of old official objects to ordinary directory ZIPs.

Not shipped in the client or used by the new service. Reads existing objects;
does not delete them or mutate the old catalogue. No account data is accessed.
"""
import argparse
from contextlib import closing
import hashlib
import json
from pathlib import Path
import sqlite3
import struct
import unicodedata
import uuid
import zipfile
from extension_archives import digest,tool_documents,valid_path,zip_info,MAX_ARCHIVE,MAX_DIRECTORY

def convert(source,output,expected_hash=None):
    source,output=Path(source),Path(output)
    if source.resolve()==output.resolve():raise ValueError('Never overwrite an original object')
    original_hash=digest(source)
    if source.stat().st_size>MAX_ARCHIVE or expected_hash and original_hash!=expected_hash:raise ValueError('Old object integrity failed')
    temporary=output.with_name(output.name+'.'+uuid.uuid4().hex+'.converting');output.parent.mkdir(parents=True,exist_ok=True)
    try:
        with source.open('rb') as stream:
            header=stream.read(12)
            if len(header)!=12 or header[:8]!=b'MDOCLI1\n':raise ValueError('Not an old official CLI object')
            length=struct.unpack_from('<I',header,8)[0]
            if not 0<length<=1048576:raise ValueError('Invalid bounded legacy index')
            meta=json.loads(stream.read(length))
            if not isinstance(meta,dict):raise ValueError('Legacy index is not an object')
            manifest,config=tool_documents(meta);files=meta.get('files');seen=set();total=0
            if not isinstance(files,list) or not 0<len(files)<=49998:raise ValueError('Invalid legacy file count')
            for row in files:
                if not isinstance(row,dict):raise ValueError('Invalid legacy file row')
                path=row.get('path');size=row.get('size');sha=row.get('sha256');key=unicodedata.normalize('NFC',path).casefold() if isinstance(path,str) else None
                if not valid_path(path) or path.endswith('/') or path in ('extension.json','tool.json') or key in seen or type(size) is not int or size<0 or size>MAX_ARCHIVE or not isinstance(sha,str) or len(sha)!=64 or any(c not in '0123456789abcdef' for c in sha):raise ValueError('Invalid legacy file index')
                seen.add(key);total+=size
            if total>MAX_DIRECTORY or stream.tell()+total!=source.stat().st_size:raise ValueError('Legacy lengths do not match the real bytes')
            paths={row['path'] for row in files}
            if config['program'] not in paths or any(row['entry'] not in paths for row in config['commands']):raise ValueError('Legacy program dependency is missing')
            with zipfile.ZipFile(temporary,'x',compression=zipfile.ZIP_DEFLATED,compresslevel=6,allowZip64=True) as archive:
                archive.writestr(zip_info('extension.json',0o100644),json.dumps(manifest,ensure_ascii=False,indent=2)+'\n')
                archive.writestr(zip_info('tool.json',0o100644),json.dumps(config,ensure_ascii=False,indent=2)+'\n')
                for row in files:
                    sha=hashlib.sha256();remaining=row['size'];mode=0o100755 if row.get('mode',0)&0o111 else 0o100644
                    with archive.open(zip_info(row['path'],mode),'w') as target:
                        while remaining:
                            data=stream.read(min(1048576,remaining))
                            if not data:raise ValueError('Truncated legacy file')
                            target.write(data);sha.update(data);remaining-=len(data)
                    if sha.hexdigest()!=row['sha256']:raise ValueError('Legacy file digest mismatch: '+row['path'])
            if temporary.stat().st_size>MAX_ARCHIVE:raise ValueError('New ZIP exceeds limits')
            with zipfile.ZipFile(temporary) as check:
                if check.testzip() is not None:raise ValueError('New ZIP integrity failed')
        if digest(source)!=original_hash:raise ValueError('Original object changed during conversion')
        if output.exists():
            if digest(output)!=digest(temporary):raise ValueError('Destination has different contents')
        else:temporary.replace(output)
        return {**manifest,'manifest':manifest,'platform':meta['platform'],'file':output.name,'sha256':digest(output),'size':output.stat().st_size,'legacy_sha256':original_hash}
    finally:
        temporary.unlink(missing_ok=True)

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--database',type=Path,required=True);parser.add_argument('--output',type=Path,required=True);args=parser.parse_args()
    if not args.database.is_file():raise ValueError('Old catalogue is missing')
    args.output.mkdir(parents=True,exist_ok=True);rows=[];identities=set()
    with closing(sqlite3.connect(args.database.resolve().as_uri()+'?mode=ro',uri=True)) as db:
        db.row_factory=sqlite3.Row
        for record in db.execute("SELECT slug,platform,sha256,size FROM tool WHERE owner=0 AND state='published' AND withdrawn=0 ORDER BY slug,platform,id"):
            key=record['slug'],record['platform']
            if key in identities:raise ValueError('Multiple published legacy versions need explicit operator selection: '+str(key))
            identities.add(key);sha=record['sha256']
            if len(sha)!=64 or any(c not in '0123456789abcdef' for c in sha):raise ValueError('Invalid old object digest')
            source=args.database.parent/'objects'/sha
            if source.stat().st_size!=record['size']:raise ValueError('Old object size changed')
            name=f"{record['slug']}-{record['platform']}.zip"
            if Path(name).name!=name or not valid_path(name):raise ValueError('Invalid published identity')
            row=convert(source,args.output/name,sha)
            if row['alias']!=record['slug'] or row['platform']!=record['platform']:raise ValueError('Old identity does not match its archive')
            rows.append(row);print('CONVERTED',row['alias'],row['platform'],row['size'],flush=True)
    (args.output/'catalog.json').write_text(json.dumps({'tools':rows},ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
if __name__=='__main__':main()
