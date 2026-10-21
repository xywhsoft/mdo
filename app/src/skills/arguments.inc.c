/* Prompt substitution only: no shell evaluation, variable expansion or execution. */
#define MDO_SKILL_ARGUMENT_BYTES 16384u
#define MDO_SKILL_RENDER_BYTES (2u * 1024u * 1024u)

typedef struct MdoSkillArguments {
    char* Text;
    xstrview* Items;
    size_t Count, Capacity;
} MdoSkillArguments;

static bool MdoSkillArgumentSpace(char c)
{
    return c==' '||c=='\t'||c=='\r'||c=='\n';
}

static bool MdoSkillArgumentIdentifier(char c)
{
    return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_';
}

static bool MdoSkillSplitArguments(cstr Raw,MdoSkillArguments* Split)
{
    size_t Length=strlen(Raw);
    if(Length>MDO_SKILL_ARGUMENT_BYTES)return false;
    Split->Text=xrtMalloc(Length+1);
    if(!Split->Text)return false;
    char* Out=Split->Text;
    const char* p=Raw;
    while(*p) {
        while(MdoSkillArgumentSpace(*p))++p;
        if(!*p)break;
        char* Start=Out;
        char Quote=0;
        while(*p && (Quote||!MdoSkillArgumentSpace(*p))) {
            if((*p=='\''||*p=='"') && (!Quote||Quote==*p)) {
                Quote=Quote?0:*p;++p;continue;
            }
            if(*p=='\\' && Quote!='\'' && p[1] &&
                (p[1]=='\\'||p[1]=='"'||(!Quote&&(p[1]=='\''||MdoSkillArgumentSpace(p[1])))))++p;
            *Out++=*p++;
        }
        /* An unfinished quote consumes the rest of this input, without failing
         * the conversation. Ordinary Windows path backslashes remain literal. */
        if(!MdoSkillsGrow((void**)&Split->Items,&Split->Capacity,Split->Count+1,
                sizeof(*Split->Items),MDO_SKILL_ARGUMENT_BYTES+1))return false;
        Split->Items[Split->Count++]=xrtStrViewN(Start,(size_t)(Out-Start));
    }
    *Out=0;return true;
}

/* Returns the original placeholder length; a missing index has no replacement.
 * Named optional positions expand to empty, matching Claude Code semantics. */
static size_t MdoSkillArgumentToken(cstr p,cstr Raw,const MdoSkillArguments* Split,
    const char* const* Names,size_t NameCount,xstrview* Value,bool* Receives)
{
    if(*p!='$')return 0;
    const char* Start=p++;
    bool Indexed=*p>='0'&&*p<='9';
    bool Brackets=false;
    size_t Index=0;
    bool Overflow=false;
    if(!Indexed) {
        const char* Name=p;
        while(MdoSkillArgumentIdentifier(*p))++p;
        size_t Size=(size_t)(p-Name);
        if(Size==9 && !memcmp(Name,"ARGUMENTS",9)) {
            if(*p!='[') {*Value=xrtStrView(Raw);*Receives=true;return (size_t)(p-Start);}
            Brackets=true;Indexed=true;++p;
            if(*p<'0'||*p>'9')return 0;
        } else {
            for(size_t i=0;i<NameCount;i++)if(strlen(Names[i])==Size&&!memcmp(Name,Names[i],Size)) {
                *Value=i<Split->Count?Split->Items[i]:XRT_STR_LITERAL("");
                *Receives=true;return (size_t)(p-Start);
            }
            return 0;
        }
    }
    if(Indexed) {
        while(*p>='0'&&*p<='9') {
            size_t Digit=(size_t)(*p++-'0');
            if(Index>(SIZE_MAX-Digit)/10)Overflow=true;
            if(!Overflow)Index=Index*10+Digit;
        }
        if(Brackets && *p++!=']')return 0;
        if(!Overflow && Index<Split->Count){*Value=Split->Items[Index];*Receives=true;}
        return (size_t)(p-Start);
    }
    return 0;
}

