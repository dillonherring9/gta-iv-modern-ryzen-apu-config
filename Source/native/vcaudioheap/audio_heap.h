/* CE 1.2.0.59 physical audio heap capacity, verified against the installed EXE.
 * Original project code. The constant was independently located in revd:
 * https://github.com/gutbash/revd/tree/fbf6306f7cbc1bcb63698df380c4d0db33a6180d
 * No engine slots, allocator hooks, sound banks or save data are changed.
 */
#ifndef VC_AUDIO_HEAP_H
#define VC_AUDIO_HEAP_H
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <string.h>

#define AH_NORMAL_RVA 0x4c158cu
#define AH_SMALL_RVA  0x4c15a3u
#define AH_HEAP_RVA   0xd62534u
#define AH_BLOCK_RVA  0xd62538u
#define AH_TARGET     (192u * 1024u * 1024u)
#define AH_IMAGE_SIZE 0x1be6400u

enum ah_result { AH_APPLIED = 0, AH_WRONG_IMAGE, AH_WRONG_CODE,
                 AH_TOO_LATE, AH_PROTECT_FAILED, AH_RESTORE_FAILED };

static const unsigned char ah_capacity_code[] = {
    0xbe,0x00,0x00,0xe0,0x07,0xe8,0xcb,0x02,0xb4,0xff,0x85,0xd2,
    0x77,0x0e,0x72,0x07,0x3d,0x80,0x99,0x99,0x59,0x77,0x05,
    0xbe,0x00,0x00,0x30,0x07
};
static const unsigned char ah_allocator_code[] = {
    0x8b,0x54,0x24,0x04,0x8b,0x0d,0xf4,0xa3,0x15,0x01,0x01,0x15,
    0xfc,0xa3,0x15,0x01,0x8b,0x01,0x6a,0x00,0xff,0x74,0x24,0x0c,
    0x52,0xff,0x50,0x08,0xc3
};

/* Called synchronously by the early ASI loader, before audio initialization.
 * No background poll can race the initialization routine or claim a late patch.
 * Both branch constants occupy one memory-protection region and change together.
 */
static enum ah_result ah_apply(unsigned char *base) {
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    IMAGE_NT_HEADERS32 *pe;
    unsigned char *code = base + AH_NORMAL_RVA - 1;
    DWORD old, ignored;
    if (!base || dos->e_magic != IMAGE_DOS_SIGNATURE ||
        dos->e_lfanew <= 0 || dos->e_lfanew > 0x1000)
        return AH_WRONG_IMAGE;
    pe = (IMAGE_NT_HEADERS32 *)(base + dos->e_lfanew);
    if (pe->Signature != IMAGE_NT_SIGNATURE ||
        pe->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
        pe->FileHeader.TimeDateStamp != 0x63d3e735u ||
        pe->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
        pe->OptionalHeader.SizeOfImage != AH_IMAGE_SIZE ||
        !(pe->FileHeader.Characteristics & IMAGE_FILE_LARGE_ADDRESS_AWARE))
        return AH_WRONG_IMAGE;
    if (memcmp(code, ah_capacity_code, sizeof ah_capacity_code) ||
        memcmp(base + 0x4850a0, ah_allocator_code, sizeof ah_allocator_code))
        return AH_WRONG_CODE;
    if (*(volatile uint32_t *)(base + AH_HEAP_RVA) ||
        *(volatile uint32_t *)(base + AH_BLOCK_RVA) ||
        *(volatile uint32_t *)(base + 0xd5a3f4))
        return AH_TOO_LATE;
    if (!VirtualProtect(code, sizeof ah_capacity_code, PAGE_EXECUTE_READWRITE, &old))
        return AH_PROTECT_FAILED;
    *(uint32_t *)(base + AH_NORMAL_RVA) = AH_TARGET;
    *(uint32_t *)(base + AH_SMALL_RVA) = AH_TARGET;
    FlushInstructionCache(GetCurrentProcess(), code, sizeof ah_capacity_code);
    if (!VirtualProtect(code, sizeof ah_capacity_code, old, &ignored))
        return AH_RESTORE_FAILED;
    return AH_APPLIED;
}
#endif
