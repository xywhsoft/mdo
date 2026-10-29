/* Search large result files with bounded memory. Ordinary workspace limits
 * stay unchanged. A snapshot ends at the size observed when the file opens. */
static xwork_explore_status xwork__explore_grep_log_line(const xwork_explore_file* File,
    xwork_grep_state* State,const char* Text,size_t Size,size_t Line)
{
    if(Size&&Text[Size-1u]=='\r')Size--;
    if(memchr(Text,0,Size)||!xrtUtf8Valid((xstrview){Text,Size},NULL))return XWORK_EXPLORE_IO;
    if(State->bTruncated)return XWORK_EXPLORE_OK;
    size_t Begin=0,End=0;bool Match=false;
    xwork_explore_status Result=xwork__explore_line_match(State,Text,Size,&Begin,&End,&Match);
    if(Result!=XWORK_EXPLORE_OK||!Match)return Result;
    State->iMatches++;
    if(State->iShown>=State->iMaxResults){State->bTruncated=true;return XWORK_EXPLORE_OK;}
    if(!xwork__buf_appendf(State->pOutput,"%s:%zu: ",File->sWorkspaceRelative,Line)||
        !xwork__explore_append_preview(State->pOutput,Text,Size,Begin,End)||
        !xwork__buf_append_char(State->pOutput,'\n'))return XWORK_EXPLORE_OUT_OF_MEMORY;
    State->iShown++;return XWORK_EXPLORE_OK;
}

static xwork_explore_status xwork__explore_grep_log(const xwork_explore_file* File,xwork_grep_state* State)
{
    if(xwork__explore_binary_extension(File->sWorkspaceRelative)){State->iBinarySkipped++;return XWORK_EXPLORE_OK;}
    xfileoptions Options;xrtFileOptionsInit(&Options);Options.Flags=XFILE_READ|XFILE_NOFOLLOW;
    xfile Handle=xrtFileOpen(File->sAbsolute,&Options);xfileinfo Info;
    if(!Handle||!xrtFileStat(Handle,&Info)||Info.Type!=XFILE_TYPE_FILE){if(Handle)xrtClose(Handle);return XWORK_EXPLORE_IO;}
    if(Info.Size>XWORK_EXPLORE_MAX_TOTAL_BYTES-State->iBytesScanned){State->bScanLimited=true;xrtClose(Handle);return XWORK_EXPLORE_LIMIT;}
    size_t BeforeSize=State->pOutput->iLen,BeforeShown=State->iShown,BeforeMatches=State->iMatches;
    bool BeforeTruncated=State->bTruncated,Binary=false;xwork_buf Line={0};size_t LineNumber=0;
    uint64_t Remaining=Info.Size;unsigned char Chunk[32768];xwork_explore_status Result=XWORK_EXPLORE_OK;
    while(Remaining) {
        Result=xwork__explore_scope_status(State->pContext);if(Result!=XWORK_EXPLORE_OK)break;
        size_t Want=Remaining>sizeof(Chunk)?sizeof(Chunk):(size_t)Remaining,Got=0;
        if(!xrtRead(Handle,Chunk,Want,&Got)){Result=XWORK_EXPLORE_IO;break;}
        if(!Got){State->bScanLimited=true;Result=XWORK_EXPLORE_LIMIT;break;}
        Remaining-=Got;State->iBytesScanned+=Got;size_t At=0;
        while(At<Got) {
            const unsigned char* Newline=memchr(Chunk+At,'\n',Got-At);
            size_t N=Newline?(size_t)(Newline-(Chunk+At)):Got-At;
            if(N>XWORK_EXPLORE_MAX_FILE_BYTES-Line.iLen){State->bScanLimited=true;Result=XWORK_EXPLORE_LIMIT;break;}
            if(!xwork__buf_append(&Line,Chunk+At,N)){Result=XWORK_EXPLORE_OUT_OF_MEMORY;break;}
            At+=N+(Newline?1u:0u);
            if(Newline) {
                Result=xwork__explore_grep_log_line(File,State,Line.pData?Line.pData:"",Line.iLen,++LineNumber);
                if(Result==XWORK_EXPLORE_IO){Binary=true;break;}
                if(Result!=XWORK_EXPLORE_OK)break;
                Line.iLen=0;if(Line.pData)Line.pData[0]=0;
            }
        }
        if(Binary||Result!=XWORK_EXPLORE_OK)break;
    }
    if(!Binary&&Result==XWORK_EXPLORE_OK&&Line.iLen) {
        Result=xwork__explore_grep_log_line(File,State,Line.pData,Line.iLen,++LineNumber);
        if(Result==XWORK_EXPLORE_IO)Binary=true;
    }
    if(Binary) {
        State->pOutput->iLen=BeforeSize;if(State->pOutput->pData)State->pOutput->pData[BeforeSize]=0;
        State->iShown=BeforeShown;State->iMatches=BeforeMatches;State->bTruncated=BeforeTruncated;
        State->iBinarySkipped++;Result=XWORK_EXPLORE_OK;
    }
    xwork__buf_unit(&Line);if(!xrtClose(Handle)&&Result==XWORK_EXPLORE_OK)Result=XWORK_EXPLORE_IO;
    return Result;
}
