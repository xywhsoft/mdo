"""Explicit, offline identity migration for manual mdo extensions.

Dry-run is the default. Managed archives remain immutable: repair old builtins
through mdo's migrate_builtins action and reinstall store works from the store.
No program is executed, downloaded or granted permission by this utility.
"""
from __future__ import annotations
import argparse
from contextlib import contextmanager
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import time
import uuid

PREFIX = dict(agents='agent', subagents='subagent', tools='tool', skills='skill', mcp='mcp', commands='command', plugins='plugin', knowledge='knowledge', **{'external-tools':'external-tool'})
REF = re.compile(r'^(agent|subagent|tool|skill|mcp|command|plugin|knowledge|external-tool):[a-z0-9_-]+(?:\.[a-z0-9_-]+)*$')


def valid_ref(value, kind=None):
    return isinstance(value,str) and len(value)<=160 and bool(REF.fullmatch(value)) and len(value.split(':')[1])<=128 and (kind is None or value.startswith(PREFIX[kind]+':'))


def digest(data):
    return hashlib.sha256(data).hexdigest() if data is not None else None


def encoded(value):
    return (json.dumps(value,ensure_ascii=False,indent=2)+'\n').encode('utf-8')


def anchored(path, roots):
    path=Path(os.path.abspath(path))
    if not any(path.is_relative_to(root) for root in roots):raise ValueError('Migration path escapes the explicitly selected roots')
    for candidate in (path,*path.parents):
        if candidate.exists() or candidate.is_symlink():
            info=candidate.lstat()
            if stat.S_ISLNK(info.st_mode) or getattr(info,'st_file_attributes',0)&0x400:raise ValueError(f'Links and reparse points are not migration targets: {candidate}')
        if candidate in roots:break
    return path


def read_json(path, default=None):
    if not path.exists():return default
    data=path.read_bytes()
    if len(data)>8*1024*1024:raise ValueError(f'Metadata exceeds the read budget: {path}')
    value=json.loads(data.decode('utf-8-sig'))
    if not isinstance(value,dict):raise ValueError(f'Expected a JSON object: {path}')
    return value


