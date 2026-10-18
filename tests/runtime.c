/* Runtime unit tests: settings validation, GPU resolution, host argv, the
 * friend share secret and the bridge ABI. No real hardware or network. */
#include "runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

static void test_bridge_abi(void) {
    CHECK(sizeof(struct Bridge) == 1840, "bridge struct size");
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
    test_bridge_abi();
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
