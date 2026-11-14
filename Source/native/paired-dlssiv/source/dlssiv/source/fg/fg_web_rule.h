// fg_web_rule.h: the game's web-page state, found in its code by shape, and the stand-by rule. Pure (no Windows or D3D
// type): shared by the mod (fg_web.h), the harness and tools/test-fg-web.
// The state is the script's HTML viewport id, one global: -1 at init, the id from CREATE_HTML_VIEWPORT, 0 from the
// viewport's destructor. The game's frame function tests it above zero beside CMenuManager::m_MenuActive.
// Found two ways that must agree: 1. the frame function's shape near the menu flag (cmp [a], 0, or cmp [a], r32; then a
// load of the flag into a byte register; then C6 05 x 01), every site naming one address; 2. that address's writers over
// the whole code: -1 once, 0 once, eax once. Anything else is "not found" and nothing stands by.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum { FG_WEB_FOUND = 0, FG_WEB_NO_FLAG = 1, FG_WEB_NO_SITE = 2, FG_WEB_TWO = 3, FG_WEB_WRITERS = 4, FG_WEB_VALUE = 5 };
struct FgWebFound
{
    uint32_t at;                    // the address the frame function names (0: none); the state's, where why is FG_WEB_FOUND
    int      why;                   // FG_WEB_*
    int      sites, others;         // sites of the shape; of them naming another address than the first
    int      setM1, set0, setEax;   // the writers counted for `at`
};
static inline const char* FgWebWhy(int why)
{
    switch (why)
    {
    case FG_WEB_FOUND:   return "found";
    case FG_WEB_NO_FLAG: return "the menu flag was not found, and the shape is read from it";
    case FG_WEB_NO_SITE: return "the game's frame function does not have the shape";
    case FG_WEB_TWO:     return "the shape names more than one address";
    case FG_WEB_WRITERS: return "the address is not written as the id of the HTML viewport is (-1 once, 0 once, eax once)";
    default:             return "the address is not readable or holds no id";
    }
}

// The rule, the game's own test: above zero, signed (-1: the game's init; 0: the last page's viewport is gone).
static inline bool FgWebUpRule(long id) { return id > 0; }
// Does a present stand by for it (FgPA::MayEngage)? lifted: the development switch FG_WEB_LIFT, for an A/B.
static inline bool FgWebStandByRule(long id, bool lifted) { return id > 0 && !lifted; }
// Development switch: this bit of the key fg_lift. It is above every row of the conditions: no row reads it.
enum : unsigned { FG_WEB_LIFT = 1u << 30 };

static inline uint32_t FgWebU32(const unsigned char* p) { uint32_t v; memcpy(&v, p, 4); return v; }
// The search. t, n: the game's code as it lies in memory. flag: the menu flag's address (0: not found). imgLo, imgHi: the
// image. Fills *o and returns the state's address, or 0.
static inline uint32_t FgWebFindIn(const unsigned char* t, size_t n, uint32_t flag, uint32_t imgLo, uint32_t imgHi, FgWebFound* o)
{
    memset(o, 0, sizeof(*o));
    if (!flag || !t || n < 32) { o->why = FG_WEB_NO_FLAG; return 0; }
    unsigned char fb[4]; memcpy(fb, &flag, 4);
    // 1. the frame function's shape, at every place the code names the menu flag
    for (size_t i = 9; i + 11 <= n; i++)
    {
        if (t[i] != fb[0] || t[i + 1] != fb[1] || t[i + 2] != fb[2] || t[i + 3] != fb[3]) continue;
        size_t start;
        if (t[i - 1] == 0xA0) start = i - 1;                                        // mov al, [flag]
        else if (t[i - 2] == 0x8A && (t[i - 1] & 0xC7) == 0x05) start = i - 2;      // mov r8, [flag]
        else continue;
        if (t[i + 4] != 0xC6 || t[i + 5] != 0x05 || t[i + 10] != 0x01) continue;    // mov byte ptr [x], 1
        uint32_t a;
        if (t[start - 7] == 0x83 && t[start - 6] == 0x3D && t[start - 1] == 0x00) a = FgWebU32(t + start - 5);        // cmp dword ptr [a], 0
        else if (t[start - 6] == 0x39 && (t[start - 5] & 0xC7) == 0x05) a = FgWebU32(t + start - 4);                  // cmp dword ptr [a], r32
        else continue;
        if (a < imgLo || a >= imgHi || imgHi - a < 4) continue;
        if (!o->sites) o->at = a; else if (a != o->at) o->others++;
        o->sites++;
    }
    if (!o->sites) { o->why = FG_WEB_NO_SITE; return 0; }
    if (o->others) { o->why = FG_WEB_TWO; return 0; }
    // 2. its writers, over the whole code
    unsigned char ab[4]; memcpy(ab, &o->at, 4);
    for (size_t i = 2; i + 8 <= n; i++)
    {
        if (t[i] != ab[0] || t[i + 1] != ab[1] || t[i + 2] != ab[2] || t[i + 3] != ab[3]) continue;
        if (t[i - 2] == 0xC7 && t[i - 1] == 0x05)
        {
            const uint32_t v = FgWebU32(t + i + 4);
            if (v == 0xFFFFFFFFu) o->setM1++; else if (v == 0u) o->set0++;
        }
        else if (t[i - 1] == 0xA3) o->setEax++;
    }
    if (o->setM1 != 1 || o->set0 != 1 || o->setEax != 1) { o->why = FG_WEB_WRITERS; return 0; }
    o->why = FG_WEB_FOUND;
    return o->at;
}
