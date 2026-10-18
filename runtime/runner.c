#include "runtime.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <systemd/sd-daemon.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t stopping, child_pid;
static void stop_runner(int sig) {
    (void)sig;
    stopping = 1;
    if (child_pid > 0)
        kill(child_pid, SIGINT);
}
static void redact_emit(char *buffer, size_t *used, const char *token,
                        size_t token_size, int eof) {
    size_t done = 0, limit = eof ? *used : (*used >= token_size ? *used-token_size+1 : 0);
    while (done < limit) {
        if (*used-done >= token_size && !memcmp(buffer+done, token, token_size)) {
            fputs("[session]", stdout);
            done += token_size;
        } else {
            fputc((unsigned char)buffer[done], stdout);
            done++;
        }
    }
    if (done) {
        memmove(buffer, buffer+done, *used-done);
        *used -= done;
        fflush(stdout);
    }
}

typedef struct {
    ph_context context;
    const char *token;
    volatile sig_atomic_t stopped;
} peer_watch;
static void *watch_peer(void *arg) {
    peer_watch *watch = arg;
    char *previous = NULL;
    while (!watch->stopped) {
        char *peer = ph_peer_resolve(&watch->context, watch->token);
        if (watch->stopped) {
            free(peer);
            break;
        }
        int changed = (!previous != !peer) || (peer && previous && strcmp(peer, previous));
        int published = 1;
        if (changed) {
            ph_error ignored = {0};
            published = ph_peer_publish(&watch->context, peer, &ignored) == 0;
        }
        /* Retain the last successfully published value so a transient write
         * failure is retried on the next poll instead of suppressed. */
        if (published) {
            free(previous);
            previous = peer;
        } else {
            free(peer);
            peer = NULL;
        }
        /* Retry registration quickly; refresh an established identity every
         * ten seconds. Cancellation is checked between each short sleep. */
        int ticks = peer ? 100 : 15;
        struct timespec delay = {0, 100000000};
        for (int i = 0; i < ticks && !watch->stopped; i++)
            nanosleep(&delay, NULL);
    }
    free(previous);
    return NULL;
}

