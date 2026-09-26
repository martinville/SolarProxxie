#include "protocol.h"
#include <string.h>
/* Explicit configured profile avoids confusing a 302-byte record split at byte
 * 292 with a complete older record. No unverified envelope length is inferred. */
void ghost_stream_feed(ghost_stream_t *s, uint32_t seq, const uint8_t *p, size_t n,
                       ghost_frame_fn emit, void *context) {
    if (!s || !p || !n || !emit || s->frame_length < 43 || s->frame_length > GHOST_FRAME_MAX)
        return;
    if (s->active) {
        int32_t delta = (int32_t)(seq - s->next_seq);
        if (delta < 0) {
            size_t duplicate = (uint32_t)(s->next_seq - seq);
            if (duplicate >= n)
                return;
            p += duplicate;
            n -= duplicate;
            seq += duplicate;
        } else if (delta > 0) {
            s->used = 0;
            s->active = false;
        }
    }
    if (!s->active) {
        if (p[0] != 0xa5)
            return;
        s->used = 0;
        s->active = true;
    }
    s->next_seq = seq + (uint32_t)n;
    if (n > sizeof(s->bytes) - s->used) {
        s->active = false;
        s->used = 0;
        return;
    }
    memcpy(s->bytes + s->used, p, n);
    s->used += n;
    while (s->used >= s->frame_length) {
        if (s->bytes[0] != 0xa5) {
            s->used = 0;
            s->active = false;
            return;
        }
        emit(s->bytes, s->frame_length, context);
        s->used -= s->frame_length;
        memmove(s->bytes, s->bytes + s->frame_length, s->used);
    }
}
