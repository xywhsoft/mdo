"""ADB smoke tests with an isolated loopback SSH/SFTP fixture (requires Paramiko)."""
from pathlib import Path
import argparse
import hashlib
import json
import logging
import os
import shlex
import socket
import stat
import subprocess
import threading
import time
import paramiko

REPO = Path(__file__).resolve().parents[3]
RUNTIME = REPO / 'tools/runtime/android-arm64-v8a'
CHECKS = REPO / '.build/runtime-tools/checks/android'
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--adb', type=Path, default=REPO / '.build/android-toolchain-windows/sdk/platform-tools/adb.exe')
parser.add_argument('--serial', required=True)
args = parser.parse_args()
ADB = args.adb
SERIAL = args.serial
REMOTE = '/data/local/tmp/mdo-runtime-tools-20261006'
CHECKS.mkdir(parents=True, exist_ok=True)
logging.basicConfig(filename=CHECKS / 'ssh-fixture.log', level=logging.DEBUG, filemode='w')
report = []

def adb(*args, timeout=60):
    done = subprocess.run([str(ADB), '-s', SERIAL, *args], capture_output=True,
                          encoding='utf-8', errors='replace', timeout=timeout)
    if done.returncode:
        raise RuntimeError((args, done.returncode, done.stdout, done.stderr))
    return done.stdout + done.stderr

def shell(argv):
    return adb('shell', ' '.join(shlex.quote(str(x)) for x in argv))

def check(name, argv, contains=None):
    output = shell(argv)
    if contains is not None and contains not in output:
        raise AssertionError((name, output))
    report.append({'check': name, 'passed': True, 'output': output.strip()[:500]})
    print(name + ': OK', flush=True)
    return output

abi = shell(['getprop', 'ro.product.cpu.abi']).strip()
api = shell(['getprop', 'ro.build.version.sdk']).strip()
if abi != 'arm64-v8a':
    raise RuntimeError('Expected arm64-v8a, got ' + abi)
shell(['mkdir', '-p', REMOTE])
adb('push', str(RUNTIME), REMOTE, timeout=180)
ROOT = REMOTE + '/android-arm64-v8a'
shell(['chmod', '-R', '700', ROOT])
B = ROOT + '/busybox/busybox'
SSH = ROOT + '/openssh/ssh'
PYTHON = ROOT + '/python'
PYENV = ['env', 'MDO_PYTHON_HOME=' + PYTHON, 'LD_LIBRARY_PATH=' + PYTHON + '/lib',
         'SSL_CERT_FILE=' + PYTHON + '/cacert.pem', PYTHON + '/bin/python3']
check('busybox_version', [B, '--help'], 'BusyBox v1.38.0')
check('busybox_pipeline_unicode', [B, 'sh', '-c',
      'printf "alpha\\nbeta\\n" > "' + REMOTE + '/中文 空格.txt"; '
      'cat "' + REMOTE + '/中文 空格.txt" | sed s/beta/BETA/'], 'BETA')
check('busybox_archive', [B, 'sh', '-c',
      'cd ' + REMOTE + '; tar -czf fixture.tar.gz "中文 空格.txt"; tar -tzf fixture.tar.gz'], '中文 空格.txt')
check('jq_json_regex', [ROOT + '/jq/jq', '-cn', '{ok:true,sum:([1,2,3]|add),regex:("mdo"|test("^md"))}'], '"regex":true')
check('curl_version', [ROOT + '/curl/curl', '--version'], 'OpenSSL/3.5.9')
check('curl_https', [ROOT + '/curl/curl', '--cacert', ROOT + '/curl/cacert.pem',
      '--fail', '--silent', '--show-error', '--max-time', '25', '-I', 'https://curl.se/'], '200')
check('python_stdlib', PYENV + ['-c',
      'import sys,json,csv,pathlib,zipfile,sqlite3,ssl,hashlib,sysconfig,ctypes; '
      'print(json.dumps({"ok":True,"version":sys.version,"sqlite":sqlite3.sqlite_version,"tls":ssl.OPENSSL_VERSION,"platform":sysconfig.get_platform()}))'], '"ok": true')
