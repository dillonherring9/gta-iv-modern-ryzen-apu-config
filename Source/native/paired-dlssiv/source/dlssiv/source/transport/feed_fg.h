// Frame generation's wire types; the rules are in feed_ipc.h. Kept apart because a declaration in feed_ipc.h, included mid-way
// through DLSS-IV.cpp, reorders the release's hot data. Included at the end of DLSS-IV.cpp behind DLSSIV_FG.
#pragma once
#ifndef FEED_FG_IMAGES
#error "feed_fg.h needs feed_ipc.h before it: the wire's constants are there"
#endif
#pragma pack(push, 1)
struct FeedFgBuildIvt       // 'g', game -> helper
{
    uint8_t  tag;                   // 'g'
    uint32_t disp_w, disp_h;        // the display size: the three images' size and the generation rect
    uint32_t present_fmt;           // the presented format (DXGI_FORMAT)
    uint32_t render_w, render_h;    // the guides' size; the helper refuses one that is not its set's
    uint32_t shape;                 // how the frame would be shown; logged only
    uint32_t flags;                 // FEED_FG_BUILD_RELEASE, FEED_FG_BUILD_NO_HUDLESS; wire 3: FEED_FG_BUILD_WIRE3 (the ack's layout), FEED_FG_BUILD_GEN2
};
struct FeedFgImageIvt { uint64_t handle; uint32_t format; uint32_t flags; };   // a handle valid IN THE GAME, its DXGI format, FEED_FG_IMG_UAV
struct FeedFgAckIvt         // helper -> game, read in one go after 'g'. WIRE 3'S (a 'g' with FEED_FG_BUILD_WIRE3): 124 bytes
{
    int32_t  status;                // FEED_FG_ST_*
    int32_t  ffx_rc;                // AMD's return code from the create
    FeedFgImageIvt img[FEED_FG_IMAGES];   // by the FEED_FG_* image index; all zero unless CREATED; the third all zero for a set asked with FEED_FG_BUILD_NO_HUDLESS, the fourth for one asked without FEED_FG_BUILD_GEN2
    uint64_t fence_in, fence_prep, fence_out;   // valid in the game; the SAME three in every ack of a connection, 0 before the first create
    uint64_t ctx_bytes;             // the context's video memory, by the provider's own figure
    uint64_t provider;              // the provider's id, as pinned at the probe
    int32_t  warm_rc;               // the warm-up dispatch's return code
    uint32_t warm_us;               // and how long the create and the warm-up took together
    uint32_t gen_counts;            // (wire 3) the counts of generated frames this set makes, FEED_FG_GENS_*; 0 unless CREATED
};
// Wire 2's ack, 104 bytes (a 'g' without FEED_FG_BUILD_WIRE3): three images, no gen_counts. Converted at the pipe only
// (FgxExchange; the helper's FgAnswer).
struct FeedFgAck2Ivt
{
    int32_t  status, ffx_rc;
    FeedFgImageIvt img[3];
    uint64_t fence_in, fence_prep, fence_out, ctx_bytes, provider;
    int32_t  warm_rc;
    uint32_t warm_us;
};
// The ack of a 'g' with FEED_FG_BUILD_TWIN, 220 bytes: wire 3's ack, then in2[k - 4] for the six places above four. Converted
// at the pipe like wire 2's.
struct FeedFgAck4Ivt
{
    FeedFgAckIvt   a;
    FeedFgImageIvt in2[FEED_FG_PLACES - FEED_FG_IMAGES];
};
struct FeedFgQIvt           // 'Q', worker -> helper
{
    uint8_t  tag;                   // 'Q'
    uint64_t n;                     // the injection's number
    uint32_t flags;                 // FEED_FG_Q_*
    float    cam[12];               // position, up, right, forward; the identity while FEED_FG_Q_CAMERA is clear
    float    frame_time_ms;         // the frame message's three, sent again (see the table above)
    float    camera_near;
    float    fov_y_rad;
    float    jitter_x, jitter_y;    // wire 2, at the end: the frame's jitter in pixels; AMD is handed 0
};
struct FeedFgGIvt           // 'G', worker -> helper
{
    uint8_t  tag;                   // 'G'
    uint64_t n;                     // the 'Q' it closes
    uint32_t flags;                 // bits 0..1 FEED_FG_G_* (the kind); bits 4..7 (wire 2) the reset's reasons, FEED_FG_G_RESET_MASK
    float    peak;                  // the display's peak in nits for the dispatch's luminance pair; 0 = not known (1000 is used)
};
#pragma pack(pop)
static_assert(sizeof(FeedFgBuildIvt) == 29,  "FeedFgBuildIvt ('g') layout changed: the wire table (feed_ipc.h) and DECISIONS, PHASE 2 STAGE 0, B5 say 29");
static_assert(sizeof(FeedFgImageIvt) == 16,  "FeedFgImageIvt layout changed");
static_assert(sizeof(FeedFgAckIvt)   == 124, "FeedFgAckIvt layout changed: wire 3's ack is 124 bytes (four images and gen_counts), and the game reads it in one go");
static_assert(sizeof(FeedFgAck2Ivt)  == 104, "FeedFgAck2Ivt is wire 2's ack, 104 bytes, as every helper and game of wire 2 reads and writes it");
static_assert(offsetof(FeedFgAck2Ivt, fence_in) == 8 + 3 * 16 && offsetof(FeedFgAckIvt, fence_in) == 8 + FEED_FG_IMAGES * 16 && offsetof(FeedFgAckIvt, gen_counts) == 120,
              "the two acks are the same fields with three images and with four, gen_counts last (feed_ipc.h, wire 3)");
