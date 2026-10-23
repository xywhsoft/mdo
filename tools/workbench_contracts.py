"""One wire contract for C mdoCall, JS sdk.call and SDK discovery."""
def contracts(add):
    s={'type':'string'};o={'type':'object'};b={'type':'boolean'}
    u={'type':'integer','minimum':0,'maximum':9007199254740991}
    identity={'id':s};revision={**identity,'revision':u}
    comparison={'repository':s,'scope':{'type':'string','enum':['workspace','staged','unstaged','branch','session']},'base':s}
    add('changes.list','Read structured Git changes without modifying the index; comparison scopes are explicit',comparison)
    add('changes.branches','List local and remote comparison refs',{'repository':s},output={'type':'array'})
    add('changes.diff','Read a bounded page of diff rows with exact line identities and a comparison revision',
        {**comparison,'path':s,'revision':s,'offset':u,'limit':{'type':'integer','minimum':1,'maximum':2000}},('path',))
    file_path={'path':s,'space':{'type':'string','enum':['workspace','native','home','application']}}
    add('files.directory','Read one directory page; revision detects changed pagination',
        {**file_path,'offset':u,'limit':{'type':'integer','minimum':1,'maximum':1024},'revision':s,'hidden':b},('path',))
    add('files.search','Bounded recursive filename search; reports incomplete results and never follows directory links',
        {**file_path,'query':s,'hidden':b,'limit':{'type':'integer','minimum':1,'maximum':200}},('path','query'))
    for action in ('resolve','stat','read','write','mkdir','move','remove'):
        add('files.'+action,'File workbench operation using the same filesystem and revision rules as fs.'+action,
            {**file_path,'offset':u,'limit':{'type':'integer','minimum':1,'maximum':262144},'revision':s,'text':s,'base64':s,'destination':s,'recursive':b,'replace':b},('path',))
    add('panel.types','List registered panel types',output={'type':'array'})
    add('panel.register','Register an extension-owned panel type; renderer is a built-in adapter or custom JS contribution',
        {'id':s,'title':s,'renderer':{'type':'string','enum':['document','plan','diff','web','browser','surface','editor','custom']},'scope':s,'frontend':s},('id','title','renderer'))
    add('panel.unregister','Remove an owned type and its instances',identity,('id',))
    for name in ('panel.list','browser.list'):
        add(name,'List owned instances, optionally restricted to one explicit UI client',{'client':s},output={'type':'array'})
    add('panel.open','Open or reuse a panel by owner/context/client/type/key',{'type':s,'client':s,'key':s,'title':s,'resource':o,'params':o,'pinned':b,'reveal':s},('type','client'))
    for name in ('panel.get','panel.close','panel.reveal','browser.get','browser.close','browser.result','browser.release','resource.get'):
        add(name,'Operate on an explicit workbench identity; UI actions return receipts',identity,('id',))
    add('panel.update','Update an immutable-context panel with a revision precondition',
        {**revision,'title':s,'resource':o,'params':o,'pinned':b,'view_state':o},('id','revision'))
    for name in ('panel.attach','browser.attach'):
        add(name,'Restore an instance after its previous UI lease expired; never steal a live window',
            {**revision,'client':s},('id','revision','client'))
    for name in ('panel.report','browser.report'):
        add(name,'Report bounded ephemeral UI state for an owned, attached instance. Restarts discard it.',
            {'id':s,'client':s,'value':o},('id','client','value'))
    add('panel.layout.get','Read durable display-window layout',identity,('id',))
    add('panel.layout.put','Atomically save layout up to 128 KiB with a revision precondition',{**revision,'value':o},('id','revision','value'))
    add('resource.list','List owned workbench documents',output={'type':'array'})
    add('resource.put','Create (revision 0) or update a typed resource up to 256 KiB; updates require the bound context',
        {**revision,'title':s,'kind':s,'value':{}},('revision','title','kind','value'))
    add('resource.remove','Remove an owned resource with a revision precondition',revision,('id','revision'))
    add('browser.capabilities','Ask an explicit client for its real browser backend features',{'client':s},('client',))
    add('browser.create','Create an owned browser session; attaching it to a panel starts its native view',{'client':s,'key':s,'url':s,'title':s},('client',))
    add('browser.update','Persist the confirmed browser location with a revision precondition',{**revision,'url':s,'title':s},('id','revision'))
    add('browser.command','Queue one browser action for its bound UI. Read browser.result before repeating an uncertain effect.',
        {'id':s,'command':{'type':'string','enum':['navigate','back','forward','reload','stop','state','wait','snapshot','screenshot','click','type','scroll','evaluate','takeover','release']},
         'url':s,'selector':s,'text':s,'script':s,'x':{'type':'integer'},'y':{'type':'integer'},'timeout_ms':{'type':'integer','minimum':1,'maximum':60000},'control_token':s},('id','command'))

def generate(root):
    """Convenience APIs remain mechanically identical to the wire directory."""
    import json
    names=[]
    contracts(lambda name,*args,**kwargs:names.append(name))
    header='/* Generated by tools/workbench_contracts.py. Edit the contract source. */\n#ifndef MDO_WORKBENCH_SDK_H\n#define MDO_WORKBENCH_SDK_H\n#include "sdk.h"\n'
    for name in names:
        function='mdo'+''.join(part.capitalize() for part in name.split('.'))
        header+='static inline mdo_result '+function+'(const mdo_sdk_context_v1* C,const xvalue* P,xvalue** R,mdo_error_v1* E)\n{return mdoCall(C,"'+name+'",P,R,E);}\n'
    header+='#endif\n'
    target=root/'include/mdo/workbench.h'
    if not target.exists() or target.read_text(encoding='utf-8')!=header:target.write_text(header,encoding='utf-8')
    tree={}
    for name in names:
        parts=name.split('.');node=tree
        for part in parts[:-1]:node=node.setdefault(part,{})
        node[parts[-1]]=name
    def render(node):
        return 'Object.freeze({'+','.join(json.dumps(key)+':'+(render(value) if isinstance(value,dict) else '(params={},options)=>call('+json.dumps(value)+',params,options)') for key,value in node.items())+'})'
    source='// Generated from the same contracts as <mdo/workbench.h>.\nexport const workbenchServices = call => '+render(tree)+';\n'
    target=root/'app/web/js/api/workbench-services.js'
    if not target.exists() or target.read_text(encoding='utf-8')!=source:target.write_text(source,encoding='utf-8')