script = CHECKS / '中文 脚本.py'
script.write_text('import pathlib,json,sqlite3\np=pathlib.Path(__file__).with_suffix(".json")\np.write_text(json.dumps({"text":"安卓中文"},ensure_ascii=False),encoding="utf-8")\nassert json.loads(p.read_text(encoding="utf-8"))["text"]=="安卓中文"\nc=sqlite3.connect(":memory:"); assert c.execute("select 2+3").fetchone()[0]==5\nprint("script+unicode+sqlite: OK")\n', encoding='utf-8')
adb('push', str(script), REMOTE + '/中文 脚本.py')
check('python_script_unicode_sqlite', PYENV + [REMOTE + '/中文 脚本.py'], 'script+unicode+sqlite: OK')
check('python_https', PYENV + ['-c',
      'import urllib.request; r=urllib.request.urlopen("https://curl.se/",timeout=25); print("HTTPS",r.status)'], 'HTTPS 200')
check('ssh_version', [SSH, '-V'], 'OpenSSH_10.3p1')
check('ssh_config', [SSH, '-F', 'none', '-G', '-p', '2222', 'fixture.invalid'], 'port 2222')
key = REMOTE + '/fixture_ed25519'
# A fresh test key lives only in the temporary phone directory.
shell(['rm', '-f', key, key + '.pub'])
check('ssh_keygen', [ROOT + '/openssh/ssh-keygen', '-q', '-t', 'ed25519', '-N', '', '-f', key])
check('ssh_fingerprint', [ROOT + '/openssh/ssh-keygen', '-lf', key + '.pub'], 'ED25519')
adb('pull', key + '.pub', str(CHECKS / 'fixture_ed25519.pub'))
allowed = (CHECKS / 'fixture_ed25519.pub').read_text().split()[1]
host_key = paramiko.RSAKey.generate(2048)

class Server(paramiko.ServerInterface):
    def check_auth_publickey(self, username, public_key):
        return paramiko.AUTH_SUCCESSFUL if username == 'mdo' and public_key.get_base64() == allowed else paramiko.AUTH_FAILED
    def get_allowed_auths(self, username):
        return 'publickey'
    def check_channel_request(self, kind, channel_id):
        return paramiko.OPEN_SUCCEEDED if kind == 'session' else paramiko.OPEN_FAILED_ADMINISTRATIVELY_PROHIBITED
    def check_channel_exec_request(self, channel, command):
        def respond():
            channel.send(b'mdo SSH fixture: OK\n' if command == b'mdo-tool-fixture' else b'unsupported fixture command\n')
            channel.send_exit_status(0 if command == b'mdo-tool-fixture' else 1)
            channel.shutdown_write()
            time.sleep(0.1)
            channel.close()
        threading.Thread(target=respond, daemon=True).start()
        return True

class Handle(paramiko.SFTPHandle):
    def stat(self):
        return paramiko.SFTPAttributes.from_stat(os.fstat(self.readfile.fileno()))
    def chattr(self, attr):
        try:
            if attr.st_size is not None:
                os.ftruncate(self.writefile.fileno(), attr.st_size)
                attr._flags &= ~attr.FLAG_SIZE
            paramiko.SFTPServer.set_file_attr(self.filename, attr)
            return paramiko.SFTP_OK
        except OSError as error:
            return paramiko.SFTPServer.convert_errno(error.errno or 13)

class Files(paramiko.SFTPServerInterface):
    def resolve(self, path):
        candidate = (CHECKS / 'sftp' / path.lstrip('/')).resolve()
        base = (CHECKS / 'sftp').resolve()
        if not candidate.is_relative_to(base):
            raise OSError('outside fixture')
        return candidate
    def stat(self, path):
        try:
            return paramiko.SFTPAttributes.from_stat(self.resolve(path).stat())
        except OSError as error:
            return paramiko.SFTPServer.convert_errno(error.errno or 13)
    lstat = stat
    def open(self, path, flags, attr):
        try:
            fd = os.open(self.resolve(path), flags | getattr(os, 'O_BINARY', 0), 0o600)
            stream = os.fdopen(fd, 'r+b' if flags & os.O_RDWR else 'wb' if flags & os.O_WRONLY else 'rb')
            handle = Handle(flags)
            handle.filename = str(self.resolve(path))
            handle.readfile = stream
            handle.writefile = stream
            return handle
        except OSError as error:
            return paramiko.SFTPServer.convert_errno(error.errno or 13)

