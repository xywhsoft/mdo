"""Exercise actual group-plugin validation against files in an existing Home.

Only an owned data fixture is created; no application or test Home is copied.
"""
import argparse
import json
from pathlib import Path
import shutil
import uuid
from native_product_runner import product

ROOT=Path(__file__).resolve().parents[1]


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--url',required=True);p.add_argument('--home',type=Path,required=True)
    p.add_argument('--project',default='default');p.add_argument('--baseline',action='store_true')
    p.add_argument('--data-home',type=Path,help='Existing Home for owned data fixtures; defaults to the running host Home');a=p.parse_args()
    out=ROOT/'.build/plugin-group-prompts-20261112';fixture=out/'contract';fixture.mkdir(parents=True,exist_ok=True)
    home=(a.data_home or a.home).resolve();owned=home/'data'/('prompt-group-qa-'+uuid.uuid4().hex[:12]);owned.mkdir()
    cases=[]
    def add(name,recipe,files,expected):
        target=owned/name;target.mkdir()
        (target/'plugin.json').write_text(json.dumps(recipe),encoding='utf-8')
        for path,data in files.items():
            f=target/path;f.parent.mkdir(parents=True,exist_ok=True)
            if data is None:f.mkdir()
            else:f.write_bytes(data)
        cases.append(dict(name=name,root=target.relative_to(home).as_posix(),expected=expected))
    for size,expected in [(0,False),(1,True),(2048,True),(2049,False)]:
        add('file-'+str(size),dict(prompts=[dict(id='guide',file='guide.md',activation='explicit')]),{'guide.md':b'x'*size},expected)
    add('inline',dict(prompts=[dict(id='guide',text='Bounded instructions',activation='explicit')]),{},True)
    add('missing',dict(prompts=[dict(id='guide',file='absent.md',activation='explicit')]),{},False)
    add('directory',dict(prompts=[dict(id='guide',file='guide.md',activation='explicit')]),{'guide.md':None},False)
    for size,expected in [(2048,True),(2049,False)]:
        add('total-'+str(2048+size),dict(prompts=[dict(id='a',file='a.md',activation='explicit'),dict(id='b',file='b.md',activation='explicit')]),{'a.md':b'a'*2048,'b.md':b'b'*size},expected)
    # A valid individually-sized third contribution must still fail aggregate limits.
    add('total-over-limit',dict(prompts=[dict(id=k,file=k+'.md',activation='explicit') for k in ('a','b','c')]),{'a.md':b'a'*2048,'b.md':b'b'*2048,'c.md':b'c'},False)
    contract=r'''
xvalue* QaProductContract(const xvalue* P,xvfs V){
 xvalue* Rows=xrtValueArray();char Error[512]={0};bool Home=QaProductHome(QA_HOME,SdkField(P,"app"),V);
 xvalue* Cases=SdkParse(QA_CASES),*Meta=SdkParse("{\"id\":\"plugin:qa.prompts\",\"entry\":\"plugin.json\",\"alias\":\"qa-prompts\"}");
 for(size_t I=0;I<xrtValueCount(Cases);I++){
  const xvalue* C=xrtValueArrayGet(Cases,I);xvalue* Plugins=xrtValueArray();Error[0]=0;
  bool Accepted=Home&&MdoExtGroupPluginRow(Plugins,Meta,SdkField(C,"root"),Error);
  xvalue* Row=xrtValueClone(C);xrtValueObjectSetNew(Row,XRT_STR_LITERAL("accepted"),xrtValueBool(Accepted));SdkPut(Row,"error",Error);xrtValueArrayAppend(Rows,Row);xrtValueRelease(Row);xrtValueRelease(Plugins);
 }
 xrtValueRelease(Cases);xrtValueRelease(Meta);if(Home)MdoHomeUnit();return Rows;
}
'''.replace('QA_HOME',json.dumps(home.as_posix())).replace('QA_CASES',json.dumps(json.dumps(cases)))
    try:
        result=product(a,fixture,contract,out/('baseline.json' if a.baseline else 'result.json'))
        assert isinstance(result,list),result
        failures=[r for r in result if r['accepted']!=r['expected']]
        print(json.dumps(dict(cases=len(result),mismatches=failures)))
        if not a.baseline:assert not failures,failures
    finally:
        assert owned.resolve().parent==home/'data' and owned.name.startswith('prompt-group-qa-')
        shutil.rmtree(owned)


if __name__=='__main__':main()
