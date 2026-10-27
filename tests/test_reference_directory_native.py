"""Exercise real Skill/knowledge discovery and retrieval from directory XIDs."""
import argparse
import json
from pathlib import Path
import shutil
import uuid
from native_product_runner import product

ROOT = Path(__file__).resolve().parents[1]
CONTRACT = r'''
xvalue* QaProductContract(const xvalue* P,xvfs V){
    cstr Root=SdkField(P,"root"),App=SdkField(P,"app");xvalue* R=xrtValueObject();char Error[512]={0};
    bool Home=QaProductHome(Root,App,V),Ok=Home&&MdoExtensionsInit()&&MdoExtensionsRecover(Error)&&MdoExtensionsSeedBuiltins(Error)&&MdoSkillManagerInit();
    xrtValueObjectSetNew(R,XRT_STR_LITERAL("loaded"),xrtValueBool(Ok));
    char* Workspace=MdoHomeExternalPath("workspace");MdoSkillCatalog* C=Ok?MdoSkillCatalogForWorkspace(Workspace):NULL;
    MdoSkillInfo Info={0};Info.Size=sizeof(Info);bool Found=C&&MdoSkillCatalogFind(C,"catalog-alias",&Info);
    Found=Found&&Info.DirectoryId&&!strcmp(Info.DirectoryId,"0123456789ABCDEFGHJKMNPQRSTVWXYZ")&&strstr(Info.SourcePath,"/instructions.md");
    xrtValueObjectSetNew(R,XRT_STR_LITERAL("skill_xid_and_custom_entry"),xrtValueBool(Found));
    xvalue* Check=xrtJsonParse(XRT_STR_LITERAL("{\"name\":\"catalog-alias\",\"scope\":\"user\"}")),*Report=xrtValueObject();bool Valid=false;
    bool Checked=Check&&Report&&MdoSkillCheckRequest(Check,Workspace,Report)&&xrtValueGetBool(SdkGet(Report,"valid"),&Valid)&&Valid;
    xrtValueObjectSetNew(R,XRT_STR_LITERAL("skill_validation_uses_directory"),xrtValueBool(Checked));xrtValueRelease(Check);xrtValueRelease(Report);
    MdoSkillContent Body={0};Body.Size=sizeof(Body);char* Base=Found?MdoSkillCatalogBaseDirectory(C,"catalog-alias"):NULL;
    bool Read=Found&&Base&&strstr(Base,"0123456789ABCDEFGHJKMNPQRSTVWXYZ")&&MdoSkillCatalogLoadBody(C,"catalog-alias",&Body)&&strstr(Body.Text,"Use directory reference");
    xrtValueObjectSetNew(R,XRT_STR_LITERAL("skill_body_and_resource_root"),xrtValueBool(Read));xrtFree(Base);MdoSkillContentUnit(&Body);
    MdoSkillResourceContent Content={0};Content.Size=sizeof(Content);
    bool Hidden=Found&&MdoSkillCatalogLoadResource(C,"catalog-alias",".hidden/reference.txt",1024,&Content)&&Content.Bytes==16&&!memcmp(Content.Data,"hidden reference",16);
    MdoSkillResourceContentUnit(&Content);xrtValueObjectSetNew(R,XRT_STR_LITERAL("hidden_resource"),xrtValueBool(Hidden));
    cstr Deep="refs/a/b/c/d/e/f/g/h/i/j/k/l/deep.txt";Content.Size=sizeof(Content);
    bool DeepRead=Found&&MdoSkillCatalogLoadResource(C,"catalog-alias",Deep,1024,&Content)&&Content.Bytes==14&&!memcmp(Content.Data,"deep reference",14);
    MdoSkillResourceContentUnit(&Content);xrtValueObjectSetNew(R,XRT_STR_LITERAL("deep_resource"),xrtValueBool(DeepRead));
    Info.Size=sizeof(Info);bool Foreign=C&&!MdoSkillCatalogFind(C,"foreign-uninstalled",&Info);
    xrtValueObjectSetNew(R,XRT_STR_LITERAL("foreign_project_not_discovered"),xrtValueBool(Foreign));
    bool Selection=MdoHomeAtomicWrite("workspace/.mdo/skills.json","{\"catalog-alias\":false}",23,false);
    MdoSkillCatalog* Selected=Selection?MdoSkillCatalogForWorkspace(Workspace):NULL;Info.Size=sizeof(Info);
    Selection=Selection&&Selected&&!MdoSkillCatalogFind(Selected,"catalog-alias",&Info);MdoSkillCatalogRelease(Selected);
    xrtValueObjectSetNew(R,XRT_STR_LITERAL("project_selection_only"),xrtValueBool(Selection));
    bool Disabled=MdoExtensionsSetEnabled("skills","0123456789ABCDEFGHJKMNPQRSTVWXYZ",false,Error);Body.Size=sizeof(Body);
    Disabled=Disabled&&!MdoSkillCatalogLoadBody(C,"catalog-alias",&Body)&&MdoSkillManagerReload();MdoSkillContentUnit(&Body);
    xrtValueObjectSetNew(R,XRT_STR_LITERAL("disabled_cached_skill_blocked"),xrtValueBool(Disabled));MdoSkillCatalogRelease(C);
    char* Prompt=xrtStrDup("system");bool Knowledge=Prompt&&MdoKnowledgeAppendPrompt(&Prompt)&&strstr(Prompt,"@knowledge/ABCDEFGHJKMNPQRSTVWXYZ0123456789")&&strstr(Prompt,".references/a/b/index.md");
    xrtValueObjectSetNew(R,XRT_STR_LITERAL("knowledge_xid_prompt"),xrtValueBool(Knowledge));xrtFree(Prompt);
    bool Quiet=MdoExtensionsSetEnabled("knowledge","ABCDEFGHJKMNPQRSTVWXYZ0123456789",false,Error);Prompt=xrtStrDup("system");
    Quiet=Quiet&&Prompt&&MdoKnowledgeAppendPrompt(&Prompt)&&!strcmp(Prompt,"system");xrtFree(Prompt);
    xrtValueObjectSetNew(R,XRT_STR_LITERAL("knowledge_disabled"),xrtValueBool(Quiet));
    bool Warning=MdoExtensionsSetEnabled("knowledge","ABCDEFGHJKMNPQRSTVWXYZ0123456789",true,Error)&&
        MdoHomeAtomicWrite("knowledge/ABCDEFGHJKMNPQRSTVWXYZ0123456789/.references/a/b/index.md","",0,false);
    xvalue* Warnings=Warning?MdoKnowledgeDiagnostics():NULL;Warning=Warning&&Warnings&&xrtValueCount(Warnings)==1;xrtValueRelease(Warnings);
    xrtValueObjectSetNew(R,XRT_STR_LITERAL("invalid_index_warns"),xrtValueBool(Warning));
    MdoSkillDiagnostics* D=MdoSkillDiagnosticsSnapshot();xvalue* Messages=xrtValueArray();
    for(size_t I=0;D&&I<D->Count;I++)xrtValueArrayAppendNew(Messages,xrtValueString(xrtStrView(D->Entries[I].Message)));
    xrtValueObjectSetNew(R,XRT_STR_LITERAL("skill_diagnostics"),Messages);MdoSkillDiagnosticsRelease(D);
    MdoSkillManagerUnit();xvalue* Removed=NULL;bool Gone=MdoExtensionsRemove("skills","0123456789ABCDEFGHJKMNPQRSTVWXYZ",&Removed,Error);xrtValueRelease(Removed);Removed=NULL;
    Gone=Gone&&MdoExtensionsRemove("knowledge","ABCDEFGHJKMNPQRSTVWXYZ0123456789",&Removed,Error);xrtValueRelease(Removed);
    xrtValueObjectSetNew(R,XRT_STR_LITERAL("whole_directory_uninstall"),xrtValueBool(Gone));
    SdkPut(R,"error",Error);xrtFree(Workspace);MdoExtensionsUnit();if(Home)MdoHomeUnit();return R;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--url', required=True); parser.add_argument('--home', type=Path, required=True)
    parser.add_argument('--project', default='default'); args = parser.parse_args()
    out = ROOT / '.build/reference-directory-native'; out.mkdir(parents=True, exist_ok=True)
    fixture = out / ('home-' + uuid.uuid4().hex[:12]); fixture.mkdir()
    def put(path, text):
        file = fixture / path; file.parent.mkdir(parents=True, exist_ok=True); file.write_text(text, encoding='utf-8')
    skill = 'skills/0123456789ABCDEFGHJKMNPQRSTVWXYZ/'
    put(skill + 'extension.json', json.dumps({'kind': 'skills', 'name': 'Reference skill', 'alias': 'catalog-alias', 'entry': 'instructions.md', 'platforms': ['any']}))
    put(skill + 'instructions.md', '---\nname: Reference skill\ndescription: Directory skill regression\n---\nUse directory reference ${MDO_SKILL_DIR}\n')
    put(skill + '.hidden/reference.txt', 'hidden reference')
    put(skill + 'refs/a/b/c/d/e/f/g/h/i/j/k/l/deep.txt', 'deep reference')
    put('workspace/.claude/skills/foreign-uninstalled/SKILL.md', '---\nname: Foreign\ndescription: Uninstalled\n---\nDo not discover.\n')
    knowledge = 'knowledge/ABCDEFGHJKMNPQRSTVWXYZ0123456789/'
    put(knowledge + 'extension.json', json.dumps({'kind': 'knowledge', 'name': 'Library', 'alias': 'library', 'entry': 'metadata.json', 'platforms': ['any']}))
    put(knowledge + 'metadata.json', json.dumps({'format': 'mdo.knowledge.v1', 'name': 'Library', 'description': 'Reference library', 'index': '.references/a/b/index.md', 'prompt': 'Consult {$path} and its index.', 'inject_prompt': True}))
    put(knowledge + '.references/a/b/index.md', '# Index\n')
    try:
        result = product(args, fixture, CONTRACT, out / 'result.json')
        for key in ('loaded', 'skill_xid_and_custom_entry', 'skill_validation_uses_directory', 'skill_body_and_resource_root', 'hidden_resource', 'deep_resource', 'foreign_project_not_discovered', 'project_selection_only', 'disabled_cached_skill_blocked', 'knowledge_xid_prompt', 'knowledge_disabled', 'invalid_index_warns', 'whole_directory_uninstall'):
            assert result.get(key), (key, result)
        print(json.dumps(result, ensure_ascii=False))
    finally:
        if fixture.resolve().parent == out.resolve() and fixture.name.startswith('home-'):
            shutil.rmtree(fixture)


if __name__ == '__main__': main()
