#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <xllm-session.h>

#include "backup_internal.h"

/* Snapshot and journal share one file-format boundary, not a model invocation.
 * Strings remain borrowed from one bounded JSON tree. Config validation uses
 * an unbound, in-memory session; no paths, hooks or client are attached. */
static bool MdoBackupModelEqual(xstrview Text, const char* Literal)
{
    return Text.Size == strlen(Literal) && memcmp(Text.Data, Literal, Text.Size) == 0;
}

static const xvalue* MdoBackupModelGet(const xvalue* Root, const char* Key)
{
    return xrtValueObjectGet(Root, xrtStrView(Key));
}

static bool MdoBackupModelKeys(const xvalue* Root, const char* const* Keys, size_t Count)
{
    size_t i, j;
    if ( xrtValueType(Root) != XVALUE_OBJECT ) return false;
    for ( i = 0u; i < xrtValueCount(Root); ++i ) {
        xstrview Key;
        (void)xrtValueObjectAt(Root, i, &Key);
        for ( j = 0u; j < Count; ++j ) if ( MdoBackupModelEqual(Key, Keys[j]) ) break;
        if ( j == Count ) return false;
    }
    return true;
}

static bool MdoBackupModelText(const xvalue* Root, const char* Key, bool Optional, bool Nullable)
{
    const xvalue* Value = MdoBackupModelGet(Root, Key);
    xstrview Text;
    if ( Value == NULL ) return Optional;
    if ( Nullable && xrtValueType(Value) == XVALUE_NULL ) return true;
    return xrtValueGetString(Value, &Text) && memchr(Text.Data, 0, Text.Size) == NULL;
}

static bool MdoBackupModelUInt(const xvalue* Root, const char* Key, uint64* Number, bool Optional)
{
    return MdoBackupModelGet(Root, Key) == NULL ? Optional : MdoBackupUInt(Root, Key, Number);
}

static bool MdoBackupModelNumber(const xvalue* Value, double* Number)
{
    int64 Signed;
    uint64 Unsigned;
    if ( xrtValueType(Value) == XVALUE_FLOAT ) return xrtValueGetFloat(Value, Number);
    if ( xrtValueType(Value) == XVALUE_UINT ) {
        if ( !xrtValueGetUInt(Value, &Unsigned) ) return false;
        *Number = (double)Unsigned;
    } else {
        if ( !xrtValueGetInt(Value, &Signed) ) return false;
        *Number = (double)Signed;
    }
    return true;
}

static bool MdoBackupModelInvalid(xwork_error* Error, const char* Path)
{
    return MdoBackupError(Error, XWORK_ERROR_IO, "invalid model ledger schema or checksum", Path);
}

static bool MdoBackupModelChecksum(const MdoBackupOwnedFile* File,
    const MdoSessionBackupLimits* Limits, const xcancel* Cancel, xwork_error* Error)
{
    /* CRC-32/ISO-HDLC covers the exact prefix before the final member, as in
     * xllm-session v3. Never verify a reserialized tree or another key order.
     * A fixed nibble table avoids a per-byte bit loop and mutable lazy state. */
    static const uint32 Table[16] = {
        0x00000000u, 0x1db71064u, 0x3b6e20c8u, 0x26d930acu,
        0x76dc4190u, 0x6b6b51f4u, 0x4db26158u, 0x5005713cu,
        0xedb88320u, 0xf00f9344u, 0xd6d6a3e8u, 0xcb61b38cu,
        0x9b64c2b0u, 0x86d3d2d4u, 0xa00ae278u, 0xbdbdf21cu
    };
    static const char Marker[] = ",\"checksum\":\"";
    const size_t Trailer = sizeof(Marker) - 1u + 10u;
    uint32 Crc = UINT32_MAX, Expected = 0u;
    size_t Prefix, i, End;
    if ( File->Bytes < Trailer ) return MdoBackupModelInvalid(Error, File->Path);
    Prefix = File->Bytes - Trailer;
    if ( memcmp(File->Data + Prefix, Marker, sizeof(Marker) - 1u) != 0 ||
         memcmp(File->Data + File->Bytes - 2u, "\"}", 2u) != 0 ) return MdoBackupModelInvalid(Error, File->Path);
    for ( i = 0u; i < 8u; ++i ) {
        unsigned char Ch = (unsigned char)File->Data[Prefix + sizeof(Marker) - 1u + i];
        unsigned Nibble;
        if ( Ch >= '0' && Ch <= '9' ) Nibble = Ch - '0';
        else if ( Ch >= 'a' && Ch <= 'f' ) Nibble = Ch - 'a' + 10u;
        else if ( Ch >= 'A' && Ch <= 'F' ) Nibble = Ch - 'A' + 10u;
        else return MdoBackupModelInvalid(Error, File->Path);
        Expected = (Expected << 4u) | Nibble;
    }
    for ( i = 0u; i < Prefix; ) {
        if ( !MdoBackupCheck(Limits, Cancel, Error) ) return false;
        End = Prefix - i > 65536u ? i + 65536u : Prefix;
        for ( ; i < End; ++i ) {
            Crc ^= (uint8)File->Data[i];
            Crc = (Crc >> 4u) ^ Table[Crc & 15u];
            Crc = (Crc >> 4u) ^ Table[Crc & 15u];
        }
    }
    return (Crc ^ UINT32_MAX) == Expected || MdoBackupModelInvalid(Error, File->Path);
}

