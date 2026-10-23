"""One wire contract for C mdoCall, JS sdk.call and SDK discovery."""
def contracts(add):
    s={'type':'string'};o={'type':'object'};b={'type':'boolean'}
    u={'type':'integer','minimum':0,'maximum':9007199254740991}
    identity={'id':s};revision={**identity,'revision':u}
    add('panel.types','List registered panel types',output={'type':'array'})
    add('panel.register','Register an extension-owned panel type; renderer is a built-in adapter or custom JS contribution',
        {'id':s,'title':s,'renderer':{'type':'string','enum':['document','plan','diff','web','browser','surface','custom']},'scope':s,'frontend':s},('id','title','renderer'))
    add('panel.unregister','Remove an owned type and its instances',identity,('id',))
    for name in ('panel.list','browser.list'):
        add(name,'List owned instances, optionally restricted to one explicit UI client',{'client':s},output={'type':'array'})
    add('panel.open','Open or reuse a panel by owner/context/client/type/key',{'type':s,'client':s,'key':s,'title':s,'resource':o,'params':o,'pinned':b,'reveal':s},('type','client'))
    for name in ('panel.get','panel.close','panel.reveal','browser.get','browser.close','browser.result','resource.get'):
        add(name,'Operate on an explicit workbench identity; UI actions return receipts',identity,('id',))
    add('panel.update','Update an immutable-context panel with a revision precondition',
        {**revision,'title':s,'resource':o,'params':o,'pinned':b,'view_state':o},('id','revision'))
    add('panel.attach','Restore a panel after its previous UI lease expired; never steal a live window',
        {**revision,'client':s},('id','revision','client'))
    add('panel.layout.get','Read durable display-window layout',identity,('id',))
    add('panel.layout.put','Atomically save layout up to 128 KiB with a revision precondition',{**revision,'value':o},('id','revision','value'))
    add('resource.list','List owned workbench documents',output={'type':'array'})
    add('resource.put','Create (revision 0) or update a typed resource up to 256 KiB; updates require the bound context',
        {**revision,'title':s,'kind':s,'value':{}},('revision','title','kind','value'))
    add('resource.remove','Remove an owned resource with a revision precondition',revision,('id','revision'))
    add('browser.capabilities','Ask an explicit client for its real browser backend features',{'client':s},('client',))
    add('browser.create','Create an owned browser session; attaching it to a panel starts its native view',{'client':s,'key':s,'url':s,'title':s},('client',))
    add('browser.command','Queue one browser action for its bound UI. Read browser.result before repeating an uncertain effect.',
        {'id':s,'command':{'type':'string','enum':['navigate','back','forward','reload','stop','snapshot','screenshot','click','type','scroll','evaluate','takeover','release']},
         'url':s,'selector':s,'text':s,'script':s,'x':{'type':'integer'},'y':{'type':'integer'},'control_token':s},('id','command'))