static bool MdoSkillRenderCopy(char* Out,size_t* Bytes,xstrview Value)
{
    if(Value.Size>=MDO_SKILL_RENDER_BYTES-*Bytes)return false;
    if(Out && Value.Size)memcpy(Out+*Bytes,Value.Data,Value.Size);
    *Bytes+=Value.Size;return true;
}

static bool MdoSkillRenderPass(cstr Text,cstr Directory,cstr Raw,
    const MdoSkillArguments* Split,const char* const* Names,size_t NameCount,
    char* Out,size_t* Bytes)
{
    static const char* const Dirs[]={"${MDO_SKILL_DIR}","${CLAUDE_SKILL_DIR}","${ZCODE_SKILL_DIR}"};
    bool Received=false;
    *Bytes=0;
    for(const char* p=Text;*p;) {
        size_t Length=0;
        for(size_t i=0;i<sizeof(Dirs)/sizeof(Dirs[0]);i++)if(!strncmp(p,Dirs[i],strlen(Dirs[i]))) {
            Length=strlen(Dirs[i]);break;
        }
        if(Length) {
            if(!MdoSkillRenderCopy(Out,Bytes,xrtStrView(Directory)))return false;
            p+=Length;continue;
        }
        if(*p=='\\') {
            size_t Slashes=0;while(p[Slashes]=='\\')++Slashes;
            xstrview Ignored={0};bool Receives=false;
            Length=Slashes==1?MdoSkillArgumentToken(p+1,Raw,Split,Names,NameCount,&Ignored,&Receives):0;
            if(Length) {
                if(!MdoSkillRenderCopy(Out,Bytes,xrtStrViewN(p+1,Length)))return false;
                p+=Length+1;continue;
            }
            if(!MdoSkillRenderCopy(Out,Bytes,xrtStrViewN(p,Slashes)))return false;
            p+=Slashes;continue;
        }
        xstrview Value={0};bool Receives=false;
        Length=MdoSkillArgumentToken(p,Raw,Split,Names,NameCount,&Value,&Receives);
        if(!Length){Length=1;Value=xrtStrViewN(p,1);}
        else if(!Receives)Value=xrtStrViewN(p,Length);
        Received=Received||Receives;
        if(!MdoSkillRenderCopy(Out,Bytes,Value))return false;
        p+=Length;
    }
    if(*Raw && !Received &&
        (!MdoSkillRenderCopy(Out,Bytes,XRT_STR_LITERAL("\n\nARGUMENTS: "))||
         !MdoSkillRenderCopy(Out,Bytes,xrtStrView(Raw))))return false;
    if(Out)Out[*Bytes]=0;
    return true;
}

char* MdoSkillRenderArguments(cstr Text,cstr Directory,cstr Arguments,
    const char* const* Names,size_t NameCount)
{
    if(!Text||!Directory||(!Names&&NameCount))return NULL;
    cstr Raw=Arguments?Arguments:"";
    MdoSkillArguments Split={0};
    char* Result=NULL;
    size_t Bytes=0;
    if(MdoSkillSplitArguments(Raw,&Split) &&
        MdoSkillRenderPass(Text,Directory,Raw,&Split,Names,NameCount,NULL,&Bytes)) {
        Result=xrtMalloc(Bytes+1);
        if(Result && !MdoSkillRenderPass(Text,Directory,Raw,&Split,Names,NameCount,Result,&Bytes)) {
            xrtFree(Result);Result=NULL;
        }
    }
    xrtFree(Split.Items);xrtFree(Split.Text);
    if(!Result)MdoSkillsSetError("Skill argument expansion exceeds 2 MiB, arguments exceed 16 KiB, or memory is unavailable; shorten the arguments or move repeated content into references");
    return Result;
}

char* MdoSkillRender(cstr Text,cstr Directory,cstr Arguments)
{
    return MdoSkillRenderArguments(Text,Directory,Arguments,NULL,0);
}