(CHECKS / 'sftp').mkdir(exist_ok=True)
class FixtureSFTP(paramiko.SFTPServer):
    def __init__(self, channel, *args, **kwargs):
        self.fixture_channel = channel
        super().__init__(channel, *args, **kwargs)
    def finish_subsystem(self):
        self.fixture_channel.send_exit_status(0)
        super().finish_subsystem()

listener = socket.socket()
listener.bind(('127.0.0.1', 0))
listener.listen(8)
listener.settimeout(1)
port = listener.getsockname()[1]
stop = threading.Event()
transports = []

def serve():
    while not stop.is_set():
        try:
            client, _ = listener.accept()
        except socket.timeout:
            continue
        except OSError:
            break
        transport = paramiko.Transport(client)
        transports.append(transport)
        transport.add_server_key(host_key)
        transport.set_subsystem_handler('sftp', FixtureSFTP, Files)
        try:
            transport.start_server(server=Server())
        except (EOFError, paramiko.SSHException):
            # ssh-keyscan ends the session immediately after reading host keys.
            transport.close()

threading.Thread(target=serve, daemon=True).start()
known_hosts = CHECKS / 'known_hosts'
known_hosts.write_text(f'[127.0.0.1]:{port} {host_key.get_name()} {host_key.get_base64()}\n', encoding='ascii')
adb('push', str(known_hosts), REMOTE + '/known_hosts')
adb('reverse', f'tcp:{port}', f'tcp:{port}')
common = ['-F', 'none', '-i', key, '-o', 'IdentitiesOnly=yes', '-o', 'BatchMode=yes',
          '-o', 'StrictHostKeyChecking=yes', '-o', 'UserKnownHostsFile=' + REMOTE + '/known_hosts',
          '-o', 'ConnectTimeout=8']
try:
    check('ssh_keyscan', [ROOT + '/openssh/ssh-keyscan', '-t', 'rsa', '-p', str(port), '127.0.0.1'], host_key.get_base64())
    check('ssh_authenticated_exec', [SSH, *common, '-p', str(port), 'mdo@127.0.0.1', 'mdo-tool-fixture'], 'mdo SSH fixture: OK')
    fixture = CHECKS / 'upload.txt'
    fixture.write_text('mdo 安卓 SSH/SFTP transfer\n', encoding='utf-8')
    adb('push', str(fixture), REMOTE + '/upload.txt')
    check('scp_upload', [ROOT + '/openssh/scp', '-S', SSH, *common, '-P', str(port), REMOTE + '/upload.txt', 'mdo@127.0.0.1:/uploaded.txt'])
    batch = CHECKS / 'batch.txt'
    batch.write_text(f'get /uploaded.txt {REMOTE}/downloaded.txt\n', encoding='ascii')
    adb('push', str(batch), REMOTE + '/batch.txt')
    check('sftp_download', [ROOT + '/openssh/sftp', '-S', SSH, *common, '-P', str(port), '-b', REMOTE + '/batch.txt', 'mdo@127.0.0.1'])
    adb('pull', REMOTE + '/downloaded.txt', str(CHECKS / 'downloaded.txt'))
    assert fixture.read_bytes() == (CHECKS / 'downloaded.txt').read_bytes() == (CHECKS / 'sftp/uploaded.txt').read_bytes()
    report.append({'check': 'ssh_transfer_sha256', 'passed': True, 'sha256': hashlib.sha256(fixture.read_bytes()).hexdigest()})
finally:
    adb('reverse', '--remove', f'tcp:{port}')
    stop.set()
    listener.close()
    for transport in transports:
        transport.close()
    shell(['rm', '-f', key, key + '.pub'])

result = {'device': {'serial': SERIAL, 'abi': abi, 'api': int(api)}, 'context': 'adb shell, isolated /data/local/tmp directory', 'checks': report}
(CHECKS / 'verification.json').write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
print(json.dumps({'passed': len(report), 'report': str(CHECKS / 'verification.json')}, ensure_ascii=False))
