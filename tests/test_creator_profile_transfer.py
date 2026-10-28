import sqlite3
from contextlib import closing
from pathlib import Path
import sys
import tempfile
import unittest
from platform_directory_schema import schema
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tools'))
from transfer_creator_profiles import transfer

class Transfer(unittest.TestCase):
 def test_only_creator_grants_and_requests_move_and_new_changes_survive_repetition(self):
  with tempfile.TemporaryDirectory(prefix='creator-transfer-',dir=ROOT/'.build') as raw:
   root=Path(raw);old=root/'old.db';new=root/'new.db'
   with closing(sqlite3.connect(old)) as db:
    db.executescript("CREATE TABLE creator_profile(owner INTEGER,creator INTEGER,work_limit INTEGER,revision INTEGER);INSERT INTO creator_profile VALUES(7,1,40,4);CREATE TABLE creator_request(id INTEGER,owner INTEGER,type TEXT,requested INTEGER,reason TEXT,state TEXT,decision TEXT,revision INTEGER,created INTEGER);INSERT INTO creator_request VALUES(1,7,'expand',50,'Author asks capacity','pending','',1,123);CREATE TABLE member(id INTEGER,private TEXT);INSERT INTO member VALUES(7,'do not copy');")
   with closing(sqlite3.connect(new)) as db:db.executescript(schema())
   self.assertEqual(transfer(old,new,root/'unused.db')['profiles'],1);self.assertFalse((root/'unused.db').exists());transfer(old,new,root/'backup.db',True)
   with closing(sqlite3.connect(new)) as db:
    self.assertEqual(db.execute('SELECT creator,work_limit,revision FROM profile WHERE owner=7').fetchone(),(1,40,4));self.assertEqual(db.execute('SELECT type,requested,state FROM request').fetchone(),('capacity',50,'pending'));self.assertIsNone(db.execute("SELECT 1 FROM sqlite_master WHERE name='member'").fetchone());db.execute('UPDATE profile SET work_limit=60,revision=5 WHERE owner=7');db.execute("UPDATE request SET state='approved',revision=2");db.commit()
   transfer(old,new,root/'again.db',True)
   with closing(sqlite3.connect(new)) as db:
    self.assertEqual(db.execute('SELECT work_limit,revision FROM profile WHERE owner=7').fetchone(),(60,5));self.assertEqual(db.execute('SELECT state,revision FROM request').fetchone(),('approved',2));self.assertEqual(db.execute('SELECT count(*) FROM request').fetchone()[0],1)
if __name__=='__main__':unittest.main()
