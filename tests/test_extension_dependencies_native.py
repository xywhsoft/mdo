"""Execute the shared dependency resolver in the real XS/TCC runtime."""
import argparse
import json
from pathlib import Path
import uuid
from native_extension_runner import run
from test_extension_zip_native import headers

ROOT=Path(__file__).resolve().parents[1]
def main():
    p=argparse.ArgumentParser();p.add_argument('--url',required=True);p.add_argument('--home',type=Path,required=True);p.add_argument('--project',default='default');a=p.parse_args()
    def row(name,*deps,kind='knowledge',**extra):
        return dict(id=kind+':'+name,kind=kind if kind in ('knowledge','mcp') else kind+'s',enabled=True,valid=True,dependencies=[dict(id=d) if isinstance(d,str) else d for d in deps],**extra)
    cases={
      'chain':[row('mdo','knowledge:xs','knowledge:xrt'),row('xs','knowledge:xrt'),row('xrt')],
      'missing':[row('mdo','knowledge:xs')],
      'optional':[row('mdo',dict(id='knowledge:xs',optional=True))],
      'cycle':[row('mdo','knowledge:xs'),row('xs','knowledge:mdo')],
      'disabled':[row('mdo','knowledge:xs'),{**row('xs'),'enabled':False}],
      'duplicate':[row('xrt'),row('xrt')],
      'registered':[row('xrt',registered=True),row('xrt')],
      'conflict':[row('mdo','knowledge:xs'),row('xs',conflicts=[dict(id='knowledge:xrt')]),row('xrt')],
      'platform':[row('mdo','knowledge:xs'),row('xs',platform_supported=False)],
      'source':[row('mdo','knowledge:xs',source='store'),row('xs',source='manual')],
      'optional_blocked':[row('mdo',dict(id='knowledge:xs',optional=True)),row('xs','knowledge:absent')],
      'depth_reversed':list(reversed([row('n'+str(i),*(['knowledge:n'+str(i+1)] if i<70 else [])) for i in range(71)])),
      'depth':[row('n'+str(i),*(['knowledge:n'+str(i+1)] if i<70 else [])) for i in range(71)],
    }
    metadata=dict(id='qa-dependency-'+uuid.uuid4().hex[:12],description='Native dependency contract',effects=['process'])
    source='/* mdo-command '+json.dumps(metadata)+' */\n#include "mdo/command.h"\n#include <xsbase.h>\n'+headers()+r'''
MDO_EXPORT mdo_result mdoCommandRun(const mdo_command_context_v1* C,const xvalue* A,xvalue** R){
 (void)C;*R=xrtValueObject();const xvalue* Cases=xrtValueArrayGet(A,0);char Error[512];bool Ok=true;
 for(size_t I=0;Ok&&I<xrtValueCount(Cases);I++){
  xstrview Key;const xvalue* Rows=xrtValueObjectAt(Cases,I,&Key);MdoExtensionGraph G;
  Ok=MdoGraphInit(&G,Rows,Error)&&MdoGraphEvaluate(&G,Error);
  if(Ok&&(xrtStrEqual(Key,XRT_STR_LITERAL("depth"))||xrtStrEqual(Key,XRT_STR_LITERAL("depth_reversed")))){xvalue* First=xrtValueArray();Ok=First&&xrtValueArrayAppend(First,xrtValueArrayGet(G.Rows,xrtStrEqual(Key,XRT_STR_LITERAL("depth_reversed"))?G.Count-1:0))&&xrtValueObjectSet(*R,Key,First);xrtValueRelease(First);}
  else if(Ok)Ok=xrtValueObjectSet(*R,Key,G.Rows);MdoGraphUnit(&G);
 }
 xvalue* Invalid=xrtJsonParse(XRT_STR_LITERAL("{\"kind\":\"knowledge\",\"id\":\"knowledge:a\",\"name\":\"A\",\"alias\":\"a\",\"entry\":\"knowledge.json\",\"platforms\":[\"any\"],\"dependencies\":[{\"id\":\"tool:foo\"}]}"));
 bool Rejected=Invalid&&!MdoExtensionManifest(Invalid,Error,sizeof(Error));xrtValueRelease(Invalid);
 Ok=Ok&&xrtValueObjectSetNew(*R,XRT_STR_LITERAL("knowledge_cross_type_rejected"),xrtValueBool(Rejected));
 return Ok?MDO_RESULT_OK:MDO_RESULT_ERROR;
}
'''
    out=ROOT/'.build/extension-dependencies/contracts';out.mkdir(parents=True,exist_ok=True)
    result=run(a,metadata['id'],source,cases,out/'graph.json')
    def ready(case):return [r['effective_enabled'] for r in result[case]]
    assert ready('chain')==[True]*3,result['chain']
    assert ready('missing')==[False] and result['missing'][0]['dependency_state']=='dependency_missing'
    assert ready('optional')==[True]
    assert ready('cycle')==[False,False]
    assert ready('disabled')==[False,False]
    assert ready('duplicate')==[False,False]
    assert ready('registered')==[True,False]
    assert ready('conflict')==[False]*3
    assert ready('depth')[0] is False
    assert ready('depth_reversed')==[False]
    assert ready('platform')==[False,False]
    assert ready('source')==[False,True]
    assert ready('optional_blocked')==[True,False] and result['optional_blocked'][0]['dependency_issues'][0]['optional'] is True
    assert result['knowledge_cross_type_rejected']
    print('PASS 14 native dependency graph contracts')
if __name__=='__main__':main()