typedef struct MdoBackupModelConfigField { const char* Key; size_t Offset, Bytes; } MdoBackupModelConfigField;
#define MDO_SNAPSHOT_FIELD(Key, Member) {Key, offsetof(xllm_session_config, Member), \
    sizeof(((xllm_session_config*)0)->Member)}

static bool MdoBackupModelConfig(const xvalue* Root, xwork_error* Error)
{
    static const MdoBackupModelConfigField Fields[] = {
        MDO_SNAPSHOT_FIELD("context_window_tokens", uContextWindowTokens),
        MDO_SNAPSHOT_FIELD("max_output_tokens", uMaxOutputTokens),
        MDO_SNAPSHOT_FIELD("output_reserve_tokens", uOutputReserveTokens),
        MDO_SNAPSHOT_FIELD("safety_reserve_tokens", uSafetyReserveTokens),
        MDO_SNAPSHOT_FIELD("recent_turns_to_keep", uRecentTurnsToKeep),
        MDO_SNAPSHOT_FIELD("tool_prune_bytes", uToolPruneBytes),
        MDO_SNAPSHOT_FIELD("summary_max_tokens", uSummaryMaxTokens),
        MDO_SNAPSHOT_FIELD("summary_min_tokens", uSummaryMinTokens),
        MDO_SNAPSHOT_FIELD("compaction_required_sections", uCompactionRequiredSections),
        MDO_SNAPSHOT_FIELD("keep_recent_tokens", uKeepRecentTokens),
        MDO_SNAPSHOT_FIELD("summary_max_bytes", uSummaryMaxBytes),
        MDO_SNAPSHOT_FIELD("user_message_cap_bytes", uUserMessageCapBytes),
        MDO_SNAPSHOT_FIELD("tool_result_cap_bytes", uToolResultCapBytes),
        MDO_SNAPSHOT_FIELD("tool_result_total_cap_bytes", uToolResultTotalCapBytes),
        MDO_SNAPSHOT_FIELD("journal_max_bytes", uJournalMaxBytes),
        MDO_SNAPSHOT_FIELD("max_input_tokens", uMaxInputTokens)
    };
    static const char* const Extra[] = {"prune_trigger", "compact_trigger",
        "window_mode", "journal_durability", "summary_style"};
    const char* Keys[sizeof(Fields) / sizeof(Fields[0]) + sizeof(Extra) / sizeof(Extra[0])];
    xllm_session_config Config;
    xllm_session* Session;
    xllm_error Cause;
    uint64 Number;
    xstrview Style;
    size_t i, Count = sizeof(Fields) / sizeof(Fields[0]);
    xllmSessionConfigInit(&Config);
    for ( i = 0u; i < Count; ++i ) Keys[i] = Fields[i].Key;
    for ( i = 0u; i < sizeof(Extra) / sizeof(Extra[0]); ++i ) Keys[Count + i] = Extra[i];
    if ( Root != NULL && !MdoBackupModelKeys(Root, Keys, sizeof(Keys) / sizeof(Keys[0])) ) goto invalid;
    for ( i = 0u; Root != NULL && i < Count; ++i ) {
        if ( MdoBackupModelGet(Root, Fields[i].Key) == NULL ) continue;
        if ( !MdoBackupUInt(Root, Fields[i].Key, &Number) ) goto invalid;
        if ( Fields[i].Bytes == sizeof(uint32) ) {
            uint32 Small;
            if ( Number > UINT32_MAX ) goto invalid;
            Small = (uint32)Number;
            memcpy((char*)&Config + Fields[i].Offset, &Small, sizeof(Small));
        } else if ( Fields[i].Bytes == sizeof(Number) )
            memcpy((char*)&Config + Fields[i].Offset, &Number, sizeof(Number));
        else goto invalid;
    }
    if ( Root != NULL ) {
        const xvalue* Value = MdoBackupModelGet(Root, "prune_trigger");
        if ( Value != NULL && !MdoBackupModelNumber(Value, &Config.fPruneTrigger) ) goto invalid;
        Value = MdoBackupModelGet(Root, "compact_trigger");
        if ( Value != NULL && !MdoBackupModelNumber(Value, &Config.fCompactTrigger) ) goto invalid;
        Number = Config.eWindowMode;
        if ( !MdoBackupModelUInt(Root, "window_mode", &Number, true) || Number > XLLM_WINDOW_SPLIT_INPUT_OUTPUT ) goto invalid;
        Config.eWindowMode = (xllm_window_mode)Number;
        Number = Config.eJournalDurability;
        if ( !MdoBackupModelUInt(Root, "journal_durability", &Number, true) || Number > XLLM_SESSION_DURABILITY_FLUSH ) goto invalid;
        Config.eJournalDurability = (xllm_session_durability)Number;
        if ( !MdoBackupModelText(Root, "summary_style", true, true) ) goto invalid;
        if ( MdoBackupView(Root, "summary_style", &Style) ) Config.sSummaryStyle = Style.Data;
    }
    Session = xllmSessionCreate(&Config, &Cause);
    if ( Session == NULL ) return MdoBackupError(Error, Cause.eCode == XLLM_ERROR_OUT_OF_MEMORY ?
        XWORK_ERROR_OUT_OF_MEMORY : XWORK_ERROR_IO, "unsupported model snapshot configuration", "snapshot.json");
    xllmSessionDestroy(Session);
    return true;
invalid:
    return MdoBackupModelInvalid(Error, "snapshot.json");
}

