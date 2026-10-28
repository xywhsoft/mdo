"""Use the actual C store's SQLite schema in offline operator tests."""
import json
from pathlib import Path
import re
ROOT=Path(__file__).resolve().parents[1]
def schema():
 source=(ROOT.parent/'home/host/xywhsoft_ai/plugin/mdo/modules/ecosystem/directory_store.inc.c').read_text(encoding='utf-8')
 block=source.split('static bool MdStoreInit',1)[1].split('sqlite3_exec(S->Db,',1)[1].split(',NULL,NULL,NULL)',1)[0]
 return ''.join(json.loads(token) for token in re.findall(r'"(?:\\.|[^"\\])*"',block))
