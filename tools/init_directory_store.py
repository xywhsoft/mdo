"""Initialize only a fresh directory-store DB from its authoritative C schema.

Operator preparation before cutover; never migrates an old database or touches
identity, sessions or billing. Preview is the default.
"""
import argparse,json,os,re,sqlite3
from contextlib import closing
from pathlib import Path
def schema(source):
 text=Path(source).read_text(encoding='utf-8');block=text.split('static bool MdStoreInit',1)[1].split('sqlite3_exec(S->Db,',1)[1].split(',NULL,NULL,NULL)',1)[0]
 sql=''.join(json.loads(token) for token in re.findall(r'"(?:\\.|[^"\\])*"',block))
 if not all(('CREATE TABLE IF NOT EXISTS '+name+'(') in sql for name in ('profile','work','version','published','upload','request','audit')):raise ValueError('Unexpected C store schema')
 return sql
def initialize(source,database,apply=False):
 database=Path(database);sql=schema(source)
 if database.name!='catalog.db' or database.parent.name!='extensions':raise ValueError('Choose the new extensions/catalog.db, never an existing ecosystem database')
 if database.exists():raise ValueError('Database already exists; inspect it instead of reinitializing')
 result=dict(database=str(database),sql_bytes=len(sql.encode()),applied=apply)
 if not apply:return result
 database.parent.mkdir(mode=0o700,parents=True,exist_ok=True)
 for name in ('objects','uploads'):(database.parent/name).mkdir(mode=0o700,exist_ok=True)
 with closing(sqlite3.connect(database)) as db:
  db.executescript(sql)
  if db.execute('PRAGMA integrity_check').fetchone()!=('ok',):raise ValueError('New store integrity check failed')
 os.chmod(database,0o600);return result
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--source',type=Path,required=True);p.add_argument('--database',type=Path,required=True);p.add_argument('--apply',action='store_true');a=p.parse_args();print(json.dumps(initialize(a.source,a.database,a.apply)))
if __name__=='__main__':main()
