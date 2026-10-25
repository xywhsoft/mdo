"""Generate the complete product service directory from the actual router."""
import hashlib,json
from pathlib import Path
import re
import application_contracts

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
    'mcp.templates.list':('GET','/api/v1/mcp/templates'), 'mcp.connections.manage':('POST','/api/v1/mcp/connections'),
    'account.get':('GET','/api/v1/account'), 'account.logout':('POST','/api/v1/account/logout'),
    'devices.list':('GET','/api/v1/connector/devices'), 'extensions.list':('GET','/api/v1/extensions/{kind}'),
}

def services(root: Path):
    source=(root/'app/src/api/router.c').read_text(encoding='utf-8')
    source=source[source.index('g_MdoApiRoutes[]'):source.index('static bool MdoApiViewEqualText')]
    records=[]
    for path,flags,handler in re.findall(r'\{\s*"(/api/v1/[^"\n]+)"\s*,\s*((?:XHTTP_METHOD_[A-Z]+\s*\|?\s*)+),\s*"[^"\n]+"\s*,\s*(\w+)\s*,',source):
        for method in re.findall(r'XHTTP_METHOD_([A-Z]+)',flags):
            if method=='HEAD':continue
            parts=path.removeprefix('/api/v1/').split('/')
            name='product.'+'.'.join('by_'+p[1:-1] if p.startswith('{') else p.replace('-','_') for p in parts)+'.'+method.lower()
            records.append(dict(id=name,method=method,path=path,domain=parts[0],handler=handler))
    pairs={(r['method'],r['path']) for r in records}
    if not records or len({r['id'] for r in records})!=len(records):raise ValueError('Invalid or duplicate product service directory')
    for name,(method,path) in ALIASES.items():
        if (method,path) not in pairs:raise ValueError('Application SDK alias has no product route: '+name)
        original=next(r for r in records if (r['method'],r['path'])==(method,path))
        records.append({**original,'id':name})
    return records

def generate(root: Path):
    from workbench_contracts import generate as generate_workbench
    generate_workbench(root)
    rows=services(root)
    target=root/'app/generated/application-routes.inc.h'
    text='/* Generated from the production router by tools/application_sdk.py. */\nstatic const MdoApplicationRoute SdkRoutes[]={\n'
    for r in rows:
        input_schema,output_schema=application_contracts.contract(r)
        r['input_schema']=input_schema;r['output_schema']=output_schema
        values=[r[k] for k in ('id','method','path','domain')]+[json.dumps(s,separators=(',',':')) for s in (input_schema,output_schema)]
        text+='    {'+','.join(json.dumps(v) for v in values)+'},\n'
    text+='};\n'
    target.parent.mkdir(parents=True,exist_ok=True)
    if not target.exists() or target.read_text(encoding='utf-8')!=text:target.write_text(text,encoding='utf-8')
    generate_native(root)
    contracts=native_contracts()
    text='/* Generated parameter contracts for native application services. */\nstatic const struct {const char *Id,*Description,*Input,*Output;} SdkContracts[]={\n'
    for name,(description,input_schema,output_schema) in contracts.items():
        text+='    {'+','.join(json.dumps(v,ensure_ascii=True) for v in (name,description,json.dumps(input_schema,separators=(',',':')),json.dumps(output_schema,separators=(',',':'))))+'},\n'
    text+='};\n';target=root/'app/generated/application-contracts.inc.h'
    if not target.exists() or target.read_text(encoding='utf-8')!=text:target.write_text(text,encoding='utf-8')
    return rows

