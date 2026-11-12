/* Re-key the DualSense mod's weapon table (step 163). GTA_IV_DualSense.asi sha256 ba15d631…: 19 entries
 * {u32 weapon id, char *fire wav, char *reload wav} at RVA 0x57C38 (.rdata), read by 0x10020D26 into std::maps keyed by id
 * (fire 0x61C28, reload 0x61C48). GTA IV ids 8/24/26-30 are unused or melee in Vice City; they become VC guns so the mod
 * reads Weapon32..35 etc. from AudioMap.ini. Runs in our DllMain (loader lock held) before the mod's worker thread. */
static void patch_mod_weapons(void) {
    BYTE *m = (BYTE *)GetModuleHandleA("GTA_IV_DualSense.asi");
    if (!m) { llog("weapon table: GTA_IV_DualSense.asi not loaded yet - not patched"); return; }
    static const uint32_t want[19] = { 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 24, 26, 27, 28, 29, 30, 31 };
    static const struct { uint32_t from, to; } remap[] = { {8, 32}, {24, 33}, {26, 34}, {27, 35}, {28, 21}, {29, 90}, {30, 91} };
    uint32_t *t = (uint32_t *)(m + 0x57C38);
    if (!readable((uintptr_t)t, 19 * 12) || !readable((uintptr_t)(m + 0x61C28), 8)) { llog("weapon table: unreadable - not patched"); return; }
    for (int i = 0; i < 19; i++) {
        const char *f = (const char *)(uintptr_t)t[i * 3 + 1];
        if (t[i * 3] != want[i] || !readable((uintptr_t)f, 22) || strncmp(f, "audio\\", 6)) { llog("weapon table: entry %d differs (unknown mod build) - not patched", i); return; }
    }
    if (*(uint32_t *)(m + 0x61C28 + 4)) { llog("weapon table: mod already read its config - not patched"); return; }
    DWORD old; if (!VirtualProtect(t, 19 * 12, PAGE_READWRITE, &old)) return;
    int n = 0;
    for (int i = 0; i < 19; i++) for (unsigned k = 0; k < sizeof remap / sizeof *remap; k++)
        if (t[i * 3] == remap[k].from) { t[i * 3] = remap[k].to; n++; break; }
    VirtualProtect(t, 19 * 12, old, &old);
    llog("weapon table: %d mod weapon slots re-keyed to Vice City ids (32 Tec9, 33 Uzi 2, 34 minigun, 35 sniper, 21 grenade launcher)", n);
}
