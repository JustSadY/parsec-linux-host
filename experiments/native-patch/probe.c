#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>

/* 0x94640 is the object iterator; 0x94670 is its key lookup wrapper. */
static void *(*lookup)(void *, const char *);

int main(int argc, char **argv) {
    struct rlimit no_core = {0, 0};
    setrlimit(RLIMIT_CORE, &no_core);
    if (argc != 2) return 2;
    void *library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!library) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 3; }
    Dl_info info;
    if (!dladdr(dlsym(library, "wx_main"), &info)) return 4;
    uintptr_t base = (uintptr_t)info.dli_fbase;
    void *(*capabilities)(void) = (void *)(base + 0x2cc60);
    lookup = (void *)(base + 0x94670);
    int (*boolean)(void *, unsigned char *) = (void *)(base + 0x941c0);
    int (*string)(void *, char *, size_t) = (void *)(base + 0x94440);
    int (*host_start)(void *, void *) = (void *)(base + 0x88440);
    void *envelope = capabilities();
    void *data = lookup(envelope, "data");
    void *renderers = lookup(data, "renderers");
    unsigned char supported = 0;
    int valid = boolean(lookup(data, "hosting_supported"), &supported);
    char gl[32] = {0}, vk[32] = {0};
    int gl_ok = string(lookup(renderers, "0"), gl, sizeof gl);
    int vk_ok = string(lookup(renderers, "5"), vk, sizeof vk);
    int start = host_start(NULL, NULL);
    printf("{\"hosting_supported\":%s,\"boolean_valid\":%s,"
           "\"renderers\":{\"0\":\"%s\",\"5\":\"%s\"},\"host_start_result\":%d}\n",
           supported ? "true" : "false", valid ? "true" : "false", gl, vk, start);
    /* Process exit releases this disposable probe's allocations. */
    return valid && gl_ok && vk_ok ? 0 : 5;
}
