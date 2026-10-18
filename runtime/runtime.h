#ifndef PH_RUNTIME_H
#define PH_RUNTIME_H
#define _GNU_SOURCE
#include <jansson.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <sys/types.h>
#include "../native/bridge.h"

#define PH_UNIT "parsec-host.service"
#define PH_LIB "/usr/lib/parsec-linux-host"
#define PH_ETC "/etc/parsec-host"
#define PH_ERROR_SIZE 256
typedef struct { char text[PH_ERROR_SIZE]; } ph_error;
typedef struct { int code; char *out; } ph_process;
typedef struct ph_context ph_context;
struct ph_context {
    const char *etc, *lib, *drm, *systemctl, *journalctl, *helper, *pkexec;
    const char *dropin, *peer_cache, *rules;
    uid_t euid, caller_uid;
    volatile sig_atomic_t *cancel;
    int (*run)(ph_context *, char *const [], const char *, char *const [], int, ph_process *, ph_error *);
    json_t *(*request)(ph_context *, const char *, const char *, json_t *, const char *, ph_error *);
};
void ph_context_init(ph_context *);
int ph_fail(ph_error *, const char *, ...);
void ph_path(char *, size_t, const char *, const char *);
json_t *ph_read_json(const char *, size_t, int, ph_error *);
int ph_atomic_json(const char *, json_t *, mode_t, ph_error *);
int ph_run(ph_context *, char *const [], const char *, char *const [], int, ph_process *, ph_error *);
void ph_process_free(ph_process *);
double ph_now(void);

json_t *ph_settings_default(void);
json_t *ph_settings_validate(json_t *, ph_error *);
json_t *ph_settings_load(ph_context *, ph_error *);
json_t *ph_inventory(const char *, ph_error *);
json_t *ph_resolve(json_t *, json_t *, ph_error *);
json_t *ph_wait_display(ph_context *, json_t *, ph_error *);
json_t *ph_encoder_select(json_t *, json_t *, ph_error *);
char **ph_host_argv(ph_context *, json_t *, json_t *, const char *secret, ph_error *);
void ph_argv_free(char **);
json_t *ph_status(ph_context *, ph_error *);
json_t *ph_dispatch(ph_context *, const char *, json_t *, ph_error *);
json_t *ph_request(ph_context *, const char *, const char *, json_t *, const char *, ph_error *);
json_t *ph_helper_call(ph_context *, const char *, json_t *, int, ph_error *);
int ph_claim_owner(ph_context *, ph_error *);
int ph_hostctl_main(int, char **);
int ph_runner_main(void);
int ph_launcher_main(int, char **);

/* lowlatd's local session channel in the tray role: the live room and kick.
 * Frames are a little-endian u32 length and a JSON body. */
#define PH_SESSION_SOCKET "/run/parsec-host/session"
typedef struct {
    uint32_t id, user_id;
    uint8_t permissions[3];
    int owner, connected;
    char name[128];
} ph_guest;
typedef struct {
    uint64_t id;
    uint32_t user_id;
    char name[128];
} ph_pending;
typedef struct {
    ph_guest guests[BRIDGE_GUESTS_MAX];
    ph_pending pending[BRIDGE_GUESTS_MAX];
    unsigned pending_count;
    unsigned count, width, height, fps, bitrate;
    char codec[8];
} ph_room;
typedef struct {
    int fd;
    unsigned char *buffer;
    size_t used;
} ph_tray;
void ph_tray_init(ph_tray *);
int ph_tray_open(ph_tray *, const char *path, ph_error *);
int ph_tray_adopt(ph_tray *, int fd, ph_error *);
int ph_tray_read(ph_tray *, ph_room *);
int ph_tray_kick(ph_tray *, uint32_t guest, ph_error *);
int ph_tray_decide(ph_tray *, uint64_t pending, int accept, ph_error *);
int ph_tray_permissions(ph_tray *, uint32_t guest, const uint8_t permissions[3], ph_error *);
void ph_tray_close(ph_tray *);
int ph_tray_parse_state(const char *body, size_t length, ph_room *);
void ph_bridge_publish_room(struct Bridge *, const ph_room *);
void ph_bridge_publish_gpus(struct Bridge *, json_t *devices);

typedef struct {
    ph_context *ctx;
    struct Bridge *state;
    char directory[PATH_MAX], path[PATH_MAX];
    pthread_t thread;
    volatile sig_atomic_t stopped;
    int read_only, started;
    json_t *settings;
    ph_tray tray;
    double tray_retry;
} ph_bridge;
int ph_bridge_start(ph_bridge *, ph_context *, int, ph_error *);
void ph_bridge_stop(ph_bridge *);
json_t *ph_bridge_form(ph_bridge *, const struct Bridge *, ph_error *);
int ph_bridge_refresh(ph_bridge *, int, int, ph_error *);
void ph_bridge_message(struct Bridge *, const char *);

/* Friend invite-link sharing. Strong definitions in share.c replace the
 * harmless weak defaults in control.c and bridge.c. */
char *ph_share_secret(ph_context *);
char *ph_peer_select(json_t *, const char *machine_id);
char *ph_peer_resolve(ph_context *, const char *token);
int ph_peer_publish(ph_context *, const char *peer, ph_error *);
json_t *ph_share_status(ph_context *, ph_error *);
json_t *ph_share_dispatch(ph_context *, const char *, json_t *, ph_error *);
void ph_bridge_extra_refresh(struct Bridge *, json_t *);
int ph_bridge_extra_action(ph_bridge *, unsigned, ph_error *);
#endif