#undef MDO_SNAPSHOT_FIELD

static bool MdoBackupModelEntry(const xvalue* Entry, uint64 CurrentTurn, uint64 Next, uint64* Sequence,
    const MdoSessionBackupLimits* Limits, const xcancel* Cancel, xwork_error* Error, const char* Path)
{
    static const char* const Keys[] = {"sequence", "turn", "flags", "role", "content",
        "reasoning", "tool_call_id", "tool_calls"};
    static const char* const CallKeys[] = {"id", "name", "arguments"};
    const xvalue* Calls;
    uint64 Turn, Flags = 0u, Role;
    size_t i;
    if ( !MdoBackupModelKeys(Entry, Keys, sizeof(Keys) / sizeof(Keys[0])) ||
         !MdoBackupUInt(Entry, "sequence", Sequence) || *Sequence == 0u || *Sequence >= Next ||
         !MdoBackupUInt(Entry, "turn", &Turn) || Turn > CurrentTurn ||
         !MdoBackupModelUInt(Entry, "flags", &Flags, true) ||
         (Flags & ~(uint64)(XLLM_SESSION_ENTRY_PINNED | XLLM_SESSION_ENTRY_SYNTHETIC)) != 0u ||
         !MdoBackupUInt(Entry, "role", &Role) || Role > XLLM_ROLE_TOOL ||
         !MdoBackupModelText(Entry, "content", true, true) ||
         !MdoBackupModelText(Entry, "reasoning", true, true) ||
         !MdoBackupModelText(Entry, "tool_call_id", true, true) ) return MdoBackupModelInvalid(Error, Path);
    Calls = MdoBackupModelGet(Entry, "tool_calls");
    if ( Calls == NULL ) return true;
    if ( xrtValueType(Calls) != XVALUE_ARRAY ) return MdoBackupModelInvalid(Error, Path);
    for ( i = 0u; i < xrtValueCount(Calls); ++i ) {
        const xvalue* Call = xrtValueArrayGet(Calls, i);
        xstrview Name;
        if ( !MdoBackupCheck(Limits, Cancel, Error) ) return false;
        /* Arguments are opaque persisted model output, including a malformed
         * call that produced a tool error. Validation must not rewrite it. */
        if ( !MdoBackupModelKeys(Call, CallKeys, sizeof(CallKeys) / sizeof(CallKeys[0])) ||
             !MdoBackupModelText(Call, "id", true, true) ||
             !MdoBackupModelText(Call, "name", false, false) ||
             !MdoBackupView(Call, "name", &Name) || Name.Size == 0u ||
             !MdoBackupModelText(Call, "arguments", true, true) ) return MdoBackupModelInvalid(Error, Path);
    }
    return true;
}

