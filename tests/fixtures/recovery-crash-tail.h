/* Test-only crash tail; call only on an owned disposable Home after draining its run. */
static bool QaRecoveryCrashTail(cstr Relative)
{
    char* Path = MdoHomeExternalPath(Relative);
    FILE* File = Path ? fopen(Path, "rb") : NULL;
    char* Text = NULL;
    long Bytes = 0;
    size_t Keep = 0;
    bool Found = false, Ok = false;
    if (!File || fseek(File, 0, SEEK_END) || (Bytes = ftell(File)) <= 0 ||
        Bytes > 512 * 1024 || fseek(File, 0, SEEK_SET)) goto done;
    Text = (char*)malloc((size_t)Bytes + 1u);
    if (!Text || fread(Text, 1u, (size_t)Bytes, File) != (size_t)Bytes) goto done;
    fclose(File); File = NULL;
    Text[Bytes] = 0;
    for (size_t At = 0u; At < (size_t)Bytes;) {
        char* End = memchr(Text + At, '\n', (size_t)Bytes - At);
        if (!End) goto done;
        size_t Length = (size_t)(End - (Text + At));
        xvalue* Row = xrtJsonParse(xrtStrViewN(Text + At, Length));
        uint64 Kind = 0;
        bool Parsed = Row && MdoEventsValueUInt(Row, "kind", &Kind);
        xrtValueRelease(Row);
        if (!Parsed) goto done;
        if (Kind == XWORK_EVENT_ERROR || Kind == XWORK_EVENT_AGENT_DONE) {
            Keep = At; Found = true; break;
        }
        At += Length + 1u;
    }
    if (!Found) goto done;
    File = fopen(Path, "wb");
    Ok = File && fwrite(Text, 1u, Keep, File) == Keep && fflush(File) == 0;
done:
    if (File && fclose(File)) Ok = false;
    free(Text); xrtFree(Path);
    return Ok;
}

