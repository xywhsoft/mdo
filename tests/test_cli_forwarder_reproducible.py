"""The real Windows forwarder and its bundled receipt must reproduce across
different product staging directories and executable filenames."""
import argparse
from pathlib import Path
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from build_cli_forwarder import build

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--sdk',type=Path,required=True);p.add_argument('--cc',default='gcc');a=p.parse_args()
    with tempfile.TemporaryDirectory(prefix='forwarder-repro-',dir=ROOT/'.build') as raw:
        root=Path(raw);one=root/'first/forwarder.exe';two=root/'second/other-name.exe'
        first=build(a.sdk,one,a.cc);second=build(a.sdk,two,a.cc)
        assert one.read_bytes()==two.read_bytes(),'Native forwarder differs by output location'
        assert first==second,'Bundled forwarder receipt differs by output location'
        assert build(a.sdk,one,a.cc)==first,'Cache reuse changed the reproducible receipt'
    print('PASS native Windows forwarder bytes, receipt and cache reuse reproduce across output locations')

if __name__=='__main__':main()