static bool MdoBackupModelFiles(const xvalue* Root, const char* NamesKey, const char* SequencesKey,
    uint64 Next, const MdoSessionBackupLimits* Limits, const xcancel* Cancel, xwork_error* Error)
{
    const xvalue* Names = MdoBackupModelGet(Root, NamesKey);
    const xvalue* Sequences = MdoBackupModelGet(Root, SequencesKey);
    size_t i;
    if ( Names == NULL && Sequences == NULL ) return true;
    if ( xrtValueType(Names) != XVALUE_ARRAY || (Sequences != NULL &&
         (xrtValueType(Sequences) != XVALUE_ARRAY || xrtValueCount(Names) != xrtValueCount(Sequences))) )
        return MdoBackupModelInvalid(Error, "snapshot.json");
    for ( i = 0u; i < xrtValueCount(Names); ++i ) {
        xstrview Text;
        uint64 Sequence;
        int64 Signed;
        const xvalue* Value = Sequences != NULL ? xrtValueArrayGet(Sequences, i) : NULL;
        if ( !MdoBackupCheck(Limits, Cancel, Error) ) return false;
        if ( !xrtValueGetString(xrtValueArrayGet(Names, i), &Text) || Text.Size == 0u ||
             memchr(Text.Data, 0, Text.Size) != NULL ) return MdoBackupModelInvalid(Error, "snapshot.json");
        if ( Value == NULL ) continue;
        if ( xrtValueType(Value) == XVALUE_UINT ) {
            if ( !xrtValueGetUInt(Value, &Sequence) ) return MdoBackupModelInvalid(Error, "snapshot.json");
        } else {
            if ( !xrtValueGetInt(Value, &Signed) || Signed < 0 ) return MdoBackupModelInvalid(Error, "snapshot.json");
            Sequence = (uint64)Signed;
        }
        if ( Sequence >= Next ) return MdoBackupModelInvalid(Error, "snapshot.json");
    }
    return true;
}

