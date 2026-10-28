"""One-time operator transfer of creator grants/requests, not old package formats.

Preview by default. Does not read or write account/session/billing tables.
Existing new-store grants and decisions are preserved on repeated execution.
"""
import argparse
from contextlib import closing
import json
from pathlib import Path
import os
import sqlite3
import time
import uuid

def transfer(source,target,backup,apply=False):
 source,target,backup=Path(source),Path(target),Path(backup)
 if not source.is_file() or not target.is_file() or source.resolve()==target.resolve():raise ValueError('Choose separate existing old/new extension catalogues')
 with closing(sqlite3.connect(source.resolve().as_uri()+'?mode=ro',uri=True)) as old:
  profiles=old.execute('SELECT owner,creator,work_limit,revision FROM creator_profile').fetchall()
  requests=old.execute('SELECT id,owner,type,requested,reason,state,decision,revision,created FROM creator_request').fetchall()
 for owner,creator,limit,revision in profiles:
  if owner<=0 or creator not in (0,1) or not 0<=limit<=10000 or revision<1:raise ValueError('Old creator profile needs operator review')
 converted=[]
 for ident,owner,kind,requested,reason,state,decision,revision,created in requests:
  kind={'expand':'capacity','creator':'creator','capacity':'capacity'}.get(kind)
  if not kind or owner<=0 or requested<0 or requested>10000 or state not in ('pending','approved','rejected') or revision<1:raise ValueError('Old creator request needs operator review')
  xid=uuid.uuid5(uuid.NAMESPACE_URL,'mdo-directory-cutover/creator-request/'+str(ident)).hex
  converted.append((xid,owner,kind,requested,reason,state,decision,revision,created))
 with closing(sqlite3.connect(target,timeout=15)) as new:
  new.execute('SELECT owner FROM profile LIMIT 0');new.execute('SELECT xid FROM request LIMIT 0')
  # A new pending request takes precedence; never silently discard either one.
  for row in converted:
   if row[5]=='pending':
    pending=new.execute("SELECT xid FROM request WHERE owner=? AND type=? AND state='pending'",row[1:3]).fetchone()
    if pending and pending[0]!=row[0]:raise ValueError('A different new-store request is pending; resolve it before transferring this member')
  summary={'profiles':len(profiles),'requests':len(converted),'applied':apply}
  if not apply:return summary
  if backup.exists():raise ValueError('Use a fresh private backup')
  backup.parent.mkdir(parents=True,exist_ok=True)
  with closing(sqlite3.connect(backup)) as destination:new.backup(destination)
  os.chmod(backup,0o600)
  with new:
   new.execute('BEGIN IMMEDIATE')
   new.executemany('INSERT INTO profile(owner,creator,work_limit,revision) VALUES(?,?,?,?) ON CONFLICT(owner) DO NOTHING',profiles)
   new.executemany('INSERT INTO request(xid,owner,type,requested,reason,state,decision,revision,created) VALUES(?,?,?,?,?,?,?,?,?) ON CONFLICT(xid) DO NOTHING',converted)
   new.execute("INSERT INTO audit(actor,work,version,action,reason,created) VALUES(0,'','','creator_profile_transfer',?,?)",(json.dumps({'profiles':len(profiles),'requests':len(converted)}),int(time.time())))
  return summary
def main():
 parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--source',type=Path,required=True);parser.add_argument('--target',type=Path,required=True);parser.add_argument('--backup',type=Path,required=True);parser.add_argument('--apply',action='store_true');args=parser.parse_args();print(json.dumps(transfer(args.source,args.target,args.backup,args.apply)))
if __name__=='__main__':main()
