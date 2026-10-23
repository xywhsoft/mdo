/* Diff output is consumed as a stream. Only the requested line page is kept;
 * long patches do not inherit process.run's 16 KiB capture budget. */
#define GIT_DIFF_LINE_BYTES 16384u
#define GIT_DIFF_PAGE_BYTES (512u*1024u)
typedef struct SdkGitDiffPage {
    const mdo_sdk_context_v1* Context;
    xvalue* Rows;
    uint64 Offset,Limit,Total,Next,Old,New,Additions,Deletions,LineBytes;
    size_t Used,PageBytes;
    char Line[GIT_DIFF_LINE_BYTES+1];
    bool Hunk,Binary,Full,Failed;
} SdkGitDiffPage;
static void SdkGitDiffLine(SdkGitDiffPage* Page){
    char* Text=Page->Line;Text[Page->Used]=0;const char* Kind="meta";uint64 Old=0,New=0;
    if(!strncmp(Text,"@@ ",3)){
        cstr A=strchr(Text,'-'),B=A?strchr(A,'+'):NULL;
        if(A&&B){Page->Old=SdkGitNumber(A+1);Page->New=SdkGitNumber(B+1);Page->Hunk=true;}Kind="hunk";
    }else if(!strncmp(Text,"Binary files ",13)||!strcmp(Text,"GIT binary patch")){Page->Binary=true;Kind="binary";}
    else if(Page->Hunk){
        if(Text[0]=='+'){Kind="added";New=Page->New++;Page->Additions++;}
        else if(Text[0]=='-'){Kind="removed";Old=Page->Old++;Page->Deletions++;}
        else if(Text[0]==' '){Kind="context";Old=Page->Old++;New=Page->New++;}
        else Kind="notice";
    }
    if(Page->Total>=Page->Offset&&!Page->Full){
        if(xrtValueCount(Page->Rows)>=Page->Limit||Page->PageBytes+Page->Used>GIT_DIFF_PAGE_BYTES)Page->Full=true;
        else {
            xvalue* Row=xrtValueObject();SdkPut(Row,"kind",Kind);SdkPutUInt(Row,"offset",Page->Total);
            if(Old)SdkPutUInt(Row,"old_line",Old);if(New)SdkPutUInt(Row,"new_line",New);
            if(xrtUtf8Valid(xrtStrViewN(Text,Page->Used),NULL))xrtValueObjectSetNew(Row,XRT_STR_LITERAL("text"),xrtValueString(xrtStrViewN(Text,Page->Used)));
            else {char* Encoded=SdkBase64(Text,Page->Used);SdkPut(Row,"base64",Encoded);xrtFree(Encoded);}
            if(Page->LineBytes>Page->Used){xrtValueObjectSetNew(Row,XRT_STR_LITERAL("truncated"),xrtValueBool(true));SdkPutUInt(Row,"bytes",Page->LineBytes);}
            if(!xrtValueArrayAppendNew(Page->Rows,Row))Page->Failed=true;
            Page->PageBytes+=Page->Used;Page->Next=Page->Total+1;
        }
    }
    Page->Total++;Page->Used=0;Page->LineBytes=0;
}
static bool SdkGitDiffOutput(xprocessstream Stream,xbytesview Bytes,void* User){
    SdkGitDiffPage* Page=User;if(Stream!=XPROCESS_STDOUT)return !SdkCancelled(Page->Context);
    for(size_t i=0;i<Bytes.Size;i++){
        if(Bytes.Data[i]=='\n')SdkGitDiffLine(Page);
        else {if(Page->Used<GIT_DIFF_LINE_BYTES)Page->Line[Page->Used++]=(char)Bytes.Data[i];Page->LineBytes++;}
    }return !Page->Failed&&!SdkCancelled(Page->Context);
}
static bool SdkGitDiff(const mdo_sdk_context_v1* C,const xvalue* P,xvalue** R,mdo_error_v1* E){
    uint64 Offset=0,Limit=400;
    if((SdkGet(P,"offset")&&!SdkUInt(SdkGet(P,"offset"),&Offset))||(SdkGet(P,"limit")&&(!SdkUInt(SdkGet(P,"limit"),&Limit)||!Limit||Limit>2000)))return SdkGitFail(E,"git_arguments_invalid","Diff pages need a valid offset and 1-2000 lines",400);
    cstr Path=SdkField(P,"path"),Expected=SdkField(P,"revision");if(!SdkGitPath(Path))return SdkGitFail(E,"git_path_invalid","Select a repository-relative file",400);
    SdkGitRange Range={0},AfterRange={0};SdkGitPrivateIndex Private={0};xvalue *Before=NULL,*After=NULL;bool Ok=false;char* Full=NULL;xprocessresult Result={0};
    SdkGitDiffPage Page={0};Page.Context=C;Page.Rows=xrtValueArray();Page.Offset=Offset;Page.Next=Offset;Page.Limit=Limit;
    if(!SdkGitList(C,P,&Range,&Before,E))goto done;
    if(Expected&&strcmp(Expected,SdkField(Before,"revision"))){SdkGitFail(E,"git_comparison_changed","Repository changed; refresh the file list before reading its diff",409);goto done;}
    const xvalue* File=SdkGitFind((xvalue*)SdkGet(Before,"files"),Path);
    if(!File){SdkGitFail(E,"git_file_missing","This file has no changes in the selected comparison",404);goto done;}
    cstr Status=SdkField(File,"status"),OldPath=SdkField(File,"old_path");cstr Args[32];size_t Count;
    bool Directory=false;xrtValueGetBool(SdkGet(File,"directory"),&Directory);
    if(Directory){
        xvalue* Row=xrtValueObject();SdkPut(Row,"kind","notice");SdkPut(Row,"text","This entry is a nested repository or directory. Open its own repository to inspect contained changes.");SdkPutUInt(Row,"offset",0);
        if(!Offset)xrtValueArrayAppendNew(Page.Rows,Row);else xrtValueRelease(Row);Page.Total=1;Page.Next=1;goto verify;
    }
    bool NewFile=!strcmp(Status,"??")||(Range.Unborn&&!strcmp(Range.Scope,"workspace"));
    if(NewFile){
        Full=xrtPathJoin(Range.Root,Path);if(!Full)goto done;
        const cstr Fresh[]={"diff","--no-index","--no-ext-diff","--no-textconv","--no-color","--unified=3","--","/dev/null",Full};Count=sizeof(Fresh)/sizeof(*Fresh);for(size_t i=0;i<Count;i++)Args[i]=Fresh[i];
    }else{
        bool Recreated=false;xrtValueGetBool(SdkGet(File,"worktree_recreated"),&Recreated);SdkGitRange Working=Range;
        if(Recreated){if(!SdkGitPrivateIndexInit(C,Range.Root,Range.Head,&Private,E))goto done;Working.Scope="unstaged";}
        Count=SdkGitDiffArgs(&Working,"--unified=3",Args);if(OldPath)Args[Count++]=OldPath;Args[Count++]=Path;
    }
    if(!SdkGitRunWithIndex(C,Range.PrivateRepo?Range.PrivateRepo:Range.Root,Args,Count,(xbytesview){0},1,SdkGitDiffOutput,&Page,&Result,E,Private.Path))goto done;
    if(Result.Status.Code!=0&&!(NewFile&&Result.Status.Code==1)){SdkGitSuccess(&Result,E,"git_diff_failed");goto done;}
    if(NewFile&&Result.Status.Code==1&&!Page.Total&&!Page.Used&&Result.StderrSize){SdkGitSuccess(&Result,E,"git_diff_failed");goto done;}
    if(Page.Used||Page.LineBytes)SdkGitDiffLine(&Page);
    if(Page.Failed){SdkGitFail(E,"git_output_failed","Diff page allocation failed",503);goto done;}
verify:
    if(Offset>Page.Total){SdkGitFail(E,"git_range_invalid","Diff offset is beyond the selected file",416);goto done;}
    if(!SdkGitList(C,P,&AfterRange,&After,E))goto done;
    if(strcmp(SdkField(Before,"revision"),SdkField(After,"revision"))){SdkGitFail(E,"git_comparison_changed","Repository changed while reading the diff; refresh before continuing",409);goto done;}
    *R=xrtValueObject();SdkPut(*R,"repository",Range.Root);SdkPut(*R,"path",Path);SdkPut(*R,"revision",SdkField(Before,"revision"));SdkPut(*R,"scope",Range.Scope);
    SdkPutUInt(*R,"offset",Offset);SdkPutUInt(*R,"next",Page.Next);SdkPutUInt(*R,"total",Page.Total);SdkPutUInt(*R,"additions",Page.Additions);SdkPutUInt(*R,"deletions",Page.Deletions);
    xrtValueObjectSetNew(*R,XRT_STR_LITERAL("more"),xrtValueBool(Page.Next<Page.Total));xrtValueObjectSetNew(*R,XRT_STR_LITERAL("binary"),xrtValueBool(Page.Binary));xrtValueObjectSet(*R,XRT_STR_LITERAL("rows"),Page.Rows);Ok=true;
done:SdkGitPrivateIndexUnit(&Private);xrtFree(Full);xrtProcessResultUnit(&Result);xrtValueRelease(Page.Rows);xrtValueRelease(Before);xrtValueRelease(After);SdkGitRangeUnit(&Range);SdkGitRangeUnit(&AfterRange);return Ok;
}
