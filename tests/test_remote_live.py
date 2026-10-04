"""Remote live events over the real relay and native routes, one local model.

Bounded functional cases only; no public model, production data or load test.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import threading
import time
import uuid
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

from test_remote_bridge import Rpc
from test_remote_manager import run, ROOT, XADMIN


class Model(BaseHTTPRequestHandler):
    def log_message(self, *_): pass

    def do_POST(self):
        payload = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
        assert self.path == '/v1/chat/completions',self.path
        messages = payload['messages']; prompt = json.dumps(messages)
        tool_done = any(item.get('role') == 'tool' for item in messages)
        finish = 'stop'
        if not tool_done and 'remote ask' in prompt:
            output = [{'tool_calls':[{'index':0,'id':'remote-ask','type':'function','function':{
                'name':'ask_user','arguments':json.dumps({'question':'Remote question?','options':['Yes','No']})}}]}]
            finish = 'tool_calls'
        elif not tool_done and 'remote approval' in prompt:
            output = [{'tool_calls':[{'index':0,'id':'remote-write','type':'function','function':{
                'name':'write','arguments':json.dumps({'path':'remote-approval.txt','content':'probe','mode':'create'})}}]}]
            finish = 'tool_calls'
        else: output = [{'reasoning_content':'Remote delivery.'}] + [{'content':f'part-{i} '} for i in range(12)]
        frames = []
        for delta in [{'role':'assistant'}]+output:
            frames.append(('data: '+json.dumps({'id':'remote-test','model':'ornith-1.5-35b',
                'choices':[{'index':0,'delta':delta,'finish_reason':None}]})+'\n\n').encode())
        frames.append(('data: '+json.dumps({'id':'remote-test','model':'ornith-1.5-35b',
            'choices':[{'index':0,'delta':{},'finish_reason':finish}],
            'usage':{'prompt_tokens':100,'completion_tokens':20,'total_tokens':120}})+'\n\n').encode())
        frames.append(b'data: [DONE]\n\n')
        self.send_response(200); self.send_header('Content-Type','text/event-stream')
        self.send_header('Content-Length',str(sum(map(len,frames)))); self.end_headers()
        try:
            for frame in frames:
                self.wfile.write(frame); self.wfile.flush(); time.sleep(.07)
        except OSError: pass


class LiveRpc(Rpc):
    def __init__(self, *args):
        super().__init__(*args)
        self.live = ''; self.events = []; self.event = None
        self.offset = self.event_sequence = 0; self.largest = 0

    def consume(self, value):
        if value.get('id') != self.live: return value.get('type','').startswith('live_')
        kind = value['type']
        if kind == 'live_event':
            assert self.event is None and value['sequence'] == self.event_sequence+1,value
            assert value['offset'] == self.offset and value['bytes'] <= self.hello['live_limit'],value
            self.event = {**value,'body':bytearray()}; self.largest = max(self.largest,value['bytes'])
        elif kind == 'live_chunk':
            assert self.event and value['offset'] == self.offset,value
            self.event['body'] += value['body']; self.offset += len(value['body'])
            assert len(self.event['body']) <= self.event['bytes']
            self.send({'type':'live_ack','id':self.live,'offset':self.offset})
        elif kind == 'live_event_end':
            assert self.event and value['sequence'] == self.event['sequence'] and value['offset'] == self.offset,value
            assert len(self.event['body']) == self.event['bytes']
            assert hashlib.sha256(self.event['body']).hexdigest() == self.event['sha256']
            event = json.loads(self.event['body']); self.event_sequence = value['sequence']; self.event = None
            if event['type'] == 'ping': self.command({'type':'pong'})
            else: self.events.append(event)
        elif kind == 'live_closed': self.events.append(value)
        elif kind != 'live_opened': return False
        return True

    def receive(self):
        while True:
            value = super().receive()
            if not self.consume(value): return value

    def next_event(self):
        while not self.events:
            value = super().receive(); assert self.consume(value),value
        return self.events.pop(0)

    def open_live(self, token):
        self.live = uuid.uuid4().hex; self.events = []; self.event = None; self.offset = self.event_sequence = 0
        self.send({'type':'live_open','id':self.live,'runtime_id':self.hello['runtime_id'],'token':token})
        while True:
            value = super().receive()
            if value['type'] == 'live_opened': assert value['id'] == self.live; break
            assert self.consume(value),value
            if self.events and self.events[-1]['type'] == 'live_closed': return self.events.pop()
        ready = self.next_event()
        assert ready['type'] == 'ready' and ready['version'] == 1 and ready['fixture'] == '图'*100000
        return None

    def command(self, value): self.send({'type':'live_send','id':self.live,'data':json.dumps(value)})
    def select(self, session, after=0, selection=1):
        self.command({'type':'subscribe','project_id':'default','session_id':session,'after':after,'selection':selection})
    def close_live(self):
        self.send({'type':'live_close','id':self.live}); self.live = ''; self.events = []; self.event = None
        time.sleep(.1)

    def until(self, predicate, timeout=8):
        end = time.monotonic()+timeout
        while time.monotonic() < end:
            value = self.next_event()
            if predicate(value): return value
        raise AssertionError('live event deadline')


def exercise(*, client, hello, app, device, call, bearer, website_port, clients):
    assert hello['live'] and hello['live_limit'] == 2*1024*1024
    rpc = LiveRpc(client,hello)
    _,headers,_,_ = rpc.json('GET','/api/v1/settings'); token = headers['X-Mdo-Write-Token']
    denied = rpc.open_live(uuid.uuid4().hex+'-0')
    assert denied and denied['status'] == 403,denied
    time.sleep(.1); assert rpc.open_live(token) is None

    def json_call(method,path,body=None):
        value,_,status,_ = rpc.json(method,path,body,[['X-Mdo-Write-Token',token]])
        assert status in (200,201,202),value
        return value['data']

    def session(permission='read-only'):
        return json_call('POST','/api/v1/sessions',{'project_id':'default','title':'Remote live',
            'agent_id':'mdo.default','model_id':'ornith-1.5-35b','protocol':'openai-chat-completions',
            'reasoning_effort':'medium','permission_profile':permission,'max_output_tokens':1024})['id']

    sid = session(); path = f'/api/v1/projects/default/sessions/{sid}'
    rpc.select(sid); assert rpc.until(lambda e:e['type']=='events')['next_cursor'] == 0
    json_call('POST',path+'/runs',{'prompt':'remote stream','timeout_ms':10000})
    items = []; resumed = False
    def finished(event):
        nonlocal resumed
        if event['type'] != 'events': return False
        items.extend(event['items'])
        if not resumed and any(item['kind']=='model_text_delta' for item in event['items']):
            resumed = True; cursor = event['next_cursor']; rpc.close_live(); assert rpc.open_live(token) is None
            rpc.select(sid,cursor,selection=2)
        return any(item['kind']=='agent_done' for item in event['items'])
    rpc.until(finished)
    assert resumed
    assert [i['event_id'] for i in items] == sorted({i['event_id'] for i in items})
    assert ''.join(i['text'] for i in items if i['kind']=='model_text_delta') == ''.join(f'part-{i} ' for i in range(12))
    assert json_call('GET',path+'/events?after=0&limit=32')['items'] == items

    json_call('POST',path+'/runs',{'prompt':'remote ask','timeout_ms':10000})
    question = None
    def asked(event):
        nonlocal question
        if event['type'] != 'changed': return False
        questions = json_call('GET',path+'/asks')['items']
        if questions: question = questions[0]
        return question is not None
    rpc.until(asked); assert question['question'] == 'Remote question?'
    json_call('PUT',path+f'/asks/{question["id"]}',{'answer':'Yes'})
    rpc.until(lambda e:e['type']=='events' and any(i['kind']=='agent_done' for i in e['items']))

    approval_sid = session('balanced'); approval_path = f'/api/v1/projects/default/sessions/{approval_sid}'
    rpc.select(approval_sid,selection=3)
    json_call('POST',approval_path+'/runs',{'prompt':'remote approval','timeout_ms':10000})
    approval = None
    def awaiting(event):
        nonlocal approval
        if event['type'] != 'changed': return False
        entries = json_call('GET','/api/v1/approvals')['items']
        if entries: approval = entries[0]
        return approval is not None
    rpc.until(awaiting); assert approval['tool'] == 'write'
    json_call('PUT',f'/api/v1/approvals/{approval["id"]}',{'decision':'deny'})
    rpc.until(lambda e:e['type']=='events' and any(i['kind']=='agent_done' for i in e['items']))

    assert rpc.largest > 262123,'event must exceed a single relay message'
    # Actual manager Unit joins an open native live worker while API remains.
    if app()['persistent']: app(path='/test-remote-lifecycle')
    print('PASS remote live token, stream/cursor resume, HTTP parity, ask/approval, large Unicode event and open-channel Unit')
    return client


def setup(site,home):
    # Disposable native fixture: add one 300 KiB Unicode field to the real
    # ready event, so it crosses the relay frame/window bounds deterministically.
    # Actual model events retain their normal xwork text limits and schema.
    path = site/'src/api/live.c'; source = path.read_text(encoding='utf-8')
    hook = r'''
static bool RemoteLiveTestReady(MdoLiveClient* Client) {
    char* text = xrtCalloc(1u,300100u);
    if (!text) return false;
    size_t size = (size_t)sprintf(text,"{\"type\":\"ready\",\"version\":1,\"fixture\":\"");
    for (size_t i = 0u; i < 100000u; i++) { memcpy(text+size,"\xe5\x9b\xbe",3u); size += 3u; }
    memcpy(text+size,"\"}",2u); size += 2u;
    bool ok = MdoLiveFrame(Client,XWS_OPCODE_TEXT,text,size);
    xrtFree(text); return ok;
}
'''
    source = source.replace('static xtaskoutcome MdoLiveRun(',hook+'\nstatic xtaskoutcome MdoLiveRun(')
    original = 'MdoLiveText(Client, "{\\"type\\":\\"ready\\",\\"version\\":1}")'
    assert original in source
    path.write_text(source.replace(original,'RemoteLiveTestReady(Client)'),encoding='utf-8')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host',type=Path,default=ROOT/'.build/host/xs.exe')
    parser.add_argument('--website-host',type=Path,default=XADMIN/'xs.exe')
    args = parser.parse_args()
    model = ThreadingHTTPServer(('127.0.0.1',0),Model)
    thread = threading.Thread(target=model.serve_forever,daemon=True); thread.start()
    endpoint = f'http://127.0.0.1:{model.server_port}/v1'
    names = {'MDO_ORNITH_API_KEY':'remote-test-key','MDO_ORNITH_CHAT_COMPLETIONS_URL':endpoint,
        'MDO_ORNITH_RESPONSES_URL':endpoint,'MDO_ORNITH_ANTHROPIC_URL':'https://example.invalid'}
    saved = {name:os.environ.get(name) for name in names}; os.environ.update(names)
    try: run(args.host.resolve(),args.website_host.resolve(),exercise,site_setup=setup)
    finally:
        model.shutdown(); model.server_close(); thread.join(3)
        for name,value in saved.items():
            if value is None: os.environ.pop(name,None)
            else: os.environ[name] = value
