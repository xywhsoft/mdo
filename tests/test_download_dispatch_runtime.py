"""Exercise updater preflight failure through the native download fixture."""
import argparse
from pathlib import Path
import sys
from test_native_downloads_runtime import main as native_downloads

if __name__ == "__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host",type=Path,required=True)
    args=parser.parse_args()
    sys.argv=[__file__,"--host",str(args.host),"--policy-failure-only"]
    native_downloads()
