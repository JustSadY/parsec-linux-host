/* Native Parsec invite links use an eight-character hexadecimal secret.
 * Account credentials stay in the root-only account file. The bearer invite
 * lives in share.json, readable only by root and the desktop owner's group. */
#include "runtime.h"
#include <errno.h>
#include <fcntl.h>
#include <openssl/rand.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static void share_path(ph_context *c, char *out, size_t size) {
    ph_path(out, size, c->etc, "share.json");
}
static void peer_path(ph_context *c, char *out, size_t size) {
    snprintf(out, size, "%s", c->peer_cache);
}
static int valid_secret(const char *secret) {
    if (!secret || strlen(secret) != 8)
        return 0;
    for (size_t i = 0; i < 8; i++)
        if (!((secret[i] >= '0' && secret[i] <= '9') ||
              (secret[i] >= 'a' && secret[i] <= 'f')))
            return 0;
    return 1;
}
static int valid_peer(const char *peer) {
    if (!peer || !*peer || strlen(peer) >= 32)
        return 0;
    for (const unsigned char *p = (const unsigned char *)peer; *p; p++)
        if (!((*p >= '0' && *p <= '9') || (*p >= 'A' && *p <= 'Z') ||
              (*p >= 'a' && *p <= 'z') || *p == '-' || *p == '_'))
            return 0;
    return 1;
}
static int share_write(ph_context *c, json_t *state, ph_error *e) {
    char path[PATH_MAX], tmp[PATH_MAX], owner_path[PATH_MAX];
    share_path(c, path, sizeof path);
    ph_path(owner_path, sizeof owner_path, c->etc, "owner.json");
    json_t *owner = ph_read_json(owner_path, 1024, 0, e);
    json_t *gid_value = owner ? json_object_get(owner, "gid") : NULL;
    json_int_t gid = json_integer_value(gid_value);
    int valid = json_is_integer(gid_value) && gid >= 0 && (uint64_t)gid <= UINT32_MAX;
    json_decref(owner);
    if (!valid)
        return ph_fail(e, "Invalid desktop owner group");
    if (snprintf(tmp, sizeof tmp, "%s.new-XXXXXX", path) >= (int)sizeof tmp)
        return ph_fail(e, "Host configuration path is too long");
    int fd = mkostemp(tmp, O_CLOEXEC);
    if (fd < 0)
        return ph_fail(e, "Cannot create share configuration");
    int failed = fchown(fd, (uid_t)-1, (gid_t)gid) || fchmod(fd, 0640) ||
        json_dumpfd(state, fd, JSON_ENSURE_ASCII | JSON_COMPACT) ||
        write(fd, "\n", 1) != 1 || fsync(fd);
    if (close(fd))
        failed = 1;
    if (!failed && rename(tmp, path))
        failed = 1;
    if (failed) {
        unlink(tmp);
        return ph_fail(e, "Cannot save share configuration");
    }
    return 0;
}

char *ph_share_secret(ph_context *c) {
    char path[PATH_MAX];
    share_path(c, path, sizeof path);
    json_t *s = ph_read_json(path, 4096, 0, NULL);
    const char *secret = s ? json_string_value(json_object_get(s, "secret")) : NULL;
    char *out = s && json_is_true(json_object_get(s, "enabled")) && valid_secret(secret)
        ? strdup(secret) : NULL;
    json_decref(s);
    return out;
}

/* A peer id is selected only from an exact self registration. A machine-id
 * match is a compatibility fallback when the API supplies that identity.
 * Ambiguous, offline or unrelated rows never become this computer's card. */
