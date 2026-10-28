import json,sys,tempfile,unittest,sqlite3
from contextlib import closing
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tools'))
from init_directory_store import initialize
SOURCE=ROOT.parent/'home/host/xywhsoft_ai/plugin/mdo/modules/ecosystem/directory_store.inc.c'
class Preparation(unittest.TestCase):
 def test_only_explicit_fresh_schema_is_created(self):
  with tempfile.TemporaryDirectory(dir=ROOT/'.build') as temporary:
   path=Path(temporary)/'extensions/catalog.db';row=initialize(SOURCE,path);self.assertFalse(row['applied']);self.assertFalse(path.exists());initialize(SOURCE,path,True)
   with closing(sqlite3.connect(path)) as db:self.assertEqual(db.execute('PRAGMA integrity_check').fetchone(),('ok',));self.assertEqual(db.execute('SELECT COUNT(*) FROM work').fetchone()[0],0)
   before=path.read_bytes()
   with self.assertRaises(ValueError):initialize(SOURCE,path,True)
   self.assertEqual(path.read_bytes(),before)
   with self.assertRaises(ValueError):initialize(SOURCE,path.parent.parent/'ecosystem/catalog.db',True)
if __name__=='__main__':unittest.main()
