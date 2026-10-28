"""Operator ZIP publication retains references across object collection races."""
import importlib.util
from contextlib import closing
from pathlib import Path
import json
import sqlite3
import sys
import tempfile
from unittest.mock import patch
from platform_directory_schema import schema
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from extension_archives import package_tool


def main():
    with tempfile.TemporaryDirectory(prefix='zip-publisher-',dir=ROOT/'.build') as folder:
        root=Path(folder);incoming=root/'incoming';stage=root/'stage';stage.mkdir();(stage/'program').write_bytes(b'Native fixture, not executed')
        rows=[]
        for platform in ('windows-x86_64','linux-x86_64','android-arm64-v8a'):
            rows.append(package_tool(stage,dict(id='sqlite',name='SQLite CLI',version='1.0.0',platform=platform,entry='program',prompt='Model usage',description='User description',license='Public Domain',links=['https://sqlite.org/'],commands=[dict(name='sqlite3',entry='program',args=[])]),incoming))
        (incoming/'catalog.json').write_text(json.dumps({'tools':rows}),encoding='utf-8');database=root/'catalog.db'
        with closing(sqlite3.connect(database)) as db:db.executescript(schema())
        spec=importlib.util.spec_from_file_location('zip_publisher',ROOT/'tools/runtime/build/publish_optional_tools.py');module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
        replace=module.os.replace;collected=[]
        def live_collection(source,destination):
            replace(source,destination);destination=Path(destination)
            if destination.parent.name=='objects' and len(destination.name)==64:
                with closing(sqlite3.connect(database)) as db:
                    retained=db.execute("SELECT 1 FROM version WHERE sha256=? AND deleted=0 UNION ALL SELECT 1 FROM upload WHERE sha256=? AND state='sealed' LIMIT 1",(destination.name,destination.name)).fetchone()
                if not retained:collected.append(destination.name);destination.unlink()
        assert len(module.publish(incoming,database,root/'dry.db',False))==3
        assert not (root/'dry.db').exists()
        with patch.object(module.os,'replace',live_collection):receipts=module.publish(incoming,database,root/'backup.db',True)
        assert not collected and len({r['work'] for r in receipts})==1
        for row in rows:assert (root/'objects'/row['sha256']).read_bytes()==(incoming/row['file']).read_bytes()
        with closing(sqlite3.connect(database)) as db:
            assert db.execute('SELECT count(*) FROM work').fetchone()[0]==1
            assert db.execute("SELECT count(*) FROM version WHERE state='published'").fetchone()[0]==3
            assert db.execute('SELECT count(*) FROM published').fetchone()[0]==3
            assert db.execute("SELECT count(*) FROM upload WHERE state='sealed'").fetchone()[0]==0
            assert db.execute('SELECT count(*) FROM audit').fetchone()[0]==3
        again=module.publish(incoming,database,root/'second-backup.db',True)
        assert all(not receipt['inserted'] for receipt in again)
        assert (root/'backup.db').is_file()
    print('PASS ZIP publication survives collection, groups all platforms under one XID and retries without duplicates')
if __name__=='__main__':main()
