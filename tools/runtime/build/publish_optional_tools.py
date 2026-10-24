#!/usr/bin/env python3
"""Publish validated official CLI artifacts as an operator, without account tokens.

Run on the server with immutable artifacts in --incoming. The default is a
read-only preview. --apply backs up SQLite and inserts only absent versions;
existing versions, recommendations and user-created works are never changed.
"""
import argparse
import hashlib
import json
from pathlib import Path
import os
import shutil
import sqlite3
import time

ALLOWED = {"git", "sqlite", "poppler", "ffmpeg", "imagemagick"}
PLATFORMS = {"windows-x86_64", "linux-x86_64", "android-arm64-v8a"}


def digest(path):
    sha = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024*1024), b""):
            sha.update(chunk)
    return sha.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--incoming", type=Path, required=True)
    parser.add_argument("--database", type=Path, required=True)
    parser.add_argument("--backup", type=Path, required=True)
    parser.add_argument("--apply", action="store_true")
    args = parser.parse_args()
    rows = json.loads((args.incoming/"catalog.json").read_text(encoding="utf-8"))["tools"]
    assert args.database.is_file() and rows
    identities = set()
    for row in rows:
        identity = (row["id"], row["version"], row["platform"])
        assert row["id"] in ALLOWED and row["platform"] in PLATFORMS and identity not in identities
        identities.add(identity)
        assert row["tool_id"] == "official."+row["id"] and row["recommended"] is False
        assert Path(row["file"]).name == row["file"]
        source = args.incoming/row["file"]
        assert source.stat().st_size == row["size"] and digest(source) == row["sha256"]
        assert len(row["sha256"]) == 64 and len(row["version"].split(".")) == 3
    db = sqlite3.connect(args.database, timeout=15)
    try:
        assert db.execute("PRAGMA integrity_check").fetchone()[0] == "ok"
        for row in rows:
            current = db.execute("SELECT id,sha256,size,state,withdrawn FROM tool WHERE owner=0 AND slug=? AND version=? AND platform=?", (row["id"],row["version"],row["platform"])).fetchone()
            if current:
                assert current[1:] == (row["sha256"], row["size"], "published", 0), (row["id"], "immutable version conflict")
            print("EXISTS" if current else "INSERT", row["id"], row["version"], row["platform"], row["size"], flush=True)
        if not args.apply:
            return
        args.backup.parent.mkdir(parents=True, exist_ok=True)
        assert not args.backup.exists(), "Use a fresh backup path"
        with sqlite3.connect(args.backup) as backup:
            db.backup(backup)
        os.chmod(args.backup, 0o600)
        objects = args.database.parent/"objects"
        objects.mkdir(exist_ok=True)
        for row in rows:
            destination = objects/row["sha256"]
            if destination.exists():
                assert destination.stat().st_size == row["size"] and digest(destination) == row["sha256"]
            else:
                temporary = objects/(row["sha256"]+".operator-pending")
                with (args.incoming/row["file"]).open("rb") as source, temporary.open("xb") as target:
                    shutil.copyfileobj(source, target, 1024*1024)
                    target.flush(); os.fsync(target.fileno())
                assert digest(temporary) == row["sha256"]
                os.replace(temporary, destination)
        now = int(time.time())
        reason = "Official optional tools: pinned upstream provenance, native functional fixtures and immutable archive import/review validation. Operator publication."
        receipt = []
        with db:
            db.execute("BEGIN IMMEDIATE")
            for row in rows:
                current = db.execute("SELECT id,sha256,size,state,withdrawn FROM tool WHERE owner=0 AND slug=? AND version=? AND platform=?", (row["id"],row["version"],row["platform"])).fetchone()
                if current:
                    assert current[1:] == (row["sha256"], row["size"], "published", 0)
                    receipt.append(dict(id=current[0], slug=row["id"], platform=row["platform"], sha256=row["sha256"], inserted=False))
                    continue
                key = ".".join(f"{int(n):010d}" for n in row["version"].split("."))
                cursor = db.execute("INSERT INTO tool(owner,slug,tool_id,name,version,version_key,platform,prompt,license,links,entry,sha256,size,author,state,reason,revision,created,recommended,withdrawn) VALUES(0,?,?,?,?,?,?,?,?,?,?,?,?,'墨斗','published',?,2,?,0,0)",
                    (row["id"],row["tool_id"],row["name"],row["version"],key,row["platform"],row["prompt"],row["license"],json.dumps(row["links"],ensure_ascii=False),row["entry"],row["sha256"],row["size"],reason,now))
                id = cursor.lastrowid
                for action, revision in (("submit",1),("approve",2)):
                    db.execute("INSERT INTO audit(tool,actor,action,reason,sha256,revision,created) VALUES(?,0,?,?,?,?,?)", (id,action,reason,row["sha256"],revision,now))
                receipt.append(dict(id=id,slug=row["id"],platform=row["platform"],sha256=row["sha256"],inserted=True))
        (args.incoming/"publication-receipt.json").write_text(json.dumps(receipt,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
        print("PUBLISHED", len(receipt), flush=True)
    finally:
        db.close()


if __name__ == "__main__":
    main()
