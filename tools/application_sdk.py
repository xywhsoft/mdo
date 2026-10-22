"""Generate the complete product service directory from the actual router."""
import hashlib,json
from pathlib import Path
import re

ALIASES = {
    'project.list':('GET','/api/v1/projects'), 'project.create':('POST','/api/v1/projects'),
    'project.get':('GET','/api/v1/projects/{project}'), 'project.update':('PUT','/api/v1/projects/{project}'),
    'project.remove':('DELETE','/api/v1/projects/{project}'),
    'session.list':('GET','/api/v1/sessions'), 'session.create':('POST','/api/v1/sessions'),
    'session.get':('GET','/api/v1/projects/{project}/sessions/{session}'),
    'session.update':('PATCH','/api/v1/projects/{project}/sessions/{session}'),
    'session.fork':('POST','/api/v1/projects/{project}/sessions/{session}/fork'),
    'session.clear':('POST','/api/v1/projects/{project}/sessions/{session}/clear'),
    'session.history':('GET','/api/v1/projects/{project}/sessions/{session}/conversation'),
    'run.start':('POST','/api/v1/projects/{project}/sessions/{session}/runs'),
    'run.get':('GET','/api/v1/runs/{run}'), 'run.cancel':('DELETE','/api/v1/runs/{run}'),
    'settings.get':('GET','/api/v1/settings'), 'models.list':('GET','/api/v1/models'),
    'tools.list':('GET','/api/v1/tools'), 'agents.list':('GET','/api/v1/agents'),
    'skills.list':('GET','/api/v1/skills'), 'mcp.list':('GET','/api/v1/mcp'),
    'account.get':('GET','/api/v1/account'), 'account.logout':('POST','/api/v1/account/logout'),
    'devices.list':('GET','/api/v1/connector/devices'), 'extensions.list':('GET','/api/v1/extensions/{kind}'),
}

def services(root: Path):
    source=(root/'app/src/api/router.c').read_text(encoding='utf-8')
    source=source[source.index('g_MdoApiRoutes[]'):source.index('static bool MdoApiViewEqualText')]
    records=[]
    for path,flags in re.findall(r'\{\s*"(/api/v1/[^"\n]+)"\s*,\s*((?:XHTTP_METHOD_[A-Z]+\s*\|?\s*)+),',source):
        for method in re.findall(r'XHTTP_METHOD_([A-Z]+)',flags):
            if method=='HEAD':continue
            parts=path.removeprefix('/api/v1/').split('/')
            name='product.'+'.'.join('by_'+p[1:-1] if p.startswith('{') else p.replace('-','_') for p in parts)+'.'+method.lower()
            records.append(dict(id=name,method=method,path=path,domain=parts[0]))
    pairs={(r['method'],r['path']) for r in records}
    if not records or len({r['id'] for r in records})!=len(records):raise ValueError('Invalid or duplicate product service directory')
    for name,(method,path) in ALIASES.items():
        if (method,path) not in pairs:raise ValueError('Application SDK alias has no product route: '+name)
        records.append(dict(id=name,method=method,path=path,domain=path.split('/')[3]))
    return records

def generate(root: Path):
    rows=services(root)
    target=root/'app/generated/application-routes.inc.h'
    text='/* Generated from the production router by tools/application_sdk.py. */\nstatic const MdoApplicationRoute SdkRoutes[]={\n'
    text+=''.join('    {'+','.join(json.dumps(r[k]) for k in ('id','method','path','domain'))+'},\n' for r in rows)+'};\n'
    target.parent.mkdir(parents=True,exist_ok=True)
    if not target.exists() or target.read_text(encoding='utf-8')!=text:target.write_text(text,encoding='utf-8')
    generate_native(root)
    return rows

def native_symbols(root: Path):
    """Export declared product functions, never private manager globals."""
    result={}
    for header in sorted((root/'app/include/mdo').glob('*.h')):
        if header.name=='application.h':continue  # SDK transport implementation
        source=re.sub(r'/\*.*?\*/|//[^\n]*','',header.read_text(encoding='utf-8'),flags=re.S)
        for match in re.finditer(r'^\s*(?!static\b|typedef\b)(?:[\w]+\s*\*?\s+|[\w]+\*\s*)+(Mdo\w+)\s*\([^;{}]*\)\s*;',source,re.M):
            name=match.group(1)
            if name in result and result[name]!=header.name:raise ValueError('Duplicate native SDK symbol '+name)
            result[name]=header.name
    return sorted(result.items())

def generate_native(root: Path):
    directory=root/'app/generated/module-sdk/mdo/native';directory.mkdir(parents=True,exist_ok=True)
    headers=sorted((root/'app/include/mdo').glob('*.h'))
    for header in headers:
        if header.name=='application.h':continue
        source=header.read_text(encoding='utf-8').replace('../../generated/module-sdk/mdo/module.h','../module.h').replace('../../generated/module-sdk/mdo/sdk.h','../sdk.h')
        target=directory/header.name
        if not target.exists() or target.read_text(encoding='utf-8')!=source:target.write_text(source,encoding='utf-8')
    symbols=native_symbols(root)
    text='/* Generated from product API declarations; no handwritten export subset. */\n'
    text+=''.join('#include "../include/mdo/'+h.name+'"\n' for h in headers if h.name!='application.h')
    text+='static const struct {const char *Name,*Header;void (*Function)(void);} SdkNativeSymbols[]={\n'
    text+=''.join('    {"'+name+'","mdo/native/'+header+'",(void(*)(void))'+name+'},\n' for name,header in symbols)+'};\n'
    target=root/'app/generated/application-native.inc.h'
    if not target.exists() or target.read_text(encoding='utf-8')!=text:target.write_text(text,encoding='utf-8')
    fingerprint=hashlib.sha256()
    for header in headers+[root/'include/mdo/sdk.h',root/'include/mdo/module.h']:
        fingerprint.update(header.name.encode()+b'\0'+header.read_bytes()+b'\0')
    target=root/'app/generated/application-sdk-fingerprint.inc.h'
    text='#define MDO_APPLICATION_SDK_FINGERPRINT "'+fingerprint.hexdigest()+'"\n'
    if not target.exists() or target.read_text(encoding='utf-8')!=text:target.write_text(text,encoding='utf-8')
