"""Packed product boots, exposes nine typed catalogues and applies dependency plans over HTTP."""
import argparse,json,os,tempfile,shutil,subprocess,sys
from pathlib import Path
from test_packed_home_lease import ROOT,site,start,stop,request,wait_bootstrap,release_packed_copies


def fixture(home):
 for name,deps in [('xrt',[]),('xs',['knowledge:qa.xrt']),('mdo',['knowledge:qa.xs','knowledge:qa.xrt'])]:
  root=home/'knowledge'/name;root.mkdir(parents=True)
  (root/'extension.json').write_text(json.dumps(dict(kind='knowledge',id='knowledge:qa.'+name,name=name,alias=name,entry='knowledge.json',platforms=['any'],dependencies=[dict(id=dep,reason='Required API reference') for dep in deps])),encoding='utf-8')
  (root/'knowledge.json').write_text(json.dumps(dict(format='mdo.knowledge.v1',index='index.md',prompt='Use {$path}/index.md',inject_prompt=True)),encoding='utf-8');(root/'index.md').write_text('# '+name+'\nReference: api.md\n',encoding='utf-8');(root/'api.md').write_text('QA reference.\n',encoding='utf-8')


def check(packed,report=None):
 evidence={};prefixes=dict(agents='agent',subagents='subagent',tools='tool',skills='skill',mcp='mcp',commands='command',plugins='plugin',knowledge='knowledge',**{'external-tools':'external-tool'})
 scratch=ROOT/'.build' if os.name=='nt' else Path(tempfile.gettempdir());raw=tempfile.mkdtemp(prefix='dependency-packed-',dir=scratch);passed=False
 try:
  base=Path(raw);home=base/'home';fixture(home);directory,port=site(base,'site',packed);env={**os.environ,'USE_WEBVIEW':'0','MDO_PLUGINS_DISABLED':'1'};process=start(directory,packed,home,env)
  try:
   status,boot=wait_bootstrap(process,port,directory/'packed.log');assert status==200 and boot['data']['ready'],boot;evidence['fresh_home_ready']=True
   for kind,prefix in prefixes.items():
    status,envelope=request(port,'GET','/api/v1/extensions/'+kind);assert status==200,envelope
    for row in envelope['data']['items']:assert row['id'].startswith(prefix+':') and row['directory_id'] and row['id']!=row['directory_id'],row
   evidence['nine_catalogue_identity_contracts']=True
   def read(ref):
    status,data=request(port,'GET','/api/v1/extensions/knowledge/'+ref);assert status==200,data;return data['data']
   mdo=read('knowledge:qa.mdo');assert len(mdo['dependency_details'])==2 and all(row['available'] for row in mdo['dependency_details']),mdo;assert len(read('knowledge:qa.xrt')['dependents'])==2;evidence['direct_and_reverse_dependency_details']=True
   def plan(op,ref):
    status,data=request(port,'POST','/api/v1/ecosystem',dict(action='dependency_plan',operation=op,ref=ref,cascade=True));assert status==200,data;return data['data']
   def apply(plan):
    status,data=request(port,'POST','/api/v1/ecosystem',dict(action='apply_plan',plan_id=plan['id'],revision=plan['revision'],trust_code=True));assert status==200,data;return data['data']
   disable=plan('disable','knowledge:qa.xrt');assert disable['ready'] and len(disable['items'])==3;receipt=apply(disable);assert receipt['runtime_ready'];assert all(not read('knowledge:qa.'+name)['desired_enabled'] for name in ('xrt','xs','mdo'));evidence['cascade_disable_commits_three_preferences']=True
   again=apply(disable);assert receipt['id']==again['id'];evidence['same_plan_returns_same_receipt']=True
   enable=plan('enable','knowledge:qa.mdo');assert len(enable['items'])==3;apply(enable);assert all(read('knowledge:qa.'+name)['effective_enabled'] for name in ('xrt','xs','mdo'));evidence['enable_restores_required_closure']=True
   migration=subprocess.run([sys.executable,str(ROOT/'tools/migrate_extension_dependencies.py'),'--home',str(home),'--apply','--expected-plan','unreviewed'],capture_output=True,text=True,timeout=10);assert migration.returncode!=0 and 'Close mdo' in migration.stderr,migration.stderr;evidence['offline_migration_refuses_real_active_home']=True
   before=read('knowledge:qa.mdo');bad={**before['manifest'],'dependencies':[dict(id='tool:qa.forbidden')]};status,result=request(port,'PUT','/api/v1/extensions/knowledge/mdo',dict(manifest=bad,edits=[],expected_revision=before['revision'],trust_code=True));assert status>=400,result;assert read('knowledge:qa.mdo')['revision']==before['revision'] and read('knowledge:qa.mdo')['desired_enabled'];evidence['cross_kind_knowledge_edit_is_atomic_and_rejected']=True
   stop(process);process=start(directory,packed,home,env);_,boot=wait_bootstrap(process,port,directory/'packed.log');assert boot['data']['ready'] and read('knowledge:qa.mdo')['effective_enabled'];evidence['restart_recovers_identity_and_activation']=True
  finally:stop(process);release_packed_copies(directory/packed.name)
  passed=True
 finally:
  if passed and base.resolve().parent==scratch.resolve() and base.name.startswith('dependency-packed-'):shutil.rmtree(base)
  elif not passed:print('Failure fixture preserved:',base)
 if report:Path(report).write_text(json.dumps(evidence,indent=2),encoding='utf-8')
 print('PASS',len(evidence),'packed-product dependency contracts');return evidence


def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--packed',type=Path,required=True);p.add_argument('--report',type=Path);a=p.parse_args();check(a.packed.resolve(),a.report)
if __name__=='__main__':main()