static bool MdoBackupModelSnapshotValidate(const MdoBackupOwnedFile* File, const xvalue* Root,
    const MdoSessionBackupLimits* Limits, const xcancel* Cancel, xwork_error* Error)
{
    static const char* const Keys[] = {"format", "version", "config", "next_sequence",
        "checkpoint_sequence", "journal_sequence", "current_turn", "compacted_through",
        "compaction_count", "summary_generation", "summary_prompt_at_birth", "summary_output_at_birth",
        "tail_floor", "fill_seen", "summary", "read_files", "read_file_sequences", "modified_files",
        "modified_file_sequences", "entries", "checksum"};
    static const char* const Counters[] = {"checkpoint_sequence", "journal_sequence", "compaction_count",
        "summary_prompt_at_birth", "summary_output_at_birth"};
    const xvalue* Entries;
    xstrview Format;
    uint64 Version, Next, CurrentTurn, Last = 0u, Number = 0u;
    size_t i;
    if ( !MdoBackupModelKeys(Root, Keys, sizeof(Keys) / sizeof(Keys[0])) ||
         !MdoBackupView(Root, "format", &Format) || !MdoBackupModelEqual(Format, "xllm-session") ||
         !MdoBackupUInt(Root, "version", &Version) || Version < 1u || Version > 3u ||
         !MdoBackupUInt(Root, "next_sequence", &Next) || Next == 0u ||
         !MdoBackupUInt(Root, "current_turn", &CurrentTurn) ||
         !MdoBackupModelText(Root, "summary", true, true) ) goto invalid;
    if ( Version == 3u || MdoBackupModelGet(Root, "checksum") != NULL ) {
        if ( !MdoBackupModelChecksum(File, Limits, Cancel, Error) ) return false;
    }
    if ( (Version == 3u && MdoBackupModelGet(Root, "journal_sequence") != NULL) ||
         (Version != 3u && MdoBackupModelGet(Root, "checkpoint_sequence") != NULL) ||
         (Version == 3u && MdoBackupModelGet(Root, "config") == NULL) ) goto invalid;
    for ( i = 0u; i < sizeof(Counters) / sizeof(Counters[0]); ++i )
        if ( !MdoBackupModelUInt(Root, Counters[i], &Number, true) ) goto invalid;
    Number = 0u;
    if ( !MdoBackupModelUInt(Root, "summary_generation", &Number, true) || Number > UINT32_MAX ) goto invalid;
    Number = 0u;
    if ( !MdoBackupModelUInt(Root, "fill_seen", &Number, true) || Number > 1u ) goto invalid;
    Number = 0u;
    if ( !MdoBackupModelUInt(Root, "compacted_through", &Number, true) || Number >= Next ) goto invalid;
    Number = 0u;
    if ( !MdoBackupModelUInt(Root, "tail_floor", &Number, true) || Number >= Next ) goto invalid;
    if ( !MdoBackupModelConfig(MdoBackupModelGet(Root, "config"), Error) ) return false;
    Entries = MdoBackupModelGet(Root, "entries");
    if ( xrtValueType(Entries) != XVALUE_ARRAY ) goto invalid;
    for ( i = 0u; i < xrtValueCount(Entries); ++i ) {
        uint64 Sequence;
        if ( !MdoBackupCheck(Limits, Cancel, Error) ) return false;
        if ( !MdoBackupModelEntry(xrtValueArrayGet(Entries, i), CurrentTurn, Next, &Sequence,
                Limits, Cancel, Error, File->Path) ) return false;
        if ( Sequence <= Last ) goto invalid;
        Last = Sequence;
    }
    return MdoBackupModelFiles(Root, "read_files", "read_file_sequences", Next, Limits, Cancel, Error) &&
        MdoBackupModelFiles(Root, "modified_files", "modified_file_sequences", Next, Limits, Cancel, Error);
invalid:
    return MdoBackupModelInvalid(Error, "snapshot.json");
}

