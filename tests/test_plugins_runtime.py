"""Current directory plugins: native/frontend lifecycle, overlays, exclusivity and deferred removal."""
import argparse
from pathlib import Path
from native_suite import run

def main():
    p=argparse.ArgumentParser(description=__doc__);s=p.add_mutually_exclusive_group(required=True)
    s.add_argument('--host',type=Path);s.add_argument('--packed',type=Path);a=p.parse_args()
    run(['test_plugin_directory_native'],a.host.resolve() if a.host else None,a.packed.resolve() if a.packed else None)
if __name__=='__main__':main()
