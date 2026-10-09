"""Opt-in Android UID network test against disposable local TLS/SSH peers.

Requires the development-signed debuggable full APK and optional paramiko.
No user's SSH server, account key, system trust store or application data is changed.
"""
import argparse
from datetime import datetime,timedelta,timezone
import hashlib
from http.server import BaseHTTPRequestHandler,ThreadingHTTPServer
import io
import ipaddress
import json
import os
from pathlib import Path
import socket
import ssl
import subprocess
import tempfile
import threading
import time
import uuid

ROOT=Path(__file__).resolve().parents[1]

def run(adb):
    import paramiko
    from cryptography import x509
    from cryptography.hazmat.primitives import hashes,serialization
    from cryptography.hazmat.primitives.asymmetric import rsa
    from cryptography.x509.oid import NameOID
    def command(*args,stdin=None,check=True):
        result=subprocess.run([str(adb),*args],input=stdin,capture_output=True,timeout=25)
        if check and result.returncode:raise AssertionError(result.stderr.decode(errors='replace')[-1000:])
        return result.stdout
    def app(*args,stdin=None):return command('shell','run-as','org.xleaves.mdo',*args,stdin=stdin)
    uid=app('id').decode();assert 'uid=' in uid
    package=command('shell','pm','path','org.xleaves.mdo').decode().strip().removeprefix('package:')
    native=package.rsplit('/',1)[0]+'/lib/arm64'
    runtime='/data/user/0/org.xleaves.mdo/files/mdo-runtime/1'
    qa='files/mdo-qa-'+uuid.uuid4().hex
    app('mkdir',qa)
    def write(name,data):app('tee',qa+'/'+name,stdin=data)
    baseline=app('sha256sum','files/mdo-home/config/settings.json').decode().split()[0]
    result=dict(app_uid=int(uid.split('uid=')[1].split('(')[0]),https=False,ssh=False,scp=False,sftp=False)
    with tempfile.TemporaryDirectory(prefix='mdo-device-') as temp:
        directory=Path(temp);host_key=paramiko.RSAKey.generate(2048);client_key=paramiko.RSAKey.generate(2048)
        stream=io.StringIO();client_key.write_private_key(stream);write('client.key',stream.getvalue().encode());app('chmod','600',qa+'/client.key')
        listener=socket.socket();listener.bind(('127.0.0.1',0));listener.listen();ssh_port=listener.getsockname()[1];listener.settimeout(.5)
        class Server(paramiko.ServerInterface):
            def check_auth_publickey(self,user,key):return paramiko.AUTH_SUCCESSFUL if user=='mdo-qa' and key==client_key else paramiko.AUTH_FAILED
            def get_allowed_auths(self,user):return 'publickey'
            def check_channel_request(self,kind,chanid):return paramiko.OPEN_SUCCEEDED if kind=='session' else paramiko.OPEN_FAILED_ADMINISTRATIVELY_PROHIBITED
            def check_channel_exec_request(self,channel,cmd):
                if cmd!=b'mdo_probe':return False
                def reply():
                    time.sleep(.05);channel.send(b'mdo_authenticated\n');channel.send_exit_status(0);channel.shutdown_write();channel.close()
                threading.Thread(target=reply,daemon=True).start();return True
        class Sftp(paramiko.SFTPServerInterface):
            def path(self,path):
                value=directory/path.lstrip('/');return value if value.resolve().is_relative_to(directory.resolve()) else None
            def canonicalize(self,path):return '/'
            def stat(self,path):
                p=self.path(path)
                try:return paramiko.SFTPAttributes.from_stat(p.stat()) if p else paramiko.SFTP_PERMISSION_DENIED
                except OSError as e:return paramiko.SFTPServer.convert_errno(e.errno)
            lstat=stat
            def open(self,path,flags,attr):
                p=self.path(path)
                try:
                    if not p:return paramiko.SFTP_PERMISSION_DENIED
                    fd=os.open(p,flags,0o600);mode='r+b' if flags&os.O_RDWR else 'wb' if flags&os.O_WRONLY else 'rb';file=os.fdopen(fd,mode)
                    class Handle(paramiko.SFTPHandle):
                        def stat(self):return paramiko.SFTPAttributes.from_stat(os.fstat(file.fileno()))
                        def chattr(self,attr):
                            if attr.st_size is not None:file.truncate(attr.st_size)
                            return paramiko.SFTP_OK
                    handle=Handle(flags);handle.readfile=file;handle.writefile=file;return handle
                except OSError as e:return paramiko.SFTPServer.convert_errno(e.errno)
        stopping=threading.Event();transports=[]
        class SftpServer(paramiko.SFTPServer):
            def __init__(self,channel,*args,**kwargs):self.qa_channel=channel;super().__init__(channel,*args,**kwargs)
            def finish_subsystem(self):self.qa_channel.send_exit_status(0);super().finish_subsystem()
        def serve_ssh():
            while not stopping.is_set():
                try:connection,_=listener.accept()
                except socket.timeout:continue
                except OSError:return
                transport=paramiko.Transport(connection);transports.append(transport);transport.add_server_key(host_key)
                transport.set_subsystem_handler('sftp',SftpServer,Sftp)
                try:transport.start_server(server=Server())
                except (OSError,paramiko.SSHException):transport.close()
        threading.Thread(target=serve_ssh,daemon=True).start()
        key=rsa.generate_private_key(public_exponent=65537,key_size=2048);subject=x509.Name([x509.NameAttribute(NameOID.COMMON_NAME,'mdo isolated TLS')]);now=datetime.now(timezone.utc)
        cert=x509.CertificateBuilder().subject_name(subject).issuer_name(subject).public_key(key.public_key()).serial_number(x509.random_serial_number()).not_valid_before(now-timedelta(minutes=1)).not_valid_after(now+timedelta(days=1)).add_extension(x509.SubjectAlternativeName([x509.IPAddress(ipaddress.ip_address('127.0.0.1'))]),False).add_extension(x509.BasicConstraints(ca=True,path_length=None),True).sign(key,hashes.SHA256())
        pem=cert.public_bytes(serialization.Encoding.PEM);(directory/'ca.pem').write_bytes(pem);(directory/'tls.key').write_bytes(key.private_bytes(serialization.Encoding.PEM,serialization.PrivateFormat.PKCS8,serialization.NoEncryption()));write('ca.pem',pem)
        class Http(BaseHTTPRequestHandler):
            def log_message(self,*args):pass
            def do_GET(self):self.send_response(200);self.send_header('Content-Length','18');self.end_headers();self.wfile.write(b'mdo_https_verified')
        https=ThreadingHTTPServer(('127.0.0.1',0),Http);tls=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER);tls.load_cert_chain(directory/'ca.pem',directory/'tls.key');https.socket=tls.wrap_socket(https.socket,server_side=True);threading.Thread(target=https.serve_forever,daemon=True).start();tls_port=https.server_port
        for port in (ssh_port,tls_port):command('reverse','tcp:'+str(port),'tcp:'+str(port))
        write('known_hosts',f'[127.0.0.1]:{ssh_port} {host_key.get_name()} {host_key.get_base64()}\n'.encode())
        ssh=runtime+'/openssh/ssh';options=['-i',qa+'/client.key','-o','BatchMode=yes','-o','StrictHostKeyChecking=yes','-o','UserKnownHostsFile='+qa+'/known_hosts','-o','ConnectTimeout=10']
        def tool(path,*args,stdin=None):return app('env','LD_LIBRARY_PATH='+native,path,*args,stdin=stdin)
        try:
            output=tool(runtime+'/curl/curl','--silent','--show-error','--max-time','10','--cacert',qa+'/ca.pem',f'https://127.0.0.1:{tls_port}/');assert output==b'mdo_https_verified';result['https']=True
            output=tool(ssh,'-n','-p',str(ssh_port),*options,'mdo-qa@127.0.0.1','mdo_probe');assert b'mdo_authenticated' in output;result['ssh']=True
            write('input',b'mdo_copy_verified')
            tool(runtime+'/openssh/scp','-P',str(ssh_port),'-S',ssh,*options,qa+'/input','mdo-qa@127.0.0.1:/uploaded.txt');assert (directory/'uploaded.txt').read_bytes()==b'mdo_copy_verified';result['scp']=True
            tool(runtime+'/openssh/sftp','-P',str(ssh_port),'-S',ssh,*options,'-b','-','mdo-qa@127.0.0.1',stdin=f'get /uploaded.txt {qa}/output\nquit\n'.encode());assert app('cat',qa+'/output')==b'mdo_copy_verified';result['sftp']=True
            after=app('sha256sum','files/mdo-home/config/settings.json').decode().split()[0];assert after==baseline;result['settings_unchanged']=True
            (ROOT/'.build/releases/android-device-network.json').write_text(json.dumps(result,indent=2),encoding='utf-8');print('PASS Android app UID, verified TLS, public-key SSH, SCP upload, SFTP download, settings unchanged')
        finally:
            for port in (ssh_port,tls_port):command('reverse','--remove','tcp:'+str(port),check=False)
            stopping.set();listener.close();https.shutdown();https.server_close()
            for transport in transports:transport.close()
            for name in ('client.key','ca.pem','known_hosts','input','output'):command('shell','run-as','org.xleaves.mdo','rm','-f',qa+'/'+name,check=False)
            command('shell','run-as','org.xleaves.mdo','rmdir',qa,check=False)

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--adb',type=Path,default=ROOT/'.build/android-toolchain-windows/sdk/platform-tools/adb.exe');run(parser.parse_args().adb)
