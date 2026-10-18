/* lowlatd's local session channel, as a tray: the room as the service tells
 * it, and a request to kick one guest.
 *
 * The socket is created by the root service in its own runtime directory,
 * limited to the desktop owner's group, and the service admits only the
 * owner's uid. This end checks the other way round: the peer must be root.
 */
#include "runtime.h"
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

/* The room is a few hundred bytes; anything far larger is not a state. */
#define TRAY_BUFFER (64 * 1024)

void ph_tray_init(ph_tray *t) {
    t->fd = -1;
    t->buffer = NULL;
    t->used = 0;
}

static int send_frame(int fd, const char *body, ph_error *e) {
    size_t length = strlen(body);
    unsigned char frame[512];
    if (length > sizeof frame - 4)
        return ph_fail(e, "Tray frame too large");
    frame[0] = length & 0xff;
    frame[1] = (length >> 8) & 0xff;
    frame[2] = (length >> 16) & 0xff;
    frame[3] = (length >> 24) & 0xff;
    memcpy(frame + 4, body, length);
    ssize_t sent = send(fd, frame, length + 4, MSG_NOSIGNAL);
    return sent == (ssize_t)(length + 4) ? 0 : ph_fail(e, "Host service channel unavailable");
}

int ph_tray_adopt(ph_tray *t, int fd, ph_error *e) {
    t->buffer = malloc(TRAY_BUFFER);
    if (!t->buffer) {
        close(fd);
        return ph_fail(e, "Out of memory");
    }
    t->fd = fd;
    t->used = 0;
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
    if (send_frame(fd, "{\"version\":1,\"role\":\"tray\",\"can\":[]}", e)) {
        ph_tray_close(t);
        return -1;
    }
    return 0;
}

int ph_tray_open(ph_tray *t, const char *path, ph_error *e) {
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return ph_fail(e, "Cannot open the host service channel");
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    snprintf(address.sun_path, sizeof address.sun_path, "%s", path);
    if (connect(fd, (struct sockaddr *)&address, sizeof address)) {
        close(fd);
        return ph_fail(e, "Host service channel unavailable");
    }
    struct ucred peer;
    socklen_t length = sizeof peer;
    if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &peer, &length) || peer.uid != 0) {
        close(fd);
        return ph_fail(e, "Host service channel is not owned by root");
    }
    return ph_tray_adopt(t, fd, e);
}

void ph_tray_close(ph_tray *t) {
    if (t->fd >= 0)
        close(t->fd);
    free(t->buffer);
    ph_tray_init(t);
}

int ph_tray_kick(ph_tray *t, uint32_t guest, ph_error *e) {
    if (t->fd < 0)
        return ph_fail(e, "Host service channel unavailable");
    char body[48];
    snprintf(body, sizeof body, "{\"kick\":%u}", guest);
    return send_frame(t->fd, body, e);
}

int ph_tray_decide(ph_tray *t, uint64_t pending, int accept, ph_error *e) {
    if (t->fd < 0)
        return ph_fail(e, "Host service channel unavailable");
    if (!pending || pending > INT64_MAX || (accept != 0 && accept != 1))
        return ph_fail(e, "Invalid connection request");
    char body[64];
    snprintf(body, sizeof body, "{\"%s\":%" PRIu64 "}", accept ? "approve" : "reject", pending);
    return send_frame(t->fd, body, e);
}
int ph_tray_permissions(ph_tray *t, uint32_t guest, const uint8_t permissions[3], ph_error *e) {
    if (t->fd < 0)
        return ph_fail(e, "Host service channel unavailable");
    for (unsigned i = 0; i < 3; i++)
        if (permissions[i] > 1)
            return ph_fail(e, "Invalid guest permissions");
    char body[160];
    snprintf(body, sizeof body,
        "{\"permissions\":{\"id\":%u,\"gamepad\":%s,\"keyboard\":%s,\"mouse\":%s}}",
        guest, permissions[0] ? "true" : "false", permissions[1] ? "true" : "false",
        permissions[2] ? "true" : "false");
    return send_frame(t->fd, body, e);
}

static void copy_text(char *out, size_t size, const char *in) {
    size_t n = in ? strlen(in) : 0;
    if (n >= size)
        n = size - 1;
    while (n && ((unsigned char)in[n] & 0xc0) == 0x80)
        n--; /* never split a UTF-8 sequence */
    if (n)
        memcpy(out, in, n);
    out[n] = 0;
    for (size_t i = 0; i < n; i++)
        if ((unsigned char)out[i] < 0x20)
            out[i] = '?';
}

