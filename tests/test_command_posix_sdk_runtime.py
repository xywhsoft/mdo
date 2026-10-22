"""Compile the POSIX worker branch against the actual compact TCC SDK VFS."""
import argparse,shutil,tempfile
from pathlib import Path
from test_home_runtime import ROOT,write_site,run_probe

def main():
    p=argparse.ArgumentParser();p.add_argument('--host',type=Path,required=True);args=p.parse_args()
    with tempfile.TemporaryDirectory(dir=ROOT/'.build',prefix='command-posix-sdk-') as tmp:
        base=Path(tmp);site=base/'site';write_site(site);(site/'mdo').mkdir()
        for name in ('command.h','module.h'):shutil.copy2(ROOT/'include/mdo'/name,site/'mdo'/name)
        shutil.copy2(ROOT/'app/command-worker.c',site/'worker.c')
        (site/'probe.c').write_text(r'''
#include <xsbase.h>
#include <stdio.h>
static void Error(void* U,cstr S){(void)U;printf("compiler_error=%s\n",S);}
void ServiceInit(XS_HostInfo* H){
 (void)H;size_t N=0;char* S=(char*)xrtVfsReadAllLimit(xsApplicationVfs(),"/app/worker.c",65536,&N);
 TCCState* C=xsCreateTCC();
 if(C){tcc_set_error_func(C,NULL,Error);tcc_undefine_symbol(C,"_WIN32");tcc_undefine_symbol(C,"_WIN64");
 tcc_define_symbol(C,"__linux__","1");tcc_define_symbol(C,"__ANDROID__","1");tcc_add_quote_include_path(C,"/app");}
 printf("posix_worker_compiled=%d\nprobe_done=1\n",C&&S&&tcc_compile_string(C,S)>=0);fflush(stdout);
 if(C)tcc_delete(C);xrtFree(S);
}
void ServiceUnit(XS_HostInfo* H){(void)H;}
''',encoding='utf-8')
        output=run_probe(args.host.resolve(),site,base/'home')
        assert 'posix_worker_compiled=1' in output,output
    print('POSIX command worker compiles with compact TCC SDK (no system header dependency): PASS')
if __name__=='__main__':main()
