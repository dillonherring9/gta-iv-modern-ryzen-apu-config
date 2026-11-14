// A file's identity: SHA-256, a file's size and hash, and two string tests, for fg_own.h's load gate (every build with frame
// generation) and fg_lib.h's development load. fg_host.h's rules hold: no static but SHA-256's constant table, nothing hot.
#pragma once

// ---- SHA-256 (FIPS 180-4), written out ----
// The helper imports no crypto library. The bench compares the hash printed with Python's hashlib of the staged file.
static const uint32_t kFglK[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u };
struct FglSha { uint32_t h[8]; UINT64 bytes; BYTE buf[64]; UINT at; };
static void FglShaBlock(FglSha* s, const BYTE* p)
{
    uint32_t w[64];
    for (int i = 0; i < 16; i++) w[i] = ((uint32_t)p[4 * i] << 24) | ((uint32_t)p[4 * i + 1] << 16) | ((uint32_t)p[4 * i + 2] << 8) | (uint32_t)p[4 * i + 3];
    for (int i = 16; i < 64; i++)
    {
        const uint32_t s0 = _rotr(w[i - 15], 7) ^ _rotr(w[i - 15], 18) ^ (w[i - 15] >> 3), s1 = _rotr(w[i - 2], 17) ^ _rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = s->h[0], b = s->h[1], c = s->h[2], d = s->h[3], e = s->h[4], f = s->h[5], g = s->h[6], hh = s->h[7];
    for (int i = 0; i < 64; i++)
    {
        const uint32_t t1 = hh + (_rotr(e, 6) ^ _rotr(e, 11) ^ _rotr(e, 25)) + ((e & f) ^ (~e & g)) + kFglK[i] + w[i];
        const uint32_t t2 = (_rotr(a, 2) ^ _rotr(a, 13) ^ _rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    s->h[0] += a; s->h[1] += b; s->h[2] += c; s->h[3] += d; s->h[4] += e; s->h[5] += f; s->h[6] += g; s->h[7] += hh;
}
static void FglShaInit(FglSha* s)
{
    s->h[0] = 0x6a09e667u; s->h[1] = 0xbb67ae85u; s->h[2] = 0x3c6ef372u; s->h[3] = 0xa54ff53au; s->h[4] = 0x510e527fu; s->h[5] = 0x9b05688cu; s->h[6] = 0x1f83d9abu; s->h[7] = 0x5be0cd19u;
    s->bytes = 0; s->at = 0;
}
static void FglShaAdd(FglSha* s, const BYTE* p, size_t n)
{
    s->bytes += n;
    while (n)
    {
        if (!s->at && n >= 64) { FglShaBlock(s, p); p += 64; n -= 64; continue; }
        const size_t take = n < 64 - s->at ? n : 64 - s->at;
        memcpy(s->buf + s->at, p, take); s->at += (UINT)take; p += take; n -= take;
        if (s->at == 64) { FglShaBlock(s, s->buf); s->at = 0; }
    }
}
static void FglShaEnd(FglSha* s, BYTE out[32])
{
    const UINT64 bits = s->bytes * 8;
    BYTE pad[72] = { 0x80 };
    const size_t n = (s->at < 56 ? 56 : 120) - s->at;
    for (int i = 0; i < 8; i++) pad[n + i] = (BYTE)(bits >> (56 - 8 * i));
    FglShaAdd(s, pad, n + 8);
    for (int i = 0; i < 8; i++) { out[4 * i] = (BYTE)(s->h[i] >> 24); out[4 * i + 1] = (BYTE)(s->h[i] >> 16); out[4 * i + 2] = (BYTE)(s->h[i] >> 8); out[4 * i + 3] = (BYTE)s->h[i]; }
}
static const char* FglHex(const BYTE sha[32], char out[65])
{
    for (int i = 0; i < 32; i++) { out[2 * i] = "0123456789abcdef"[sha[i] >> 4]; out[2 * i + 1] = "0123456789abcdef"[sha[i] & 15]; }
    out[64] = 0;
    return out;
}
// A file's size and SHA-256. deny: the handle shares reading alone, so nobody writes or replaces the file while it is
// open; with keep it stays open for the caller (who loads the file and then closes it). False with the system's code.
static bool FglHashFile(const char* path, bool deny, UINT64* size, BYTE sha[32], HANDLE* keep, DWORD* err)
{
    *size = 0; *err = 0; if (keep) *keep = nullptr;
    const HANDLE f = CreateFileA(path, GENERIC_READ, deny ? FILE_SHARE_READ : (FILE_SHARE_READ | FILE_SHARE_DELETE), nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (f == INVALID_HANDLE_VALUE) { *err = GetLastError(); return false; }
    enum { CHUNK = 1 << 20 };
    BYTE* const buf = (BYTE*)HeapAlloc(GetProcessHeap(), 0, CHUNK);
    bool ok = buf != nullptr;
    if (!ok) *err = ERROR_NOT_ENOUGH_MEMORY;
    FglSha s; FglShaInit(&s);
    for (DWORD got = 1; ok && got; )
    {
        got = 0;
        ok = ReadFile(f, buf, CHUNK, &got, nullptr) != 0;
        if (!ok) *err = GetLastError(); else if (got) FglShaAdd(&s, buf, got);
    }
    if (buf) HeapFree(GetProcessHeap(), 0, buf);
    if (ok) { *size = s.bytes; FglShaEnd(&s, sha); }
    if (ok && keep) *keep = f; else CloseHandle(f);
    return ok;
}

// Do two names start alike over n characters, ASCII case-insensitive; n at a path's capacity compares whole paths. And two SHA-256
// values. Written out, like every string test here: the C runtime's versions move runtime statics in the helper's .data, which
// tools\prove-fg-off.py --dev holds fixed.
static bool FglSameSha(const BYTE* a, const BYTE* b)
{
    BYTE d = 0;
    for (int i = 0; i < 32; i++) d |= (BYTE)(a[i] ^ b[i]);
    return d == 0;
}
static bool FglSameStart(const char* a, const char* b, size_t n)
{
    for (size_t i = 0; i < n; i++)
    {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z') x += 32;
        if (y >= 'A' && y <= 'Z') y += 32;
        if (x != y) return false;
        if (!x) break;
    }
    return true;
}
