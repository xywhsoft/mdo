"""Recovered legacy contexts retain pinned text without accumulating live MCP blocks."""
import argparse, json, os, shutil, subprocess, tempfile, time
from test_api_runtime import ROOT, free_port

SOURCE = r'''
#define ServiceInit QaBaseServiceInit
#define ServiceUnit QaBaseServiceUnit
#include "generated/mdo_unity.c"
#undef ServiceInit
#undef ServiceUnit
void ServiceInit(XS_HostInfo* Host) {
    (void)Host;
    const char* Block="<mdo_mcp_capabilities>\nThe following enabled MCP services\nfixture\n</mdo_mcp_capabilities>";
    char* Middle=xrtFormat("base\n%s\nkeep",Block);
    char* Repeated=xrtFormat("base\n%s\n%s\nkeep",Block,Block);
    char* Tail=xrtFormat("base\n\n%s",Block);
    const char* Cases[]={"plain",Middle,Repeated,Tail,"<mdo_mcp_capabilities>user text</mdo_mcp_capabilities>","base\n<mdo_mcp_capabilities>\nThe following enabled MCP services"};
    const char* Expected[]={"plain","base\n\nkeep","base\n\n\nkeep","base",Cases[4],Cases[5]};
    bool Good=MdoAgentsLegacyPrompt(NULL)==NULL;
    for(size_t I=0;I<sizeof(Cases)/sizeof(*Cases);I++) {
        char* Clean=MdoAgentsLegacyPrompt(Cases[I]);
        Good=Good&&Clean&&!strcmp(Clean,Expected[I]);
        char* Again=Clean?MdoAgentsLegacyPrompt(Clean):NULL;
        Good=Good&&Again&&!strcmp(Again,Expected[I]);
        xrtFree(Again);xrtFree(Clean);
    }
    xrtFree(Middle);xrtFree(Repeated);xrtFree(Tail);
    printf("legacy_context=%u probe_done=1\n",Good);fflush(stdout);
}
void ServiceUnit(XS_HostInfo* Host){(void)Host;}
'''


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--host',required=True)
    host=parser.parse_args().host
    with tempfile.TemporaryDirectory(dir=ROOT/'.build',prefix='legacy-context-') as raw:
        from pathlib import Path
        site=Path(raw);shutil.copytree(ROOT/'app',site,dirs_exist_ok=True)
        (site/'probe.c').write_text(SOURCE,encoding='utf-8')
        (site/'xs.json').write_text(json.dumps(dict(services=[dict(enabled=True,**{'class':'http'},name='context',ip='127.0.0.1',port=free_port(),host_default=dict(enabled=True,path='web',devlang='c',devfile='probe.c'))])),encoding='utf-8')
        with (site/'host.log').open('wb') as log:
            process=subprocess.Popen([str(Path(host).resolve()),'xs.json'],cwd=site,stdout=log,stderr=log,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
            try:
                deadline=time.monotonic()+20
                while time.monotonic()<deadline:
                    output=(site/'host.log').read_text(encoding='utf-8',errors='replace')
                    if 'probe_done=1' in output or process.poll() is not None:break
                    time.sleep(.05)
                assert 'legacy_context=1 probe_done=1' in output,output
            finally:
                process.terminate()
                try:process.wait(timeout=10)
                except subprocess.TimeoutExpired:process.kill();process.wait()
    print('PASS native legacy context: middle/tail/repeated blocks, pinned text, unrelated/incomplete markup and repeated recovery')


if __name__=='__main__':main()
