// bonehash.h: the hash of the shadowed bone block, on its own so tools/perfproof/equiv_bonehash.cpp compiles this very text.
// Four FNV-1a lanes, not one chain: one accumulator is a loop-carried dependency of about four cycles a word over 552 words.
// The lanes are the four words of each group, so the loads stay one cache line an iteration.
// Every consumer is an equality test within one run (pose dedupe, pose-changed test, memo), so only equal inputs giving equal
// hashes matters. HashWorld is not treated this way: its value picks a slot in a saturated table.
// n is a multiple of four by construction, (boneMaxReg + 1) * 4, so there is no tail. n = 0 folds to a non-zero constant,
// since 0 is the "no pose" sentinel.
#pragma once

static inline unsigned BoneHashWords(const unsigned* u, int n)
{
    unsigned h0 = 2166136261u, h1 = 2166136261u, h2 = 2166136261u, h3 = 2166136261u;
    for (int i = 0; i + 4 <= n; i += 4)
    {
        h0 = (h0 ^ u[i])     * 16777619u;
        h1 = (h1 ^ u[i + 1]) * 16777619u;
        h2 = (h2 ^ u[i + 2]) * 16777619u;
        h3 = (h3 ^ u[i + 3]) * 16777619u;
    }
    unsigned h = 2166136261u;
    h = (h ^ h0) * 16777619u;
    h = (h ^ h1) * 16777619u;
    h = (h ^ h2) * 16777619u;
    h = (h ^ h3) * 16777619u;
    return h;
}