int ph_runner_main(void) {
    ph_context c;
    ph_context_init(&c);
    ph_error e = {0};
    json_t *owner = NULL, *account = NULL, *settings = NULL, *devices = NULL, *device = NULL;
    char **args = NULL;
    int result = 1, watch_started = 0;
    peer_watch watch = {0};
    pthread_t watch_thread;
    if (geteuid() != 0) {
        ph_fail(&e, "Host service must run as root");
        goto finish;
    }
    /* A stale registration must never label a different remote computer as
     * this machine after a failed start or an earlier unclean service exit. */
    if (ph_peer_publish(&c, NULL, &e))
        goto finish;
    char path[PATH_MAX];
    ph_path(path, sizeof path, c.etc, "owner.json");
    owner = ph_read_json(path, 1024, 0, &e);
    json_t *uid_obj = owner ? json_object_get(owner, "uid") : NULL;
    json_int_t uid = json_integer_value(uid_obj);
    if (!json_is_integer(uid_obj) || uid < 1000 || uid > UINT32_MAX) {
        ph_fail(&e, "Invalid desktop owner");
        goto finish;
    }
    ph_path(path, sizeof path, c.etc, "account.json");
    account = ph_read_json(path, 8192, 1, &e);
    json_t *token_obj = account ? json_object_get(account, "session_id") : NULL;
    const char *token = json_string_value(token_obj);
    if (!token || !strlen(token) || strlen(token) > 4096 ||
        strlen(token) != json_string_length(token_obj)) {
        ph_fail(&e, "Invalid host session");
        goto finish;
    }
    settings = ph_settings_load(&c, &e);
    if (!settings)
        goto finish;
    devices = ph_inventory(c.drm, &e);
    device = ph_resolve(settings, devices, &e);
    if (!device)
        goto finish;
    /* Sharing is controlled by the root-owned live file, including when it
     * changes after startup. Do not put its bearer secret in process argv. */
    args = ph_host_argv(&c, settings, device, NULL, &e);
    if (!args)
        goto finish;
    char session[4120], pulse[128], owner_env[64];
    snprintf(session, sizeof session, "KESSEL_SESSION=%s", token);
    snprintf(pulse, sizeof pulse, "PULSE_SERVER=unix:/run/user/%lld/pulse/native", (long long)uid);
    snprintf(owner_env, sizeof owner_env, "LOWLAT_OWNER_UID=%lld", (long long)uid);
    char *env[] = {"PATH=/usr/bin:/bin", "HOME=/root", "LANG=C.UTF-8", session, pulse,
                   owner_env, "LOWLAT_SHARE_FILE=" PH_ETC "/share.json", NULL};
    int pipefd[2];
    if (pipe2(pipefd, O_CLOEXEC)) {
        ph_fail(&e, "Cannot open host output");
        explicit_bzero(session, sizeof session);
        goto finish;
    }
    struct sigaction sa = {0}, oldint, oldterm;
    sa.sa_handler = stop_runner;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, &oldint);
    sigaction(SIGTERM, &sa, &oldterm);
    stopping = 0;
    child_pid = 0;
    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        ph_fail(&e, "Cannot start host service");
        explicit_bzero(session, sizeof session);
        sigaction(SIGINT, &oldint, NULL);
        sigaction(SIGTERM, &oldterm, NULL);
        goto finish;
    }
    if (!pid) {
        dup2(pipefd[1], 1);
        dup2(pipefd[1], 2);
        int null = open("/dev/null", O_RDONLY);
        if (null >= 0)
            dup2(null, 0);
        close(pipefd[0]);
        close(pipefd[1]);
        if (null > 2)
            close(null);
        execve(args[0], args, env);
        _exit(127);
    }
    child_pid = pid;
    close(pipefd[1]);
    explicit_bzero(session, sizeof session);
    if (stopping)
        kill(pid, SIGINT);
    watch.context = c;
    watch.context.cancel = &watch.stopped;
    watch.token = token;
    if (!pthread_create(&watch_thread, NULL, watch_peer, &watch))
        watch_started = 1;
    else
        fprintf(stderr, "Host identity refresh unavailable\n");
    sd_notify(0, "READY=1");
    char buffer[12288];
    size_t used = 0, token_size = strlen(token);
    for (;;) {
        ssize_t n = read(pipefd[0], buffer+used, sizeof buffer-used);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            break;
        used += (size_t)n;
        redact_emit(buffer, &used, token, token_size, 0);
    }
    redact_emit(buffer, &used, token, token_size, 1);
    explicit_bzero(buffer, sizeof buffer);
    close(pipefd[0]);
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    child_pid = 0;
    watch.stopped = 1;
    if (watch_started) {
        pthread_join(watch_thread, NULL);
        watch_started = 0;
    }
    sigaction(SIGINT, &oldint, NULL);
    sigaction(SIGTERM, &oldterm, NULL);
    sd_notify(0, "STOPPING=1");
    result = WIFEXITED(status) ? WEXITSTATUS(status) : 128+WTERMSIG(status);
    if (stopping && (result == 0 || result == 128+SIGINT || result == 128+SIGTERM))
        result = 0;
finish:
    if (watch_started) {
        watch.stopped = 1;
        pthread_join(watch_thread, NULL);
    }
    if (geteuid() == 0) {
        ph_error ignored = {0};
        ph_peer_publish(&c, NULL, &ignored);
    }
    if (*e.text)
        fprintf(stderr, "%s\n", e.text);
    ph_argv_free(args);
    json_decref(device);
    json_decref(devices);
    json_decref(settings);
    json_decref(owner);
    json_decref(account);
    return result;
}
