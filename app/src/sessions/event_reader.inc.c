/* Read-only, size-snapshotted JSONL cursor. Memory is bounded by one record,
 * even when a conversation journal grows beyond the former 16 MiB limit.
 * Incomplete tails are never committed records; every seek uses the anchored
 * Home handle, so pagination does not reopen an untrusted filesystem path. */
typedef struct MdoEventReader {
    xfile File;
    uint64 Size;
    uint64 Position;
    xfileinfo Info;
    bool Failed;
    char Block[8192];
    char Record[MDO_SESSION_EVENT_RECORD_LIMIT + 1u];
} MdoEventReader;

static MdoEventReader* MdoEventReaderOpen(const char* Path)
{
    MdoEventReader* Reader = (MdoEventReader*)xrtCalloc(1u, sizeof(*Reader));
    xfileinfo Info;
    bool Exists = false;
    if ( Reader == NULL ) return NULL;
    if ( !MdoHomeExternalStat(Path, &Exists, &Info) ) goto fail;
    if ( !Exists ) return Reader;
    Reader->File = MdoHomeOpenRead(Path);
    if ( Reader->File == NULL || !xrtFileStat(Reader->File, &Info) ||
         Info.Type != XFILE_TYPE_FILE || !(Info.Available & XFILE_INFO_SIZE) ||
         Info.Size > INT64_MAX ) goto fail;
    Reader->Size = Info.Size;
    Reader->Info = Info;
    return Reader;
fail:
    if ( Reader->File != NULL ) xrtClose(Reader->File);
    xrtFree(Reader);
    return NULL;
}

static void MdoEventReaderClose(MdoEventReader* Reader)
{
    if ( Reader == NULL ) return;
    if ( Reader->File != NULL ) xrtClose(Reader->File);
    xrtFree(Reader);
}

static bool MdoEventReaderRead(MdoEventReader* Reader, uint64 At,
    void* Buffer, size_t Size)
{
    if ( Size == 0u ) return true;
    if ( At > Reader->Size || Size > Reader->Size - At ||
         !xrtSeek(Reader->File, (int64)At, XSEEK_START, NULL) ||
         !xrtReadFull(Reader->File, Buffer, Size, NULL) ) {
        Reader->Failed = true;
        return false;
    }
    return true;
}

/* Find the last newline strictly before End. No match is an ordinary BOF. */
static bool MdoEventReaderNewline(MdoEventReader* Reader, uint64 End,
    uint64* At)
{
    while ( End != 0u ) {
        size_t Size = End < sizeof(Reader->Block) ? (size_t)End : sizeof(Reader->Block);
        uint64 Start = End - Size;
        size_t i;
        if ( !MdoEventReaderRead(Reader, Start, Reader->Block, Size) ) return false;
        for ( i = Size; i != 0u; --i ) {
            if ( Reader->Block[i - 1u] == '\n' ) {
                *At = Start + i - 1u;
                return true;
            }
        }
        End = Start;
    }
    return false;
}

static bool MdoEventReaderNext(MdoEventReader* Reader, xstrview* Record)
{
    uint64 Start = Reader->Position;
    size_t Used = 0u;
    bool Oversized = false;
    while ( Reader->Position < Reader->Size ) {
        uint64 Left = Reader->Size - Reader->Position;
        size_t Size = Left < sizeof(Reader->Block) ? (size_t)Left : sizeof(Reader->Block);
        const char* End;
        size_t Length;
        if ( !MdoEventReaderRead(Reader, Reader->Position, Reader->Block, Size) ) return false;
        End = (const char*)memchr(Reader->Block, '\n', Size);
        Length = End != NULL ? (size_t)(End - Reader->Block) : Size;
        if ( Length > MDO_SESSION_EVENT_RECORD_LIMIT - Used ) Oversized = true;
        if ( !Oversized ) {
            memcpy(Reader->Record + Used, Reader->Block, Length);
            Used += Length;
        }
        Reader->Position += Length + (End != NULL ? 1u : 0u);
        if ( End != NULL ) {
            Reader->Record[Used] = '\0';
            *Record = xrtStrViewN(Reader->Record, Oversized ? 0u : Used);
            return true;
        }
    }
    /* Retain the incomplete tail's offset for recovery diagnostics. */
    Reader->Position = Start;
    return false;
}

static bool MdoEventReaderPrevious(MdoEventReader* Reader, uint64* End,
    xstrview* Record)
{
    uint64 Last;
    uint64 Newline;
    uint64 Start;
    uint64 Length;
    if ( *End == 0u || !MdoEventReaderNewline(Reader, *End, &Last) ) return false;
    Start = MdoEventReaderNewline(Reader, Last, &Newline) ? Newline + 1u : 0u;
    if ( Reader->Failed ) return false;
    Length = Last - Start;
    *End = Start;
    if ( Length > MDO_SESSION_EVENT_RECORD_LIMIT ) {
        *Record = xrtStrViewN(Reader->Record, 0u);
        return true;
    }
    if ( !MdoEventReaderRead(Reader, Start, Reader->Record, (size_t)Length) ) return false;
    Reader->Record[Length] = '\0';
    *Record = xrtStrViewN(Reader->Record, (size_t)Length);
    return true;
}

static uint64 MdoEventReaderLatest(MdoEventReader* Reader,
    const char* Project, const char* Session, bool* Lost)
{
    uint64 End = Reader->Size;
    xstrview Record;
    while ( MdoEventReaderPrevious(Reader, &End, &Record) ) {
        MdoSessionEventOwned Event;
        if ( MdoEventsParse(Project, Session, Record, &Event) ) {
            uint64 Id = Event.Info.EventId;
            MdoEventsOwnedUnit(&Event);
            return Id;
        }
        *Lost = true;
        xrtClearError();
    }
    return 0u;
}

/* Event IDs are increasing, including intentional edit/clear gaps. Binary
 * search byte offsets, then finish from a known record boundary. A malformed
 * probe falls back to a forward scan rather than guessing its identity. */
static void MdoEventReaderAfter(MdoEventReader* Reader, const char* Project,
    const char* Session, uint64 After)
{
    uint64 Low = 0u, High = Reader->Size;
    while ( After != 0u && High > Low && High - Low > 2u * MDO_SESSION_EVENT_RECORD_LIMIT ) {
        uint64 Middle = Low + (High - Low) / 2u;
        xstrview Record;
        MdoSessionEventOwned Event;
        Reader->Position = Middle;
        if ( !MdoEventReaderNext(Reader, &Record) ||
             !MdoEventReaderNext(Reader, &Record) ) { High = Middle; continue; }
        if ( !MdoEventsParse(Project, Session, Record, &Event) ) {
            xrtClearError();
            break;
        }
        if ( Event.Info.EventId <= After ) Low = Reader->Position;
        else High = Middle;
        MdoEventsOwnedUnit(&Event);
    }
    Reader->Position = Low;
}
