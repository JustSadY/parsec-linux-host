/* Runtime unit tests: settings validation, GPU resolution, host argv, the
 * friend share secret and the bridge ABI. No real hardware or network. */
#include "runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <pwd.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sys/wait.h>
#include <time.h>

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
        "{\"encoder\":\"nvenc\",\"conversion\":\"opengl\"}",
        "{\"max_guests\":0}", "{\"max_guests\":9}", "{\"port\":80}", "{\"port\":70000}",
        "{\"port\":\"9000\"}", "{\"exclusive_input\":1}", "{\"audio_device\":\"--synth\"}",
        "{\"audio_device\":\"a b\"}", "{\"audio_device\":\"\"}"};
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

static const char *arg_after(char **argv, const char *flag) {
    for (int i = 0; argv && argv[i]; i++)
        if (!strcmp(argv[i], flag))
            return argv[i + 1];
    return NULL;
}

/* Parsec host options that map onto lowlatd flags, and their defaults. */
static void test_host_options(void) {
    ph_error e = {0};
    ph_context c;
    ph_context_init(&c);
    json_t *devices = json_pack("[o]", nvidia());
    json_t *defaults = ph_settings_default();
    json_t *device = ph_resolve(defaults, devices, &e);
    char **args = ph_host_argv(&c, defaults, device, NULL, &e);
    CHECK(args && arg_after(args, "--max-guests") && !strcmp(arg_after(args, "--max-guests"), "1"),
          "default stays one guest");
    CHECK(!has_arg(args, "--port") && !has_arg(args, "--exclusive-pointer") && !has_arg(args, "--audio-device"),
          "defaults leave port, input and audio to lowlatd");
    ph_argv_free(args);
    json_t *asked = json_pack("{s:i,s:i,s:b,s:s}", "max_guests", 4, "port", 9100,
        "exclusive_input", 1, "audio_device", "alsa_output.usb-0600_USBFC1-A-00.analog-stereo.monitor");
    json_t *set = ph_settings_validate(asked, &e);
    json_decref(asked);
    CHECK(set != NULL, "host options validate");
    args = set ? ph_host_argv(&c, set, device, NULL, &e) : NULL;
    CHECK(args && !strcmp(arg_after(args, "--max-guests"), "4"), "max guests reaches lowlatd");
    CHECK(args && !strcmp(arg_after(args, "--port"), "9100"), "start port reaches lowlatd");
    CHECK(args && has_arg(args, "--exclusive-pointer"), "exclusive input reaches lowlatd");
    CHECK(args && !strcmp(arg_after(args, "--audio-device"), "alsa_output.usb-0600_USBFC1-A-00.analog-stereo.monitor"),
          "audio device reaches lowlatd as one argument");
    ph_argv_free(args);

    struct Bridge b;
    memset(&b, 0, sizeof b);
    ph_bridge w = {.settings = set};
    strcpy(b.name, "");strcpy(b.output, "auto");strcpy(b.gpu, "auto");strcpy(b.encoding_gpu, "auto");
    b.fps = 60; b.bitrate = 10; b.max_guests = 3; b.exclusive_input = 1;
    strcpy(b.audio_device, "auto"); strcpy(b.port, "");
    json_t *form = ph_bridge_form(&w, &b, &e);
    CHECK(form && json_integer_value(json_object_get(form, "port")) == 0 &&
          json_integer_value(json_object_get(form, "max_guests")) == 3 &&
          json_is_true(json_object_get(form, "exclusive_input")) &&
          !strcmp(json_string_value(json_object_get(form, "audio_device")), "auto"),
          "native form carries host options; empty port is automatic");
    json_decref(form);
    strcpy(b.port, "9a");
    form = ph_bridge_form(&w, &b, &e);
    CHECK(!form && strstr(e.text, "port"), "a non-numeric port is refused with a reason");
    strcpy(b.port, "99999");
    form = ph_bridge_form(&w, &b, &e);
    CHECK(!form, "an out-of-range port is refused");
    strcpy(b.port, "9200");
    form = ph_bridge_form(&w, &b, &e);
    CHECK(form && json_integer_value(json_object_get(form, "port")) == 9200, "typed port is saved");
    json_decref(form);
    b.max_guests = 9;
    form = ph_bridge_form(&w, &b, &e);
    CHECK(!form, "out-of-range guest count is refused");
    json_decref(form);

    memset(&b, 0, sizeof b);
    ph_bridge_load_form(&b, set);
    CHECK(!strcmp(b.port, "9100") && b.max_guests == 4 && b.exclusive_input == 1, "saved options load into the page");

    json_t *outputs = json_pack("[{s:s,s:s},{s:s,s:s}]",
        "id", "alsa_output.pci-0000_0c_00.4.analog-stereo.monitor", "label", "Speakers",
        "id", "bluez_output.AA_BB.1.monitor", "label", "Headset");
    strcpy(b.audio_device, "auto");
    ph_bridge_publish_audio(&b, outputs);
    CHECK(b.audio_count == 2 && !strcmp(b.audio_labels[1], "Headset"), "outputs are listed");
    strcpy(b.audio_device, "alsa_output.usb-gone.analog-stereo.monitor");
    ph_bridge_publish_audio(&b, outputs);
    CHECK(b.audio_count == 3 && !strcmp(b.audio_ids[2], b.audio_device) &&
          strstr(b.audio_labels[2], "(unavailable)"), "a configured device that is gone stays visible");
    json_t *many = json_array();
    for (int i = 0; i < 12; i++) {
        char id[64];
        snprintf(id, sizeof id, "sink%d.monitor", i);
        json_array_append_new(many, json_pack("{s:s,s:s}", "id", id, "label", id));
    }
    strcpy(b.audio_device, "sink11.monitor");
    ph_bridge_publish_audio(&b, many);
    int seen = 0;
    for (unsigned i = 0; i < b.audio_count; i++)
        if (!strcmp(b.audio_ids[i], "sink11.monitor")) seen = 1;
    CHECK(b.audio_count == 8 && seen && !strstr(b.audio_labels[7], "unavailable"),
          "the configured device keeps a slot in a long list");
    json_decref(many);
    json_decref(outputs);
    json_decref(set);
    json_decref(device);
    json_decref(defaults);
    json_decref(devices);
}

