"""Real Python MCP imports/cache and writable state do not change its ZIP tree."""
import argparse
import json
from pathlib import Path
import shutil
import sys
import uuid
from mcp_template_fixture import template
from native_product_runner import product

ROOT=Path(__file__).resolve().parents[1]
SERVER='''import helper, json, os, pathlib, sys
root=pathlib.Path(os.environ['MDO_EXTENSION_ROOT'])
data=pathlib.Path(os.environ['MDO_EXTENSION_DATA'])
cache=pathlib.Path(sys.pycache_prefix or '')
cached=pathlib.Path(helper.__cached__)
ok=pathlib.Path.cwd()==data and (root/'helper.py').is_file() and cache.is_relative_to(data) and cached.is_relative_to(cache)
pathlib.Path('state.txt').write_text(json.dumps({'cache':str(cache),'cached':str(cached)}),encoding='utf-8')
for line in sys.stdin:
    msg=json.loads(line)
    if 'id' not in msg: continue
    method=msg.get('method')
    if method=='initialize': result={'protocolVersion':'2025-11-25','capabilities':{'tools':{},'resources':{}},'serverInfo':{'name':'python-cache-fixture','version':'1'}}
    elif method=='tools/list': result={'tools':[]}
    elif method=='resources/read': result={'contents':[{'uri':'fixture://state','mimeType':'text/plain','text':'runtime-data-ok' if ok else 'wrong-cwd'}]}
    else: result={}
    print(json.dumps({'jsonrpc':'2.0','id':msg['id'],'result':result}),flush=True)
'''
CONTRACT=r'''
xvalue* QaProductContract(const xvalue* P,xvfs V){
 xvalue* R=xrtValueObject(),*Out=NULL;char Error[512]={0},Digest[65],After[65];
 bool Home=QaProductHome(SdkField(P,"root"),SdkField(P,"app"),V);
 bool Ok=Home&&MdoExtensionsInit()&&MdoExtensionsApprovalDigest("mcp:qa.python-runtime",Digest,NULL,Error);
 xvalue* Input=SdkParse("{\"id\":\"qa-runtime\",\"extension_id\":\"mcp:qa.python-runtime\",\"variant\":\"default\",\"values\":{},\"enabled\":true,\"expected_revision\":\"\",\"action\":\"preview\"}");
 Ok=Ok&&MdoMcpConnectionManage(Input,NULL,&Out,Error);xrtValueRelease(Out);Out=NULL;
 bool Exists=true;xfileinfo Info;Ok=Ok&&MdoHomeExternalStat("data/mcp/runtime/qa-runtime",&Exists,&Info)&&!Exists;
 xrtValueObjectSetNew(R,XRT_STR_LITERAL("preview_stays_cold"),xrtValueBool(Ok));
 Ok=Ok&&SdkPut(Input,"action","save")&&SdkPut(Input,"approval_digest",Digest)&&MdoMcpConnectionManage(Input,NULL,&Out,Error);xrtValueRelease(Out);xrtValueRelease(Input);
 xwork_runtime_config C;xworkRuntimeConfigInit(&C);xwork_error E;xworkErrorInit(&E);xwork_runtime* Runtime=Ok?xworkRuntimeCreate(&C,&E):NULL;
 Ok=Ok&&Runtime&&MdoMcpManagerInit(Runtime);char* Text=NULL;
 bool First=Ok&&MdoMcpRead("qa-runtime","resources/read","{\"uri\":\"fixture://state\"}",NULL,MdoDeadlineAfterUs(10000000),&Text,&E)&&Text&&strstr(Text,"runtime-data-ok");xrtFree(Text);Text=NULL;
 xrtValueObjectSetNew(R,XRT_STR_LITERAL("real_python_mcp_called"),xrtValueBool(First));
 bool Reload=First&&MdoExtensionsApprovalDigest("mcp:qa.python-runtime",After,NULL,Error)&&!strcmp(Digest,After)&&MdoMcpManagerReload();
 MdoMcpCatalog* Catalog=MdoMcpCatalogSnapshot();Reload=Reload&&Catalog&&Catalog->Count==1;MdoMcpCatalogRelease(Catalog);
 Reload=Reload&&MdoMcpRead("qa-runtime","resources/read","{\"uri\":\"fixture://state\"}",NULL,MdoDeadlineAfterUs(10000000),&Text,&E)&&Text&&strstr(Text,"runtime-data-ok");xrtFree(Text);
 xrtValueObjectSetNew(R,XRT_STR_LITERAL("reload_keeps_approval"),xrtValueBool(Reload));
 SdkPut(R,"error",Error[0]?Error:E.sMessage);MdoMcpManagerUnit();if(Runtime)xworkRuntimeRelease(Runtime);MdoExtensionsUnit();if(Home)MdoHomeUnit();return R;
}
'''

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--url',required=True);p.add_argument('--home',type=Path,required=True);p.add_argument('--project',default='default');a=p.parse_args()
    out=ROOT/'.build/mcp-runtime-data-native';out.mkdir(exist_ok=True,parents=True)
    fixture=out/('home-'+uuid.uuid4().hex[:12]);directory=fixture/'mcp/python-runtime';directory.mkdir(parents=True)
    (directory/'extension.json').write_text(json.dumps(dict(kind='mcp',id='mcp:qa.python-runtime',name='Python runtime data',alias='python-runtime',entry='mcp.json',platforms=['any'])),encoding='utf-8')
    (directory/'helper.py').write_text('value = 7\n',encoding='utf-8');(directory/'server.py').write_text(SERVER,encoding='utf-8')
    recipe=template('python-runtime','stdio');recipe['inputs']=[dict(id='interpreter',type='text',label='Python',description='Existing test interpreter',required=True,secret=False,default=sys.executable)]
    recipe['variants'][0]['config']['transport']=dict(type='stdio',program={'input':'interpreter'},arguments=['{$path}/server.py'],inherit_environment=False,environment=[])
    (directory/'mcp.json').write_text(json.dumps(recipe),encoding='utf-8')
    try:
        result=product(a,fixture,CONTRACT,out/'result.json')
        assert all(result.get(k) for k in ('preview_stays_cold','real_python_mcp_called','reload_keeps_approval')),result
        assert not list(directory.rglob('*.pyc')) and not (directory/'state.txt').exists()
        runtime=fixture/'data/mcp/runtime/qa-runtime'
        state=json.loads((runtime/'state.txt').read_text(encoding='utf-8'))
        # Python may skip bytecode writes on a read-only/long-path filesystem;
        # its actual cache destination must still be outside the published tree.
        assert Path(state['cache']).is_relative_to(runtime)
        assert Path(state['cached']).is_relative_to(Path(state['cache']))
        print(json.dumps(result))
    finally:
        if fixture.parent==out and fixture.name.startswith('home-'):shutil.rmtree(fixture)

if __name__=='__main__':main()
