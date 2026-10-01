#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <xllm-session.h>

#include "backup_internal.h"

/* This is a file-format boundary, not a model invocation. All strings remain
 * borrowed from one bounded JSON tree. Runtime configuration validation uses
 * an unbound, in-memory session; no paths, hooks or client are attached. */
static bool MdoSnapshotEqual(xstrview Text, const char* Literal)
{
    return Text.Size == strlen(Literal) && memcmp(Text.Data, Literal, Text.Size) == 0;
}

static const xvalue* MdoSnapshotGet(const xvalue* Root, const char* Key)
{
    return xrtValueObjectGet(Root, xrtStrView(Key));
}

static bool MdoSnapshotKeys(const xvalue* Root, const char* const* Keys, size_t Count)
{
    size_t i, j;
    if ( xrtValueType(Root) != XVALUE_OBJECT ) return false;
    for ( i = 0u; i < xrtValueCount(Root); ++i ) {
        xstrview Key;
        (void)xrtValueObjectAt(Root, i, &Key);
        for ( j = 0u; j < Count; ++j ) if ( MdoSnapshotEqual(Key, Keys[j]) ) break;
        if ( j == Count ) return false;
    }
    return true;
}

static bool MdoSnapshotText(const xvalue* Root, const char* Key, bool Optional, bool Nullable)
{
    const xvalue* Value = MdoSnapshotGet(Root, Key);
    xstrview Text;
    if ( Value == NULL ) return Optional;
    if ( Nullable && xrtValueType(Value) == XVALUE_NULL ) return true;
    return xrtValueGetString(Value, &Text) && memchr(Text.Data, 0, Text.Size) == NULL;
}

static bool MdoSnapshotUInt(const xvalue* Root, const char* Key, uint64* Number, bool Optional)
{
    return MdoSnapshotGet(Root, Key) == NULL ? Optional : MdoBackupUInt(Root, Key, Number);
}

static bool MdoSnapshotNumber(const xvalue* Value, double* Number)
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

static bool MdoSnapshotInvalid(xwork_error* Error)
{
    return MdoBackupError(Error, XWORK_ERROR_IO, "invalid model snapshot schema or checksum", "snapshot.json");
}

static bool MdoSnapshotChecksum(const MdoBackupOwnedFile* File,
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
    if ( File->Bytes < Trailer ) return MdoSnapshotInvalid(Error);
    Prefix = File->Bytes - Trailer;
    if ( memcmp(File->Data + Prefix, Marker, sizeof(Marker) - 1u) != 0 ||
         memcmp(File->Data + File->Bytes - 2u, "\"}", 2u) != 0 ) return MdoSnapshotInvalid(Error);
    for ( i = 0u; i < 8u; ++i ) {
        unsigned char Ch = (unsigned char)File->Data[Prefix + sizeof(Marker) - 1u + i];
        unsigned Nibble;
        if ( Ch >= '0' && Ch <= '9' ) Nibble = Ch - '0';
        else if ( Ch >= 'a' && Ch <= 'f' ) Nibble = Ch - 'a' + 10u;
        else if ( Ch >= 'A' && Ch <= 'F' ) Nibble = Ch - 'A' + 10u;
        else return MdoSnapshotInvalid(Error);
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
    return (Crc ^ UINT32_MAX) == Expected || MdoSnapshotInvalid(Error);
}

typedef struct MdoSnapshotConfigField { const char* Key; size_t Offset, Bytes; } MdoSnapshotConfigField;
#define MDO_SNAPSHOT_FIELD(Key, Member) {Key, offsetof(xllm_session_config, Member), \
    sizeof(((xllm_session_config*)0)->Member)}

