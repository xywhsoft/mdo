"""Current store/ZIP lifecycle replaces retired core/Python toolpack contracts.
Range/retry transport is covered separately by test_native_downloads_runtime.
These local contracts cover tasks, cancellation, atomic installation,
ownership, updating and dependency-store behavior.
"""
import argparse
from pathlib import Path
from native_suite import run

def main():
    p=argparse.ArgumentParser(description=__doc__);s=p.add_mutually_exclusive_group(required=True)
    s.add_argument('--host',type=Path);s.add_argument('--packed',type=Path);a=p.parse_args()
    run(['test_extension_service_native','test_extension_slow_download_native','test_extension_updates_native',
         'test_extension_dependency_store_native','test_extension_dependency_service_native'],
        a.host.resolve() if a.host else None,a.packed.resolve() if a.packed else None)
    print('PASS current ZIP distribution contracts; no retired toolpack payloads')

if __name__=='__main__':main()