static_assert(sizeof(FeedFgQIvt)     == 81,  "FeedFgQIvt ('Q') layout changed: 81 on wire 2 (73 on wire 1), and the pipe's budget is counted with it (DLSS-IV.cpp, beside this header's include); a layout change is a new wire revision and a new caps bit (feed_ipc.h, FEED_CAP_FG)");
static_assert(offsetof(FeedFgQIvt, jitter_x) == 73 && offsetof(FeedFgQIvt, fov_y_rad) == 69 && offsetof(FeedFgQIvt, cam) == 13, "FeedFgQIvt: wire 2 added the jitter at the end and moved nothing before it");
static_assert((FEED_FG_Q_RESET_MASK << FEED_FG_G_RESET_SHIFT) == FEED_FG_G_RESET_MASK && (FEED_FG_G_RESET_MASK & FEED_FG_G_KIND_MASK) == 0 && FEED_FG_WIRE == 3,
              "the reasons on 'G' are the four of 'Q', shifted, clear of the kind (feed_ipc.h)");
static_assert((3u << FEED_FG_G_GEN_SHIFT) == FEED_FG_G_GEN_MASK && (FEED_FG_G_GEN_MASK & (FEED_FG_G_KIND_MASK | FEED_FG_G_RESET_MASK)) == 0
              && FEED_FG_G_GEN_FLOW == 0 && FEED_FG_G_GEN_ONE == 1 && FEED_FG_G_GEN_TWO == 2
              && FEED_FG_GENS_FLOW == (1u << FEED_FG_G_GEN_FLOW) && FEED_FG_GENS_ONE == (1u << FEED_FG_G_GEN_ONE) && FEED_FG_GENS_TWO == (1u << FEED_FG_G_GEN_TWO),
              "wire 3: the count on 'G' is bits 2 and 3, the two free bits between the kind and the reasons, and gen_counts' bit k is count k (feed_ipc.h)");
