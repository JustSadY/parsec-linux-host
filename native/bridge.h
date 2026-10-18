#ifndef PARSEC_HOST_BRIDGE_H
#define PARSEC_HOST_BRIDGE_H
#include <stdint.h>
struct Bridge {
    uint32_t magic, version, seq, ack, action, configured, running, busy;
    char status[256], name[128], output[64];
    int32_t fps, bitrate, codec, quality, autostart;
    char gpu[32];
    int32_t encoder, conversion, full_fps, mute_speakers, clipboard;
    uint32_t gpu_count;
    char gpu_ids[8][32], gpu_labels[8][96];
    /* Friend share link. share_enabled mirrors the service state; share_link
     * holds https://parsec.gg/g/<peer_id>/<secret>/ once the host registers. */
    int32_t share_enabled;
    char share_link[256];
};
#define BRIDGE_VERSION 3
#endif
