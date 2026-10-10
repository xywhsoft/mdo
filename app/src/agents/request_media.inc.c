/* Project tool media at the model boundary. The durable ledger remains the
 * source of truth, so a later vision route can still inspect the original. */
static bool MdoAgentsMessageHasImage(const xllm_message* Message)
{
    size_t i;
    for ( i = 0u; i < Message->iPartCount; ++i )
        if ( Message->pParts[i].eKind == XLLM_PART_IMAGE ) return true;
    return false;
}

static bool MdoAgentsToolImageText(const xllm_message* Source,
    xllm_message* Target)
{
    size_t i;
    xllm_part Part;
    char Notice[1024];
    char* Plain;
    size_t Bytes = 1u, Offset = 0u;
    xllmMessageInit(Target, Source->eRole);
    if ( Source->sReasoningContent &&
         !xllmMessageSetReasoning(Target, Source->sReasoningContent) ) goto fail;
    if ( Source->sToolCallId &&
         !xllmMessageSetToolCallId(Target, Source->sToolCallId) ) goto fail;
    if ( Source->sNative &&
         !xllmMessageSetNative(Target, Source->sNative) ) goto fail;
    for ( i = 0u; i < Source->iToolCallCount; ++i ) {
        const xllm_tool_call* Call = &Source->pToolCalls[i];
        if ( !xllmMessageAddToolCall(Target, Call->sId, Call->sName,
                Call->sArgumentsJson) ) goto fail;
    }
    for ( i = 0u; i < Source->iPartCount; ++i ) {
        const xllm_part* Original = &Source->pParts[i];
        if ( Original->eKind != XLLM_PART_IMAGE ) {
            if ( !xllmMessageAddPart(Target, Original) ) goto fail;
            continue;
        }
        snprintf(Notice, sizeof(Notice),
            "[Tool returned an image (%s, %zu bytes). "
            "This model cannot view images. The original image is saved in "
            "the conversation. Do not infer its visual contents; read source "
            "text or use a model with image input when needed.]",
            Original->sMediaType ? Original->sMediaType : "unknown type",
            Original->iDataSize);
        xllmPartInit(&Part, XLLM_PART_TEXT);
        if ( !xllmPartSetText(&Part, Notice) ||
             !xllmMessageAddPart(Target, &Part) ) {
            xllmPartUnit(&Part);
            goto fail;
        }
        xllmPartUnit(&Part);
        if ( Original->sSourceUrl != NULL ) {
            xllmPartInit(&Part, XLLM_PART_TEXT);
            if ( !xllmPartSetText(&Part, Original->sSourceUrl) ||
                 !xllmMessageAddPart(Target, &Part) ) {
                xllmPartUnit(&Part);
                goto fail;
            }
            xllmPartUnit(&Part);
        }
    }
    /* Text-only tool results use the ordinary string wire shape, including
     * providers that accept multimodal arrays only for user messages. */
    for ( i = 0u; i < Target->iPartCount; ++i ) {
        size_t Length;
        if ( Target->pParts[i].eKind != XLLM_PART_TEXT ) return true;
        Length = Target->pParts[i].sText ? strlen(Target->pParts[i].sText) : 0u;
        if ( Length >= SIZE_MAX - Bytes ) goto fail;
        Bytes += Length + 1u;
    }
    Plain = (char*)malloc(Bytes);
    if ( Plain == NULL ) goto fail;
    for ( i = 0u; i < Target->iPartCount; ++i ) {
        const char* Text = Target->pParts[i].sText;
        size_t Length = Text ? strlen(Text) : 0u;
        if ( i != 0u ) Plain[Offset++] = '\n';
        if ( Length != 0u ) memcpy(Plain + Offset, Text, Length);
        Offset += Length;
    }
    Plain[Offset] = '\0';
    if ( !xllmMessageSetContent(Target, Plain) ) {
        free(Plain);
        goto fail;
    }
    free(Plain);
    return true;
fail:
    xllmMessageUnit(Target);
    return false;
}

/* Storage owns only the replacement array and transformed messages. View
 * borrows every other request field, including cancellation and hooks. */
static bool MdoAgentsProjectImages(const xllm_request* Request,
    const xllm_model_profile* Profile, xllm_request* Storage,
    xllm_request* View, const xllm_request** Projected, xllm_error* Error)
{
    size_t i;
    bool NeedsProjection = false;
    xllmRequestInit(Storage);
    *Projected = Request;
    if ( (Profile->uCapabilities & XLLM_CAP_IMAGE_IN) != 0u ) return true;
    for ( i = 0u; i < Request->iMessageCount; ++i ) {
        const xllm_message* Message = &Request->pMessages[i];
        if ( !MdoAgentsMessageHasImage(Message) ) continue;
        if ( Message->eRole != XLLM_ROLE_TOOL ) {
            MdoModelProfileErrorInit(Error, "profile_image_unsupported",
                "the selected model cannot view images submitted in the conversation");
            return false;
        }
        NeedsProjection = true;
    }
    if ( !NeedsProjection ) return true;
    for ( i = 0u; i < Request->iMessageCount; ++i ) {
        const xllm_message* Message = &Request->pMessages[i];
        if ( MdoAgentsMessageHasImage(Message) ) {
            xllm_message Text;
            bool Added;
            if ( !MdoAgentsToolImageText(Message, &Text) ) goto oom;
            Added = xllmRequestAddMessage(Storage, &Text);
            xllmMessageUnit(&Text);
            if ( !Added ) goto oom;
        } else if ( !xllmRequestAddMessageView(Storage, Message) ) goto oom;
    }
    *View = *Request;
    View->pMessages = Storage->pMessages;
    View->iMessageCount = Storage->iMessageCount;
    View->iMessageCap = Storage->iMessageCap;
    View->pbMessageBorrowed = Storage->pbMessageBorrowed;
    /* A projected message must never reuse a serialization of its image. */
    View->pStablePrefixOwner = NULL;
    View->uStablePrefixStamp = 0u;
    View->iStableMessages = 0u;
    *Projected = View;
    return true;
oom:
    if ( Error != NULL ) {
        xllmErrorInit(Error);
        Error->eCode = XLLM_ERROR_OUT_OF_MEMORY;
        snprintf(Error->sMessage, sizeof(Error->sMessage), "%s",
            "cannot prepare tool image text for the selected model");
    }
    return false;
}
