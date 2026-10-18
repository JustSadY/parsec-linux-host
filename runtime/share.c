/* Friend invite-link sharing.
 *
 * The invite link is the vendor web guest path, https://parsec.gg/g/<peer>/<secret>/.
 * <secret> is generated here and advertised by lowlatd; the signaling service
 * gates admission on it, so only someone holding the link connects. <peer> is
 * this host's registration id, read back from the account's own host listing.
 *
 * State lives in /etc/parsec-host/share.json: {enabled, secret, link}. The
 * secret travels inside the link by design, so the file is world-readable; it
 * never holds the account session. Only the root helper writes it.
 */
#include "runtime.h"
#include <fcntl.h>
#include <openssl/rand.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static void share_path(ph_context *c, char *out, size_t size) {
    ph_path(out, size, c->etc, "share.json");
}

/* The enabled secret, validated, or NULL. Used by the root runner and helper. */
char *ph_share_secret(ph_context *c) {
    char path[PATH_MAX];
    share_path(c, path, sizeof path);
    json_t *s = ph_read_json(path, 4096, 0, NULL);
    if (!s)
        return NULL;
    char *out = NULL;
    const char *secret = json_string_value(json_object_get(s, "secret"));
    if (json_is_true(json_object_get(s, "enabled")) && secret) {
        size_t n = strlen(secret);
        int hex = n >= 8 && n <= 64;
        for (size_t i = 0; i < n && hex; i++)
            hex = (secret[i] >= '0' && secret[i] <= '9') || (secret[i] >= 'a' && secret[i] <= 'f');
        if (hex)
            out = strdup(secret);
    }
    json_decref(s);
    return out;
}

