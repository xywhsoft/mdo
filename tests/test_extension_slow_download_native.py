"""A progressing archive body that exceeds the former 30-second total deadline."""
import argparse,http.server,json,socketserver,threading,time
from pathlib import Path
from native_product_runner import product
ROOT=Path(__file__).resolve().parents[1]

class Handler(http.server.BaseHTTPRequestHandler):
 def do_GET(self):
  assert self.path.startswith('/api/v1/mdo/extensions/')
  self.send_response(200);self.send_header('Content-Type','application/zip');self.send_header('Content-Length',str(1048576));self.end_headers()
  try:
   for i in range(32):
    if i:time.sleep(1.06)
    self.wfile.write(b'x'*32768);self.wfile.flush()
  except (BrokenPipeError,ConnectionResetError):pass
 def log_message(self,*args):pass

CONTRACT=r'''
xvalue* QaProductContract(const xvalue* P,xvfs V){
 bool Home=QaProductHome(SdkField(P,"root"),SdkField(P,"app"),V),Configured=Home&&MdoConfigInit();xnetengineconfig Config;xrtNetEngineConfigInit(&Config);xnetengine* Engine=Configured?xrtNetEngineCreate(&Config):NULL;bool Started=Engine&&xrtNetEngineStart(Engine);MdoDownloadNetworkSetEngine(Started?Engine:NULL);XS_FetchResponse Response={0};bool Retryable=false;char Error[512]={0};uint64 Start=xllmClockUs();
 bool Ok=MdoExtServiceFetchAttempt("/api/v1/mdo/extensions/archive","GET",NULL,0,true,NULL,NULL,&Response,&Retryable,Error);uint64 Elapsed=xllmClockUs()-Start;
 xvalue* R=xrtValueObject();xrtValueObjectSetNew(R,XRT_STR_LITERAL("slow_progressing_archive_completed"),xrtValueBool(Ok&&Elapsed>30000000&&Response.BodySize==1048576));xrtValueObjectSetNew(R,XRT_STR_LITERAL("elapsed_ms"),xrtValueUInt(Elapsed/1000));xrtValueObjectSetNew(R,XRT_STR_LITERAL("error"),xrtValueString(xrtStrView(Error)));MdoDownloadFetchUnit(&Response);MdoDownloadNetworkSetEngine(NULL);if(Engine){xrtNetEngineStop(Engine);xrtNetEngineDestroy(Engine);}if(Configured)MdoConfigUnit();if(Home)MdoHomeUnit();return R;
}
'''

def main():
 p=argparse.ArgumentParser();p.add_argument('--url',required=True);p.add_argument('--home',type=Path,required=True);p.add_argument('--project',default='default');a=p.parse_args();out=ROOT/'.build/slow-extension-download';out.mkdir(parents=True,exist_ok=True)
 with socketserver.TCPServer(('127.0.0.1',0),Handler) as server:
  thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
  try:
   setup='tcc_define_symbol(T,"MDO_ACCOUNT_SERVICE_ORIGIN","\\\"http://127.0.0.1:'+str(server.server_address[1])+'\\\"");'
   result=product(a,out,CONTRACT,out/'result.json',compiler_setup=setup);assert result['slow_progressing_archive_completed'],result;print(json.dumps(result))
  finally:server.shutdown();thread.join()
if __name__=='__main__':main()
