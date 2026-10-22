"""Native command metadata, typed arrays, scope and invalid-shadow regression."""
import argparse,json,tempfile
from pathlib import Path
from test_home_runtime import write_site,run_probe,ROOT
from runtime_sources import copy_app_source

def command(id,body="return 0;"):
    return '/* mdo-command\n'+json.dumps({"id":id,"description":"fixture","effects":["read"]})+'\n*/\n'+body

def main():
    p=argparse.ArgumentParser();p.add_argument('--host',type=Path,required=True);args=p.parse_args()
    with tempfile.TemporaryDirectory(prefix='command-catalog-',dir=ROOT/'.build') as raw:
        base=Path(raw);site=base/'site';home=base/'home';workspace=base/'workspace'
        write_site(site);copy_app_source("src/commands/catalog.c",site)
        folder=site/'default-home/commands';folder.mkdir()
        (folder/'echo.c').write_text(command('echo'));(folder/'shadow.c').write_text(command('shadow'))
        (workspace/'.mdo/commands').mkdir(parents=True)
        (workspace/'.mdo/commands/shadow.c').write_text('invalid project shadow')
        (workspace/'.mdo/commands/local.c').write_text(command('local'))
        code=r'''
#include <stdio.h>
#include "src/storage/home.c"
#include "src/commands/catalog.c"
static unsigned failures;
#define CHECK(x) do {if(!(x)){printf("failed_line=%d\n",__LINE__);failures++;}}while(0)
void ServiceInit(XS_HostInfo* H){
 (void)H;CHECK(MdoHomeInit());char Error[512],Revision[65];xvalue *A,*M;char* S=NULL;
 const char* inputs[]={"", "\"\" ''", "123 true", "a\\ b \"a b\" 'c d'", "C:\\foo\\bar", "[1,true,null,{\"a\":[2]}]", "\"unterminated"};
 const size_t counts[]={0,2,2,3,1,4,999};
 for(size_t i=0;i<7;i++){A=MdoCommandArguments(inputs[i],Error);
 CHECK(counts[i]==999?!A:A&&xrtValueCount(A)==counts[i]);xrtValueRelease(A);}
 A=MdoCommandArguments("\"\" C:\\foo\\bar",Error);CHECK(A);
 xstrview T={0};CHECK(xrtValueGetString(xrtValueArrayGet(A,0),&T)&&T.Size==0);
 CHECK(xrtValueGetString(xrtValueArrayGet(A,1),&T)&&!strcmp(T.Data,"C:\\foo\\bar"));xrtValueRelease(A);
 A=MdoCommandArguments("\\\\server\\share\\file",Error);CHECK(A);
 CHECK(xrtValueGetString(xrtValueArrayGet(A,0),&T)&&!strcmp(T.Data,"\\\\server\\share\\file"));xrtValueRelease(A);
 M=MdoCommandMetadata("x","/* mdo-command {\"id\":\"x\",\"description\":\"x\",\"timeout_ms\":30000} */",Error);CHECK(M);xrtValueRelease(M);
 M=MdoCommandMetadata("x","/* mdo-command {\"id\":\"x\",\"description\":\"x\",\"timeout_ms\":0} */",Error);CHECK(!M);
 M=MdoCommandCatalog(WORKSPACE);CHECK(M&&xrtValueCount(M)==3);
 bool Valid=true;CHECK(xrtValueGetBool(xrtValueObjectGet(xrtValueObjectGet(M,XRT_STR_LITERAL("shadow")),XRT_STR_LITERAL("valid")),&Valid)&&!Valid);
 xrtValueRelease(M);M=NULL;
 CHECK(!MdoCommandResolve(WORKSPACE,"shadow",&S,&M,Revision,Error));
 CHECK(MdoCommandResolve(WORKSPACE,"local",&S,&M,Revision,Error));xrtFree(S);xrtValueRelease(M);
 CHECK(MdoCommandResolve(NULL,"echo",&S,&M,Revision,Error));xrtFree(S);xrtValueRelease(M);
 printf("command_failures=%u\nprobe_done=1\n",failures);fflush(stdout);
}
void ServiceUnit(XS_HostInfo* H){(void)H;MdoHomeUnit();}
'''
        (site/'probe.c').write_text(code.replace("WORKSPACE",json.dumps(workspace.as_posix())),encoding='utf-8')
        output=run_probe(args.host.resolve(),site,home)
        assert 'command_failures=0' in output,output
        assert not home.exists(),'Discovery unexpectedly created Home'
    print('Command native metadata/arguments/scoped invalid shadow: PASS')
if __name__=='__main__':main()