static bool MdoSnapshotConfig(const xvalue* Root, xwork_error* Error)
{
    static const MdoSnapshotConfigField Fields[] = {
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
    if ( Root != NULL && !MdoSnapshotKeys(Root, Keys, sizeof(Keys) / sizeof(Keys[0])) ) goto invalid;
    for ( i = 0u; Root != NULL && i < Count; ++i ) {
        if ( MdoSnapshotGet(Root, Fields[i].Key) == NULL ) continue;
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
        const xvalue* Value = MdoSnapshotGet(Root, "prune_trigger");
        if ( Value != NULL && !MdoSnapshotNumber(Value, &Config.fPruneTrigger) ) goto invalid;
        Value = MdoSnapshotGet(Root, "compact_trigger");
        if ( Value != NULL && !MdoSnapshotNumber(Value, &Config.fCompactTrigger) ) goto invalid;
        Number = Config.eWindowMode;
        if ( !MdoSnapshotUInt(Root, "window_mode", &Number, true) || Number > XLLM_WINDOW_SPLIT_INPUT_OUTPUT ) goto invalid;
        Config.eWindowMode = (xllm_window_mode)Number;
        Number = Config.eJournalDurability;
        if ( !MdoSnapshotUInt(Root, "journal_durability", &Number, true) || Number > XLLM_SESSION_DURABILITY_FLUSH ) goto invalid;
        Config.eJournalDurability = (xllm_session_durability)Number;
        if ( !MdoSnapshotText(Root, "summary_style", true, true) ) goto invalid;
        if ( MdoBackupView(Root, "summary_style", &Style) ) Config.sSummaryStyle = Style.Data;
    }
    Session = xllmSessionCreate(&Config, &Cause);
    if ( Session == NULL ) return MdoBackupError(Error, Cause.eCode == XLLM_ERROR_OUT_OF_MEMORY ?
        XWORK_ERROR_OUT_OF_MEMORY : XWORK_ERROR_IO, "unsupported model snapshot configuration", "snapshot.json");
    xllmSessionDestroy(Session);
    return true;
invalid:
    return MdoSnapshotInvalid(Error);
}
#undef MDO_SNAPSHOT_FIELD

static bool MdoSnapshotEntry(const xvalue* Entry, uint64 CurrentTurn, uint64 Next, uint64* Sequence,
    const MdoSessionBackupLimits* Limits, const xcancel* Cancel, xwork_error* Error)
{
    static const char* const Keys[] = {"sequence", "turn", "flags", "role", "content",
        "reasoning", "tool_call_id", "tool_calls"};
    static const char* const CallKeys[] = {"id", "name", "arguments"};
    const xvalue* Calls;
    uint64 Turn, Flags = 0u, Role;
    size_t i;
    if ( !MdoSnapshotKeys(Entry, Keys, sizeof(Keys) / sizeof(Keys[0])) ||
         !MdoBackupUInt(Entry, "sequence", Sequence) || *Sequence == 0u || *Sequence >= Next ||
         !MdoBackupUInt(Entry, "turn", &Turn) || Turn > CurrentTurn ||
         !MdoSnapshotUInt(Entry, "flags", &Flags, true) ||
         (Flags & ~(uint64)(XLLM_SESSION_ENTRY_PINNED | XLLM_SESSION_ENTRY_SYNTHETIC)) != 0u ||
         !MdoBackupUInt(Entry, "role", &Role) || Role > XLLM_ROLE_TOOL ||
         !MdoSnapshotText(Entry, "content", true, true) ||
         !MdoSnapshotText(Entry, "reasoning", true, true) ||
         !MdoSnapshotText(Entry, "tool_call_id", true, true) ) return MdoSnapshotInvalid(Error);
    Calls = MdoSnapshotGet(Entry, "tool_calls");
    if ( Calls == NULL ) return true;
    if ( xrtValueType(Calls) != XVALUE_ARRAY ) return MdoSnapshotInvalid(Error);
    for ( i = 0u; i < xrtValueCount(Calls); ++i ) {
        const xvalue* Call = xrtValueArrayGet(Calls, i);
        xstrview Name;
        if ( !MdoBackupCheck(Limits, Cancel, Error) ) return false;
        /* Arguments are opaque persisted model output, including a malformed
         * call that produced a tool error. Validation must not rewrite it. */
        if ( !MdoSnapshotKeys(Call, CallKeys, sizeof(CallKeys) / sizeof(CallKeys[0])) ||
             !MdoSnapshotText(Call, "id", true, true) ||
             !MdoSnapshotText(Call, "name", false, false) ||
             !MdoBackupView(Call, "name", &Name) || Name.Size == 0u ||
             !MdoSnapshotText(Call, "arguments", true, true) ) return MdoSnapshotInvalid(Error);
    }
    return true;
}

static bool MdoSnapshotFiles(const xvalue* Root, const char* NamesKey, const char* SequencesKey,
    uint64 Next, const MdoSessionBackupLimits* Limits, const xcancel* Cancel, xwork_error* Error)
{
    const xvalue* Names = MdoSnapshotGet(Root, NamesKey);
    const xvalue* Sequences = MdoSnapshotGet(Root, SequencesKey);
    size_t i;
    if ( Names == NULL && Sequences == NULL ) return true;
    if ( xrtValueType(Names) != XVALUE_ARRAY || (Sequences != NULL &&
         (xrtValueType(Sequences) != XVALUE_ARRAY || xrtValueCount(Names) != xrtValueCount(Sequences))) )
        return MdoSnapshotInvalid(Error);
    for ( i = 0u; i < xrtValueCount(Names); ++i ) {
        xstrview Text;
        uint64 Sequence;
        int64 Signed;
        const xvalue* Value = Sequences != NULL ? xrtValueArrayGet(Sequences, i) : NULL;
        if ( !MdoBackupCheck(Limits, Cancel, Error) ) return false;
        if ( !xrtValueGetString(xrtValueArrayGet(Names, i), &Text) || Text.Size == 0u ||
             memchr(Text.Data, 0, Text.Size) != NULL ) return MdoSnapshotInvalid(Error);
        if ( Value == NULL ) continue;
        if ( xrtValueType(Value) == XVALUE_UINT ) {
            if ( !xrtValueGetUInt(Value, &Sequence) ) return MdoSnapshotInvalid(Error);
        } else {
            if ( !xrtValueGetInt(Value, &Signed) || Signed < 0 ) return MdoSnapshotInvalid(Error);
            Sequence = (uint64)Signed;
        }
        if ( Sequence >= Next ) return MdoSnapshotInvalid(Error);
    }
    return true;
}

bool MdoBackupSnapshotValidate(const MdoBackupOwnedFile* File, const xvalue* Root,
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
    if ( !MdoSnapshotKeys(Root, Keys, sizeof(Keys) / sizeof(Keys[0])) ||
         !MdoBackupView(Root, "format", &Format) || !MdoSnapshotEqual(Format, "xllm-session") ||
         !MdoBackupUInt(Root, "version", &Version) || Version < 1u || Version > 3u ||
         !MdoBackupUInt(Root, "next_sequence", &Next) || Next == 0u ||
         !MdoBackupUInt(Root, "current_turn", &CurrentTurn) ||
         !MdoSnapshotText(Root, "summary", true, true) ) goto invalid;
    if ( Version == 3u || MdoSnapshotGet(Root, "checksum") != NULL ) {
        if ( !MdoSnapshotChecksum(File, Limits, Cancel, Error) ) return false;
    }
    if ( (Version == 3u && MdoSnapshotGet(Root, "journal_sequence") != NULL) ||
         (Version != 3u && MdoSnapshotGet(Root, "checkpoint_sequence") != NULL) ||
         (Version == 3u && MdoSnapshotGet(Root, "config") == NULL) ) goto invalid;
    for ( i = 0u; i < sizeof(Counters) / sizeof(Counters[0]); ++i )
        if ( !MdoSnapshotUInt(Root, Counters[i], &Number, true) ) goto invalid;
    Number = 0u;
    if ( !MdoSnapshotUInt(Root, "summary_generation", &Number, true) || Number > UINT32_MAX ) goto invalid;
    Number = 0u;
    if ( !MdoSnapshotUInt(Root, "fill_seen", &Number, true) || Number > 1u ) goto invalid;
    Number = 0u;
    if ( !MdoSnapshotUInt(Root, "compacted_through", &Number, true) || Number >= Next ) goto invalid;
    Number = 0u;
    if ( !MdoSnapshotUInt(Root, "tail_floor", &Number, true) || Number >= Next ) goto invalid;
    if ( !MdoSnapshotConfig(MdoSnapshotGet(Root, "config"), Error) ) return false;
    Entries = MdoSnapshotGet(Root, "entries");
    if ( xrtValueType(Entries) != XVALUE_ARRAY ) goto invalid;
    for ( i = 0u; i < xrtValueCount(Entries); ++i ) {
        uint64 Sequence;
        if ( !MdoBackupCheck(Limits, Cancel, Error) ) return false;
        if ( !MdoSnapshotEntry(xrtValueArrayGet(Entries, i), CurrentTurn, Next, &Sequence,
                Limits, Cancel, Error) ) return false;
        if ( Sequence <= Last ) goto invalid;
        Last = Sequence;
    }
    return MdoSnapshotFiles(Root, "read_files", "read_file_sequences", Next, Limits, Cancel, Error) &&
        MdoSnapshotFiles(Root, "modified_files", "modified_file_sequences", Next, Limits, Cancel, Error);
invalid:
    return MdoSnapshotInvalid(Error);
}
