#include "dongle_identity.h"
#include <string.h>

bool ghost_identity_parse(const uint8_t *message, size_t length, ghost_dongle_identity_t *out) {
    if (!message || !out || length != GHOST_IDENTITY_MESSAGE_LENGTH || message[0] != 0xa5 ||
        message[1] != 0x06 || message[2] != 0x01 || message[3] != 0x01 || message[4] != 0 ||
        message[9] != 0 || message[10] != 0x20)
        return false;
    for (unsigned i = 11; i < 23; i++)
        if (message[i] < 33 || message[i] > 126)
            return false;
    for (unsigned i = 23; i < 27; i++)
        if (message[i])
            return false;
    for (unsigned i = 27; i < 35; i++)
        if (message[i] < 33 || message[i] > 126)
            return false;
    for (unsigned i = 35; i < length; i++)
        if (message[i])
            return false;
    memcpy(out->serial, message + 11, 12);
    out->serial[12] = 0;
    memcpy(out->register_key, message + 27, 8);
    out->register_key[8] = 0;
    return true;
}

bool ghost_identity_feed(ghost_identity_stream_t *stream, uint32_t seq, const uint8_t *data,
                         size_t length, ghost_dongle_identity_t *out) {
    if (!stream || !data || !out || !length)
        return false;
    if (stream->active) {
        int32_t delta = (int32_t)(seq - stream->next_seq);
        if (delta < 0) {
            size_t duplicate = (uint32_t)(stream->next_seq - seq);
            if (duplicate >= length)
                return false;
            data += duplicate;
            length -= duplicate;
            seq += duplicate;
        } else if (delta > 0)
            stream->used = 0;
    }
    stream->active = true;
    stream->next_seq = seq + (uint32_t)length;
    bool found = false;
    for (size_t i = 0; i < length; i++) {
        if (stream->used == sizeof(stream->window)) {
            memmove(stream->window, stream->window + 1, sizeof(stream->window) - 1);
            stream->used--;
        }
        stream->window[stream->used++] = data[i];
        if (stream->used == sizeof(stream->window) &&
            ghost_identity_parse(stream->window, sizeof(stream->window), out))
            found = true;
    }
    return found;
}