static int pactl_calls;
static int fake_pactl(ph_context *c, char *const argv[], const char *input,
                      char *const env[], int timeout, ph_process *out, ph_error *e) {
    (void)c; (void)input; (void)env; (void)timeout; (void)e;
    pactl_calls++;
    out->code = 0;
    out->out = strdup(!strcmp(argv[0], "/usr/bin/pactl") && !strcmp(argv[4], "sinks")
        ? "[{\"name\":\"alsa_output.a\",\"description\":\"Speakers\",\"monitor_source\":\"alsa_output.a.monitor\"},"
          "{\"name\":\"bad\",\"description\":\"Bad\",\"monitor_source\":\"-bad\"}]"
        : "not json");
    return 0;
}
static void test_audio_outputs(void) {
    ph_error e = {0};
    ph_context c;
    ph_context_init(&c);
    c.run = fake_pactl;
    json_t *outputs = ph_audio_outputs(&c, &e);
    CHECK(pactl_calls == 1 && json_array_size(outputs) == 1 &&
          !strcmp(json_string_value(json_object_get(json_array_get(outputs, 0), "id")), "alsa_output.a.monitor") &&
          !strcmp(json_string_value(json_object_get(json_array_get(outputs, 0), "label")), "Speakers"),
          "pactl sinks become monitor sources; unsafe names are dropped");
    json_decref(outputs);
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

    char **shared = ph_host_argv(&c, settings, chosen, "01234567", &e);
    CHECK(shared != NULL, "host argv builds with a secret");
    CHECK(has_arg(shared, "--secret"), "sharing passes --secret");
    CHECK(has_arg(shared, "01234567"), "sharing passes the secret value");
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

static void test_separate_encoder(void) {
    ph_error e={0};json_t *devices=json_pack("[o,o]",amd(),nvidia());
    json_t *settings=ph_settings_default();
    CHECK(!strcmp(json_string_value(json_object_get(settings,"encoding_gpu")),"auto"),"old configurations keep same-GPU encoding");
    json_object_set_new(settings,"encoding_gpu",json_string("0000:05:00.0"));
    json_t *selected=ph_encoder_select(settings,devices,&e);
    CHECK(selected&&!strcmp(json_string_value(json_object_get(selected,"vendor")),"1002")&&!json_array_size(json_object_get(selected,"outputs")),"headless AMD is valid for encoding");
    json_decref(selected);
    json_object_set_new(settings,"encoding_gpu",json_string("0000:01:00.0"));
    selected=ph_encoder_select(settings,devices,&e);CHECK(!selected,"separate NVENC adapter is not falsely advertised");json_decref(selected);
    json_object_set_new(settings,"encoding_gpu",json_string("0000:99:00.0"));
    selected=ph_encoder_select(settings,devices,&e);CHECK(!selected,"missing or VFIO encoder is refused");json_decref(selected);
    json_object_set_new(settings,"encoding_gpu",json_string("../../tmp"));
    json_t *valid=ph_settings_validate(settings,&e);CHECK(!valid,"encoding device accepts PCI identity only");json_decref(valid);
    struct Bridge b={0};strcpy(b.encoding_gpu,"0000:05:00.0");strcpy(b.gpu,"0000:01:00.0");strcpy(b.output,"DP-1");b.fps=60;b.bitrate=25;b.encoder=2;b.max_guests=1;strcpy(b.audio_device,"auto");
    ph_bridge w={.settings=ph_settings_default()};json_t *form=ph_bridge_form(&w,&b,&e);
    CHECK(form&&!strcmp(json_string_value(json_object_get(form,"encoding_gpu")),"0000:05:00.0"),"native selection survives validated autosave snapshot");
    json_decref(form);json_decref(w.settings);json_decref(settings);json_decref(devices);
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
    fputs("{\"enabled\":true,\"secret\":\"01234567\",\"link\":\"\"}", f);
    fclose(f);
    char *secret = ph_share_secret(&c);
    CHECK(secret && !strcmp(secret, "01234567"), "enabled secret is returned");
    free(secret);

    f = fopen(path, "w");
    fputs("{\"enabled\":false,\"secret\":\"01234567\"}", f);
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
    "\"codec\":\"h264\",\"guests\":[{\"id\":3,\"user_id\":123,\"owner\":true,\"name\":\"lykia\",\"connected\":true,"
    "\"permissions\":{\"gamepad\":true,\"keyboard\":true,\"mouse\":true}},"
    "{\"id\":4,\"user_id\":456,\"owner\":false,\"name\":\"friend\\nx\",\"connected\":false,"
    "\"permissions\":{\"gamepad\":true,\"keyboard\":false,\"mouse\":false}}],"
    "\"pending\":[{\"id\":9007199254740993,\"user_id\":789,\"name\":\"Misafir\"}]}}";

static void test_tray_state(void) {
    ph_room room;
    CHECK(ph_tray_parse_state(room_frame, strlen(room_frame), &room) == 0, "state frame parses");
    CHECK(room.count == 2 && room.guests[0].id == 3 && room.guests[1].id == 4, "guest ids");
    CHECK(room.guests[0].owner && room.guests[0].connected, "owner connected");
    CHECK(!room.guests[1].owner && !room.guests[1].connected, "friend connecting");
    CHECK(!strcmp(room.guests[1].name, "friend?x"), "control characters are not drawn");
    CHECK(room.guests[0].user_id == 123 && room.guests[1].user_id == 456, "guest user ids");
    CHECK(room.guests[0].permissions[1] && room.guests[1].permissions[0] &&
          !room.guests[1].permissions[1], "guest permissions parsed without upgrading guests");
    CHECK(room.pending_count == 1 && room.pending[0].id == UINT64_C(9007199254740993) &&
          room.pending[0].user_id == 789, "pending keeps integer precision above JavaScript exact range");
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
    CHECK(!ph_tray_decide(&tray, UINT64_C(9007199254740993), 1, &e), "approval sent");
    CHECK(!frame_in(sv[1], body, sizeof body) &&
          !strcmp(body, "{\"approve\":9007199254740993}"), "approval id has full precision");
    CHECK(!ph_tray_decide(&tray, 77, 0, &e), "rejection sent");
    CHECK(!frame_in(sv[1], body, sizeof body) && !strcmp(body, "{\"reject\":77}"), "rejection frame");
    CHECK(ph_tray_decide(&tray, 0, 1, &e) == -1 &&
          ph_tray_decide(&tray, UINT64_MAX, 1, &e) == -1 &&
          ph_tray_decide(&tray, 1, 2, &e) == -1, "malformed decisions refused");
    uint8_t permissions[3] = {1, 0, 1};
    CHECK(!ph_tray_permissions(&tray, 4, permissions, &e), "permissions sent");
    CHECK(!frame_in(sv[1], body, sizeof body) && !strcmp(body,
        "{\"permissions\":{\"id\":4,\"gamepad\":true,\"keyboard\":false,\"mouse\":true}}"),
        "permissions frame requires three booleans");
    permissions[0] = 2;
    CHECK(ph_tray_permissions(&tray, 4, permissions, &e) == -1, "nonboolean permission refused");
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
    CHECK(b.guest_user_ids[0] == 123 && !strcmp(b.guest_names[1], "friend?x"),
          "native names and user ids published");
    CHECK(b.guest_permissions[0][2] && !b.guest_permissions[1][2], "native permissions published");
    CHECK(b.pending_count == 1 && b.pending_ids[0] == UINT64_C(9007199254740993) &&
          b.pending_user_ids[0] == 789 && !strcmp(b.pending_names[0], "Misafir"),
          "native pending request published");
    CHECK(!strcmp(b.stream, "Streaming 1920x1080 · 60 FPS · 10 Mbps · H.264"), "stream line");
    ph_bridge_publish_room(&b, NULL);
    CHECK(b.guest_count == 0 && !b.stream[0] && b.pending_count == 0 &&
          !b.pending_ids[0] && !b.pending_names[0][0] && !b.guest_names[0][0], "room cleared");
}

static void test_gpus_published(void) {
    struct Bridge b;
    memset(&b, 0, sizeof b);
    json_t *devices = json_pack(
        "[{s:s,s:s,s:s,s:[]},{s:s,s:s,s:s,s:[s]}]",
        "pci", "0000:05:00.0", "label", "AMD [67df] · 0000:05:00.0", "vendor", "1002", "outputs",
        "pci", "0000:01:00.0", "label", "NVIDIA [2f04] · 0000:01:00.0", "vendor", "10de", "outputs", "DP-1");
    ph_bridge_publish_gpus(&b, devices);
    CHECK(b.gpu_count == 2, "every GPU is listed");
    CHECK(!b.gpu_encoding_ok[0]&&b.gpu_encoding_ok[1],"headless AMD remains selectable for encoding");
    CHECK(!strcmp(b.gpu_ids[0], "0000:01:00.0") && b.gpu_usable[0], "a GPU with a display comes first");
    CHECK(!strcmp(b.gpu_labels[0], "NVIDIA [2f04] · 0000:01:00.0"), "its label is unchanged");
    CHECK(!strcmp(b.gpu_ids[1], "0000:05:00.0") && !b.gpu_usable[1], "a GPU without one follows");
    CHECK(!strcmp(b.gpu_labels[1], "AMD [67df] · 0000:05:00.0 (no display connected)"),
          "and says why it cannot capture");
    json_decref(devices);
}

static void test_bridge_abi(void) {
    CHECK(sizeof(struct Bridge) == 6144, "bridge struct size");
    struct Bridge b;
    memset(&b, 0, sizeof b);
    json_t *status = json_pack("{s:b,s:s,s:s,s:s}", "share_enabled", 1,
        "share_link", "https://untrusted.invalid/stale", "host_peer_id", "PEER",
        "share_secret", "01234567");
    ph_bridge_extra_refresh(&b, status);
    CHECK(b.share_enabled == 1, "extra refresh copies share_enabled");
    CHECK(!strcmp(b.share_link, "https://parsec.gg/g/PEER/01234567/"), "extra refresh copies link");
    CHECK(!strcmp(b.host_peer_id, "PEER") && !strcmp(b.share_secret, "01234567"),
          "extra refresh copies native card fields");
    json_object_set_new(status, "share_secret", json_string("0123456789abcdef"));
    ph_bridge_extra_refresh(&b, status);
    CHECK(!b.share_enabled && !b.share_link[0] && !b.share_secret[0],
          "legacy long secret never yields an incompatible native link");
    json_decref(status);
}

static void test_peer_identity(void) {
    const char *answers[] = {
        "{\"data\":[{\"peer_id\":\"REMOTE\",\"self\":false}]}",
        "{\"data\":[{\"peer_id\":\"REMOTE\",\"self\":false,\"device_id\":\"elsewhere\"}]}",
        "{\"data\":[{\"peer_id\":\"A\",\"self\":true},{\"peer_id\":\"B\",\"self\":true}]}",
        "{\"data\":[{\"peer_id\":\"../../bad\",\"self\":true}]}",
        "{\"data\":[{\"peer_id\":\"OLD\",\"self\":true,\"online\":false}]}"
    };
    for (size_t i = 0; i < sizeof answers / sizeof *answers; i++) {
        json_t *answer = json_loads(answers[i], 0, NULL);
        char *peer = ph_peer_select(answer, "this-machine");
        CHECK(peer == NULL, "unrelated, ambiguous, invalid or offline peer is not local");
        free(peer);
        json_decref(answer);
    }
    json_t *answer = json_loads(
        "{\"data\":[{\"peer_id\":\"OTHER\",\"self\":false},"
        "{\"peer_id\":\"LOCAL_ab-12\",\"self\":true}]}", 0, NULL);
    char *peer = ph_peer_select(answer, "this-machine");
    CHECK(peer && !strcmp(peer, "LOCAL_ab-12"), "self row wins among account hosts");
    free(peer);
    json_decref(answer);
    answer = json_loads("{\"data\":[{\"peer_id\":\"LOCAL\",\"device_id\":\"this-machine\"}]}", 0, NULL);
    peer = ph_peer_select(answer, "this-machine");
    CHECK(peer && !strcmp(peer, "LOCAL"), "verified exact machine id fallback");
    free(peer);
    json_decref(answer);
}
static int request_count;
static json_t *fake_request(ph_context *c, const char *method, const char *path,
                            json_t *body, const char *token, ph_error *e) {
    (void)c; (void)body; (void)e;
    request_count++;
    CHECK(!strcmp(method, "GET") && !strcmp(path, "/v2/hosts?mode=desktop") &&
          !strcmp(token, "unit-test-session"), "peer resolver uses only scoped host listing");
    return json_loads("{\"data\":[{\"peer_id\":\"THIS\",\"self\":true}]}", 0, NULL);
}
static void test_peer_status(void) {
    char dir[] = "/tmp/parsec-peer-test-XXXXXX";
    CHECK(mkdtemp(dir) != NULL, "peer fixture directory");
    ph_context c;
    ph_context_init(&c);
    c.etc = dir;
    char peer_path[512];
    snprintf(peer_path, sizeof peer_path, "%s/host-peer.json", dir);
    c.peer_cache = peer_path;
    c.request = fake_request;
    ph_error e = {0};
    request_count = 0;
    char *peer = ph_peer_resolve(&c, "unit-test-session");
    CHECK(peer && !strcmp(peer, "THIS") && request_count == 1, "peer resolves with host session");
    CHECK(!ph_peer_publish(&c, peer, &e), "public host peer cache published");
    free(peer);
    json_t *status = ph_share_status(&c, &e);
    CHECK(!strcmp(json_string_value(json_object_get(status, "host_peer_id")), "THIS") &&
          !json_is_true(json_object_get(status, "share_enabled")), "host identity exists when sharing is off");
    json_decref(status);
    char path[512];
    snprintf(path, sizeof path, "%s/share.json", dir);
    FILE *f = fopen(path, "w");
    fputs("{\"enabled\":true,\"secret\":\"a0b1c2d3\",\"link\":\"https://wrong.invalid\"}", f);
    fclose(f);
    status = ph_share_status(&c, &e);
    CHECK(!strcmp(json_string_value(json_object_get(status, "share_link")),
                  "https://parsec.gg/g/THIS/a0b1c2d3/"), "link derives from exact peer and native secret");
    json_decref(status);
    CHECK(!ph_peer_publish(&c, NULL, &e), "host peer cache removed on stop");
    status = ph_share_status(&c, &e);
    CHECK(!*json_string_value(json_object_get(status, "host_peer_id")) &&
          !*json_string_value(json_object_get(status, "share_link")), "stopped host has no stale peer link");
    json_decref(status);
    volatile sig_atomic_t cancelled = 1;
    c.cancel = &cancelled;
    CHECK(ph_peer_resolve(&c, "unit-test-session") == NULL && request_count == 1,
          "cancelled resolver makes no account request");
    unlink(path);
    rmdir(dir);
}
static int service_calls;
static int fake_service(ph_context *c, char *const argv[], const char *input,
                        char *const env[], int timeout, ph_process *out, ph_error *e) {
    (void)c; (void)argv; (void)input; (void)env; (void)timeout; (void)e;
    service_calls++;
    out->code = 0;
    out->out = strdup("");
    return 0;
}
static void test_live_revoke(void) {
    /* Root CI has no UID 1000. Use a real non-root owner there too. */
    struct passwd *user = getuid() ? getpwuid(getuid()) : getpwnam("nobody");
    CHECK(user && user->pw_uid >= 1000, "fixture desktop identity exists");
    if (!user) return;
    uid_t uid = user->pw_uid;
    gid_t gid = user->pw_gid;
    char dir[] = "/tmp/parsec-live-test-XXXXXX";
    CHECK(mkdtemp(dir) != NULL, "live share fixture directory");
    ph_context c;
    ph_context_init(&c);
    c.etc = dir;
    c.euid = 0;
    c.caller_uid = uid;
    c.run = fake_service;
    char dropin[512], path[1024], peer_path[512];
    snprintf(peer_path, sizeof peer_path, "%s/host-peer.json", dir);
    c.peer_cache = peer_path;
    snprintf(dropin, sizeof dropin, "%s/dropin", dir);
    mkdir(dropin, 0755);
    c.dropin = dropin;
    snprintf(path, sizeof path, "%s/owner.conf", dropin);
    FILE *f = fopen(path, "w");
    fprintf(f, "[Service]\nGroup=%u\n", (unsigned)gid);
    fclose(f);
    snprintf(path, sizeof path, "%s/share.json", dir);
    f = fopen(path, "w");
    fputs("{\"enabled\":true,\"secret\":\"a0b1c2d3\"}", f);
    fclose(f);
    service_calls = 0;
    ph_error e = {0};
    json_t *result = ph_share_dispatch(&c, "unshare", NULL, &e);
    CHECK(result != NULL && !json_is_true(json_object_get(result, "share_enabled")), "live revoke succeeds");
    CHECK(service_calls == 0, "revoking does not stop or restart a running host");
    json_decref(result);
    snprintf(path, sizeof path, "%s/share.json", dir);
    struct stat st;
    CHECK(!stat(path, &st) && (st.st_mode & 0777) == 0640 && st.st_gid == gid,
          "bearer share state is restricted to desktop owner group");
    unlink(path);
    c.rules = dropin; /* Only temporary fixtures may receive policy. */
    result = ph_dispatch(&c, "authorize-owner", NULL, &e);
    CHECK(result && json_is_true(json_object_get(result, "passwordless")),
          "administrator can opt in to owner authorization");
    json_decref(result);
    snprintf(path, sizeof path, "%s/60-parsec-linux-host-owner.rules", dropin);
    char rule[2048] = {0};
    f = fopen(path, "r");
    if (f) {
        size_t length = fread(rule, 1, sizeof rule - 1, f);
        rule[length] = 0;
        fclose(f);
    }
    CHECK(strstr(rule, "org.parsec-linux-host.manage") &&
          strstr(rule, "action.lookup('program') === '" PH_LIB "/hostctl'") &&
          strstr(rule, "action.lookup('user') === 'root'") &&
          strstr(rule, "subject.local && subject.active") &&
          strstr(rule, "subject.user === \"") &&
          !stat(path, &st) && (st.st_mode & 0777) == 0644,
          "owner policy restricts executable, target and local active account");
    c.euid = uid;
    result = ph_dispatch(&c, "revoke-owner-authorization", NULL, &e);
    CHECK(!result && !access(path, F_OK), "unprivileged code cannot edit owner policy");
    json_decref(result);
    c.euid = 0;
    c.caller_uid = 0;
    result = ph_dispatch(&c, "authorize-owner", NULL, &e);
    CHECK(!result, "root is not accepted as a desktop owner");
    json_decref(result);
    c.caller_uid = uid;
    result = ph_dispatch(&c, "revoke-owner-authorization", NULL, &e);
    CHECK(result && !json_is_true(json_object_get(result, "passwordless")) &&
          access(path, F_OK), "administrator can revoke passwordless authorization");
    json_decref(result);
    snprintf(path, sizeof path, "%s/owner.json", dir);
    unlink(path);
    snprintf(path, sizeof path, "%s/owner.conf", dropin);
    unlink(path);
    rmdir(dropin);
    rmdir(dir);
}
static volatile sig_atomic_t wait_cancelled;
static void cancel_wait(int sig) { (void)sig; wait_cancelled = 1; }
static void test_boot_display_wait(void) {
    char dir[] = "/tmp/parsec-display-test-XXXXXX";
    CHECK(mkdtemp(dir) != NULL, "boot display fixture directory");
    ph_context c;
    ph_context_init(&c);
    c.drm = dir;
    c.cancel = &wait_cancelled;
    json_t *settings = ph_settings_default();
    ph_error e = {0};
    wait_cancelled = 1;
    double before = ph_now();
    json_t *result = ph_wait_display(&c, settings, &e);
    CHECK(!result && ph_now() - before < .5, "cancelled boot wait stops immediately");
    json_decref(result);
    int ready[2];
    CHECK(!pipe(ready), "boot wait readiness pipe");
    pid_t pid = fork();
    if (!pid) {
        close(ready[0]);
        wait_cancelled = 0;
        struct sigaction sa = {0};
        sa.sa_handler = cancel_wait;
        sigemptyset(&sa.sa_mask);
        sigaction(SIGTERM, &sa, NULL);
        alarm(4); /* Fail boundedly if cancellation regresses. */
        if (write(ready[1], "R", 1) != 1) _exit(3);
        close(ready[1]);
        json_t *device = ph_wait_display(&c, settings, &e);
        int ok = !device && wait_cancelled;
        json_decref(device);
        _exit(ok ? 0 : 2);
    }
    close(ready[1]);
    CHECK(pid > 0, "boot wait process starts");
    if (pid > 0) {
        char byte = 0;
        CHECK(read(ready[0], &byte, 1) == 1 && byte == 'R', "boot wait cancellation handler is ready");
        struct timespec delay = {0, 100000000};
        nanosleep(&delay, NULL);
        int status = 0;
        CHECK(waitpid(pid, &status, WNOHANG) == 0, "missing display waits instead of failing at boot");
        kill(pid, SIGTERM);
        waitpid(pid, &status, 0);
        CHECK(WIFEXITED(status) && !WEXITSTATUS(status), "stop signal cancels pending boot cleanly");
    }
    close(ready[0]);
    json_decref(settings);
    rmdir(dir);
}
static void test_pending_bounds_and_utf8(void) {
    json_t *state = json_object(), *guests = json_array(), *pending = json_array();
    json_object_set_new(state, "guests", guests);
    json_object_set_new(state, "pending", pending);
    json_array_append_new(pending, json_pack("{s:I,s:i,s:s}", "id", (json_int_t)-1,
        "user_id", 1, "name", "invalid"));
    json_array_append_new(pending, json_pack("{s:i,s:i,s:s}", "id", 1, "user_id", 8, "name", "same"));
    json_array_append_new(pending, json_pack("{s:i,s:i,s:s}", "id", 1, "user_id", 9, "name", "duplicate"));
    char unicode[193] = "";
    for (unsigned i = 0; i < 64; i++)
        strcat(unicode, "ş");
    for (unsigned i = 2; i < 20; i++)
        json_array_append_new(pending, json_pack("{s:i,s:I,s:s}", "id", i,
            "user_id", (json_int_t)UINT32_MAX+1, "name", unicode));
    json_array_append_new(guests, json_pack("{s:i,s:I,s:s}", "id", 1,
        "user_id", (json_int_t)UINT32_MAX+1, "name", unicode));
    json_t *frame = json_pack("{s:o}", "state", state);
    char *body = json_dumps(frame, JSON_COMPACT);
    ph_room room;
    CHECK(!ph_tray_parse_state(body, strlen(body), &room) && room.pending_count == 8,
          "pending list is bounded and ignores invalid and duplicate ids");
    CHECK(room.pending[0].user_id == 8 && !room.pending[1].user_id && !room.guests[0].user_id,
          "out of range native user ids are clamped");
    struct Bridge b = {0};
    ph_bridge_publish_room(&b, &room);
    json_t *valid_pending = json_string(b.pending_names[1]);
    json_t *valid_guest = json_string(b.guest_names[0]);
    CHECK(strlen(b.pending_names[1]) == 30 && valid_pending != NULL &&
          strlen(b.guest_names[0]) == 30 && valid_guest != NULL,
          "native names stay valid UTF-8 when trimmed to 32 bytes");
    json_decref(valid_pending);
    json_decref(valid_guest);
    json_decref(frame);
    free(body);
}

int main(void) {
    test_defaults();
    test_validation();
    test_resolve_and_argv();
    test_host_options();
    test_audio_outputs();
    test_encoder_device();
    test_separate_encoder();
    test_share_secret();
    test_tray_state();
    test_tray_channel();
    test_room_published();
    test_gpus_published();
    test_bridge_abi();
    test_peer_identity();
    test_peer_status();
    test_live_revoke();
    test_boot_display_wait();
    test_pending_bounds_and_utf8();
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
