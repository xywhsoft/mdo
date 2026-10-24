"""Publication must retain an object while the live service can collect files."""
import importlib.util
from contextlib import closing
from pathlib import Path
import sqlite3
import sys
import tempfile
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]


def main():
    import hashlib
    import json
    with tempfile.TemporaryDirectory(prefix="mdo-publisher-") as folder:
        root = Path(folder)
        incoming = root/"incoming"; incoming.mkdir()
        artifact = incoming/"sqlite.mdo-cli"; artifact.write_bytes(b"validated-native-fixture-placeholder")
        sha = hashlib.sha256(artifact.read_bytes()).hexdigest()
        row = dict(id="sqlite", tool_id="official.sqlite", name="SQLite CLI", version="3.54.1", platform="windows-x86_64", recommended=False,
                   file=artifact.name, sha256=sha, size=artifact.stat().st_size, prompt="SQLite", license="Public Domain", links=[], entry="sqlite/sqlite3.exe")
        (incoming/"catalog.json").write_text(json.dumps(dict(tools=[row])))
        database = root/"catalog.db"
        with closing(sqlite3.connect(database)) as db:
            db.executescript("""
                CREATE TABLE tool(id INTEGER PRIMARY KEY,owner,slug,tool_id,name,version,version_key,platform,prompt,license,links,entry,sha256,size,author,state,reason,revision,created,recommended,withdrawn);
                CREATE TABLE upload(id TEXT PRIMARY KEY,owner,size,sha256,offset,state,created,purpose);
                CREATE TABLE audit(tool,actor,action,reason,sha256,revision,created);
            """)
        spec = importlib.util.spec_from_file_location("publisher", ROOT/"tools/runtime/build/publish_optional_tools.py")
        module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
        replace = module.os.replace; collected = []
        def live_collection(source, destination):
            replace(source, destination)
            destination = Path(destination)
            if destination.parent.name == "objects" and len(destination.name) == 64:
                with closing(sqlite3.connect(database)) as db:
                    retained = db.execute("SELECT 1 FROM tool WHERE sha256=? UNION ALL SELECT 1 FROM upload WHERE sha256=? LIMIT 1", (sha, sha)).fetchone()
                if not retained:
                    collected.append(destination.name); destination.unlink()
        command = ["publish_optional_tools.py", "--incoming", str(incoming), "--database", str(database), "--backup", str(root/"backup.db"), "--apply"]
        with patch.object(sys, "argv", command), patch.object(module.os, "replace", live_collection):
            module.main()
        assert not collected and (root/"objects"/sha).read_bytes() == artifact.read_bytes()
        with closing(sqlite3.connect(database)) as db:
            assert db.execute("SELECT count(*) FROM tool WHERE state='published'").fetchone()[0] == 1
            assert db.execute("SELECT count(*) FROM upload").fetchone()[0] == 0
            assert db.execute("SELECT count(*) FROM audit").fetchone()[0] == 2
        assert (root/"backup.db").is_file()
    print("PASS publication survives live object collection; temporary references removed atomically")


if __name__ == "__main__":
    main()
