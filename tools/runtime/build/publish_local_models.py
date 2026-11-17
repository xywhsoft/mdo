"""Publish a natively validated starter catalogue; preview unless --apply.

This operator path writes only the extension store. It never reads accounts,
provider credentials, gateway routes or billing tables. Immutable IDs and ZIP
bytes are checked again under the publication transaction.
"""
import argparse, hashlib, json, os, shutil, sqlite3, time, uuid, zipfile
from contextlib import closing
from pathlib import Path
from publish_optional_tools import inspect as inspect_tool

def digest(path):
    h=hashlib.sha256()
    with path.open('rb') as f:
        for b in iter(lambda:f.read(1048576),b''):h.update(b)
    return h.hexdigest()
def identity(db,meta,work):
    found=db.execute('SELECT work,owner,kind FROM identity WHERE ref=?',(meta['id'],)).fetchone()
    if found and found!=(work,0,meta['kind']):raise ValueError('An immutable logical ID belongs to another work')
    other=db.execute('SELECT ref FROM identity WHERE work=?',(work,)).fetchone()
    if other and other!=(meta['id'],):raise ValueError('A work cannot change its logical ID')
def prepare(incoming,db):
    rows=json.loads((incoming/'catalog.json').read_text(encoding='utf-8'))['packages']
    proof=json.loads((incoming/'native-validation.json').read_text(encoding='utf-8'))
    if not proof.get('passed') or not proof.get('validator_sha256'):raise ValueError('Native package validation is required')
    verified={r['version_id']:r['sha256'] for r in proof['packages']};items=[];seen=set()
    if not 0<len(rows)<=16:raise ValueError('Unbounded starter catalogue')
    for row in rows:
        if any(not isinstance(row.get(k),str) or len(row[k])!=32 or any(c not in '-0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ_abcdefghijklmnopqrstuvwxyz' for c in row[k]) for k in ('work','version_id')):raise ValueError('Invalid immutable identity')
        source=incoming/row['file']
        if Path(row['file']).name!=row['file'] or source.is_symlink() or source.stat().st_size!=row['size'] or digest(source)!=row['sha256'] or verified.get(row['version_id'])!=row['sha256']:raise ValueError('Package differs from native verification')
        if row['kind']=='external-tools':meta,config,_=inspect_tool(row,incoming);doc=None;source_url=config['links'][0]
        elif row['kind']=='local-models':
            if row['size']>1048576:raise ValueError('Recipe exceeds the small-package bound')
            with zipfile.ZipFile(source) as z:meta=json.loads(z.read('extension.json'));doc=json.loads(z.read('model.json'))
            if meta.get('kind')!='local-models' or meta.get('entry')!='model.json' or meta.get('platforms')!=[row['platform']]:raise ValueError('Wrong recipe identity or platform')
            source_url=doc['upstream']['url']
        else:raise ValueError('Only local-models and their runtime tools may be published')
        if meta['id'] in seen:raise ValueError('Duplicate logical identity')
        seen.add(meta['id']);identity(db,meta,row['work'])
        works=db.execute('SELECT owner,kind,alias,deleted FROM work WHERE xid=?',(row['work'],)).fetchone()
        if works and works!=(0,meta['kind'],meta['alias'],0):raise ValueError('Work identity is already used')
        aliases=db.execute('SELECT xid FROM work WHERE owner=0 AND kind=? AND alias=? AND deleted=0',(meta['kind'],meta['alias'])).fetchall()
        if aliases and aliases!=[(row['work'],)]:raise ValueError('Alias is already published under another work')
        old=db.execute('SELECT work,sha256,state,manifest FROM version WHERE xid=?',(row['version_id'],)).fetchone()
        manifest=json.dumps(meta,ensure_ascii=False,separators=(',',':'))
        head=db.execute('SELECT v.sha256,v.manifest,w.withdrawn FROM published p JOIN version v ON v.xid=p.version JOIN work w ON w.xid=p.work WHERE p.work=? AND p.platform=?',(row['work'],row['platform'])).fetchone()
        if head and head[2]:raise ValueError('This work is withdrawn; review before republishing')
        if head and head[0]!=row['sha256'] and json.loads(head[1]).get('version')==meta.get('version'):raise ValueError('An immutable displayed version has different bytes')
        if old and (old[:3]!=(row['work'],row['sha256'],'published') or json.loads(old[3])!=meta):raise ValueError('Immutable version changed')
        items.append(dict(row=row,meta=meta,doc=doc,source=source,source_url=source_url,manifest=manifest,exists=bool(old),upload=uuid.uuid4().hex))
    for item in items:
        if not item['doc']:continue
        for r in item['doc']['runtimes']:
            staged=next((i for i in items if i['row']['version_id']==r['version_id']),None)
            if staged:
                if staged['meta']['id']!=r['ref'] or staged['row']['sha256']!=r['sha256'] or staged['row']['platform']!=r['platform'] or staged['meta']['kind']!='external-tools':raise ValueError('Wrong staged runtime binding')
            else:
                v=db.execute("SELECT v.sha256,v.manifest,w.withdrawn,w.deleted FROM version v JOIN work w ON w.xid=v.work WHERE v.xid=? AND v.state='published'",(r['version_id'],)).fetchone()
                if not v or v[0]!=r['sha256'] or json.loads(v[1]).get('id')!=r['ref'] or v[2] or v[3]:raise ValueError('Pinned public runtime is missing')
    return items
