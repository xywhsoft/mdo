"""Discoverable contracts for the shared orchestration services."""

def contracts(add):
    string={'type':'string'}
    uint={'type':'integer','minimum':0}
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
