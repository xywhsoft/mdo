"""Run Computer Use regressions on the current mainline product, without model calls."""
import argparse,json,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
MAIN=ROOT
sys.path.insert(0,str(MAIN/'tests'))
from native_product_runner import product

def main():
 p=argparse.ArgumentParser();p.add_argument('--url',required=True);p.add_argument('--home',type=Path,required=True);p.add_argument('--project',default='default');a=p.parse_args()
 out=ROOT/'.build/computer-use-native';out.mkdir(parents=True,exist_ok=True);results={}
 for name in ('responses','catalog','result-cap','image-cap','discovery'):
  fixture=out/('contract-'+name);fixture.mkdir(exist_ok=True)
  source=(ROOT/('tests/fixtures/computer-use-'+name+'.c')).read_text(encoding='utf-8')
  source=source.replace('void ServiceInit(XS_HostInfo* Host)','static void QaRun(XS_HostInfo* Host)')
  source=source.replace('void ServiceUnit(XS_HostInfo* Host)','static void QaUnit(XS_HostInfo* Host)')
  source=source.replace(' fflush(stdout); exit(0);',' fflush(stdout);').replace('fflush(stdout);exit(0);','fflush(stdout);')
  source=source.replace('"tests/fixtures/computer-use-image-peer.py"','"'+(ROOT/'tests/fixtures/computer-use-image-peer.py').as_posix()+'"')
  source=source.replace('QA_UTF8_PEER',(ROOT/'tests/fixtures/mcp-utf8-peer.py').as_posix())
  source=source.replace('".build/computer-use/large-image-result.png"','"'+(out/'large-image-result.png').as_posix()+'"')
  if name=='responses':
   source=source.replace('static xllm_client* Client;','static xllm_client* Client;static xvalue* QaResult;')
   source=source.replace('printf("schema_wire=%s\\n",Json);','xrtValueObjectSetNew(QaResult,XRT_STR_LITERAL("schema"),xrtJsonParse(xrtStrView(Json)));')
   source=source.replace('printf("wire_%s=%s\\n", Name, Json);','xrtValueObjectSetNew(QaResult,xrtStrView(Name),xrtJsonParse(xrtStrView(Json)));')
   source+='\nxvalue* QaProductContract(const xvalue* P,xvfs V){(void)P;(void)V;QaResult=xrtValueObject();QaRun(NULL);return QaResult;}\n'
  else:source+='\nxvalue* QaProductContract(const xvalue* P,xvfs V){(void)P;(void)V;QaRun(NULL);return xrtValueBool(true);}\n'
  result=product(a,fixture,source,out/('native-'+name+'.json'))
  assert not isinstance(result,dict) or 'compile_error' not in result,result
  if name=='responses':
   expected={'plain':'plain-result','text':'window-tree','mixed':[{'type':'input_text','text':'window-tree'},{'type':'input_image','image_url':'data:image/png;base64,iVBORw=='}],'image':[{'type':'input_image','image_url':'data:image/png;base64,iVBORw=='}],'texts':[{'type':'input_text','text':'window-tree'},{'type':'input_text','text':'second-part'}]}
   assert all(result[k]['input'][0]['output']==v for k,v in expected.items()),result
   tools=result['schema']['tools'];assert tools[0]['strict'] is False and tools[1]['strict'] is True
  else:assert result is True,result
  results[name]=True;print(name,'PASS',flush=True)
 (out/'native-regressions.json').write_text(json.dumps(results,indent=2),encoding='utf-8')
if __name__=='__main__':main()
