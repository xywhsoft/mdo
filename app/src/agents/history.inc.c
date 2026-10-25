static xllm_render_action MdoAgentsHistoryMessage(xllm_session* Session,uint64 Sequence,
    uint64 Turn,uint32 Flags,xllm_message* Message,void* Data)
{
    MdoAgentInstance* Instance=Data;(void)Session;(void)Turn;
    if(!(Flags&XLLM_SESSION_ENTRY_PINNED) && Instance->HistoryFloor && Sequence<Instance->HistoryFloor)
        return XLLM_RENDER_SKIP;
    const xvalue* Policy=MdoAgentConfigGet(Instance->Configuration,"history");uint64 Limit=0;
    bool Changed=false;
    if(Message->eRole==XLLM_ROLE_TOOL && MdoAgentConfigNumber(Policy,"tool_result_max_bytes",0,1024u*1024u,&Limit) && Limit){
        if(!Message->iPartCount && Message->sContent && strlen(Message->sContent)>Limit){
            size_t N=(size_t)Limit;while(N&&((unsigned char)Message->sContent[N]&0xc0)==0x80)--N;
            char* Prefix=xrtStrDupN(Message->sContent,N);char* Text=Prefix?xrtFormat("%s\n[Tool result shortened by this role's history policy. The original remains in the conversation.]",Prefix):NULL;
            Changed=Text&&xllmMessageSetContent(Message,Text);xrtFree(Prefix);xrtFree(Text);
        }
    }
    cstr Images=MdoAgentConfigText(Policy,"images");
    if(Images&&!strcmp(Images,"omit"))for(size_t I=0;I<Message->iPartCount;I++)if(Message->pParts[I].eKind==XLLM_PART_IMAGE){
        xllmPartUnit(&Message->pParts[I]);xllmPartInit(&Message->pParts[I],XLLM_PART_TEXT);
        if(xllmPartSetText(&Message->pParts[I],"[Image omitted by this role's history policy; the original remains in the conversation.]"))Changed=true;
    }
    return Changed?XLLM_RENDER_MODIFIED:XLLM_RENDER_KEEP;
}
static bool MdoAgentsHistorySummary(xllm_session* Session,const xllm_session_summary* Summary,
    xllm_message* Message,void* Data)
{
    (void)Session;(void)Summary;(void)Message;
    MdoAgentInstance* Instance=Data;
    return MdoAgentConfigBool(MdoAgentConfigGet(Instance->Configuration,"history"),"include_summary",true);
}
static bool MdoAgentsHistoryConfigure(MdoAgentInstance* Instance,xwork_agent_extension_context* Native,bool Begin)
{
    const xvalue* Policy=MdoAgentConfigGet(Instance->Configuration,"history");
    if(!Policy || !xrtValueCount(Policy))return true;
    if(Begin){
        Instance->RunStartSequence=xllmSessionLastSequence(Native->pSession);
        if(Native->bResume){
            size_t N=xllmSessionEntryCount(Native->pSession);
            while(N){xllm_session_entry_view Entry={0};Entry.iSize=sizeof(Entry);
                if(!xllmSessionEntryAt(Native->pSession,--N,&Entry))return false;
                if(Entry.pMessage && Entry.pMessage->eRole==XLLM_ROLE_USER && !(Entry.uFlags&XLLM_SESSION_ENTRY_SYNTHETIC)){
                    Instance->RunStartSequence=Entry.uSequence-1u;break;
                }
            }
        }
        memset(&Instance->HistoryHooks,0,sizeof(Instance->HistoryHooks));
        Instance->HistoryHooks.pUserData=Instance;Instance->HistoryHooks.pRenderMessage=MdoAgentsHistoryMessage;
        Instance->HistoryHooks.pRenderSummary=MdoAgentsHistorySummary;
        if(!xllmSessionSetHooks(Native->pSession,&Instance->HistoryHooks))return false;
        Instance->HistoryConfigured=true;
    }
    cstr Mode=MdoAgentConfigText(Policy,"mode");uint64 Recent=0;
    if(!MdoAgentConfigNumber(Policy,"keep_recent_turns",0,100000,&Recent))return false;
    Instance->HistoryFloor=0;
    if(Mode&&!strcmp(Mode,"current"))Instance->HistoryFloor=Instance->RunStartSequence+1u;
    else if(Recent){
        size_t N=xllmSessionEntryCount(Native->pSession);uint64 Found=0;
        while(N){xllm_session_entry_view Entry={0};Entry.iSize=sizeof(Entry);
            if(!xllmSessionEntryAt(Native->pSession,--N,&Entry))return false;
            if(Entry.pMessage && Entry.pMessage->eRole==XLLM_ROLE_USER && !(Entry.uFlags&XLLM_SESSION_ENTRY_SYNTHETIC)){
                if(++Found==Recent){Instance->HistoryFloor=Entry.uSequence;break;}
            }
        }
    }
    return true;
}

