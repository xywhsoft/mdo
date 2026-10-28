"""Use the actual C store's SQLite schema in offline operator tests."""
from pathlib import Path
import sys
ROOT=Path(__file__).resolve().parents[1]
if str(ROOT/'tools') not in sys.path:sys.path.insert(0,str(ROOT/'tools'))
from init_directory_store import schema as read_schema
def schema():
 return read_schema(ROOT.parent/'home/host/xywhsoft_ai/plugin/mdo/modules/ecosystem/directory_store.inc.c')
