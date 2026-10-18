/* Runtime unit tests: settings validation, GPU resolution, host argv, the
 * friend share secret and the bridge ABI. No real hardware or network. */
#include "runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static int failures, checks;
#define CHECK(cond, name) do { checks++; if (!(cond)) { failures++; \
    fprintf(stderr, "FAIL: %s\n", name); } } while (0)

static int has_arg(char **argv, const char *value) {
    for (int i = 0; argv && argv[i]; i++)
        if (!strcmp(argv[i], value))
            return 1;
    return 0;
}

static json_t *nvidia(void) {
    return json_pack("{s:s,s:s,s:s,s:[s]}", "card", "card7", "pci", "0000:01:00.0",
                     "vendor", "10de", "outputs", "DP-1");
}
static json_t *amd(void) {
    return json_pack("{s:s,s:s,s:s,s:[]}", "card", "card2", "pci", "0000:05:00.0",
                     "vendor", "1002", "outputs");
}

static void test_defaults(void) {
    json_t *d = ph_settings_default();
    CHECK(json_integer_value(json_object_get(d, "fps")) == 60, "default fps");
    CHECK(json_integer_value(json_object_get(d, "bitrate")) == 10, "default bitrate");
    CHECK(!strcmp(json_string_value(json_object_get(d, "client_renderer")), "opengl"), "default renderer");
    CHECK(!strcmp(json_string_value(json_object_get(d, "name")), ""), "default name empty");
    json_decref(d);
}

static void test_validation(void) {
    ph_error e = {0};
    const char *bad[] = {
        "{\"command\":\"sh\"}", "{\"name\":\"bad\\nname\"}", "{\"gpu\":\"../../dev\"}",
        "{\"fps\":true}", "{\"fps\":999}", "{\"bitrate\":-1}", "{\"output\":\";sh\"}",
        "{\"encoder\":\"cpu\"}", "{\"client_renderer\":\"d3d11\"}",
        "{\"encoder\":\"nvenc\",\"conversion\":\"opengl\"}"};
    for (size_t i = 0; i < sizeof bad / sizeof *bad; i++) {
        json_t *data = json_loads(bad[i], 0, NULL);
        json_t *result = ph_settings_validate(data, &e);
        CHECK(result == NULL, bad[i]);
        json_decref(result);
        json_decref(data);
    }
    json_t *empty = json_object();
    json_t *ok = ph_settings_validate(empty, &e);
    CHECK(ok != NULL, "empty settings validate");
    json_decref(ok);
    json_decref(empty);
}

static void test_resolve_and_argv(void) {
    ph_context c;
    ph_context_init(&c);
    ph_error e = {0};
    json_t *devices = json_pack("[o,o]", amd(), nvidia());
    json_t *settings = ph_settings_default();

    json_t *chosen = ph_resolve(settings, devices, &e);
    CHECK(chosen && !strcmp(json_string_value(json_object_get(chosen, "card")), "card7"),
          "auto resolves to the connected GPU");

    char **args = ph_host_argv(&c, settings, chosen, NULL, &e);
    CHECK(args != NULL, "host argv builds");
    CHECK(has_arg(args, "card7:DP-1"), "argv targets card7:DP-1");
    CHECK(has_arg(args, "--owner-only"), "argv is owner-only without a secret");
    CHECK(!has_arg(args, "--secret"), "no secret without sharing");
    CHECK(!has_arg(args, "--encoder"), "auto encoder omits --encoder");
    CHECK(!has_arg(args, "--no-host-mute"), "default keeps speakers muted");
    ph_argv_free(args);

    char **shared = ph_host_argv(&c, settings, chosen, "0123456789abcdef", &e);
    CHECK(shared != NULL, "host argv builds with a secret");
    CHECK(has_arg(shared, "--secret"), "sharing passes --secret");
    CHECK(has_arg(shared, "0123456789abcdef"), "sharing passes the secret value");
    CHECK(!has_arg(shared, "--owner-only"), "sharing relaxes owner-only");
    ph_argv_free(shared);

    char **bad_secret = ph_host_argv(&c, settings, chosen, "NOTHEX!!", &e);
    CHECK(bad_secret == NULL, "invalid secret is rejected");
    ph_argv_free(bad_secret);

    json_decref(chosen);
    json_decref(settings);
    json_decref(devices);
}

