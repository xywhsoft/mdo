#!/usr/bin/env python3
"""Operator publication of ordinary directory ZIPs; preview by default.

Only the new extensions/catalog.db is accepted. All bytes are checked before
writes. A temporary sealed receipt protects each object from live collection;
the publication transaction atomically replaces it with version references.
No account credentials or user/account tables are read or changed.
"""
import argparse
from contextlib import closing
import hashlib
import json
from pathlib import Path
import os
import shutil
import sqlite3
import stat
import sys
import time
import unicodedata
import uuid
import zipfile
sys.path.insert(0,str(Path(__file__).resolve().parents[2]))
from extension_archives import digest,tool_documents,valid_path,MAX_ARCHIVE,MAX_DIRECTORY


def inspect(row,incoming):
    name=row.get('file','');source=incoming/name
    if Path(name).name!=name or source.is_symlink() or source.stat().st_size!=row['size'] or not 0<row['size']<=MAX_ARCHIVE or digest(source)!=row['sha256']:raise ValueError('Archive integrity or path failed')
    with zipfile.ZipFile(source) as archive:
        infos=archive.infolist()
        if not 0<len(infos)<=50000 or sum(item.file_size for item in infos)>MAX_DIRECTORY:raise ValueError('Archive limits exceeded')
        seen=set();names={};
        for info in infos:
            key=unicodedata.normalize('NFC',info.filename.rstrip('/')).casefold();mode=info.external_attr>>16
            if not valid_path(info.filename) or key in seen or info.flag_bits&1 or info.file_size>MAX_ARCHIVE or stat.S_IFMT(mode) not in (0,stat.S_IFREG,stat.S_IFDIR):raise ValueError('Unsafe ZIP directory entry')
            seen.add(key);names[info.filename]=info
        for name in names:
            parts=name.rstrip('/').split('/')
            if any('/'.join(parts[:n]) in names for n in range(1,len(parts))):raise ValueError('ZIP contains a file/directory collision')
        for name,limit in [('extension.json',65536),('tool.json',262144)]:
            if name not in names or not 0<names[name].file_size<=limit:raise ValueError('Missing or oversized publication document')
        meta=json.loads(archive.read('extension.json'));config=json.loads(archive.read('tool.json'))
        if meta.get('kind')!='external-tools' or meta.get('entry')!='tool.json' or meta.get('platforms')!=[row['platform']] or any(key in meta for key in ('files','resources','base64','format','min_build_id','format_version')):raise ValueError('Not a whole-directory external tool')
        checked,recipe=tool_documents({**meta,'id':meta['alias'],'platform':row['platform'],'entry':config.get('program'),'prompt':config.get('prompt'),'links':config.get('links',[]),'commands':config.get('commands',[])})
        if recipe!=config or meta!=checked:raise ValueError('Unexpected official tool recipe or metadata')
        for path in [config['program'],*[item['entry'] for item in config['commands']]]:
            if path not in names or names[path].is_dir() or not names[path].file_size:raise ValueError('Program or command target is missing')
        if archive.testzip() is not None:raise ValueError('ZIP CRC failed')
    return meta,config,source