char *ph_peer_select(json_t *answer, const char *machine) {
    json_t *data = json_object_get(answer, "data"), *host;
    const char *chosen = NULL;
    size_t i;
    for (int pass = 0; pass < 2; pass++) {
        chosen = NULL;
        json_array_foreach(data, i, host) {
            if (json_is_false(json_object_get(host, "online")))
                continue;
            const char *dev = json_string_value(json_object_get(host, "device_id"));
            int match = pass == 0 ? json_is_true(json_object_get(host, "self"))
                : machine && *machine && dev && !strcmp(machine, dev);
            const char *peer = json_string_value(json_object_get(host, "peer_id"));
            if (!match || !valid_peer(peer))
                continue;
            if (chosen && strcmp(chosen, peer))
                return NULL;
            chosen = peer;
        }
        if (chosen)
            return strdup(chosen);
    }
    return NULL;
}
static char *machine_id(void) {
    int fd = open("/etc/machine-id", O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0)
        return NULL;
    char buffer[64] = {0};
    ssize_t n = read(fd, buffer, sizeof buffer - 1);
    close(fd);
    if (n <= 0)
        return NULL;
    buffer[strcspn(buffer, "\r\n")] = 0;
    return *buffer ? strdup(buffer) : NULL;
}
char *ph_peer_resolve(ph_context *c, const char *token) {
    if (c->cancel && *c->cancel)
        return NULL;
    ph_error ignored = {0};
    json_t *answer = c->request(c, "GET", "/v2/hosts?mode=desktop", NULL, token, &ignored);
    char *machine = machine_id();
    char *peer = answer ? ph_peer_select(answer, machine) : NULL;
    free(machine);
    json_decref(answer);
    return peer;
}
int ph_peer_publish(ph_context *c, const char *peer, ph_error *e) {
    char path[PATH_MAX];
    peer_path(c, path, sizeof path);
    if (!peer) {
        if (unlink(path) && errno != ENOENT)
            return ph_fail(e, "Cannot clear host registration");
        return 0;
    }
    if (!valid_peer(peer))
        return ph_fail(e, "Invalid host registration");
    json_t *state = json_pack("{s:s}", "peer_id", peer);
    int result = ph_atomic_json(path, state, 0644, e);
    json_decref(state);
    return result;
}
static char *cached_peer(ph_context *c) {
    char path[PATH_MAX];
    peer_path(c, path, sizeof path);
    json_t *state = ph_read_json(path, 1024, 0, NULL);
    const char *value = state ? json_string_value(json_object_get(state, "peer_id")) : NULL;
    char *peer = valid_peer(value) ? strdup(value) : NULL;
    json_decref(state);
    return peer;
}

/* Local-only status: the public peer id is available with sharing disabled.
 * Derive the link from validated components rather than trusting a stale URL. */
