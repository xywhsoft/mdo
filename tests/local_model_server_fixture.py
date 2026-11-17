"""Controlled llama-server protocol substitute; test-only, no inference."""
import argparse,json,os,sys,time
from http.server import BaseHTTPRequestHandler,ThreadingHTTPServer
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--port',type=int,required=True);p.add_argument('--alias',required=True);p.add_argument('--ctx-size',type=int,required=True);p.add_argument('--parallel',type=int,required=True);args,_=p.parse_known_args()
key=os.environ['LLAMA_API_KEY'];control=Path(os.environ['MDO_QA_MODEL_CONTROL'])
def flags():
    try:return json.loads(control.read_text())
    except (OSError,ValueError):return {}
class Handler(BaseHTTPRequestHandler):
    def log_message(self,*args):pass
    def reply(self,value,status=200):
        data=json.dumps(value).encode();self.send_response(status);self.send_header('Content-Type','application/json');self.send_header('Content-Length',str(len(data)));self.end_headers();self.wfile.write(data)
    def do_GET(self):
        if self.path=='/health':return self.reply(dict(status='ok'))
        if self.headers.get('Authorization')!='Bearer '+key:return self.reply(dict(error='unauthorized'),401)
        if self.path=='/v1/models':return self.reply(dict(data=[dict(id=args.alias)]))
        if self.path=='/props':return self.reply(dict(default_generation_settings=dict(n_ctx=args.ctx_size//args.parallel+int(bool(flags().get('bad_context')))),total_slots=args.parallel))
        return self.reply({},404)
    def do_POST(self):
        if self.headers.get('Authorization')!='Bearer '+key:return self.reply(dict(error='unauthorized'),401)
        body=json.loads(self.rfile.read(int(self.headers['Content-Length'])));assert body['model']==args.alias
        if flags().get('slow_call'):time.sleep(2)
        tools=bool(body.get('tools')) and not any(m['role']=='tool' for m in body['messages'])
        delta=dict(role='assistant',tool_calls=[dict(index=0,id='probe',type='function',function=dict(name='mdo_connection_probe',arguments='{}'))]) if tools else dict(role='assistant',content='OK')
        if body.get('stream'):
            self.send_response(200);self.send_header('Content-Type','text/event-stream');self.end_headers()
            try:
                for item in [dict(id='qa',object='chat.completion.chunk',model=args.alias,choices=[dict(index=0,delta=delta,finish_reason=None)]),dict(id='qa',object='chat.completion.chunk',model=args.alias,choices=[dict(index=0,delta={},finish_reason='tool_calls' if tools else 'stop')],usage=dict(prompt_tokens=10,completion_tokens=2,total_tokens=12))]:
                    self.wfile.write(('data: '+json.dumps(item)+'\n\n').encode())
                self.wfile.write(b'data: [DONE]\n\n');self.wfile.flush()
            except (BrokenPipeError,ConnectionResetError):pass
        else:self.reply(dict(id='qa',model=args.alias,choices=[dict(index=0,message=delta,finish_reason='tool_calls' if tools else 'stop')]))
print('Test fixture auth redaction check: '+key,flush=True)
ThreadingHTTPServer(('127.0.0.1',args.port),Handler).serve_forever()