def publish(incoming,database,backup,apply=False):
    incoming,database,backup=Path(incoming),Path(database),Path(backup)
    if not database.is_file():raise ValueError('Initialize the new directory store first')
    catalogue=json.loads((incoming/'catalog.json').read_text(encoding='utf-8'));rows=catalogue['tools']
    if not isinstance(rows,list) or not 0<len(rows)<=500:raise ValueError('Invalid catalogue')
    prepared=[];identities=set();work_ids={}
    with closing(sqlite3.connect(database,timeout=15)) as db:
        db.execute('PRAGMA foreign_keys=ON')
        if db.execute('PRAGMA integrity_check').fetchone()[0]!='ok':raise ValueError('Database integrity failed')
        for name in ('work','version','published','upload','audit'):db.execute('SELECT * FROM '+name+' LIMIT 0')
        for row in rows:
            meta,config,source=inspect(row,incoming);key=meta['alias'],row['platform']
            if key in identities:raise ValueError('Duplicate publication head')
            identities.add(key)
            works=db.execute("SELECT xid,revision FROM work WHERE owner=0 AND kind='external-tools' AND alias=? AND deleted=0",(meta['alias'],)).fetchall()
            if len(works)>1:raise ValueError('Ambiguous official work identity')
            work=works[0][0] if works else work_ids.setdefault(meta['alias'],uuid.uuid4().hex)
            previous=db.execute("SELECT v.xid,v.sha256,v.manifest FROM published p JOIN version v ON v.xid=p.version WHERE p.work=? AND p.platform=?",(work,row['platform'])).fetchone()
            unchanged=previous and previous[1]==row['sha256']
            if previous and not unchanged and json.loads(previous[2]).get('version')==meta.get('version'):raise ValueError('An immutable published version has different bytes')
            upload_id=uuid.uuid4().hex;prepared.append(dict(row=row,meta=meta,source=source,work=work,existing=bool(works),unchanged=bool(unchanged),version=previous[0] if unchanged else uuid.uuid4().hex,upload=upload_id,source_url=next((url for url in config['links'] if url.startswith('https://')),'')))
            print('EXISTS' if unchanged else 'PUBLISH',meta['alias'],row['platform'],row['size'],flush=True)
        if not apply:return prepared
        if backup.exists():raise ValueError('Use a fresh backup path')
        backup.parent.mkdir(parents=True,exist_ok=True)
        with closing(sqlite3.connect(backup)) as destination:db.backup(destination)
        os.chmod(backup,0o600);now=int(time.time())
        with db:
            for item in prepared:
                if not item['unchanged']:
                    row=item['row'];db.execute("INSERT INTO upload(xid,owner,purpose,kind,sha256,bytes,state,manifest,created) VALUES(?,0,'archive','external-tools',?,?,'sealed',?,?)",(item['upload'],row['sha256'],row['size'],json.dumps(item['meta'],ensure_ascii=False),now))
        objects=database.parent/'objects';objects.mkdir(exist_ok=True)
        for item in prepared:
            if item['unchanged']:continue
            row=item['row'];destination=objects/row['sha256']
            if destination.exists():
                if destination.is_symlink() or digest(destination)!=row['sha256']:raise ValueError('Existing object is corrupt')
                continue
            temporary=objects/(row['sha256']+'.'+item['upload']+'.pending')
            try:
                with item['source'].open('rb') as source,temporary.open('xb') as target:
                    shutil.copyfileobj(source,target,1048576);target.flush();os.fsync(target.fileno())
                if digest(temporary)!=row['sha256']:raise ValueError('Copied object changed')
                os.replace(temporary,destination)
            finally:temporary.unlink(missing_ok=True)
        receipts=[]
        with db:
            db.execute('BEGIN IMMEDIATE')
            for item in prepared:
                row=item['row'];meta=item['meta'];work=item['work'];version=item['version']
                if item['unchanged']:
                    receipts.append(dict(work=work,version_id=version,alias=meta['alias'],platform=row['platform'],sha256=row['sha256'],inserted=False));continue
                # Re-check the identity under the write lock; never silently join
                # a work created concurrently after the preview.
                matches=db.execute("SELECT xid FROM work WHERE owner=0 AND kind='external-tools' AND alias=? AND deleted=0",(meta['alias'],)).fetchall()
                if item['existing'] and matches!=[(work,)] or not item['existing'] and matches and matches!=[(work,)]:raise ValueError('Official identity changed; refresh')
                if not matches:db.execute("INSERT INTO work(xid,owner,kind,alias,name,created) VALUES(?,0,'external-tools',?,?,?)",(work,meta['alias'],meta['name'],now))
                nonce=uuid.uuid4().hex;manifest=json.dumps(meta,ensure_ascii=False,separators=(',',':'));reason='Operator publication: pinned upstream provenance and complete directory ZIP; original license/source references retained.'
                db.execute("INSERT INTO version(xid,work,owner,nonce,request_digest,manifest,sha256,bytes,proof_hash,proof_bytes,notes,source_url,source_included,state,requested,created) VALUES(?,?,0,?,?,?,?,?,'',0,?,?,0,'published',1,?)",(version,work,nonce,hashlib.sha256(manifest.encode()).hexdigest(),manifest,row['sha256'],row['size'],reason,item['source_url'],now))
                db.execute('INSERT INTO published(work,platform,version) VALUES(?,?,?) ON CONFLICT(work,platform) DO UPDATE SET version=excluded.version',(work,row['platform'],version))
                db.execute('UPDATE work SET name=?,withdrawn=0,revision=revision+1 WHERE xid=?',(meta['name'],work))
                db.execute("INSERT INTO audit(actor,work,version,action,reason,created) VALUES(0,?,?,'operator_publish',?,?)",(work,version,reason,now))
                db.execute("UPDATE upload SET state='consumed' WHERE xid=? AND owner=0 AND state='sealed'",(item['upload'],))
                receipts.append(dict(work=work,version_id=version,alias=meta['alias'],platform=row['platform'],sha256=row['sha256'],inserted=True))
        (incoming/'publication-receipt.json').write_text(json.dumps(receipts,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
        print('PUBLISHED',len(receipts),flush=True);return receipts


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--incoming',type=Path,required=True);parser.add_argument('--database',type=Path,required=True);parser.add_argument('--backup',type=Path,required=True);parser.add_argument('--apply',action='store_true');args=parser.parse_args();publish(args.incoming,args.database,args.backup,args.apply)
if __name__=='__main__':main()