/* 0 for a state, 1 for another kind of frame, -1 for one that is not JSON. */
int ph_tray_parse_state(const char *body, size_t length, ph_room *room) {
    json_t *frame = json_loadb(body, length, 0, NULL);
    if (!frame)
        return -1;
    json_t *state = json_object_get(frame, "state");
    if (!json_is_object(state)) {
        json_decref(frame);
        return 1;
    }
    memset(room, 0, sizeof *room);
    room->width = (unsigned)json_integer_value(json_object_get(state, "width"));
    room->height = (unsigned)json_integer_value(json_object_get(state, "height"));
    room->fps = (unsigned)json_integer_value(json_object_get(state, "fps"));
    room->bitrate = (unsigned)json_integer_value(json_object_get(state, "bitrate"));
    copy_text(room->codec, sizeof room->codec, json_string_value(json_object_get(state, "codec")));
    json_t *guest;
    size_t i;
    json_array_foreach(json_object_get(state, "guests"), i, guest) {
        if (room->count >= BRIDGE_GUESTS_MAX)
            break;
        json_t *id = json_object_get(guest, "id");
        if (!json_is_integer(id) || json_integer_value(id) < 0 || json_integer_value(id) > UINT32_MAX)
            continue;
        ph_guest *g = &room->guests[room->count++];
        g->id = (uint32_t)json_integer_value(id);
        json_t *user_id = json_object_get(guest, "user_id");
        json_int_t user = json_integer_value(user_id);
        g->user_id = json_is_integer(user_id) && user >= 0 && user <= UINT32_MAX ? (uint32_t)user : 0;
        json_t *permissions = json_object_get(guest, "permissions");
        const char *keys[] = {"gamepad", "keyboard", "mouse"};
        for (unsigned j = 0; j < 3; j++)
            g->permissions[j] = json_is_true(json_object_get(permissions, keys[j]));
        g->owner = json_is_true(json_object_get(guest, "owner"));
        g->connected = json_is_true(json_object_get(guest, "connected"));
        copy_text(g->name, sizeof g->name, json_string_value(json_object_get(guest, "name")));
    }
    json_t *pending;
    json_array_foreach(json_object_get(state, "pending"), i, pending) {
        if (room->pending_count >= BRIDGE_GUESTS_MAX)
            break;
        json_t *id_value = json_object_get(pending, "id");
        json_int_t id = json_integer_value(id_value);
        if (!json_is_integer(id_value) || id <= 0)
            continue;
        /* Reject duplicate IDs so one native menu never targets two rows. */
        int duplicate = 0;
        for (unsigned j = 0; j < room->pending_count; j++)
            if (room->pending[j].id == (uint64_t)id)
                duplicate = 1;
        if (duplicate)
            continue;
        ph_pending *p = &room->pending[room->pending_count++];
        p->id = (uint64_t)id;
        json_t *user_id = json_object_get(pending, "user_id");
        json_int_t user = json_integer_value(user_id);
        p->user_id = json_is_integer(user_id) && user >= 0 && user <= UINT32_MAX ? (uint32_t)user : 0;
        copy_text(p->name, sizeof p->name, json_string_value(json_object_get(pending, "name")));
    }
    json_decref(frame);
    return 0;
}

/* 1 when a new room arrived, 0 for nothing yet, -1 when the channel closed. */
int ph_tray_read(ph_tray *t, ph_room *room) {
    if (t->fd < 0)
        return -1;
    int fresh = 0;
    for (;;) {
        if (t->used == TRAY_BUFFER)
            return -1; /* a frame that cannot fit is not a tray's */
        ssize_t n = recv(t->fd, t->buffer + t->used, TRAY_BUFFER - t->used, 0);
        if (n == 0)
            return -1;
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            if (errno == EINTR)
                continue;
            return -1;
        }
        t->used += (size_t)n;
        while (t->used >= 4) {
            size_t length = (size_t)t->buffer[0] | (size_t)t->buffer[1] << 8 |
                            (size_t)t->buffer[2] << 16 | (size_t)t->buffer[3] << 24;
            if (length > TRAY_BUFFER - 4)
                return -1;
            if (t->used < 4 + length)
                break;
            if (!ph_tray_parse_state((const char *)t->buffer + 4, length, room))
                fresh = 1;
            memmove(t->buffer, t->buffer + 4 + length, t->used - 4 - length);
            t->used -= 4 + length;
        }
    }
    return fresh;
}
