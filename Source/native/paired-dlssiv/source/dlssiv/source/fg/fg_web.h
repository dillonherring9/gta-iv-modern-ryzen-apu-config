// fg_web.h: frame generation stands by while one of the game's web pages (the internet cafe) is up, as in a menu.
// A page is full-screen 2D interface; where it matches the scene's colour the HUD mask misses it and the generated frame
// shows the interpolated scene behind it. The game keeps the page's viewport id in one dword (fg_web_rule.h) and treats a
// page as a menu; FgPA::MayEngage does the same. Not found: FgWebUp answers false and nothing changes.
#pragma once
#include "fg_web_rule.h"

struct FgWeb
{
    const LONG* id;                  // the game's dword (null: not found, or not looked for yet)
    FgWebFound  f;                   // what the search said
};
static FgWeb fgWeb;

// Is a web page up now (the game's own test: the id above zero)? False where the state was not found.
static __forceinline bool FgWebUp()
{
    const LONG* const p = fgWeb.id;
    return p != nullptr && FgWebUpRule(*p);
}

// The search, once, at the product's install (FgpArmInstall), after the menu flag was looked for (MenuFlagFind, which
// runs at hdr = 1 and above: frame generation asks for hdr = 1).
static void FgWebFind()
{
    FgWeb& w = fgWeb;
    w.id = nullptr;
    BYTE* s = nullptr; size_t n = 0;
    BYTE* const img = (BYTE*)GetModuleHandleA(nullptr);
    if (!img || !TextRange(&s, &n)) { memset(&w.f, 0, sizeof(w.f)); w.f.why = FG_WEB_NO_SITE; }
    else
    {
        const size_t imgSize = ((IMAGE_NT_HEADERS*)(img + ((IMAGE_DOS_HEADER*)img)->e_lfanew))->OptionalHeader.SizeOfImage;
        const uint32_t a = FgWebFindIn(s, n, (uint32_t)(size_t)gameMenuActive, (uint32_t)(size_t)img, (uint32_t)((size_t)img + imgSize), &w.f);
        if (a)
        {
            // The value it holds now must be an id or one of the two "none" values: a pointer or a float there is something else.
            const LONG* const p = (const LONG*)(size_t)a;
            if (IsReadable(p, 4) && *p >= -1 && *p < 0x01000000) w.id = p;
            else w.f.why = FG_WEB_VALUE;
        }
    }
}