/* Reported to the unprivileged UI on every status poll; no network or root. */
json_t *ph_share_status(ph_context *c, ph_error *e) {
    (void)e;
    char path[PATH_MAX];
    share_path(c, path, sizeof path);
    json_t *s = ph_read_json(path, 4096, 0, NULL);
    int enabled = s && json_is_true(json_object_get(s, "enabled"));
    const char *link = s ? json_string_value(json_object_get(s, "link")) : NULL;
    json_t *out = json_pack("{s:b,s:s}", "share_enabled", enabled, "share_link", link ? link : "");
    json_decref(s);
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

static char *machine_id(void) {
    int fd = open("/etc/machine-id", O_RDONLY | O_CLOEXEC);
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

/* The peer id from the account's own desktop host listing. Retried briefly,
 * because a just-restarted host takes a moment to register and appear. */
static char *resolve_peer(ph_context *c, const char *token) {
    char *id = machine_id();
    char *peer = NULL;
    for (int attempt = 0; attempt < 6 && !peer; attempt++) {
        if (attempt) {
            struct timespec delay = {1, 500000000};
            nanosleep(&delay, NULL);
        }
        ph_error ignored = {0};
        json_t *answer = c->request(c, "GET", "/v2/hosts?mode=desktop", NULL, token, &ignored);
        if (!answer)
            continue;
        json_t *data = json_object_get(answer, "data"), *host = NULL, *chosen = NULL;
        size_t i;
        json_array_foreach(data, i, host) {
            const char *dev = json_string_value(json_object_get(host, "device_id"));
            int self = json_is_true(json_object_get(host, "self"));
            if (self || (id && dev && !strcmp(id, dev))) {
                chosen = host;
                break;
            }
        }
        if (!chosen && json_array_size(data) == 1)
            chosen = json_array_get(data, 0);
        if (chosen) {
            const char *value = json_string_value(json_object_get(chosen, "peer_id"));
            if (!value)
                value = json_string_value(json_object_get(chosen, "access_link_id"));
            if (value && *value)
                peer = strdup(value);
        }
        json_decref(answer);
    }
    free(id);
    return peer;
}

/* Fetch the peer id with the root session and write the resolved link back. */
static int resolve_and_store(ph_context *c, json_t *state, ph_error *e) {
    char path[PATH_MAX];
    ph_path(path, sizeof path, c->etc, "account.json");
    json_t *account = ph_read_json(path, 8192, 1, e);
    if (!account)
        return ph_fail(e, "Connect your Host account first");
    const char *token = json_string_value(json_object_get(account, "session_id"));
    const char *secret = json_string_value(json_object_get(state, "secret"));
    char *peer = token && secret ? resolve_peer(c, token) : NULL;
    int ok = 0;
    if (peer) {
        char link[256];
        snprintf(link, sizeof link, "https://parsec.gg/g/%s/%s/", peer, secret);
        json_object_set_new(state, "link", json_string(link));
        share_path(c, path, sizeof path);
        ok = ph_atomic_json(path, state, 0644, e) == 0;
        free(peer);
    }
    json_decref(account);
    return ok ? 0 : -1;
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
    char path[PATH_MAX];
    share_path(c, path, sizeof path);

    if (unshare) {
        json_t *state = json_pack("{s:b,s:s,s:s}", "enabled", 0, "secret", "", "link", "");
        int failed = ph_atomic_json(path, state, 0644, e);
        json_decref(state);
        if (failed)
            return NULL;
        json_t *status = ph_status(c, e);
        if (status) {
            const char *active = json_string_value(
                json_object_get(json_object_get(status, "service"), "ActiveState"));
            if (active && !strcmp(active, "active"))
                systemctl_do(c, "restart", e);
            json_decref(status);
        }
        return ph_share_status(c, e);
    }

    /* share / share_resolve: keep an existing secret, mint one if missing. */
    json_t *state = ph_read_json(path, 4096, 0, NULL);
    if (!state)
        state = json_object();
    char *existing = ph_share_secret(c);
    if (existing) {
        json_object_set_new(state, "secret", json_string(existing));
        free(existing);
    } else if (!resolve) {
        unsigned char raw[16];
        if (RAND_bytes(raw, sizeof raw) != 1) {
            json_decref(state);
            ph_fail(e, "Cannot generate a share secret");
            return NULL;
        }
        char secret[33];
        for (int i = 0; i < 16; i++)
            snprintf(secret + i * 2, 3, "%02x", raw[i]);
        json_object_set_new(state, "secret", json_string(secret));
    } else if (!json_is_true(json_object_get(state, "enabled"))) {
        json_decref(state);
        return ph_share_status(c, e); /* resolve with nothing to share */
    }
    json_object_set_new(state, "enabled", json_true());
    if (!json_object_get(state, "link"))
        json_object_set_new(state, "link", json_string(""));
    if (ph_atomic_json(path, state, 0644, e)) {
        json_decref(state);
        return NULL;
    }

    /* The host must be registered for a peer id to exist, so start it. */
    json_t *status = ph_status(c, e);
    if (!status) {
        json_decref(state);
        return NULL;
    }
    int configured = json_is_true(json_object_get(status, "configured"));
    const char *active = json_string_value(
        json_object_get(json_object_get(status, "service"), "ActiveState"));
    int running = active && !strcmp(active, "active");
    json_decref(status);
    if (!configured) {
        json_decref(state);
        ph_fail(e, "Connect your Host account first");
        return NULL;
    }
    if (share && systemctl_do(c, running ? "restart" : "start", e)) {
        json_decref(state);
        return NULL;
    }
    ph_error ignored = {0};
    resolve_and_store(c, state, &ignored);
    json_decref(state);
    return ph_share_status(c, e);
}

void ph_bridge_extra_refresh(struct Bridge *b, json_t *result) {
    b->share_enabled = json_is_true(json_object_get(result, "share_enabled"));
    const char *link = json_string_value(json_object_get(result, "share_link"));
    size_t n = link ? strlen(link) : 0;
    if (n >= sizeof b->share_link)
        n = sizeof b->share_link - 1;
    memcpy(b->share_link, link ? link : "", n);
    b->share_link[n] = 0;
}

static int copy_clipboard(ph_context *c, const char *text, ph_error *e) {
    const char *tools[][4] = {
        {"/usr/bin/wl-copy", NULL, NULL, NULL},
        {"/usr/bin/xclip", "-selection", "clipboard", NULL},
        {"/usr/bin/xsel", "--clipboard", "--input", NULL}};
    for (int i = 0; i < 3; i++) {
        if (access(tools[i][0], X_OK))
            continue;
        ph_process p;
        if (!c->run(c, (char *const *)tools[i], text, NULL, 10, &p, e)) {
            int code = p.code;
            ph_process_free(&p);
            if (!code)
                return 0;
        } else {
            ph_process_free(&p);
        }
    }
    return ph_fail(e, "No clipboard tool found; copy the link manually");
}

int ph_bridge_extra_action(ph_bridge *w, unsigned action, ph_error *e) {
    if (action == 8) {
        if (!w->state->share_link[0])
            return ph_fail(e, "No share link yet");
        if (copy_clipboard(w->ctx, w->state->share_link, e))
            return -1;
        ph_bridge_message(w->state, "Invite link copied to the clipboard.");
        return 0;
    }
    const char *call = action == 6 ? "share" : action == 7 ? "unshare"
                                           : action == 9   ? "share_resolve"
                                                           : NULL;
    if (!call)
        return ph_fail(e, "Unknown native Host action");
    json_t *reply = ph_helper_call(w->ctx, call, NULL, 1, e);
    if (!reply)
        return -1;
    json_decref(reply);
    if (ph_bridge_refresh(w, 0, 0, e))
        return -1;
    if (action == 7)
        ph_bridge_message(w->state, "Sharing stopped.");
    else if (w->state->share_link[0]) {
        ph_error ignored = {0};
        if (!copy_clipboard(w->ctx, w->state->share_link, &ignored))
            ph_bridge_message(w->state, "Invite link ready and copied to the clipboard.");
        else
            ph_bridge_message(w->state, "Invite link ready.");
    } else {
        ph_bridge_message(w->state, "Sharing on. Start hosting, then choose Get link.");
    }
    return 0;
}