@contextmanager
def home_lease(home):
    """Uses the same .mdo.lock file and an overlapping exclusive byte range."""
    lock=anchored(home/'.mdo.lock',[home]).open('a+b')
    try:
        if os.name=='nt':
            import msvcrt
            lock.seek(0);msvcrt.locking(lock.fileno(),msvcrt.LK_NBLCK,1)
        else:
            import fcntl
            fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB);fcntl.lockf(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
        yield
    except (BlockingIOError,PermissionError) as error:
        raise RuntimeError('Close mdo for this Home before applying or recovering an offline migration') from error
    finally:
        lock.close()


def atomic(path, data, mode=0o600):
    path.parent.mkdir(parents=True,exist_ok=True);temporary=path.with_name('.mdo-migrate-'+uuid.uuid4().hex)
    try:
        with temporary.open('xb') as file:
            os.chmod(temporary,mode);file.write(data);file.flush();os.fsync(file.fileno())
        os.replace(temporary,path)
    finally:
        if temporary.exists():temporary.unlink()


class Migration:
    def __init__(self, home, mapping=None, workspaces=()):
        self.home=Path(home).absolute();self.roots=[self.home,*[Path(p).absolute() for p in workspaces]]
        if not self.home.is_dir():raise ValueError('Choose an existing mdo Home')
        for root in self.roots:anchored(root,self.roots)
        self.mapping=mapping or {};self.ids={};self.rows={};self.changes={};self.errors=[];self.managed=[];self.warnings=[]
        if not isinstance(self.mapping,dict) or not isinstance(self.mapping.get('ids',self.mapping),dict) or not isinstance(self.mapping.get('dependencies',{}),dict):raise ValueError('Migration mapping must contain ID and dependency objects')
        self.state=read_json(anchored(self.home/'data/extensions/state.json',self.roots),{})
        self.bindings=read_json(anchored(self.home/'data/extensions/bindings.json',self.roots),{})
        self.connections={}
        for path in sorted((self.home/'data/mcp/connections').glob('*.json')):
            self.connections[path.stem]=read_json(anchored(path,self.roots))

    def stage(self,path,value):
        path=anchored(path,self.roots);before=path.read_bytes() if path.exists() else None;after=value if isinstance(value,bytes) else encoded(value)
        if before!=after:
            if path in self.changes and self.changes[path]['after']!=after:raise ValueError(f'Conflicting migration edits: {path}')
            self.changes[path]=dict(path=path,before=before,after=after,mode=stat.S_IMODE(path.stat().st_mode) if before is not None else 0o600)

    def resolve(self,kind,value):
        if valid_ref(value,kind):return value
        legacy=value
        if isinstance(value,str) and value.startswith(PREFIX[kind]+'.'):value=value[len(PREFIX[kind])+1:]
        if kind=='agents' and value=='mdo.default':value='default'
        found={identity for key,identity in self.ids.items() if key.startswith(kind+'/') and (key==kind+'/'+str(value) or self.rows[key].get('alias')==value)}
        if len(found)!=1:raise ValueError(f'Unresolved or ambiguous {kind} reference: {legacy}')
        return found.pop()

    def dependency(self,manifest,ref,reason,optional=False):
        if not valid_ref(ref):raise ValueError('Dependency requires an explicit typed extension ID')
        if ref==manifest['id']:raise ValueError(f'Self dependency in {ref}')
        if manifest['kind']=='knowledge' and not ref.startswith('knowledge:'):raise ValueError('Knowledge libraries may reference only knowledge libraries')
        deps=manifest.setdefault('dependencies',[])
        if not isinstance(deps,list) or any(not isinstance(row,dict) for row in deps):raise ValueError('Dependencies must be a list of relation objects')
        if not isinstance(optional,bool):raise ValueError('Dependency optional must be boolean')
        existing=next((row for row in deps if row.get('id')==ref),None)
        if existing and existing.get('optional',False) and not optional:raise ValueError(f'Selected capability must be a required dependency: {ref}')
        if existing is None:deps.append(dict(id=ref,reason=reason,**({'optional':True} if optional else {})))

    def role(self,config,manifest):
        parent=config.get('extends')
        if parent and parent!='mdo.general':
            kind='subagents' if parent.startswith(('subagent.','subagent:')) else 'agents';ref=self.resolve(kind,parent);config['extends']=ref;self.dependency(manifest,ref,'Base role')
        for field,kind in (('skills','skills'),('mcp_servers','mcp')):
            output=[]
            for value in config.get(field,[]):
                if isinstance(value,dict):
                    if set(value)-{'ref','export'} or not valid_ref(value.get('ref'),kind):raise ValueError(f'Invalid typed role selection: {field}')
                    self.dependency(manifest,value['ref'],'Selected '+field);output.append(value);continue
                connection=self.connections.get(value) if field=='mcp_servers' else None
                ref=self.resolve(kind,connection.get('extension_id') if connection else value)
                if not any(row.get('ref')==ref for row in output):output.append(dict(ref=ref))
                self.dependency(manifest,ref,'Selected '+field)
                if connection:
                    binding=self.bindings.setdefault(manifest['id'],{}).setdefault(connection.get('project_id') or 'global',{})
                    if ref in binding and binding[ref]!=value:raise ValueError(f'Multiple private connections require an explicit choice for {ref}')
                    binding[ref]=value
            if field in config:config[field]=output
        for field in ('allow','deny'):
            values=config.get('delegation',{}).get(field)
            if values is not None:config['delegation'][field]=[self.resolve('subagents',value) if value.startswith('subagent.') and '*' not in value else value for value in values]
        return config

    def build(self):
        explicit=self.mapping.get('ids',self.mapping)
        for kind in PREFIX:
            directory=anchored(self.home/kind,self.roots)
            if not directory.exists():continue
            for folder in sorted(directory.iterdir()):
                anchored(folder,self.roots)
                if not folder.is_dir():continue
                path=anchored(folder/'extension.json',self.roots);manifest=read_json(path)
                if not manifest:continue
                key=kind+'/'+folder.name;self.rows[key]=manifest;identity=manifest.get('id') or explicit.get(key)
                source=self.state.get(kind+':'+folder.name,{}).get('source','manual')
                if not identity and source!='manual':self.managed.append(dict(directory=key,action='migrate_builtins' if source=='builtin' else 'reinstall_verified_store_work'));continue
                if not valid_ref(identity,kind):self.errors.append(f'Explicit typed ID required in mapping: {key}');continue
                if manifest.get('id') and key in explicit and explicit[key]!=manifest['id']:self.errors.append(f'An existing logical ID is immutable: {key}');continue
                if identity in self.ids.values():self.errors.append(f'Duplicate logical ID: {identity}');continue
                self.ids[key]=identity
        for key,manifest in self.rows.items():
            if key not in self.ids:continue
            kind,directory=key.split('/',1);record=self.state.get(kind+':'+directory,{})
            if record.get('source','manual')!='manual':
                if not manifest.get('id'):self.managed.append(dict(directory=key,id=self.ids[key],action='migrate_builtins' if record.get('source')=='builtin' else 'reinstall_verified_store_work'))
                continue
            try:
                manifest=json.loads(json.dumps(manifest));manifest['id']=self.ids[key];manifest['kind']=kind
                for dependency in self.mapping.get('dependencies',{}).get(key,[]):
                    if set(dependency)-{'id','optional','reason'}:raise ValueError(f'Unsupported dependency fields: {key}')
                    self.dependency(manifest,dependency['id'],dependency.get('reason','Reviewed migration dependency'),dependency.get('optional',False))
                entry=anchored(self.home/key/manifest['entry'],self.roots)
                if kind=='plugins':
                    recipe=read_json(entry)
                    for dependency in recipe.pop('requires',[]):
                        if set(dependency)-{'id','reason'}:raise ValueError(f'Review unsupported legacy plugin dependency constraints: {key}')
                        self.dependency(manifest,self.resolve('plugins',dependency['id']),dependency.get('reason','Required plugin'))
                    for conflict in recipe.pop('conflicts',[]):
                        if set(conflict)-{'id','reason'}:raise ValueError(f'Review unsupported legacy plugin conflict constraints: {key}')
                        manifest.setdefault('conflicts',[]).append(dict(id=self.resolve('plugins',conflict['id']),reason=conflict.get('reason','Plugin conflict')))
                    self.stage(entry,recipe)
                elif kind in ('agents','subagents'):
                    text=entry.read_text(encoding='utf-8-sig')
                    if text.lstrip().startswith('{'):self.stage(entry,self.role(json.loads(text),manifest))
                    elif text.startswith('---\n') or text.startswith('---\r\n'):
                        lines=text.splitlines(keepends=True);end=next((i for i in range(1,len(lines)) if lines[i].strip()=='---'),None)
                        if end is None:raise ValueError(f'Unterminated role frontmatter: {key}')
                        config={};positions={}
                        for index,line in enumerate(lines[1:end],1):
                            name,separator,value=line.partition(':')
                            if separator and name in ('extends','skills','mcp_servers','delegation'):
                                value=value.strip();positions[name]=index
                                try:config[name]=json.loads(value)
                                except ValueError:config[name]=value
                        self.role(config,manifest)
                        for name,index in positions.items():lines[index]=name+': '+json.dumps(config[name],ensure_ascii=False)+'\n'
                        self.stage(entry,''.join(lines).encode('utf-8'))
                    else:self.warnings.append(f'Review references embedded in the C role manually: {key}')
                for field in ('dependencies','conflicts'):
                    values=manifest.get(field,[])
                    if not isinstance(values,list) or len(values)>128:raise ValueError(f'Invalid {field} in {key}')
                    seen=set()
                    for value in values:
                        if not isinstance(value,dict) or not valid_ref(value.get('id')) or value['id'] in seen or value['id']==manifest['id'] or kind=='knowledge' and not value['id'].startswith('knowledge:'):raise ValueError(f'Invalid {field} identity in {key}')
                        allowed={'id','reason','optional'} if field=='dependencies' else {'id','reason'}
                        if set(value)-allowed or 'optional' in value and not isinstance(value['optional'],bool) or 'reason' in value and (not isinstance(value['reason'],str) or len(value['reason'].encode('utf-8'))>600):raise ValueError(f'Invalid {field} fields in {key}')
                        seen.add(value['id'])
                self.stage(self.home/key/'extension.json',manifest)
                record={**record,'source':'manual','kind':kind,'id':directory,'logical_id':manifest['id']};self.state[kind+':'+directory]=record
            except (ValueError,KeyError,TypeError) as error:self.errors.append(str(error))
        for name,connection in self.connections.items():
            try:
                ref=self.resolve('mcp',connection['extension_id'])
                if connection['extension_id']!=ref:connection={**connection,'extension_id':ref,'directory_id':next(key.split('/',1)[1] for key,value in self.ids.items() if value==ref),'approval_digest':''};self.stage(self.home/f'data/mcp/connections/{name}.json',connection)
            except (KeyError,ValueError,StopIteration) as error:self.errors.append(f'Private connection {name}: {error}')
        workspaces={self.home/'workspace',*self.roots[1:]}
        for workspace in workspaces:
            for kind in PREFIX:
                path=anchored(workspace/f'.mdo/{kind}.json',self.roots)
                if not path.exists():continue
                try:
                    document=read_json(path);flags=document['disabled'] if kind=='commands' else document;converted={}
                    for alias,enabled in flags.items():
                        if not isinstance(enabled,bool):raise ValueError(f'Invalid project selection: {path}')
                        ref=self.resolve(kind,alias)
                        if ref in converted and converted[ref]!=enabled:raise ValueError(f'Conflicting project selection: {ref}')
                        converted[ref]=enabled
                    if kind=='commands':document['disabled']=converted
                    else:document=converted
                    if len(encoded(document))>65536:raise ValueError(f'Project selection exceeds 64 KiB: {path}')
                    self.stage(path,document)
                except (ValueError,KeyError) as error:self.errors.append(str(error))
        self.stage(self.home/'data/extensions/state.json',self.state)
        if self.bindings:self.stage(self.home/'data/extensions/bindings.json',self.bindings)
        return self

    def report(self):
        changes=[dict(path=str(path),before_sha256=digest(change['before']),after_sha256=digest(change['after'])) for path,change in sorted(self.changes.items())]
        body=dict(ids=self.ids,files=changes,managed=self.managed,errors=self.errors,warnings=self.warnings)
        return {**body,'plan_sha256':digest(encoded(body)),'ready':not self.errors}

    def recover(self):
        for directory in sorted((self.home/'data/extension-migrations').glob('*')):
            journal=read_json(anchored(directory/'journal.json',self.roots))
            if not journal or journal.get('phase')!='prepared':continue
            entries=journal.get('entries',[])
            if len(entries)>8192:raise ValueError('Migration recovery exceeds its file budget')
            for index,entry in enumerate(entries):
                target=anchored(Path(entry['path']),self.roots);current=target.read_bytes() if target.exists() else None
                if digest(current) not in (entry['before_sha256'],entry['after_sha256']):raise RuntimeError(f'Migration recovery preserved a file changed outside the transaction: {target}')
                before=anchored(directory/f'{index}.before',self.roots).read_bytes() if entry['before_sha256'] is not None else None
                if digest(before)!=entry['before_sha256']:raise RuntimeError('Migration backup checksum mismatch; files were preserved')
            for index,entry in reversed(list(enumerate(entries))):
                target=anchored(Path(entry['path']),self.roots)
                if entry['before_sha256'] is None:
                    if target.exists():target.unlink()
                else:atomic(target,(directory/f'{index}.before').read_bytes(),entry['mode'])
            journal['phase']='rolledback';atomic(directory/'journal.json',encoded(journal))

    def apply(self, expected):
        report=self.report()
        if not report['ready'] or expected!=report['plan_sha256']:raise ValueError('Inspect an error-free dry-run and supply its exact --expected-plan hash')
        if len(self.changes)>8192 or sum(len(change['after']) for change in self.changes.values())>32*1024*1024:raise ValueError('Migration exceeds its file/32 MiB metadata budget')
        directory=anchored(self.home/'data/extension-migrations'/uuid.uuid4().hex,self.roots);directory.mkdir(parents=True)
        entries=[]
        for index,change in enumerate(self.changes.values()):
            path=anchored(change['path'],self.roots);current=path.read_bytes() if path.exists() else None
            if digest(current)!=digest(change['before']):raise RuntimeError('Migration input changed; inspect a new dry-run')
            if current is not None:atomic(directory/f'{index}.before',current)
            atomic(directory/f'{index}.after',change['after']);entries.append(dict(path=str(path),mode=change['mode'],before_sha256=digest(current),after_sha256=digest(change['after'])))
        journal=dict(phase='prepared',created=int(time.time()),plan_sha256=expected,entries=entries)
        if len(encoded(journal))>8*1024*1024:raise ValueError('Migration journal exceeds 8 MiB')
        atomic(directory/'journal.json',encoded(journal))
        try:
            for index,change in enumerate(self.changes.values()):atomic(anchored(change['path'],self.roots),(directory/f'{index}.after').read_bytes(),change['mode'])
            journal['phase']='committed';atomic(directory/'journal.json',encoded(journal))
        except BaseException:
            self.recover();raise
        return directory


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--home',type=Path,required=True);parser.add_argument('--map',type=Path);parser.add_argument('--workspace',type=Path,action='append',default=[]);parser.add_argument('--apply',action='store_true');parser.add_argument('--expected-plan');parser.add_argument('--recover',action='store_true');args=parser.parse_args()
    try:
        mapping=read_json(args.map,{}) if args.map else {};migration=Migration(args.home,mapping,args.workspace)
        if args.apply or args.recover:
            with home_lease(migration.home):
                migration.recover()
                if args.apply:migration=Migration(args.home,mapping,args.workspace).build();directory=migration.apply(args.expected_plan);print(json.dumps(dict(committed=True,backup=str(directory)),ensure_ascii=False))
                else:print(json.dumps(dict(recovered=True)))
        else:print(json.dumps(migration.build().report(),ensure_ascii=False,indent=2))
    except (OSError,ValueError,RuntimeError) as error:parser.exit(1,str(error)+'\n')


if __name__=='__main__':main()