def publish(incoming,database,backup,apply=False):
    incoming,database,backup=map(Path,(incoming,database,backup))
    if not database.is_file():raise ValueError('Extension database does not exist')
    with closing(sqlite3.connect(database,timeout=15)) as db:
        db.execute('PRAGMA foreign_keys=ON')
        if db.execute('PRAGMA integrity_check').fetchone()[0]!='ok':raise ValueError('Database integrity failed')
        for table in ('work','version','published','identity','upload','audit','local_model_index'):db.execute('SELECT * FROM '+table+' LIMIT 0')
        items=prepare(incoming,db)
        if not apply:return [dict(**i['row'],exists=i['exists']) for i in items]
        if backup.exists():raise ValueError('Choose a new backup path')
        backup.parent.mkdir(parents=True,exist_ok=True)
        with closing(sqlite3.connect(backup)) as dst:db.backup(dst)
        os.chmod(backup,0o600);now=int(time.time())
        # Sealed receipts keep uploaded objects alive while being staged.
        with db:
            for i in items:
                if not i['exists']:db.execute("INSERT INTO upload(xid,owner,purpose,kind,sha256,bytes,state,manifest,created) VALUES(?,0,'archive',?,?,?,'sealed',?,?)",(i['upload'],i['meta']['kind'],i['row']['sha256'],i['row']['size'],i['manifest'],now))
        objects=database.parent/'objects';objects.mkdir(exist_ok=True)
        for i in items:
            target=objects/i['row']['sha256']
            if target.exists():
                if target.is_symlink() or digest(target)!=i['row']['sha256']:raise ValueError('Existing object is corrupt')
                continue
            temp=objects/(target.name+'.'+i['upload']+'.pending')
            try:
                with i['source'].open('rb') as src,temp.open('xb') as dst:shutil.copyfileobj(src,dst,1048576);dst.flush();os.fsync(dst.fileno())
                if digest(temp)!=i['row']['sha256']:raise ValueError('Object changed during staging')
                os.replace(temp,target)
            finally:temp.unlink(missing_ok=True)
        with db:
            db.execute('BEGIN IMMEDIATE');checked=prepare(incoming,db)
            for i in checked:
                r,m=i['row'],i['meta'];work,version=r['work'],r['version_id']
                if not i['exists']:
                    reason='Operator publication: Windows CPU/Vulkan real acceptance; exact runtime/weight hashes and upstream licenses retained.'
                    db.execute('INSERT OR IGNORE INTO work(xid,owner,kind,alias,name,created) VALUES(?,0,?,?,?,?)',(work,m['kind'],m['alias'],m['name'],now))
                    identity(db,m,work);db.execute('INSERT OR IGNORE INTO identity(ref,work,owner,kind,created) VALUES(?,?,0,?,?)',(m['id'],work,m['kind'],now))
                    db.execute("INSERT INTO version(xid,work,owner,nonce,request_digest,manifest,sha256,bytes,proof_hash,proof_bytes,notes,source_url,source_included,state,requested,created) VALUES(?,?,0,?,?,?,?,?,'',0,?,?,0,'published',1,?)",(version,work,uuid.uuid4().hex,hashlib.sha256(i['manifest'].encode()).hexdigest(),i['manifest'],r['sha256'],r['size'],reason,i['source_url'],now))
                    if i['doc']:db.execute('INSERT INTO local_model_index(version,search_text) VALUES(?,?)',(version,json.dumps(i['doc'],ensure_ascii=False)))
                    db.execute("INSERT INTO audit(actor,work,version,action,reason,created) VALUES(0,?,?,'operator_publish',?,?)",(work,version,reason,now))
                # Do not let an idempotent old package replace a newer head.
                head=db.execute('SELECT version FROM published WHERE work=? AND platform=?',(work,r['platform'])).fetchone()
                if i['exists'] and head and head!=(version,):raise ValueError('A newer publication head exists; refuse downgrade')
                db.execute('INSERT INTO published(work,platform,version) VALUES(?,?,?) ON CONFLICT(work,platform) DO UPDATE SET version=excluded.version',(work,r['platform'],version))
                if not i['exists']:db.execute('UPDATE work SET name=?,withdrawn=0,revision=revision+1 WHERE xid=?',(m['name'],work))
                upload=next(x['upload'] for x in items if x['row']['version_id']==version)
                db.execute("UPDATE upload SET state='consumed' WHERE xid=? AND owner=0 AND state='sealed'",(upload,))
        receipt=dict(passed=True,packages=[i['row'] for i in items],backup=str(backup))
        (incoming/'publication-receipt.json').write_text(json.dumps(receipt,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');return receipt
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--incoming',type=Path,required=True);p.add_argument('--database',type=Path,required=True);p.add_argument('--backup',type=Path,required=True);p.add_argument('--apply',action='store_true');a=p.parse_args();print(json.dumps(publish(a.incoming,a.database,a.backup,a.apply),indent=2))
