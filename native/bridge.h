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
};
#define BRIDGE_VERSION 2
#endif
