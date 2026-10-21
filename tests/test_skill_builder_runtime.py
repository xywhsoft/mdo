"""Read-only Skill builder validation through the pinned native xs/TCC host."""
import argparse
from pathlib import Path
import tempfile
import test_skill_runtime as fixture

ROOT = Path(__file__).resolve().parents[1]
CHECKS = r'''
    {
        int failures=0;
        xvalue *input=NULL,*report=NULL;
        bool valid=false,resources=false,behavior=true;
        const char* doc="---\nname: sample\ndescription: Builder fixture\n---\nTask";
        input=xrtValueObject();report=xrtValueObject();
        xrtValueObjectSetNew(input,XRT_STR_LITERAL("content"),xrtValueString(xrtStrView(doc)));
        if(!MdoSkillCheckRequest(input,NULL,report)||!xrtValueGetBool(xrtValueObjectGet(report,XRT_STR_LITERAL("valid")),&valid)||!valid)failures++;
        xrtValueGetBool(xrtValueObjectGet(report,XRT_STR_LITERAL("resources_checked")),&resources);
        xrtValueGetBool(xrtValueObjectGet(report,XRT_STR_LITERAL("behavior_tested")),&behavior);
        if(resources||behavior||!xrtValueCount(xrtValueObjectGet(report,XRT_STR_LITERAL("warnings"))))failures++;
        xrtValueRelease(report);report=xrtValueObject();
        xrtValueObjectSetNew(input,XRT_STR_LITERAL("content"),xrtValueString(XRT_STR_LITERAL("invalid")));
        if(!MdoSkillCheckRequest(input,NULL,report))failures++;
        xrtValueGetBool(xrtValueObjectGet(report,XRT_STR_LITERAL("valid")),&valid);
        if(valid||!xrtValueCount(xrtValueObjectGet(report,XRT_STR_LITERAL("errors"))))failures++;
        xrtValueRelease(input);xrtValueRelease(report);
        MdoHomeAtomicWrite("builder-project/.mdo/skills/sample/SKILL.md",doc,strlen(doc),false);
        char* root=MdoHomeExternalPath("builder-project");
        input=xrtJsonParse(XRT_STR_LITERAL("{\"name\":\"sample\",\"scope\":\"project\"}"));report=xrtValueObject();
        if(!MdoSkillCheckRequest(input,root,report))failures++;
        xrtValueGetBool(xrtValueObjectGet(report,XRT_STR_LITERAL("valid")),&valid);
        xrtValueGetBool(xrtValueObjectGet(report,XRT_STR_LITERAL("resources_checked")),&resources);
        if(!valid||!resources)failures++;
        xrtValueRelease(report);report=xrtValueObject();
        const char* missing="---\nname: sample\ndescription: Fixture\nscripts: [scripts/missing.sh]\n---\nTask";
        MdoHomeAtomicWrite("builder-project/.mdo/skills/sample/SKILL.md",missing,strlen(missing),false);
        if(!MdoSkillCheckRequest(input,root,report))failures++;
        xrtValueGetBool(xrtValueObjectGet(report,XRT_STR_LITERAL("valid")),&valid);
        if(valid||!xrtValueCount(xrtValueObjectGet(report,XRT_STR_LITERAL("errors"))))failures++;
        xrtValueRelease(report);report=xrtValueObject();
        const char bad[]="---\nname: sample\ndescription: Fixture\n---\n\xff";
        MdoHomeAtomicWrite("builder-project/.mdo/skills/sample/SKILL.md",bad,sizeof(bad)-1,false);
        if(!MdoSkillCheckRequest(input,root,report))failures++;
        xrtValueGetBool(xrtValueObjectGet(report,XRT_STR_LITERAL("valid")),&valid);
        if(valid)failures++;
        xrtValueRelease(input);xrtValueRelease(report);xrtFree(root);
        input=xrtJsonParse(XRT_STR_LITERAL("{\"name\":\"../escape\",\"scope\":\"user\"}"));report=xrtValueObject();
        if(MdoSkillCheckRequest(input,NULL,report))failures++;xrtClearError();
        xrtValueRelease(input);xrtValueRelease(report);
        input=xrtJsonParse(XRT_STR_LITERAL("{\"name\":\"sample\",\"scope\":\"project\"}"));report=xrtValueObject();
        if(MdoSkillCheckRequest(input,NULL,report))failures++;xrtClearError();
        xrtValueRelease(input);xrtValueRelease(report);
        MdoSkillInfo creator={0};creator.Size=sizeof(creator);
        if(!MdoSkillCatalogFind(builtin,"skill-creator",&creator)||creator.ResourceCount!=3)failures++;
        printf("builder_validation_failures=%d\n",failures);
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
        with tempfile.TemporaryDirectory(prefix='skill-builder-',dir=ROOT/'.build') as raw:
            base=Path(raw);fixture.write_site(base/'site')
            output=fixture.run_probe(args.host.resolve(),base/'site',base/'home')
            assert 'builder_validation_failures=0' in output and 'probe_done=1' in output,output
    finally:fixture.PROBE_SOURCE=source
    print('Skill builder native validation: PASS')


if __name__=='__main__':main()
