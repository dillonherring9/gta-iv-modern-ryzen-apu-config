#include "audio_heap.h"
#include <stdio.h>
#include <stdlib.h>

static void check(int condition, const char *message) {
    if (!condition) { fprintf(stderr, "FAIL: %s\n", message); exit(1); }
}
static void seed(unsigned char *b) {
    memset(b, 0, AH_IMAGE_SIZE);
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)b;
    IMAGE_NT_HEADERS32 *pe = (IMAGE_NT_HEADERS32 *)(b + 0x100);
    dos->e_magic = IMAGE_DOS_SIGNATURE; dos->e_lfanew = 0x100;
    pe->Signature = IMAGE_NT_SIGNATURE;
    pe->FileHeader.Machine = IMAGE_FILE_MACHINE_I386;
    pe->FileHeader.TimeDateStamp = 0x63d3e735u;
    pe->FileHeader.Characteristics = IMAGE_FILE_LARGE_ADDRESS_AWARE;
    pe->OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR32_MAGIC;
    pe->OptionalHeader.SizeOfImage = AH_IMAGE_SIZE;
    memcpy(b + AH_NORMAL_RVA - 1, ah_capacity_code, sizeof ah_capacity_code);
    memcpy(b + 0x4850a0, ah_allocator_code, sizeof ah_allocator_code);
}
int main(int argc, char **argv) {
    unsigned char *b = VirtualAlloc(NULL, AH_IMAGE_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    unsigned char *before = malloc(AH_IMAGE_SIZE);
    DWORD old; MEMORY_BASIC_INFORMATION mbi;
    check(b && before, "fixture image allocation");
    seed(b); memcpy(before, b, AH_IMAGE_SIZE);
    check(VirtualProtect(b + 0x4c1000, 0x1000, PAGE_EXECUTE_READ, &old), "make text read-only");
    check(ah_apply(b) == AH_APPLIED, "valid CE fixture patched before init");
    check(*(uint32_t *)(b + AH_NORMAL_RVA) == AH_TARGET, "normal RAM branch");
    check(*(uint32_t *)(b + AH_SMALL_RVA) == AH_TARGET, "low RAM branch");
    VirtualQuery(b + AH_NORMAL_RVA, &mbi, sizeof mbi);
    check(mbi.Protect == PAGE_EXECUTE_READ, "original page protection restored");
    memcpy(before + AH_NORMAL_RVA, b + AH_NORMAL_RVA, 4);
    memcpy(before + AH_SMALL_RVA, b + AH_SMALL_RVA, 4);
    check(!memcmp(before, b, AH_IMAGE_SIZE), "only the two capacity operands changed");
    check(ah_apply(b) == AH_WRONG_CODE, "second application fails without writing");
    check(VirtualProtect(b + 0x4c1000, 0x1000, PAGE_READWRITE, &old), "reset fixture protection");
    for (int test = 0; test < 5; ++test) {
        seed(b);
        if (test == 0) b[AH_NORMAL_RVA] ^= 1;
        if (test == 1) b[0x4850a0] ^= 1;
        if (test == 2) *(uint32_t *)(b + AH_HEAP_RVA) = 1;
        if (test == 3) *(uint32_t *)(b + AH_BLOCK_RVA) = 1;
        if (test == 4) *(uint32_t *)(b + 0xd5a3f4) = 1;
        memcpy(before, b, AH_IMAGE_SIZE);
        check(ah_apply(b) == (test < 2 ? AH_WRONG_CODE : AH_TOO_LATE), "mismatch/late-load refusal");
        check(!memcmp(before, b, AH_IMAGE_SIZE), "refusal writes nothing");
    }
    seed(b); b[0] = 0; memcpy(before, b, AH_IMAGE_SIZE);
    check(ah_apply(b) == AH_WRONG_IMAGE, "unsupported image refused");
    check(!memcmp(before, b, AH_IMAGE_SIZE), "unsupported image unmodified");
    if (argc == 2) {
        HMODULE dll = LoadLibraryA(argv[1]);
        check(dll != NULL, "production DLL loads on unsupported host");
        FreeLibrary(dll);
    }
    VirtualFree(b, 0, MEM_RELEASE); free(before);
    puts("PASS: both capacities, exact mutation scope, read-only protection restore, repeat/mismatch/late-load guards and production DLL load.");
    return 0;
}
