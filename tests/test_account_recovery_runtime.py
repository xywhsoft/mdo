"""Release-gate entry for login recovery across transport, account and Agent.

Loopback fixtures only. In particular, a transient renewal 401 must not erase
the account and interrupt a tool-using Agent. Each child cleans its own Home.
"""
import argparse
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', type=Path, default=ROOT / '.build/host/xs.exe')
    args = parser.parse_args()
    cases = [
        ('test_online_model_auth_retry.py', []),
        ('test_online_model_auth_account.py', []),
        ('test_agent_account_recovery.py', []),
        ('test_agent_account_recovery.py', ['--storage-fault']),
        ('test_agent_account_recovery.py', ['--storage-cancel']),
        ('test_agent_account_recovery.py', ['--refresh-status', '401']),
        ('test_agent_account_recovery.py', ['--refresh-status', '410','--refresh-failures','1','--relogin']),
        ('test_agent_account_recovery.py', ['--refresh-status', '401',
             '--refresh-failures', '99', '--exhausted']),
        ('test_agent_account_recovery.py', ['--refresh-status', '401',
             '--refresh-failures', '99', '--exhausted','--retries','2']),
    ]
    for script, options in cases:
        subprocess.run([sys.executable, str(ROOT / 'tests' / script),
                        '--host', str(args.host.resolve()), *options],
                       cwd=ROOT, check=True, timeout=90)
    print('PASS account recovery release gate: bounded retries, reset, cancellation and no tool replay')


if __name__ == '__main__':
    main()
