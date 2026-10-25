#ifndef MDO_VERSION_CODE_H
#define MDO_VERSION_CODE_H
#include <stdint.h>
#include <string.h>

/* The high byte is a migration tag, never part of version ordering. Keep this
 * header identical in the client and delivery service. */
#define MDO_VERSION_ORDER_MASK UINT32_C(0x00FFFFFF)
#define MDO_VERSION_1_0 UINT32_C(0x00010000)
static inline int MdoVersionCodeParse(const char* text, uint32_t* result)
{
    if (!text || !result) return 0;
    size_t size = strlen(text);
    if (size != 8 && size != 11) return 0;
    uint32_t value = 0;
    for (size_t i = 0; i < size; ++i) {
        if (size == 11 && (i == 2 || i == 5 || i == 8)) {
            if (text[i] != '.') return 0;
            continue;
        }
        unsigned char c = (unsigned char)text[i]; unsigned digit;
        if (c >= '0' && c <= '9') digit = c - '0';
        else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
        else return 0;
        value = (value << 4) | digit;
    }
    *result = value; return 1;
}
static inline void MdoVersionCodeFormat(uint32_t value, char text[9])
{
    const char* hex = "0123456789ABCDEF";
    for (unsigned i = 0; i < 8; ++i) text[i] = hex[(value >> ((7-i)*4)) & 15];
    text[8] = 0;
}
static inline int MdoVersionCodeCompare(uint32_t left, uint32_t right)
{
    left &= MDO_VERSION_ORDER_MASK; right &= MDO_VERSION_ORDER_MASK;
    return left > right ? 1 : left < right ? -1 : 0;
}
#endif