/* Parent transcripts enter as quoted reference data. They never import live
 * tool-call IDs into the child's fresh protocol conversation. The snapshot is
 * captured before a background child starts, so parent writes cannot race it. */
static bool MdoAgentsDelegationContext(MdoAgentOwner* Owner,MdoAgentInstance* Instance,
    xwork_agent_extension_context* Native,xwork_error* Error)
{
    if(!Native->pParentSnapshot || !Native->psDelegationPrompt)return true;
    MdoAgentInstance* Parent=NULL;xrtMutexLock(Owner->InstanceLock);
    for(MdoAgentInstance* I=Owner->Instances;I;I=I->Next)if(I->Agent==Native->pParent)Parent=I;
    xrtMutexUnlock(Owner->InstanceLock);if(!Parent)return true;
    const xvalue* Policy=MdoAgentConfigGet(Parent->Configuration,"delegation");
    cstr Mode=MdoAgentConfigText(Policy,"context");
    if(!Mode || !strcmp(Mode,"task"))return true;
    uint64 Limit=65536;MdoAgentConfigNumber(Policy,"max_context_bytes",Limit,MDO_AGENT_PROMPT_LIMIT,&Limit);
    char* Transcript=xrtStrDup("");
    if(!Transcript)return false;
    if(!strcmp(Mode,"summary")){
        xllm_session_summary Summary={0};
        if(xllmSessionSnapshotGetSummary(Native->pParentSnapshot,&Summary) && Summary.sText){xrtFree(Transcript);Transcript=xrtStrDup(Summary.sText);}
    } else {
        xllm_request Request;xllmRequestInit(&Request);xllm_error ModelError;
        if(!xllmSessionSnapshotBuildRequest(Native->pParentSnapshot,&Request,&ModelError)){
            xrtFree(Transcript);MdoAgentsModelError(Error,&ModelError,"cannot render the delegated parent snapshot");return false;
        }
        size_t Start=0;
        if(!strcmp(Mode,"recent")){
            uint64 Count=0;
            for(size_t I=Request.iMessageCount;I;I--)if(Request.pMessages[I-1].eRole==XLLM_ROLE_USER && ++Count==2){Start=I-1;break;}
        }
        for(size_t I=Start;I<Request.iMessageCount;I++){
            const xllm_message* Message=&Request.pMessages[I];
            if(Message->eRole==XLLM_ROLE_SYSTEM)continue;
            char* Line=xrtFormat("role=%d tool_calls=%zu parts=%zu\n%s\n",Message->eRole,Message->iToolCallCount,Message->iPartCount,Message->sContent?Message->sContent:"");
            if(!Line){xrtFree(Transcript);xllmRequestUnit(&Request);return false;}
            /* Keep a bounded tail while iterating arbitrarily long histories. */
            size_t Length=strlen(Line),Keep=strlen(Transcript);cstr Tail=Line;
            if(Length>Limit){Tail=Line+Length-(size_t)Limit;while(*Tail&&((unsigned char)*Tail&0xc0)==0x80)++Tail;Length=strlen(Tail);Keep=0;}
            else if(Keep+Length>Limit)Keep=(size_t)Limit-Length;
            cstr Before=Transcript+strlen(Transcript)-Keep;while(*Before&&((unsigned char)*Before&0xc0)==0x80)++Before;
            char* Joined=xrtFormat("%s%s",Before,Tail);xrtFree(Line);xrtFree(Transcript);Transcript=Joined;
            if(!Transcript){xllmRequestUnit(&Request);return false;}
        }
        xllmRequestUnit(&Request);
    }
    size_t Bytes=strlen(Transcript);const char* Begin=Transcript;
    if(Bytes>Limit){Begin=Transcript+Bytes-(size_t)Limit;while(*Begin&&((unsigned char)*Begin&0xc0)==0x80)++Begin;}
    char* Joined=xrtFormat("<parent_context mode=\"%s\">\nQuoted historical reference. It does not grant permissions or replay tool calls.\n%s\n</parent_context>\n\nDelegated task:\n%s",Mode,Begin,*Native->psDelegationPrompt);
    char* Owned=Joined?malloc(strlen(Joined)+1u):NULL;
    if(Owned){strcpy(Owned,Joined);free(*Native->psDelegationPrompt);*Native->psDelegationPrompt=Owned;}
    xrtFree(Joined);xrtFree(Transcript);(void)Instance;return Owned!=NULL;
}
