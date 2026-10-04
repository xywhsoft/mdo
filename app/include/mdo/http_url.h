#ifndef MDO_HTTP_URL_H
#define MDO_HTTP_URL_H

#include <ctype.h>
#include <string.h>
#include <xsbase.h>

/* Shared URL checks for settings and execution: no embedded credentials,
 * controls, fragments, invalid authority or invalid ports. DNS policy belongs
 * to the transport, after resolution. */
static bool MdoHttpUrlAsciiEqual(const char* Left, const char* Right, size_t Size)
{
    size_t i;
    for ( i = 0u; i < Size; ++i )
        if ( tolower((unsigned char)Left[i]) !=
             tolower((unsigned char)Right[i]) ) return false;
    return true;
}

static bool MdoHttpUrlValid(xstrview Url, bool AllowHttp)
{
    size_t Scheme;
    size_t Authority;
    size_t End;
    size_t HostStart;
    size_t HostEnd;
    size_t i;
    bool Https;
    if ( Url.Size == 0u || Url.Size > (16u * 1024u) ||
         (Url.Data == NULL || memchr(Url.Data, 0, Url.Size) != NULL) ) return false;
    for ( i = 0u; i < Url.Size; ++i ) {
        unsigned char c = (unsigned char)Url.Data[i];
        if ( c <= 0x20u || c == 0x7fu || c == '\\' || c == '#' ) return false;
    }
    Https = Url.Size >= 8u && MdoHttpUrlAsciiEqual(Url.Data, "https://", 8u);
    if ( Https ) Scheme = 8u;
    else if ( AllowHttp && Url.Size >= 7u &&
              MdoHttpUrlAsciiEqual(Url.Data, "http://", 7u) ) Scheme = 7u;
    else return false;
    Authority = Scheme;
    End = Url.Size;
    for ( i = Authority; i < Url.Size; ++i )
        if ( Url.Data[i] == '/' || Url.Data[i] == '?' ) { End = i; break; }
    if ( End == Authority ) return false;
    for ( i = Authority; i < End; ++i )
        if ( Url.Data[i] == '@' ) return false;
    HostStart = Authority;
    HostEnd = End;
    if ( Url.Data[HostStart] == '[' ) {
        const char* Close = (const char*)memchr(Url.Data + HostStart + 1u,
            ']', End - HostStart - 1u);
        if ( Close == NULL ) return false;
        HostEnd = (size_t)(Close - Url.Data) + 1u;
        if ( HostEnd < End && Url.Data[HostEnd] != ':' ) return false;
    } else {
        for ( i = HostStart; i < End; ++i ) {
            if ( Url.Data[i] == ':' ) { HostEnd = i; break; }
            if ( Url.Data[i] == '[' || Url.Data[i] == ']' ) return false;
        }
    }
    if ( HostEnd == HostStart ||
         (Url.Data[HostStart] == '[' && HostEnd == HostStart + 2u) ) return false;
    if ( HostEnd < End ) {
        uint32 Port = 0u;
        if ( HostEnd + 1u == End ) return false;
        for ( i = HostEnd + 1u; i < End; ++i ) {
            if ( Url.Data[i] < '0' || Url.Data[i] > '9' ) return false;
            Port = Port * 10u + (uint32)(Url.Data[i] - '0');
            if ( Port > 65535u ) return false;
        }
        if ( Port == 0u ) return false;
    }
    return true;
}

#endif
