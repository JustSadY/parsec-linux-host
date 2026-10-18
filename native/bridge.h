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
    /* The live room from lowlatd's local tray channel. Titles and details are
     * formatted by the launcher; the page only draws them. kick_guest is the
     * id the page asks to remove with action 10. */
    uint32_t guest_count;
    uint32_t guest_ids[8];
    char guest_titles[8][80], guest_details[8][64], guest_kick[8][24];
    uint32_t kick_guest;
    char stream[96];
    /* Whether each listed GPU has a connected display and so can capture.
     * GPUs without one are listed after the others, labelled, and the page
     * does not select them. */
    int32_t gpu_usable[8];
    /* The service's exact registration, never an account-wide guess. */
    char host_peer_id[32], share_secret[65];
    uint32_t guest_user_ids[8];
    char guest_names[8][32];
    uint8_t guest_permissions[8][3];
    uint32_t pending_count;
    uint64_t pending_ids[8];
    uint32_t pending_user_ids[8];
    char pending_names[8][32];
    uint64_t decision_id;
    uint32_t permissions_guest;
    uint8_t permissions_value[3];
};
#define BRIDGE_VERSION 6
#define BRIDGE_GUESTS_MAX 8
#endif
