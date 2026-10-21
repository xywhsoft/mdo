"""Native Skill behavior probes; only disposable Home and local code are used."""
import argparse
from pathlib import Path
import tempfile
import test_skill_runtime as fixture

ROOT = Path(__file__).resolve().parents[1]
CHECKS = r'''
    {
        char* prompt=xrtStrDup("base");
        bool first=MdoSkillCatalogAppendPrompt(builtin,&prompt);
        char* saved=first?xrtStrDup(prompt):NULL;
        bool second=first&&MdoSkillCatalogAppendPrompt(builtin,&prompt);
        printf("feature_metadata_idempotent=%d\n",second&&saved&&!strcmp(saved,prompt));
        MdoSkillInfo info={0};info.Size=sizeof(info);MdoSkillCatalogAt(builtin,0,&info);
        printf("feature_casefold=%d\n",MdoSkillSearchMatches(&info,"EXPLORER")&&!MdoSkillSearchMatches(&info,"not-present"));
        printf("feature_lazy_index=%d\n",first&&strstr(prompt,"project-explorer")&&!strstr(prompt,"# Project Explorer"));
        xrtFree(prompt);xrtFree(saved);
        char* base=MdoSkillCatalogBaseDirectory(builtin,"project-explorer");
        char* path=base?xrtPathJoin(base,"templates/report.md"):NULL;
        printf("feature_native_resources=%d\n",path&&xrtFileExists(path));xrtFree(path);xrtFree(base);
        xvalue* page=xrtValueObject();
        const char* chinese="\xe4\xb8\xad\xe6\x96\x87xyz";
        bool paged=MdoSkillTextPage(chinese,9,1,4,page);
        uint64 cursor=99,next=99;bool eof=true;
        xrtValueGetUInt(xrtValueObjectGet(page,XRT_STR_LITERAL("cursor")),&cursor);
        xrtValueGetUInt(xrtValueObjectGet(page,XRT_STR_LITERAL("next_cursor")),&next);
        xrtValueGetBool(xrtValueObjectGet(page,XRT_STR_LITERAL("eof")),&eof);
        printf("feature_utf8_page=%d\n",paged&&cursor==0&&next==3&&!eof);
        printf("feature_binary_rejected=%d\n",!MdoSkillTextPage("a\0b",3,0,48,page));xrtClearError();xrtValueRelease(page);
    }
'''

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host',type=Path,default=ROOT/'.build/host/xs.exe')
    args=parser.parse_args()
    source=fixture.PROBE_SOURCE
    fixture.PROBE_SOURCE=source.replace('PrintFirst(builtin, "initial_skill");',
        'PrintFirst(builtin, "initial_skill");\n'+CHECKS,1)
    try:
        with tempfile.TemporaryDirectory(prefix='skill-features-',dir=ROOT/'.build') as raw:
            base=Path(raw);fixture.write_site(base/'site')
            output=fixture.run_probe(args.host.resolve(),base/'site',base/'home')
            for field in ('metadata_idempotent','casefold','lazy_index','native_resources','utf8_page','binary_rejected'):
                assert f'feature_{field}=1' in output, output
            assert 'probe_done=1' in output, output
    finally: fixture.PROBE_SOURCE=source
    print('Skill feature runtime: PASS')

if __name__=='__main__':main()
