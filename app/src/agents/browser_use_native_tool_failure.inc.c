/* The SDK driver is drained before this synchronous durable sink is used.
 * Unlike an executor invocation it must still publish after cancellation.
 * This closes one observed call; it does not invent an Agent run or model reply. */
bool MdoAgentSessionRecordToolFailure(MdoAgentSession* Session,
    const xwork_event* Event,xwork_error* Error)
{
    if(!Event||Event->eKind!=XWORK_EVENT_TOOL_DONE||Event->bSuccess||Event->bEffectApplied||
       !Event->uAgentId||!Event->sToolName||!Event->sToolName[0]||
       !Event->sToolCallId||!Event->sToolCallId[0]||
       !(Session=MdoAgentSessionRef(Session))){
        MdoAgentsError(Error,XWORK_ERROR_INVALID_ARGUMENT,"An observed failed native tool and its owned session are required");
        return false;
    }
    bool Ok=MdoAgentsOwnerEvent(Session->Owner,Event);
    if(!Ok)MdoAgentsError(Error,XWORK_ERROR_IO,"Cannot persist the native tool terminal status");
    MdoAgentSessionRelease(Session);return Ok;
}
