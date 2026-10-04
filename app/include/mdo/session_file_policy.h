#ifndef MDO_SESSION_FILE_POLICY_H
#define MDO_SESSION_FILE_POLICY_H

#include <xsbase.h>
#include <string.h>

/* Shared portable backup/private-restore inventory policy. No filesystem or
 * runtime dependency: bootstrap recovery must use the same whitelist before
 * session managers exist. Runtime lock/temp/backup files are never payload. */
static inline bool MdoSessionFileDigits(const char* Text, size_t Size, bool Hex)
{
    size_t i;
    if ( Size == 0u ) return false;
    for ( i = 0u; i < Size; ++i ) {
        char Ch = Text[i];
        if ( !(Ch >= '0' && Ch <= '9') &&
             !(Hex && Ch >= 'a' && Ch <= 'f') ) return false;
    }
    return true;
}

static inline bool MdoSessionFileDecimal(const char* Text, size_t Size)
{
    uint64 Value = 0u;
    size_t i;
    if ( Size > 20u || !MdoSessionFileDigits(Text, Size, false) ) return false;
    for ( i = 0u; i < Size; ++i ) {
        unsigned Digit = (unsigned)(Text[i] - '0');
        if ( Value > (UINT64_MAX - Digit) / 10u ) return false;
        Value = Value * 10u + Digit;
    }
    return Value != 0u;
}

static inline bool MdoSessionFileSuffix(const char* Text, const char* Suffix)
{
    size_t Size = strlen(Text), Tail = strlen(Suffix);
    return Size >= Tail && strcmp(Text + Size - Tail, Suffix) == 0;
}

/* Whitelist the current logical files; unknown content fails rather than
 * silently creating an allegedly complete bundle. No native path is exported. */
static inline size_t MdoSessionFileLimit(const char* Path, bool Directory)
{
    const char* Name;
    size_t Size, i;
    if ( Directory ) {
        if ( strcmp(Path, "") == 0 || strcmp(Path, "attachments") == 0 ||
             strcmp(Path, "attachments/events") == 0 ||
             strcmp(Path, "attachments/runs") == 0 ||
             strcmp(Path, "queue-receipts") == 0 || strcmp(Path, "artifacts") == 0 )
            return 1u;
        return strncmp(Path, "artifacts/run-", 14u) == 0 && strlen(Path) == 34u &&
            MdoSessionFileDecimal(Path + 14u, 20u) ? 1u : 0u;
    }
    if ( strcmp(Path, "meta.json") == 0 ) return 64u * 1024u;
    if ( strcmp(Path, "snapshot.json") == 0 || strcmp(Path, "journal.jsonl") == 0 )
        return (32u * 1024u * 1024u);
    if ( strcmp(Path, "ui-events.jsonl") == 0 ) return 16u * 1024u * 1024u;
    if ( strcmp(Path, "todo.json") == 0 ) return 16u * 1024u;
    if ( strcmp(Path, "draft.json") == 0 || strcmp(Path, "queue.json") == 0 )
        return 256u * 1024u;
    /* Retired sidecar: bounded legacy inventory/import only. Live writers,
     * capture and restored sessions no longer retain message ratings. */
    if ( strcmp(Path, "feedback.json") == 0 ) return 32u * 1024u;
    if ( strcmp(Path, "restore-inputs.json") == 0 ) return 8u * 1024u * 1024u;
    if ( strcmp(Path, "restore-origin.json") == 0 ) return 8u * 1024u * 1024u;
    if ( strncmp(Path, "attachments/", 12u) == 0 ) {
        Name = Path + 12u; Size = strlen(Name);
        if ( Size == 36u && MdoSessionFileDigits(Name, 32u, true) &&
             strcmp(Name + 32u, ".bin") == 0 ) return 8u * 1024u * 1024u;
        if ( Size == 37u && MdoSessionFileDigits(Name, 32u, true) &&
             strcmp(Name + 32u, ".json") == 0 ) return 4096u;
        if ( strncmp(Name, "events/", 7u) != 0 && strncmp(Name, "runs/", 5u) != 0 )
            return 0u;
        Name += strncmp(Name, "events/", 7u) == 0 ? 7u : 5u;
        Size = strlen(Name);
        return Size > 5u && strcmp(Name + Size - 5u, ".json") == 0 &&
            MdoSessionFileDecimal(Name, Size - 5u) ? 320u : 0u;
    }
    if ( strncmp(Path, "queue-receipts/", 15u) == 0 ) {
        Name = Path + 15u;
        return strlen(Name) == 37u && MdoSessionFileDigits(Name, 32u, true) &&
            strcmp(Name + 32u, ".json") == 0 ? 512u : 0u;
    }
    if ( strncmp(Path, "artifacts/run-", 14u) != 0 || strlen(Path) < 60u ||
         Path[34] != '/' || !MdoSessionFileDecimal(Path + 14u, 20u) ) return 0u;
    Name = Path + 35u; Size = strlen(Name);
    if ( Size < 26u || Size > 153u || Name[20] != '-' ||
         !MdoSessionFileDecimal(Name, 20u) || !MdoSessionFileSuffix(Name, ".txt") ) return 0u;
    for ( i = 21u; i < Size - 4u; ++i ) {
        char Ch = Name[i];
        if ( !((Ch >= 'a' && Ch <= 'z') || (Ch >= 'A' && Ch <= 'Z') ||
               (Ch >= '0' && Ch <= '9') || Ch == '-' || Ch == '_') ) return 0u;
    }
    return (32u * 1024u * 1024u);
}

#endif
