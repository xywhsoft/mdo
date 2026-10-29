"""Verify directory ecosystems sequentially using one existing native host.

No copied runtime, extra service, production account, or model call is created.
Small owned file/ZIP fixtures are removed by each case.
"""
import argparse
from datetime import datetime,timezone
import json
from pathlib import Path
import subprocess
import sys
import time
ROOT=Path(__file__).resolve().parents[1]
CASES=("test_extension_zip_native.py",
 "test_extension_directory_native.py","test_extension_builtins_native.py",
 "test_command_directory_native.py","test_module_directory_native.py",
 "test_reference_directory_native.py","test_mcp_directory_native.py","test_mcp_runtime_data_native.py",
 "test_plugin_directory_native.py","test_extension_tasks_native.py",
 "test_extension_save_native.py","test_extension_authoring_native.py",
 "test_extension_directory_api_native.py","test_extension_service_native.py",
 "test_platform_directory_store_native.py","test_platform_directory_routes_native.py",
 "test_platform_directory_startup_native.py","test_project_extension_selection_native.py",
 "test_external_directory_native.py","test_android_directory_native.py")
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument("--url",required=True);p.add_argument("--home",type=Path,required=True);p.add_argument("--project",default="default");p.add_argument("--output",type=Path,default=ROOT/".build/directory-final-gate");a=p.parse_args()
 a.output.mkdir(parents=True,exist_ok=True);report=dict(started_at=datetime.now(timezone.utc).isoformat(),passed=False,scope="Existing host; current product compiled per case; no model/extra runtime",cases=[])
 for name in CASES:
  source=ROOT/"tests"/name
  if not source.is_file():raise ValueError("Missing required regression: "+name)
  start=time.monotonic();log=a.output/(name+".log")
  with log.open("wb") as stream:result=subprocess.run([sys.executable,str(source),"--url",a.url,"--home",str(a.home),"--project",a.project],cwd=ROOT,stdout=stream,stderr=subprocess.STDOUT)
  report["cases"].append(dict(name=name,passed=result.returncode==0,seconds=round(time.monotonic()-start,3),log=log.name));(a.output/"results.json").write_text(json.dumps(report,indent=2),encoding="utf-8");print(("PASS " if not result.returncode else "FAIL ")+name,flush=True)
  if result.returncode:raise SystemExit(result.returncode)
 report.update(passed=True,finished_at=datetime.now(timezone.utc).isoformat());(a.output/"results.json").write_text(json.dumps(report,indent=2),encoding="utf-8")
if __name__=="__main__":main()