static bool MdoBackupModelJournalRecord(const MdoBackupOwnedFile* File, const xvalue* Root,
    uint64 Checkpoint, uint64* LastRecord, uint64* ReplaySequence,
    const MdoSessionBackupLimits* Limits, const xcancel* Cancel, xwork_error* Error)
{
    static const char* const Begin[] = {"turn"};
    static const char* const Message[] = {"entry"};
    static const char* const Compact[] = {"through_sequence", "generation", "usage", "compaction_count", "summary"};
    static const char* const Ledger[] = {"kind", "path", "after_sequence"};
    static const char* const Truncate[] = {"from_sequence", "to_sequence", "reason"};
    static const char* const Rewrite[] = {"through_sequence", "previous_next_sequence"};
    static const char* const UsageKeys[] = {"prompt_tokens", "output_tokens"};
    const char* Keys[10] = {"format", "version", NULL, NULL, "checksum"};
    const char* const* Payload = NULL;
    size_t Count = 0u, i;
    uint64 Version, Sequence, A = 0u, B = 0u;
    xstrview Format, Operation, Text;
    const xvalue* Usage;
    if ( !MdoBackupView(Root, "format", &Format) || !MdoBackupModelEqual(Format, "xllm-session-journal") ||
         !MdoBackupUInt(Root, "version", &Version) || Version < 1u || Version > 3u ) goto invalid;
    Keys[2] = Version == 3u ? "sequence" : "journal_sequence";
    Keys[3] = Version == 3u ? "type" : "operation";
    if ( !MdoBackupUInt(Root, Keys[2], &Sequence) || Sequence == 0u ||
         !MdoBackupView(Root, Keys[3], &Operation) ) goto invalid;
    /* Covered records still need a sound schema and CRC. Only their replay is
     * deduplicated; corrupt bytes are not made harmless by an old sequence. */
    if ( Version == 3u || MdoBackupModelGet(Root, "checksum") != NULL )
        if ( !MdoBackupModelChecksum(File, Limits, Cancel, Error) ) return false;
    if ( MdoBackupModelEqual(Operation, "begin_turn") ) { Payload = Begin; Count = 1u; }
    else if ( MdoBackupModelEqual(Operation, "add_message") ) { Payload = Message; Count = 1u; }
    else if ( MdoBackupModelEqual(Operation, "compact") ) { Payload = Compact; Count = 5u; }
    else if ( MdoBackupModelEqual(Operation, "ledger") ) { Payload = Ledger; Count = 3u; }
    else if ( MdoBackupModelEqual(Operation, "truncate") ) { Payload = Truncate; Count = 3u; }
    else if ( MdoBackupModelEqual(Operation, "rewind") || MdoBackupModelEqual(Operation, "clear") ) { Payload = Rewrite; Count = 2u; }
    else goto invalid;
    for ( i = 0u; i < Count; ++i ) Keys[5u + i] = Payload[i];
    if ( !MdoBackupModelKeys(Root, Keys, 5u + Count) ) goto invalid;
    if ( Payload == Begin ) {
        if ( !MdoBackupUInt(Root, "turn", &A) || A == 0u ) goto invalid;
    } else if ( Payload == Message ) {
        /* Context relationships (turn advancement, tool pairs, truncation and
         * summary quality) belong to actual library replay, not this reader. */
        if ( !MdoBackupModelEntry(MdoBackupModelGet(Root, "entry"), UINT64_MAX, UINT64_MAX, &A,
                Limits, Cancel, Error, File->Path) ) return false;
    } else if ( Payload == Compact ) {
        if ( !MdoBackupUInt(Root, "through_sequence", &A) || A == 0u || A == UINT64_MAX ||
             !MdoBackupUInt(Root, "compaction_count", &B) || B == 0u ||
             !MdoBackupModelText(Root, "summary", false, false) ||
             !MdoBackupView(Root, "summary", &Text) || Text.Size == 0u ) goto invalid;
        A = 0u;
        /* These additive fields also default during legacy v3 replay. */
        if ( !MdoBackupModelUInt(Root, "generation", &A, true) || A > UINT32_MAX ) goto invalid;
        Usage = MdoBackupModelGet(Root, "usage");
        if ( Usage != NULL && (!MdoBackupModelKeys(Usage, UsageKeys, 2u) ||
             !MdoBackupModelUInt(Usage, "prompt_tokens", &A, true) ||
             !MdoBackupModelUInt(Usage, "output_tokens", &B, true)) ) goto invalid;
    } else if ( Payload == Ledger ) {
        if ( !MdoBackupView(Root, "kind", &Text) ||
             (!MdoBackupModelEqual(Text, "read") && !MdoBackupModelEqual(Text, "modified")) ||
             !MdoBackupModelText(Root, "path", false, false) ||
             !MdoBackupView(Root, "path", &Text) || Text.Size == 0u ||
             !MdoBackupModelUInt(Root, "after_sequence", &A, true) || A == UINT64_MAX ) goto invalid;
    } else if ( Payload == Truncate ) {
        if ( !MdoBackupUInt(Root, "from_sequence", &A) || A == 0u ||
             !MdoBackupUInt(Root, "to_sequence", &B) || B <= A || B == UINT64_MAX ||
             !MdoBackupModelText(Root, "reason", true, false) ) goto invalid;
    } else {
        if ( !MdoBackupUInt(Root, "through_sequence", &A) ||
             !MdoBackupUInt(Root, "previous_next_sequence", &B) || B == 0u || A >= B ||
             (MdoBackupModelEqual(Operation, "clear") && A != 0u) ) goto invalid;
    }
    if ( Sequence <= *LastRecord ) goto invalid;
    if ( Sequence > Checkpoint ) {
        if ( *ReplaySequence == UINT64_MAX || Sequence != *ReplaySequence + 1u ) goto invalid;
        *ReplaySequence = Sequence;
    }
    *LastRecord = Sequence;
    return true;
invalid:
    return MdoBackupModelInvalid(Error, File->Path);
}