static void test_encoder_device(void) {
    ph_context c;
    ph_context_init(&c);
    ph_error e = {0};
    json_t *n = nvidia(), *a = amd();
    json_t *vaapi = ph_settings_validate(json_pack("{s:s}", "encoder", "vaapi"), &e);
    char **r1 = ph_host_argv(&c, vaapi, n, NULL, &e);
    CHECK(r1 == NULL, "vaapi on NVIDIA is rejected");
    ph_argv_free(r1);
    json_t *nvenc = ph_settings_validate(json_pack("{s:s}", "encoder", "nvenc"), &e);
    char **r2 = ph_host_argv(&c, nvenc, a, NULL, &e);
    CHECK(r2 == NULL, "nvenc on AMD is rejected");
    ph_argv_free(r2);
    json_t *good = ph_settings_validate(
        json_pack("{s:s,s:s,s:b}", "encoder", "nvenc", "conversion", "vulkan", "full_fps", 1), &e);
    char **r3 = ph_host_argv(&c, good, n, NULL, &e);
    CHECK(r3 && has_arg(r3, "--full-fps"), "nvenc+vulkan+full_fps builds");
    ph_argv_free(r3);
    json_decref(vaapi); json_decref(nvenc); json_decref(good);
    json_decref(n); json_decref(a);
}

static void test_share_secret(void) {
    char dir[] = "/tmp/parsec-host-test-XXXXXX";
    CHECK(mkdtemp(dir) != NULL, "temp etc dir");
    ph_context c;
    ph_context_init(&c);
    c.etc = dir;
    char path[512];
    snprintf(path, sizeof path, "%s/share.json", dir);

    FILE *f = fopen(path, "w");
    fputs("{\"enabled\":true,\"secret\":\"0123456789abcdef\",\"link\":\"\"}", f);
    fclose(f);
    char *secret = ph_share_secret(&c);
    CHECK(secret && !strcmp(secret, "0123456789abcdef"), "enabled secret is returned");
    free(secret);

    f = fopen(path, "w");
    fputs("{\"enabled\":false,\"secret\":\"0123456789abcdef\"}", f);
    fclose(f);
    secret = ph_share_secret(&c);
    CHECK(secret == NULL, "disabled share returns no secret");
    free(secret);

    f = fopen(path, "w");
    fputs("{\"enabled\":true,\"secret\":\"NOTHEX\"}", f);
    fclose(f);
    secret = ph_share_secret(&c);
    CHECK(secret == NULL, "invalid secret is rejected");
    free(secret);

    unlink(path);
    rmdir(dir);
}

static const char room_frame[] =
    "{\"state\":{\"output\":\"DP-1\",\"width\":1920,\"height\":1080,\"fps\":60,\"bitrate\":10,"
    "\"codec\":\"h264\",\"guests\":[{\"id\":3,\"owner\":true,\"name\":\"lykia\",\"connected\":true},"
    "{\"id\":4,\"owner\":false,\"name\":\"friend\\nx\",\"connected\":false}]}}";

static void test_tray_state(void) {
    ph_room room;
    CHECK(ph_tray_parse_state(room_frame, strlen(room_frame), &room) == 0, "state frame parses");
    CHECK(room.count == 2 && room.guests[0].id == 3 && room.guests[1].id == 4, "guest ids");
    CHECK(room.guests[0].owner && room.guests[0].connected, "owner connected");
    CHECK(!room.guests[1].owner && !room.guests[1].connected, "friend connecting");
    CHECK(!strcmp(room.guests[1].name, "friend?x"), "control characters are not drawn");
    CHECK(room.width == 1920 && room.fps == 60 && !strcmp(room.codec, "h264"), "stream fields");
    CHECK(ph_tray_parse_state("{\"bye\":\"x\"}", 11, &room) == 1, "other frames are not a state");
    CHECK(ph_tray_parse_state("nope", 4, &room) == -1, "garbage is refused");
}

