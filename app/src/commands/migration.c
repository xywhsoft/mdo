#include "../../include/mdo/commands.h"
#include "../../include/mdo/prompt_file.h"
#include "../../include/mdo/skills.h"

/* Retain every original Markdown file. A partial migration resumes only when
 * its generated Skill still matches byte for byte; never overwrite edits. */
static bool CommandMigrationFile(cstr Path,cstr Text) {
    bool Missing=false;char* Old=MdoExtensionRead(Path,true,MDO_COMMAND_TEXT_LIMIT,&Missing);
    bool Ok=Old?!strcmp(Old,Text):Missing&&MdoHomeAtomicWrite(Path,Text,strlen(Text),false);
    xrtFree(Old);return Ok;
}
bool MdoCommandMigrateLegacy(void) {
    xdir Dir=MdoHomeOpenDirectory("commands",XDIR_STAT);if(!Dir)return CommandMissing();
    xdirentry E;xdirnext Next=XDIR_NEXT_END;xvalue* Report=xrtValueArray();bool Ok=Report!=NULL;size_t Count=0;
    while(Ok&&(Next=xrtDirNext(Dir,&E))==XDIR_NEXT_ITEM){
        size_t N=E.Name.Size;if(N<4||N>67||memcmp(E.Name.Data+N-3,".md",3))continue;
        char Id[65];memcpy(Id,E.Name.Data,N-3);Id[N-3]=0;if(!MdoExtensionIdValid(Id))continue;
        if(++Count>MDO_COMMAND_LIMIT){Ok=false;break;}
        char Path[192],CPath[160];snprintf(Path,sizeof(Path),"commands/%s.md",Id);snprintf(CPath,sizeof(CPath),"commands/%s.c",Id);
        bool Exists=false;xfileinfo Info={0};
        if(!MdoHomeExternalStat(CPath,&Exists,&Info)){Ok=false;break;}
        if(Exists)continue; /* A new or edited C command is authoritative. */
        bool Missing=false;char Error[512]={0};char* Text=MdoExtensionRead(Path,true,MDO_COMMAND_TEXT_LIMIT,&Missing);
        xvalue* Doc=Text?MdoPromptParse(Text,false,Error,sizeof(Error)):NULL;
        cstr Body=CommandText(Doc,"prompt"),Description=CommandText(Doc,"description");
        char Hash[65]={0},Skill[65],SkillPath[160];
        bool Migrated=Body&&xrtStrTrim(xrtStrView(Body)).Size&&MdoExtensionHash(Text,Hash);
        snprintf(Skill,sizeof(Skill),"command-%.44s-%.8s",Id,Hash);snprintf(SkillPath,sizeof(SkillPath),"skills/%s/SKILL.md",Skill);
        xvalue* D=xrtValueString(xrtStrView(Description&&Description[0]?Description:Id));char* Quoted=xrtJsonStringify(D,false,NULL);xrtValueRelease(D);
        size_t Capacity=Body?strlen(Body)+4096:4096;char* Document=xrtMalloc(Capacity);char* Source=xrtMalloc(8192);
        if(Migrated&&Quoted&&Document&&Source){
            int Size=snprintf(Document,Capacity,"---\nname: %s\ndescription: %s\ndisable-model-invocation: true\n---\n%s",Skill,Quoted,Body);
            xvalue* Meta=xrtValueObject();CommandSet(Meta,"id",Id);CommandSet(Meta,"description",Description&&Description[0]?Description:Id);
            CommandSet(Meta,"argument_hint","[arguments]");xrtValueObjectSetNew(Meta,XRT_STR_LITERAL("effects"),xrtJsonParse(XRT_STR_LITERAL("[\"ui\"]")));
            char* Header=xrtJsonStringify(Meta,false,NULL);xrtValueRelease(Meta);
            int SourceSize=Header?snprintf(Source,8192,"/* mdo-command %s */\n#include \"mdo/command.h\"\nmdo_result mdoCommandRun(const mdo_command_context_v1* C,const xvalue* A,xvalue** R){\n(void)A;xvalue* P=C->Host->Parse(\"{\\\"id\\\":\\\"%s\\\"}\");\nif(!P)return MDO_RESULT_ERROR;mdo_result E=mdoCommandCall(C,\"skill.invoke\",P,R);C->Host->Release(P);return E;}\n",Header,Skill):-1;
            xrtFree(Header);
            Migrated=Size>0&&(size_t)Size<Capacity&&SourceSize>0&&SourceSize<8192&&
                MdoSkillValidateText(Document,Error,sizeof(Error))&&CommandMigrationFile(SkillPath,Document)&&CommandMigrationFile(CPath,Source);
        }else Migrated=false;
        xvalue* Row=xrtValueObject();Ok=Row&&CommandSet(Row,"id",Id)&&CommandSet(Row,"skill_id",Skill)&&
            CommandSet(Row,"state",Migrated?"migrated":"needs_attention")&&CommandSet(Row,"message",Migrated?"Original Markdown retained":Error[0]?Error:"Legacy command is invalid or a generated Skill conflicts with an existing file")&&xrtValueArrayAppendNew(Report,Row);
        xrtFree(Text);xrtValueRelease(Doc);xrtFree(Quoted);xrtFree(Document);xrtFree(Source);
    }
    if(!xrtDirClose(Dir)||Next==XDIR_NEXT_ERROR)Ok=false;
    if(Ok&&Count){char* Json=xrtJsonStringify(Report,false,NULL);Ok=Json&&MdoHomeAtomicWrite("data/commands/migration-report.json",Json,strlen(Json),false);xrtFree(Json);}
    xrtValueRelease(Report);return Ok;
}