bool MdoBackupModelValidate(const MdoSessionBackup* Backup,
    const MdoSessionBackupLimits* Limits, const xcancel* Cancel, xwork_error* Error)
{
    const MdoBackupOwnedFile* Snapshot = MdoBackupFind(Backup, "snapshot.json");
    const MdoBackupOwnedFile* Journal = MdoBackupFind(Backup, "journal.jsonl");
    xvalue* Root;
    uint64 Version = 0u, Checkpoint = 0u, LastRecord = 0u, ReplaySequence;
    const char* Path = "snapshot.json";
    size_t Offset = 0u;
    bool Ok;
    if ( !MdoBackupCheck(Limits, Cancel, Error) ) return false;
    if ( Snapshot == NULL ) return MdoBackupModelInvalid(Error, "snapshot.json");
    Root = MdoBackupJson(Snapshot->Data, Snapshot->Bytes);
    if ( Root == NULL ) goto parse;
    Ok = MdoBackupModelSnapshotValidate(Snapshot, Root, Limits, Cancel, Error);
    if ( Ok ) {
        (void)MdoBackupUInt(Root, "version", &Version);
        (void)MdoBackupModelUInt(Root, Version == 3u ? "checkpoint_sequence" : "journal_sequence", &Checkpoint, true);
    }
    xrtValueRelease(Root);
    if ( !Ok ) return false;
    if ( !MdoBackupCheck(Limits, Cancel, Error) ) return false;
    if ( Journal == NULL ) return true;
    ReplaySequence = Checkpoint; Path = Journal->Path;
    /* Own no additional whole-journal DOM. Validate each complete record once,
     * with CRLF framing accepted but CRC over the exact JSON bytes only.
     * A torn tail is rejected, never discarded or written back by a backup. */
    while ( Offset < Journal->Bytes ) {
        const char* End;
        size_t Framed;
        MdoBackupOwnedFile Record = *Journal;
        if ( !MdoBackupCheck(Limits, Cancel, Error) ) return false;
        End = (const char*)memchr(Journal->Data + Offset, '\n', Journal->Bytes - Offset);
        if ( End == NULL ) return MdoBackupModelInvalid(Error, Journal->Path);
        Framed = (size_t)(End - (Journal->Data + Offset));
        Record.Data += Offset; Record.Bytes = Framed;
        if ( Record.Bytes != 0u && Record.Data[Record.Bytes - 1u] == '\r' ) --Record.Bytes;
        if ( Record.Bytes == 0u ) return MdoBackupModelInvalid(Error, Journal->Path);
        Root = MdoBackupJson(Record.Data, Record.Bytes);
        if ( Root == NULL ) goto parse;
        Ok = MdoBackupModelJournalRecord(&Record, Root, Checkpoint, &LastRecord, &ReplaySequence, Limits, Cancel, Error);
        xrtValueRelease(Root);
        if ( !Ok ) return false;
        Offset += Framed + 1u;
    }
    return MdoBackupCheck(Limits, Cancel, Error);
parse:
    return MdoBackupError(Error, xrtGetError() != NULL && xrtErrorKind(xrtGetError()) == XERR_MEMORY ?
        XWORK_ERROR_OUT_OF_MEMORY : XWORK_ERROR_IO, "cannot parse model ledger", Path);
}