def native_contracts():
    """Discovery metadata; each service retains its own business validation."""
    string={'type':'string'};uint={'type':'integer','minimum':0};boolean={'type':'boolean'};obj={'type':'object'};arr={'type':'array'}
    result={}
    def add(name,description,properties=None,required=(),output=None):
        result[name]=(description,{'type':'object','properties':properties or {},'required':list(required)},output or obj)
    for name in ('sdk.services','sdk.native'):add(name,'Discover registered services or typed native product function declarations',output=arr)
    add('sdk.describe','Read the exact registered service descriptor',{'id':string},('id',))
    add('sdk.context','Read the invocation, project, session, workspace, deadline and optional shared namespace/current member')
    add('events.read','Read events with epoch/cursor loss detection; resync on overflow or restart',{'after':uint,'epoch':string,'topic':string,'limit':{**uint,'minimum':1,'maximum':256}})
    add('events.wait','Wait up to 60 seconds for a matching event, cancellation or epoch/cursor gap; timeout returns an empty page',{'after':uint,'epoch':string,'topic':string,'limit':{**uint,'minimum':1,'maximum':256},'timeout_ms':{**uint,'maximum':60000}})
    add('events.emit','Publish an extension event; event data is visible to trusted application subscribers',{'topic':string,'data':{}},('topic',))
    for name in ('runtime.info','runtime.clock'):add(name,'Read application identity and native runtime metadata')
    add('runtime.log','Write at most 16 KiB of diagnostic text',{'message':string},('message',))
    add('runtime.sleep','Cancellable wait; inherited deadline remains authoritative',{'milliseconds':{**uint,'maximum':86400000}},('milliseconds',))
    add('runtime.env.get','Read a native environment variable',{'name':string},('name',))
    add('runtime.env.set','Set an environment variable; omit value to remove it',{'name':string,'value':string},('name',))
    add('system.open','Ask the OS to open a native file or URL',{'target':string},('target',))
    path={'path':string,'space':{'type':'string','enum':['native','workspace','home','application']}}
    for prefix in ('fs','vfs','storage'):
        names={'fs':['resolve','stat','read','write','append','list','mkdir','remove','copy','move'],'vfs':['read','list'],'storage':['read','write','remove']}[prefix]
        for action in names:
            fields=dict(path)
            if action=='read':fields.update(offset=uint,limit={**uint,'minimum':1,'maximum':262144})
            if action=='list':fields.update(offset=uint,limit={**uint,'minimum':1,'maximum':1024})
            if action in ('write','append'):fields.update(text=string,base64=string,parents=boolean,revision=string)
            if action=='remove':fields.update(recursive=boolean)
            if action in ('copy','move'):fields.update(destination=string,replace=boolean)
            add(prefix+'.'+action,'Native resource operation. VFS paths start /app/; storage paths are extension-relative. Byte payloads use exactly one text/base64 field.',fields,('path',)+(('destination',) if action in ('copy','move') else ()))
    add('secret.store','Seal a credential without placing it in logs or event facts',{'value':string},('value',))
    for action in ('read','remove'):add('secret.'+action,'Operate on a device credential reference',{'reference':string},('reference',))
    add('process.run','Run exact argv, including empty strings. Nonzero exit is a tool outcome with stdout/stderr; no shell expansion is added.',{'program':string,'args':{'type':'array','items':string,'maxItems':1024},'cwd':string,'input':string,'timeout_ms':uint,'stream':boolean},('program',))
    add('network.request','HTTP(S) transport; returns the HTTP status and binary/text body, capped at 256 KiB',{'url':string,'method':string,'body':string,'headers':{'type':'object','additionalProperties':string}},('url',))
    add('operation.start','Durably admit a service call once. Reusing an ID with a different request is a conflict. Params/results are not persisted.',{'id':string,'method':string,'params':{}},('method',))
    add('operation.list','List retained in-process operation receipts',output=arr)
    for action in ('get','wait','cancel','result','release'):
        fields={'id':string}
        if action=='wait':fields['timeout_ms']={**uint,'maximum':60000}
        if action=='result':fields.update(offset=uint,limit={**uint,'minimum':1,'maximum':16384})
        add('operation.'+action,'Read, wait, cancel or release an operation. Large results use UTF-8 JSON byte ranges; released/restarted results return 410.',fields,('id',))
    add('stream.open','Create an opaque bounded byte stream; idle lease is five minutes',{'capacity':{**uint,'minimum':1,'maximum':1048576}})
    for action in ('read','write','close','release','stat'):
        fields={'handle':string}
        if action in ('read','write'):fields.update(timeout_ms={**uint,'maximum':60000})
        if action=='read':fields['limit']={**uint,'minimum':1,'maximum':16384}
        if action=='write':fields.update(text=string,base64=string)
        add('stream.'+action,'Operate on a stream handle. A would-block failure accepts no bytes; close preserves buffered data until EOF.',fields,('handle',))
    add('plugin.list','List installed VFS plugins and recovery status')
    add('plugin.get','Read one verified plugin inventory and manifest.',{'id':string,'revision':string},('id',))
    add('plugin.file.read','Read an immutable plugin inventory file in bounded byte ranges.',{'id':string,'revision':string,'path':string,'offset':uint,'limit':{**uint,'minimum':1,'maximum':65536}},('id','path'))
    add('plugin.apply','Stage an immutable plugin revision. Modern plugins default to disabled; application mappings and optional entries are declared in manifest.',{'id':string,'frontend':string,'manifest':obj,'enabled':boolean,'files':{'type':'array','minItems':1,'maxItems':128,'items':{'type':'object','properties':{'path':string,'text':string,'base64':string},'required':['path']}}},('id','files'))
    plugin_changes={'type':'array','maxItems':32,'items':{'type':'object','properties':{'id':string,'enabled':boolean,'revision':string,'remove':boolean},'required':['id'],'additionalProperties':False}}
    add('plugin.plan','Validate the final plugin set, exclusive slots, declared conflicts, dependency bounds and immutable file ownership.',{'changes':plugin_changes},('changes',))
    add('plugin.commit','Commit exactly the reviewed plugin selection; stale plans are rejected.',{'changes':plugin_changes,'expected_revision':string,'expected_plan':string},('changes','expected_revision','expected_plan'))
    add('plugin.frontend.hello','Register a fresh document for one window; old document contributions have ceased.',{'client':string,'page':string},('client','page'))
    for action in ('enter','leave'):
        add('plugin.frontend.'+action,'Reserve a frontend instance before execution, or acknowledge completed cleanup. An outgoing instance retains exclusivity until cleanup.',{'client':string,'page':string,'id':string,'revision':string},('client','page','id','revision'))
    add('plugin.frontend.fail','Report an activation or cleanup failure to the shared warnings indicator. No raw exception, token or response is retained.',{'client':string,'page':string,'id':string,'revision':string,'phase':{'type':'string','enum':['activation','cleanup']}},('client','page','id','revision','phase'))
    add('plugin.preset.list','List local pinned plugin combinations without credentials or runtime data.')
    add('plugin.preset.save','Save the current plugin identities, revisions and enabled selections.',{'id':string,'name':string,'expected_revision':string},('id','name','expected_revision'))
    add('plugin.preset.remove','Remove one local combination using the observed list revision.',{'id':string,'expected_revision':string},('id','expected_revision'))
    add('plugin.preset.plan','Plan the exact saved combination; missing plugins/revisions are never silently substituted.',{'id':string},('id',))
    for action in ('enable','disable','remove','rollback'):add('plugin.'+action,'Change plugin activation while retaining immutable revision backups',{'id':string},('id',))
    add('runtime.reload','Reload modules or submit a host source reload. Host reload completion is queried separately.',{'domain':{'type':'string','enum':['modules','host']}},('domain',))
    add('runtime.reload.status','Read a native host reload receipt',{'id':uint},('id',))
    add('client.hello','Register or renew a target window lease',{'client':string,'context':obj},('client',))
    add('client.poll','Read pending actions for exactly one window',{'client':string},('client',),arr)
    add('client.ack','Acknowledge the actual window outcome; duplicate acknowledgements do not replay effects',{'client':string,'id':string,'success':boolean,'error':string,'result':{}},('client','id','success'))
    add('ui.clients','Discover active target windows; never select the first window implicitly',output=arr)
    add('ui.invoke','Queue an action for an explicit client, or append it to the originating command receipt',{'client':string,'action':string,'params':{},'context':obj},('action',))
    add('ui.result','Read queued/succeeded/failed UI action status',{'id':string},('id',))
    add('ui.release','Release a terminal UI receipt; pending actions remain retained',{'id':string},('id',))
    add('model.complete','Managed model boundary with messages, tools, multimodal parts, usage and optional newline-delimited stream events',{'model':string,'protocol':{**uint,'maximum':3},'reasoning_effort':string,'max_output_tokens':uint,'messages':{'type':'array','minItems':1,'maxItems':4096,'items':obj},'tools':{'type':'array','maxItems':256,'items':obj},'extra_body':obj,'stream':string},('messages',))
    add('tool.call','Execute a tool through a recovered session and its live permission scope; a tool-level failure is a successful SDK result with success:false',{'project':string,'session':string,'tool':string,'arguments':obj},('tool',))
    add('session.compact','Compact the immutable invocation session; original history remains available')
    from orchestration_contracts import contracts as orchestration_contracts
    orchestration_contracts(add)
    from workbench_contracts import contracts as workbench_contracts
    workbench_contracts(add)
    return result

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
    for header in headers+[root/'include/mdo/sdk.h',root/'include/mdo/module.h',root/'include/mdo/workbench.h',root/'include/mdo/agent.h']:
        fingerprint.update(header.name.encode()+b'\0'+header.read_bytes()+b'\0')
    target=root/'app/generated/application-sdk-fingerprint.inc.h'
    text='#define MDO_APPLICATION_SDK_FINGERPRINT "'+fingerprint.hexdigest()+'"\n'
    if not target.exists() or target.read_text(encoding='utf-8')!=text:target.write_text(text,encoding='utf-8')
