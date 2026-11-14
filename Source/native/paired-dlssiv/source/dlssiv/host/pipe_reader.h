#pragma once
// The helper's buffered pipe reader: the game writes a message's tag and body in one WriteFile (IvtSendFrame, DlssIvTransport.h),
// so reading up to a whole in-buffer at once gets a frame in one ReadFile instead of two. The framing is unchanged and the loop owns
// it; bytes past the current message stay here, in order.
// A larger read never waits for bytes that are not coming: a synchronous ReadFile on a byte-mode pipe completes with whatever is
// there (Wine's pipes too). tools/test-pipe-reader proves the framing on a real pipe.
// Every read after the handshake must go through this reader, or bytes it holds would be skipped; the handshake reads exactly
// (ReadFull) before it starts.
#include <windows.h>
#include <cstring>

struct HostPipeReader
{
    HANDLE pipe;
    DWORD  pos, len;             // the unread bytes are buf[pos .. len)
    DWORD  fills;                // ReadFile calls made, for the session line and the test
    BYTE   buf[1024];            // FEED_PIPE_BYTES, the pipe's own in-buffer: one read can take all it holds
};

static void HostPipeReaderInit(HostPipeReader* r, HANDLE pipe)
{
    r->pipe = pipe; r->pos = 0; r->len = 0; r->fills = 0;
}

// Exactly len bytes into dst, or false when the pipe fails or closes first (the loop then ends the session, as the old
// ReadFull's false did).
static bool HostPipeRead(HostPipeReader* r, void* dst, DWORD len)
{
    BYTE* p = (BYTE*)dst;
    while (len)
    {
        if (r->pos == r->len)
        {
            DWORD got = 0;
            if (!ReadFile(r->pipe, r->buf, (DWORD)sizeof(r->buf), &got, nullptr) || got == 0) return false;
            r->pos = 0; r->len = got; r->fills++;
        }
        DWORD take = r->len - r->pos;
        if (take > len) take = len;
        memcpy(p, r->buf + r->pos, take);
        r->pos += take; p += take; len -= take;
    }
    return true;
}