static_assert(sizeof(FeedFgGIvt)     == 17,  "FeedFgGIvt ('G') layout changed: 17, and the pipe's budget is counted with it (DLSS-IV.cpp, beside this header's include)");
static_assert(FEED_FG_PRESENT == 0 && FEED_FG_GEN == 1 && FEED_FG_HUDLESS == 2 && FEED_FG_GEN2 == 3 && FEED_FG_IMAGES == 4, "the ack is indexed by the machine's image order (fg_machine.h, FgImg; GEN2 since wire 3)");
static_assert(sizeof(FeedFgAck4Ivt) == 220 && offsetof(FeedFgAck4Ivt, in2) == 124 && FEED_FG_PRESENT2 == FEED_FG_IMAGES && FEED_FG_HUDLESS2 == FEED_FG_IMAGES + 1
              && FEED_FG_GDEPTH(0) == 6 && FEED_FG_GMV(0) == 7 && FEED_FG_GDEPTH(1) == 8 && FEED_FG_GMV(1) == 9 && FEED_FG_PLACES == 10,
              "the second input set's ack is wire 3's 124 bytes and six images, the places above the ack's img[]: PRESENT2, HUDLESS2 and the two sets' guides (feed_ipc.h)");
static_assert(!(FEED_FG_BUILD_TWIN & (FEED_FG_BUILD_RELEASE | FEED_FG_BUILD_NO_HUDLESS | FEED_FG_BUILD_WIRE3 | FEED_FG_BUILD_GEN2))
              && !(FEED_FG_Q_SET1 & (FEED_FG_Q_RESET_MASK | FEED_FG_Q_CAMERA | FEED_FG_Q_PIPELINED))
              && !(FEED_FG_G_SET1 & (FEED_FG_G_KIND_MASK | FEED_FG_G_GEN_MASK | FEED_FG_G_RESET_MASK | 0x100u)),
              "the second input set's three bits are bits of their own on 'g', 'Q' and 'G' (and not the machine's mark, 0x100, on 'G')");
static_assert((FEED_FG_BUILD_WIRE3 | FEED_FG_BUILD_GEN2) & ~(FEED_FG_BUILD_RELEASE | FEED_FG_BUILD_NO_HUDLESS) && !(FEED_FG_BUILD_WIRE3 & FEED_FG_BUILD_GEN2)
              && !((FEED_FG_BUILD_WIRE3 | FEED_FG_BUILD_GEN2) & (FEED_FG_BUILD_RELEASE | FEED_FG_BUILD_NO_HUDLESS)), "wire 3's two flags of 'g' are bits of their own");

// Under Wine the game makes frame generation's fences (source/fg/fg_linux.h): Wine silently breaks a D3D12 fence imported
// from another process. With FEED_FG_BUILD_GAMEFENCES, FeedFgFencesIvt follows 'g' in the same write. A CPU-signalled probe
// value proves the fences both ways before the game holds the set; failure is FEED_FG_ST_FENCES for the connection.
#define FEED_FG_BUILD_GAMEFENCES 32u    // FeedFgBuildIvt::flags (Wine and Proton): the game made frame generation's three fences; FeedFgFencesIvt follows
#define FEED_FG_ST_FENCES        15     // (Wine and Proton) the game's fences could not be opened, or the game's test signal was not seen: refused for the connection
#define FEED_FG_WINE_PROBE_N     1ull   // the probe's value on all three (below every pair's number: under Wine frame numbers start above 4)
#define FEED_FG_WINE_PROBE_MS    1500u  // how long each side looks for the other's test signal, once a session
#pragma pack(push, 1)
struct FeedFgFencesIvt { uint64_t in, prep, out; };   // after a 'g' with FEED_FG_BUILD_GAMEFENCES: the game's three fences, as handles valid IN THE HELPER
#pragma pack(pop)
static_assert(sizeof(FeedFgFencesIvt) == 24, "FeedFgFencesIvt: three handles of 64 bits, after the 'g' that says FEED_FG_BUILD_GAMEFENCES");
static_assert(!(FEED_FG_BUILD_GAMEFENCES & (FEED_FG_BUILD_RELEASE | FEED_FG_BUILD_NO_HUDLESS | FEED_FG_BUILD_WIRE3 | FEED_FG_BUILD_GEN2 | FEED_FG_BUILD_TWIN))
              && FEED_FG_ST_FENCES > FEED_FG_ST_OBJECTS, "the game's fences: a flag of its own on 'g', a status of its own in the ack");
