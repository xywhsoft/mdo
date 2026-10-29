"""Path of the single explicitly locked SDK export, without duplicating it."""
import json
import os
from pathlib import Path
import sys
ROOT=Path(__file__).resolve().parents[1]
def sdk_root():
    lock=json.loads((ROOT/'deps.lock').read_text(encoding='utf-8'))
    explicit=os.environ.get('MDO_TEST_SDK')
    if explicit:
        sys.path.insert(0,str(ROOT/'tools'))
        import build_mdo
        source=Path(explicit).resolve()
        build_mdo.verify_dependencies(source,lock)
        return source
    source=ROOT/'.build/sdk-overlays'/lock['sdk_overlay']['sha256']
    if not source.is_dir():
        raise RuntimeError('Locked SDK export missing; set MDO_TEST_SDK to an existing verified SDK')
    return source
