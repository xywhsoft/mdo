from pathlib import Path
import json
import os
import subprocess

repo = Path(__file__).resolve().parents[3]
root = repo / 'tools/runtime/windows-x86_64'
checks = repo / '.build/runtime-tools/checks/windows'
checks.mkdir(parents=True, exist_ok=True)
env = os.environ.copy()
env['HOME'] = str(checks)
env['USERPROFILE'] = str(checks)
env['PATH'] = str(root / 'openssh') + os.pathsep + str(Path(os.environ['SystemRoot']) / 'System32')
report = []

def run(name, argv, accepted=(0,), stdin=None, contains=None):
    done = subprocess.run([str(x) for x in argv], input=stdin, capture_output=True, encoding='utf-8', errors='replace', env=env, cwd=checks, timeout=35)
    output = done.stdout + done.stderr
    assert done.returncode in accepted, (name, done.returncode, output)
    assert contains is None or contains in output, (name, output)
    report.append({'check': name, 'exit_code': done.returncode, 'output': output[:200]})

busybox = root / 'busybox/busybox.exe'
run('busybox_version', [busybox, '--help'], contains='BusyBox v1.38.0')
run('busybox_shell_pipeline', [busybox, 'sh', '-c', "printf 'a\\nb\\n' | sed 's/b/B/'"], contains='B')
unicode_file = checks / '中文 空格.txt'
unicode_file.write_text('alpha\nbeta\n', encoding='utf-8')
run('busybox_unicode_path', [busybox, 'cat', unicode_file], contains='beta')
run('jq_json', [root / 'jq/jq.exe', '-cn', '{ok:true,sum:([1,2,3]|add)}'], contains='"sum":6')
run('curl_version', [root / 'curl/curl.exe', '--version'], contains='Schannel')
run('curl_https', [root / 'curl/curl.exe', '-I', '--fail', '--silent', '--show-error', '--max-time', '25', 'https://curl.se/'], contains='200')
run('python_stdlib', [root / 'python/python.exe', '-c', 'import json,csv,pathlib,zipfile,sqlite3,ssl,hashlib; print(json.dumps({"ok":True,"sqlite":sqlite3.sqlite_version,"tls":ssl.OPENSSL_VERSION}))'], contains='"ok": true')
run('git_version', [root / 'git/cmd/git.exe', '--version'], contains='git version')
run('ssh_version', [root / 'openssh/ssh.exe', '-V'], contains='OpenSSH_10.3p1')
run('ssh_config', [root / 'openssh/ssh.exe', '-F', 'none', '-G', '-p', '2222', 'fixture.invalid'], contains='port 2222')
run('scp_usage', [root / 'openssh/scp.exe', '-h'], accepted=(1,), contains='usage: scp')
run('sftp_usage', [root / 'openssh/sftp.exe', '-h'], accepted=(1,), contains='usage: sftp')
key = checks / 'ed25519'
if not key.exists():
    run('ssh_keygen', [root / 'openssh/ssh-keygen.exe', '-q', '-t', 'ed25519', '-N', '', '-f', key])
run('ssh_fingerprint', [root / 'openssh/ssh-keygen.exe', '-lf', key.with_suffix('.pub')], contains='ED25519')
(checks / 'verification.json').write_text(json.dumps(report, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')
print(json.dumps({'passed': len(report), 'report': str(checks / 'verification.json')}, ensure_ascii=False))
