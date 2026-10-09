"""Bounded functional tests for downloads, Unicode search and archive round trips.

Windows uses bundled executables. --adb tests Android staging; --app-uid tests
installed APK code using run-as, without depending on execution from writable data.
"""
import argparse,base64,hashlib,json,os,shlex,subprocess,tempfile,threading,ssl
from pathlib import Path
from http.server import BaseHTTPRequestHandler,ThreadingHTTPServer
ROOT=Path(__file__).resolve().parents[3]
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--adb',type=Path);p.add_argument('--serial');p.add_argument('--app-uid',action='store_true');a=p.parse_args()
 report={};payload=bytes(range(256))*49152;ranges=[]
 class Peer(BaseHTTPRequestHandler):
  def log_message(self,*args):pass
  def do_GET(self):
   value=self.headers.get('Range');start,end=0,len(payload)-1
   if value:
    pair=value.removeprefix('bytes=').split('-');start=int(pair[0]);end=int(pair[1]) if pair[1] else end;ranges.append((start,end));self.send_response(206);self.send_header('Content-Range',f'bytes {start}-{end}/{len(payload)}')
   else:self.send_response(200)
   self.send_header('Content-Length',str(end-start+1));self.send_header('Accept-Ranges','bytes');self.end_headers()
   try:self.wfile.write(payload[start:end+1])
   except (BrokenPipeError,ConnectionResetError):pass
 server=ThreadingHTTPServer(('127.0.0.1',0),Peer);threading.Thread(target=server.serve_forever,daemon=True).start()
 adb_prefix=[str(a.adb),*(['-s',a.serial] if a.serial else [])] if a.adb else None
 def adb(*args,data=None):return subprocess.run([*adb_prefix,*args],input=data,capture_output=True,timeout=90,check=True).stdout
 with tempfile.TemporaryDirectory(prefix='mdo-extra-') as temp:
  local=Path(temp);remote='/data/local/tmp/mdo-extra-tools-20261007';native=None;tls=None
  if a.adb:
   if a.app_uid:
    package=adb('shell','pm','path','org.xleaves.mdo').decode().strip().removeprefix('package:');native=package.rsplit('/',1)[0]+'/lib/arm64'
    remote='files/mdo-extra-qa'
    def shell(cmd,data=None):return adb('shell','-T','run-as','org.xleaves.mdo','sh','-c',shlex.quote(cmd),data=data)
    # adb joins shell arguments: quote each complete shell program as one argument.
    shell('mkdir -p '+shlex.quote(remote))
    def put(name,data):shell('/system/bin/toybox base64 -d > '+shlex.quote(remote+'/'+name),base64.b64encode(data))
    programs={'aria2':native+'/libmdo_aria2_aria2c.so','rg':native+'/libmdo_ripgrep_rg.so','7zip':native+'/libmdo_7zip_7zz.so'}
   else:
    def shell(cmd,data=None):return adb('shell','-T',cmd,data=data)
    shell('mkdir -p '+remote);adb('push',str(ROOT/'tools/runtime/android-arm64-v8a/aria2'),remote);adb('push',str(ROOT/'tools/runtime/android-arm64-v8a/ripgrep'),remote);adb('push',str(ROOT/'tools/runtime/android-arm64-v8a/7zip'),remote);shell('chmod -R 700 '+remote)
    def put(name,data):shell('/system/bin/toybox base64 -d > '+shlex.quote(remote+'/'+name),base64.b64encode(data))
    programs={k:remote+'/'+v for k,v in {'aria2':'aria2/aria2c','rg':'ripgrep/rg','7zip':'7zip/7zz'}.items()}
   def run(tool,*args):
    argv=[programs[tool],*map(str,args)];command=' '.join(map(shlex.quote,argv));return shell('cd '+shlex.quote(remote)+' && '+command).decode('utf-8',errors='replace')
   def read(name):return adb('exec-out',('run-as org.xleaves.mdo ' if a.app_uid else '')+'cat '+shlex.quote(remote+'/'+name))
   def mkdir(name):shell('mkdir -p '+shlex.quote(remote+'/'+name))
   # Staged paths become relative after cd; APK paths are already absolute.
   if not a.app_uid:programs={k:v.removeprefix(remote+'/') for k,v in programs.items()}
   adb('reverse','tcp:'+str(server.server_port),'tcp:'+str(server.server_port))
  else:
   base=ROOT/'tools/runtime/windows-x86_64';programs={'aria2':base/'aria2/aria2c.exe','rg':base/'ripgrep/rg.exe','7zip':base/'7zip/7z.exe'}
   def put(name,data):target=local/name;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(data)
   def run(tool,*args):return subprocess.run([str(programs[tool]),*map(str,args)],cwd=local,capture_output=True,timeout=90,check=True).stdout.decode('utf-8',errors='replace')
   def read(name):return (local/name).read_bytes()
   def mkdir(name):(local/name).mkdir(parents=True,exist_ok=True)
  try:
   run('aria2','--version');run('rg','--version');run('7zip','i')
   opts=['--no-conf','--enable-rpc=false','--allow-overwrite=true','--auto-file-renaming=false','--file-allocation=none','--console-log-level=error','--summary-interval=0','--check-certificate=true','--checksum=sha-256='+hashlib.sha256(payload).hexdigest()]
   url=f'http://127.0.0.1:{server.server_port}/payload'
   run('aria2',*opts,'-x','4','-s','4','-k','1M','-o','parallel.bin',url)
   downloaded=read('parallel.bin');assert downloaded==payload and len({start for start,end in ranges})>=3,(len(downloaded),hashlib.sha256(downloaded).hexdigest(),ranges)
   report['parallel_download_sha256']=True;report['parallel_ranges']=len(ranges);ranges.clear()
   prefix=262144;put('resume.bin',payload[:prefix]);assert read('resume.bin')==payload[:prefix],('seed',len(read('resume.bin')));run('aria2',*opts,'--continue=true','-x','1','-s','1','-o','resume.bin',url)
   assert read('resume.bin')==payload and any(start==prefix for start,end in ranges),ranges;report['resume_sha256']=True
   mkdir('搜索 空格');put('搜索 空格/中文.txt','第一行\n墨斗命中\n'.encode());put('搜索 空格/ignored.txt','墨斗命中\n'.encode());put('搜索 空格/.ignore',b'ignored.txt\n')
   data=[json.loads(line) for line in run('rg','--no-config','--json','--fixed-strings','墨斗','搜索 空格').splitlines()]
   hits=[row for row in data if row['type']=='match'];assert len(hits)==1 and hits[0]['data']['line_number']==2 and hits[0]['data']['path']['text'].endswith('中文.txt');report['unicode_json_ignore']=True
   run('7zip','a','-t7z','-mx=1','-y','压缩包.7z','搜索 空格/中文.txt');run('7zip','t','压缩包.7z');run('7zip','x','-y','-o解压 空格','压缩包.7z')
   assert read('解压 空格/搜索 空格/中文.txt')=='第一行\n墨斗命中\n'.encode();report['archive_unicode_roundtrip']=True
   if a.adb:
    from datetime import datetime,timedelta,timezone
    from cryptography import x509
    from cryptography.hazmat.primitives import hashes,serialization
    from cryptography.hazmat.primitives.asymmetric import rsa
    from cryptography.x509.oid import NameOID
    import ipaddress
    key=rsa.generate_private_key(public_exponent=65537,key_size=2048);subject=x509.Name([x509.NameAttribute(NameOID.COMMON_NAME,'mdo download QA')]);now=datetime.now(timezone.utc)
    cert=x509.CertificateBuilder().subject_name(subject).issuer_name(subject).public_key(key.public_key()).serial_number(x509.random_serial_number()).not_valid_before(now-timedelta(minutes=1)).not_valid_after(now+timedelta(days=1)).add_extension(x509.SubjectAlternativeName([x509.IPAddress(ipaddress.ip_address('127.0.0.1'))]),False).add_extension(x509.BasicConstraints(ca=True,path_length=None),True).sign(key,hashes.SHA256())
    pem=cert.public_bytes(serialization.Encoding.PEM);put('ca.pem',pem);(local/'ca.pem').write_bytes(pem);(local/'key.pem').write_bytes(key.private_bytes(serialization.Encoding.PEM,serialization.PrivateFormat.PKCS8,serialization.NoEncryption()))
    tls=ThreadingHTTPServer(('127.0.0.1',0),Peer);ctx=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER);ctx.load_cert_chain(local/'ca.pem',local/'key.pem');tls.socket=ctx.wrap_socket(tls.socket,server_side=True);threading.Thread(target=tls.serve_forever,daemon=True).start();adb('reverse','tcp:'+str(tls.server_port),'tcp:'+str(tls.server_port))
    run('aria2',*opts,'--ca-certificate=ca.pem','-o','tls.bin',f'https://127.0.0.1:{tls.server_port}/payload');assert read('tls.bin')==payload;report['verified_tls']=True
   else:
    run('aria2','--no-conf','--check-certificate=true','--console-log-level=error','--summary-interval=0','-o','https.html','https://www.7-zip.org/');assert b'7-Zip' in read('https.html');report['verified_tls']=True
   report['context']='Android app UID installed code' if a.app_uid else 'ADB isolated staging' if a.adb else 'Windows bundled executables'
   out=ROOT/'.build/runtime-tools/checks'/('extra-app-uid.json' if a.app_uid else 'extra-android.json' if a.adb else 'extra-windows.json');out.parent.mkdir(parents=True,exist_ok=True);out.write_text(json.dumps(report,indent=2,ensure_ascii=False)+'\n');print(json.dumps(report,ensure_ascii=False))
  finally:
   server.shutdown();server.server_close()
   if a.adb:
    adb('reverse','--remove','tcp:'+str(server.server_port))
    if tls:adb('reverse','--remove','tcp:'+str(tls.server_port));tls.shutdown();tls.server_close()
    # Explicit task-owned path only; never the application Home/runtime.
    assert remote in ('files/mdo-extra-qa','/data/local/tmp/mdo-extra-tools-20261007')
    shell('rm -rf '+shlex.quote(remote))
if __name__=='__main__':main()
