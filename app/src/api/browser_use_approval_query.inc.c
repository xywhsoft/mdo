/* Select before applying the four-item response budget. Unrelated pending
 * sessions must not hide this conversation. Unscoped callers keep the original
 * global endpoint; malformed/partial/duplicate scopes are never broadened. */
static bool BuApprovalQuery(xstrview Query, char* Project, char* Session)
{
    size_t Position = 0u;
    unsigned Seen = 0u;
    Project[0] = Session[0] = '\0';
    while ( Position < Query.Size ) {
        size_t Start = Position, Equal = SIZE_MAX, End;
        while ( Position < Query.Size && Query.Data[Position] != '&' ) {
            if ( Query.Data[Position] == '=' && Equal == SIZE_MAX ) Equal = Position;
            ++Position;
        }
        End = Position;
        if ( Equal == SIZE_MAX || Equal == Start || Equal + 1u == End ) return false;
        char* Output = NULL;
        size_t Capacity = 0u;
        unsigned Bit = 0u;
        if ( Equal - Start == 10u && !memcmp(Query.Data + Start, "project_id", 10u) ) {
            Output = Project; Capacity = MDO_APPROVAL_PROJECT_CAPACITY; Bit = 1u;
        } else if ( Equal - Start == 10u && !memcmp(Query.Data + Start, "session_id", 10u) ) {
            Output = Session; Capacity = MDO_APPROVAL_SESSION_CAPACITY; Bit = 2u;
        } else return false;
        size_t Length = End - Equal - 1u;
        if ( (Seen & Bit) || Length >= Capacity ) return false;
        for ( size_t I = 0u; I < Length; ++I ) {
            unsigned char C = (unsigned char)Query.Data[Equal + 1u + I];
            if ( !((C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') ||
                   (C >= '0' && C <= '9') || C == '_' || C == '-' || (C == '.' && I)) ) return false;
        }
        memcpy(Output, Query.Data + Equal + 1u, Length); Output[Length] = '\0';
        Seen |= Bit;
        if ( Position < Query.Size && ++Position == Query.Size ) return false;
    }
    return Seen == 0u || Seen == 3u;
}