static void frame_out(int fd, const char *body) {
    uint32_t n = (uint32_t)strlen(body);
    unsigned char head[4] = {n & 0xff, (n >> 8) & 0xff, (n >> 16) & 0xff, n >> 24};
    CHECK(write(fd, head, 4) == 4 && write(fd, body, n) == (ssize_t)n, "test frame written");
}

static int frame_in(int fd, char *out, size_t size) {
    unsigned char head[4];
    if (read(fd, head, 4) != 4)
        return -1;
    size_t n = head[0] | head[1] << 8 | head[2] << 16 | (size_t)head[3] << 24;
    if (n >= size || read(fd, out, n) != (ssize_t)n)
        return -1;
    out[n] = 0;
    return 0;
}

static void test_tray_channel(void) {
    int sv[2];
    CHECK(!socketpair(AF_UNIX, SOCK_STREAM, 0, sv), "socketpair");
    ph_tray tray;
    ph_tray_init(&tray);
    ph_error e = {0};
    CHECK(!ph_tray_adopt(&tray, sv[0], &e), "tray adopts a socket");
    char body[512];
    CHECK(!frame_in(sv[1], body, sizeof body) &&
          !strcmp(body, "{\"version\":1,\"role\":\"tray\",\"can\":[]}"), "tray says hello");
    ph_room room;
    CHECK(ph_tray_read(&tray, &room) == 0, "nothing yet");
    frame_out(sv[1], "{\"bye\":\"later\"}");
    frame_out(sv[1], room_frame);
    CHECK(ph_tray_read(&tray, &room) == 1 && room.count == 2, "state arrives after another frame");
    CHECK(!ph_tray_kick(&tray, 4, &e), "kick is sent");
    CHECK(!frame_in(sv[1], body, sizeof body) && !strcmp(body, "{\"kick\":4}"), "kick frame");
    close(sv[1]);
    CHECK(ph_tray_read(&tray, &room) == -1, "a closed channel is reported");
    ph_tray_close(&tray);
    CHECK(tray.fd == -1 && ph_tray_kick(&tray, 1, &e) == -1, "no kick without a channel");
}

static void test_room_published(void) {
    struct Bridge b;
    memset(&b, 0, sizeof b);
    ph_room room;
    ph_tray_parse_state(room_frame, strlen(room_frame), &room);
    ph_bridge_publish_room(&b, &room);
    CHECK(b.guest_count == 2 && b.guest_ids[1] == 4, "guests published");
    CHECK(!strcmp(b.guest_titles[0], "lykia (#3)"), "title carries the id");
    CHECK(!strcmp(b.guest_details[0], "Your account · connected"), "owner details");
    CHECK(!strcmp(b.guest_details[1], "Invited guest · connecting"), "guest details");
    CHECK(!strcmp(b.guest_kick[1], "Kick##guest-4"), "kick button has its own id");
    CHECK(!strcmp(b.stream, "Streaming 1920x1080 · 60 FPS · 10 Mbps · H.264"), "stream line");
    ph_bridge_publish_room(&b, NULL);
    CHECK(b.guest_count == 0 && !b.stream[0], "room cleared");
}

static void test_bridge_abi(void) {
    CHECK(sizeof(struct Bridge) == 3320, "bridge struct size");
    struct Bridge b;
    memset(&b, 0, sizeof b);
    json_t *status = json_pack("{s:b,s:s}", "share_enabled", 1, "share_link",
                               "https://parsec.gg/g/PEER/SECRET/");
    ph_bridge_extra_refresh(&b, status);
    CHECK(b.share_enabled == 1, "extra refresh copies share_enabled");
    CHECK(!strcmp(b.share_link, "https://parsec.gg/g/PEER/SECRET/"), "extra refresh copies link");
    json_decref(status);
}

int main(void) {
    test_defaults();
    test_validation();
    test_resolve_and_argv();
    test_encoder_device();
    test_share_secret();
    test_tray_state();
    test_tray_channel();
    test_room_published();
    test_bridge_abi();
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
