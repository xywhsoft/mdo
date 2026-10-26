"""Discoverable contracts for the shared orchestration services."""

def contracts(add):
    string={'type':'string'}
    uint={'type':'integer','minimum':0}
    add('instance.history','Read actual visible member events without running a model. Owner-session scope, UTF-8 chunks and committed byte cursors; no hidden reasoning. Legacy members explicitly report incomplete historical coverage.',
        {'namespace':string,'id':string,'after':uint,'limit':{**uint,'minimum':1,'maximum':64}},('id',))
    guard={'type':'object','properties':{'id':string,'fence':uint,'token':string},'required':['id','fence','token']}
    common={'namespace':string,'key':string,'expected_revision':uint,'request_id':string,'lease_guard':guard}
    add('state.get','Read a project/extension-scoped value, including missing/tombstone revision',common,('key',))
    add('state.list','List key metadata at a namespace revision without duplicating stored values',
        {'namespace':string,'offset':uint,'limit':{**uint,'minimum':1,'maximum':256},'expected_revision':uint})
    for method in ('state.put','state.compare_exchange'):
        add(method,'Atomically write a value only at its expected revision; null is an ordinary value',
            {**common,'value':{}},('key','expected_revision','value'))
    add('state.remove','Publish a tombstone at the expected revision; recreation cannot reset the revision',common,('key','expected_revision'))
    write={'type':'object','properties':{**common,'value':{},'remove':{'type':'boolean'}},'required':['key','expected_revision']}
    check={'type':'object','properties':{'key':string,'expected_revision':uint},'required':['key','expected_revision']}
    add('state.transaction','Check and publish 1..64 writes in one checksummed atomic document; any conflict publishes nothing',
        {'namespace':string,'request_id':string,'lease_guard':guard,'writes':{'type':'array','minItems':1,'maxItems':64,'items':write},
         'checks':{'type':'array','maxItems':64,'items':check}},('writes',))

    add('state.receipt.get','Read a retained atomic mutation receipt without replaying its effect',{'namespace':string,'request_id':string},('request_id',))

    lease={'namespace':string,'id':string,'owner':string,'request_id':string,'fence':uint,'token':string,'ttl_ms':{**uint,'minimum':1,'maximum':300000}}
    add('lease.acquire','Acquire an expiring process-epoch lease with a durable monotonically increasing fence',lease,('id','owner','request_id'))
    add('lease.inspect','Inspect live owner/fence without disclosing the lease token',lease,('id',))
    add('lease.validate','Validate the fence/token; atomic state writes must instead use lease_guard',lease,('id','fence','token'))
    for method in ('lease.renew','lease.release'):
        add(method,'Renew or release only a currently live lease; repeated request_id returns its original receipt',lease,('id','fence','token','request_id'))

    mail={'namespace':string,'mailbox':string,'message_id':string,'body':{},'after':uint,
          'limit':{**uint,'minimum':1,'maximum':64},'timeout_ms':{**uint,'maximum':60000},
          'lease_guard':guard,'writes':{'type':'array','minItems':1,'maxItems':64,'items':write},
          'checks':{'type':'array','maxItems':64,'items':check}}
    add('mailbox.send','Durably admit one bounded message; message_id deduplicates canonical content and sender',mail,('mailbox','message_id','body'))
    add('mailbox.get','Inspect message delivery/ack state; acknowledged payloads are discarded',mail,('mailbox','message_id'))
    add('mailbox.read','Read pending messages without consuming them; page by cursor with a 32 KiB response budget',mail,('mailbox',))
    add('mailbox.ack','Atomically acknowledge and optionally commit state writes; mismatched repeated ack is rejected',mail,('mailbox','message_id'))
    add('mailbox.wait','Wait for durable pending messages; notifications are hints and state is rechecked',mail,('mailbox',))

    member={'namespace':string,'id':string,'agent_id':string,'model_id':string,'permission_profile':{'type':'string','enum':['read-only','balanced','full-access']},'message_id':string,'prompt':string}
    add('instance.create','Establish a durable multi-turn member identity; no model call occurs',member,('id',))
    add('instance.list','List up to 64 retained member identities in this execution namespace',{'namespace':string},output={'type':'array'})
    for action in ('get','cancel','close'):
        add('instance.'+action,'Read or control an owned member',member,('id',))
    add('instance.send','Durably queue a bounded prompt, deduplicated by message_id; does not run the model',member,('id','message_id','prompt'))
    add('instance.step','Claim and execute at most one inbox turn, checkpoint then settle; interrupted turns require review',member,('id',))
    for action in ('result','wait'):
        add('instance.'+action,'Read/wait for one durable message result; waiting never starts a model call',
            {**member,'timeout_ms':{**uint,'maximum':60000}},('id','message_id'))
    decision={'type':'object','properties':{'tool_call_id':string,'action':{'type':'string','enum':['retry','record_uncertain']}},'required':['tool_call_id','action']}
    for action in ('resume','abandon'):
        add('instance.'+action,'Explicitly continue or finish an interrupted turn at the current member revision; effects are never silently replayed',
            {**member,'expected_revision':uint,'decisions':{'type':'array','items':decision,'maxItems':64}},('id','message_id','expected_revision'))
    work={'namespace':string,'id':string,'title':string,'description':string,'depends_on':{'type':'array','items':string,'maxItems':32},
        'expected_revision':uint,'worker':string,'claim_id':string,'claim_token':string,'result':{},'reason':string}
    add('work.create','Create one shared task, depending only on existing tasks; repeated identical creation is safe',work,('id','title'))
    add('work.get','Read a shared task and its result; claim tokens are not disclosed',work,('id',))
    add('work.list','Page shared task metadata and optionally only ready tasks',{'namespace':string,'offset':uint,'limit':{**uint,'minimum':1,'maximum':64},'ready_only':{'type':'boolean'},'expected_revision':uint})
    add('work.claim','Atomically claim a ready task at its revision; reuse worker/claim_id to recover its current token',work,('id','expected_revision','worker','claim_id'))
    add('work.complete','Submit a result using the current claim token; repeated identical completion is safe',work,('id','claim_token','result'))
    add('work.release','Explicitly make a claimed task available again, invalidating late completion when next claimed',work,('id','claim_token'))
    add('work.cancel','Cancel a shared task at its current revision; invalidates its claim token',work,('id','expected_revision'))
