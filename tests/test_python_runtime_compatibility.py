"""Run project imports against the actual prepared portable Python binaries."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools/runtime/build"))
from python_runtime import prepare_windows_python


def probe(source):
    rows = []
    with tempfile.TemporaryDirectory(prefix="python-cli-", dir=ROOT / ".build") as raw:
        base = Path(raw)
        runtime = base / "portable Python 中文"
        shutil.copytree(source, runtime)
        prepare_windows_python(runtime)
        assert not list(runtime.glob("python*._pth"))
        # Verify relocation, without embedding any absolute home in the package.
        moved = base / "relocated 目录"
        runtime.rename(moved)
        exe = moved / "python.exe"
        project = base / "project 中文 空格"
        project.mkdir()
        (project / "sibling_probe.py").write_text("value = 42\n", encoding="utf-8")
        script = project / "main probe.py"
        script.write_text("import sibling_probe,sys;print(sibling_probe.value);print(repr(sys.argv[1:]))\n", encoding="utf-8")
        module = project / "probe_module"
        module.mkdir()
        (module / "__init__.py").write_text("", encoding="utf-8")
        (module / "__main__.py").write_text("import sibling_probe;print(sibling_probe.value)\n", encoding="utf-8")
        extra = base / "extra modules"
        extra.mkdir()
        (extra / "extra_probe.py").write_text("value = 73\n", encoding="utf-8")
        env = os.environ.copy()
        for key in list(env):
            if key.upper().startswith("PYTHON"):
                env.pop(key)

        def run(label, args, *, cwd=base, pythonpath=None, code=0, contains=None, stdin=None):
            local = dict(env)
            if pythonpath is not None:
                local["PYTHONPATH"] = str(pythonpath)
            done = subprocess.run([str(exe), *map(str, args)], cwd=cwd, env=local,
                input=stdin.encode("utf-8") if stdin is not None else None,
                capture_output=True, timeout=15)
            # Windows CPython uses the console locale unless UTF-8 was selected;
            # -I intentionally ignores PYTHONUTF8/PYTHONIOENCODING as well.
            output = (done.stdout + done.stderr).decode("utf-8", errors="replace")
            assert done.returncode == code, (label, done.returncode, output)
            if contains is not None:
                assert contains in output, (label, output)
            rows.append({"case": label, "passed": True})
            print("Python CLI PASS: " + label, flush=True)

        values = ["", "a b", 'a "quote"']
        run("sibling-import-and-argv", [script, *values], contains=repr(values))
        run("command-current-directory", ["-c", "import sibling_probe;print(sibling_probe.value)"], cwd=project, contains="42")
        run("module-current-directory", ["-m", "probe_module"], cwd=project, contains="42")
        run("explicit-PYTHONPATH", ["-c", "import extra_probe;print(extra_probe.value)"], pythonpath=extra, contains="73")
        run("no-site-script", ["-S", script], contains="42")
        run("ignore-env-retains-script-directory", ["-E", script], pythonpath=extra, contains="42")
        run("ignore-env-hides-PYTHONPATH", ["-E", "-c", "import extra_probe"], pythonpath=extra, code=1, contains="ModuleNotFoundError")
        run("isolated-hides-project", ["-I", script], pythonpath=project, code=1, contains="ModuleNotFoundError")
        run("safe-path-hides-current-directory", ["-P", "-c", "import sibling_probe"], cwd=project, code=1, contains="ModuleNotFoundError")
        run("isolated-stdlib-and-native-modules", ["-I", "-c", "import ssl,sqlite3,ctypes,json;print('stdlib-ok')"], contains="stdlib-ok")
        run("stdin-project-import", ["-"], cwd=project, stdin="import sibling_probe;print(sibling_probe.value)\n", contains="42")
        run("script-exit-code", ["-c", "raise SystemExit(23)"], code=23)
    return {"passed": True, "cases": rows}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=ROOT / "tools/runtime/windows-x86_64/python")
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    (ROOT / ".build").mkdir(exist_ok=True)
    report = probe(args.source)
    if args.report:
        args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"Python compatibility PASS: {len(report['cases'])} real-runtime cases")