json_t *ph_share_status(ph_context *c, ph_error *e) {
    (void)e;
    char *peer = cached_peer(c), *secret = ph_share_secret(c), link[256] = "";
    if (peer && secret)
        snprintf(link, sizeof link, "https://parsec.gg/g/%s/%s/", peer, secret);
    json_t *out = json_pack("{s:b,s:s,s:s,s:s}", "share_enabled", secret != NULL,
        "share_link", link, "share_secret", secret ? secret : "",
        "host_peer_id", peer ? peer : "");
    if (secret)
        explicit_bzero(secret, strlen(secret));
    free(secret);
    free(peer);
    return out;
}
static int systemctl_do(ph_context *c, const char *action, ph_error *e) {
    char *args[] = {(char *)c->systemctl, (char *)action, PH_UNIT, NULL};
    ph_process p;
    if (c->run(c, args, NULL, NULL, 25, &p, e)) {
        ph_process_free(&p);
        return -1;
    }
    int code = p.code;
    ph_process_free(&p);
    return code ? ph_fail(e, "Service operation failed; open Host logs") : 0;
}
static int resolve_and_store(ph_context *c, ph_error *e) {
    char path[PATH_MAX];
    ph_path(path, sizeof path, c->etc, "account.json");
    json_t *account = ph_read_json(path, 8192, 1, e);
    if (!account)
        return ph_fail(e, "Connect your Host account first");
    const char *token = json_string_value(json_object_get(account, "session_id"));
    char *peer = NULL;
    for (int attempt = 0; token && attempt < 6 && !peer; attempt++) {
        if (c->cancel && *c->cancel)
            break;
        if (attempt) {
            struct timespec delay = {0, 100000000};
            for (int i = 0; i < 15 && !(c->cancel && *c->cancel); i++)
                nanosleep(&delay, NULL);
        }
        peer = ph_peer_resolve(c, token);
    }
    int result = -1;
    if (peer) {
        /* Network completion may race with a user revoking sharing. Publish
         * only public identity; never rewrite an earlier invite snapshot. */
        result = ph_peer_publish(c, peer, e);
        free(peer);
    }
    json_decref(account);
    return result;
}
json_t *ph_share_dispatch(ph_context *c, const char *action, json_t *data, ph_error *e) {
    (void)data;
    int share = !strcmp(action, "share"), unshare = !strcmp(action, "unshare"),
        resolve = !strcmp(action, "share_resolve");
    if (!share && !unshare && !resolve) {
        ph_fail(e, "Unknown action");
        return NULL;
    }
    if (ph_claim_owner(c, e))
        return NULL;
    if (unshare) {
        json_t *state = json_pack("{s:b,s:s,s:s}", "enabled", 0, "secret", "", "link", "");
        int failed = share_write(c, state, e);
        json_decref(state);
        return failed ? NULL : ph_share_status(c, e);
    }
    char *existing = ph_share_secret(c);
    if (resolve && !existing)
        return ph_share_status(c, e);
    json_t *status = ph_status(c, e);
    if (!status) {
        free(existing);
        return NULL;
    }
    int configured = json_is_true(json_object_get(status, "configured"));
    const char *active = json_string_value(
        json_object_get(json_object_get(status, "service"), "ActiveState"));
    int running = active && !strcmp(active, "active");
    json_decref(status);
    if (!configured) {
        free(existing);
        ph_fail(e, "Connect your Host account first");
        return NULL;
    }
    char secret[9];
    if (existing) {
        memcpy(secret, existing, sizeof secret);
        explicit_bzero(existing, strlen(existing));
        free(existing);
    } else {
        unsigned char raw[4];
        if (RAND_bytes(raw, sizeof raw) != 1) {
            ph_fail(e, "Cannot generate a share secret");
            return NULL;
        }
        for (int i = 0; i < 4; i++)
            snprintf(secret + i * 2, 3, "%02x", raw[i]);
        explicit_bzero(raw, sizeof raw);
    }
    json_t *state = json_pack("{s:b,s:s,s:s}", "enabled", 1, "secret", secret, "link", "");
    explicit_bzero(secret, sizeof secret);
    if (share_write(c, state, e)) {
        json_decref(state);
        return NULL;
    }
    /* lowlatd polls this file. Publishing/revoking never restarts a running
     * host, so current guests and the owner session remain connected. */
    if (share && !running && systemctl_do(c, "start", e)) {
        json_decref(state);
        return NULL;
    }
    ph_error ignored = {0};
    if (share || running)
        resolve_and_store(c, &ignored);
    json_decref(state);
    return ph_share_status(c, e);
}
static void bridge_text(char *out, size_t cap, const char *in) {
    size_t n = in ? strlen(in) : 0;
    if (n >= cap)
        n = cap - 1;
    if (n)
        memcpy(out, in, n);
    out[n] = 0;
}
void ph_bridge_extra_refresh(struct Bridge *b, json_t *result) {
    const char *peer = json_string_value(json_object_get(result, "host_peer_id"));
    const char *secret = json_string_value(json_object_get(result, "share_secret"));
    bridge_text(b->host_peer_id, sizeof b->host_peer_id, valid_peer(peer) ? peer : "");
    bridge_text(b->share_secret, sizeof b->share_secret, valid_secret(secret) ? secret : "");
    b->share_enabled = json_is_true(json_object_get(result, "share_enabled")) && b->share_secret[0];
    if (!b->share_enabled)
        b->share_secret[0] = 0;
    b->share_link[0] = 0;
    if (b->share_enabled && b->host_peer_id[0])
        snprintf(b->share_link, sizeof b->share_link, "https://parsec.gg/g/%s/%s/",
                 b->host_peer_id, b->share_secret);
}
int ph_bridge_extra_action(ph_bridge *w, unsigned action, ph_error *e) {
    if (action == 11 || action == 12) {
        if (ph_tray_decide(&w->tray, w->state->decision_id, action == 11, e))
            return -1;
        ph_bridge_message(w->state, action == 11 ? "Connection accepted." : "Connection rejected.");
        return 0;
    }
    if (action == 13) {
        if (ph_tray_permissions(&w->tray, w->state->permissions_guest,
                                w->state->permissions_value, e))
            return -1;
        ph_bridge_message(w->state, "Guest permissions updated.");
        return 0;
    }
    const char *call = action == 6 ? "share" : action == 7 ? "unshare"
                                           : action == 9 ? "share_resolve" : NULL;
    if (!call)
        return ph_fail(e, "Unknown native Host action");
    json_t *reply = ph_helper_call(w->ctx, call, NULL, 1, e);
    if (!reply)
        return -1;
    json_decref(reply);
    if (ph_bridge_refresh(w, 0, 0, e))
        return -1;
    ph_bridge_message(w->state, action == 7 ? "Sharing stopped." :
        w->state->share_link[0] ? "Invite link ready." : "Sharing on. Waiting for host registration.");
    return 0;
}
