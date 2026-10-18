/* New Host UI, rendered by the vendor client's existing ImGui functions.
 * Private ABI, hash-pinned to 150-105c. No vendor host stub is changed.
 */
#include <stddef.h>
#include <stdint.h>
#include "bridge.h"

#pragma GCC visibility push(hidden)
extern void tab(float, const char *, const char *, uint64_t);
extern void header(void *, int, const char *);
extern void text(const char *, const char *);
extern void label(float, const char *, const char *, int);
extern float field(float, const char *);
extern void same_line(float, float);
extern void begin_group(void), end_group(void), pop_id(void);
extern void pop_vars(int), pop_colors(int), unindent(float);
extern float cursor_x(void), cursor_y(void);
extern void cursor_set(const float *);
extern unsigned char combo(const char *, int *, const char *const *, int, int);
extern unsigned char input(const char *, const char *, char *, size_t, int, void *, void *);
extern unsigned char button(float, const char *, int);
extern char *vendor_getenv(const char *);
extern float vendor_roundf(float);
extern unsigned char *ui_context;
#pragma GCC visibility pop

/* Shared only with our unprivileged launcher. No account/session data. */
_Static_assert(sizeof(struct Bridge) == 1840, "bridge ABI");
static struct Bridge *bridge;
static int attempted;

/* Prevent aggregate initializers becoming absolute pointer tables at -Os. */
__attribute__((noinline)) static const char *relative_string(const char *p) {
    __asm__ volatile("" : "+r"(p));
    return p;
}
#define S(s) relative_string(s)

static long syscall6(long n, long a, long b, long c, long d, long e, long f) {
    register long r10 __asm__("r10") = d;
    register long r8 __asm__("r8") = e;
    register long r9 __asm__("r9") = f;
    long result;
    __asm__ volatile("syscall" : "=a"(result) : "a"(n), "D"(a), "S"(b), "d"(c),
                     "r"(r10), "r"(r8), "r"(r9) : "rcx", "r11", "memory");
    return result;
}

static struct Bridge *connect_bridge(void) {
    if (attempted) return bridge;
    attempted = 1;
    const char *path = vendor_getenv("PARSEC_NATIVE_HOST_BRIDGE");
    if (!path || !*path) return 0;
    long fd = syscall6(2, (long)path, 2 | 0x80000, 0, 0, 0, 0);
    if (fd < 0) return 0;
    /* Validate size before mapping, so a truncated file cannot cause SIGBUS. */
    long size = syscall6(8, fd, 0, 2, 0, 0, 0);
    long address = size == sizeof(struct Bridge)
        ? syscall6(9, 0, sizeof(struct Bridge), 3, 1, fd, 0) : -1;
    syscall6(3, fd, 0, 0, 0, 0, 0);
    if ((unsigned long)address >= (unsigned long)-4095) return 0;
    struct Bridge *b = (void *)address;
    if (b->magic != 0x50484f53 || b->version != BRIDGE_VERSION) {
        syscall6(11, address, sizeof(*b), 0, 0, 0, 0);
        return 0;
    }
    bridge = b;
    return b;
}

static float scale(void) { return *(float *)(ui_context + 0x80); }
static void gap(float amount) {
    float xy[2] = {cursor_x(), cursor_y() + vendor_roundf(scale() * amount)};
    cursor_set(xy);
}

void client_and_host(float spacing, const char *title, const char *tip, uint64_t mask) {
    tab(spacing, title, tip, mask);
    tab(vendor_roundf(scale() * 20), "Host", "Linux hosting settings.", 0x200000040000002ULL);
}

static float row(float width, const char *title, const char *description) {
    label(width, title, description, 0);
    same_line(0, -1);
    float padding = field(width, title);
    begin_group();
    return padding;
}
static void finish(float width, float padding) {
    end_group();
    pop_vars(4);
    pop_colors(8);
    pop_vars(2);
    unindent(width + padding);
    pop_id();
    gap(30);
}
static void request(struct Bridge *b, unsigned action) {
    if (b->busy || b->seq != b->ack) return;
    b->action = action;
    b->busy = 1;
    __atomic_add_fetch(&b->seq, 1, __ATOMIC_RELEASE);
}

static int equal(const char *a, const char *b) {
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
static void copy_gpu(char *target, const char *source) {
    unsigned i = 0;
    for (; i < 31 && source[i]; ++i) target[i] = source[i];
    target[i] = 0;
}

void host_page(float width, void *context) {
    header(context, 0, "HOST SETTINGS");
    struct Bridge *b = connect_bridge();
    if (!b) {
        text("Host controller unavailable. Open this test with the project launcher.", 0);
        return;
    }
    text("Linux hosting uses lowlat. Save changes, then restart hosting to apply.", 0);
    gap(8);
    /* The worker always terminates these buffers; enforce it for the UI too. */
    b->status[255] = b->name[127] = b->output[63] = 0;
    text(b->status, 0);
    if (!b->configured && button(0, "Connect Host account", b->busy)) request(b, 5);
    gap(20);
    float padding = row(width, "Hosting Enabled", "Allow connections to this Linux computer.");
    if (button(0, b->running ? "Stop hosting" : "Start hosting", b->busy || !b->configured))
        request(b, b->running ? 3 : 2);
    finish(width, padding);

    padding = row(width, "Host Name", "The name shown in your list of computers.");
    input("", "Computer name", b->name, sizeof(b->name), 0, 0, 0);
    finish(width, padding);
    padding = row(width, "Display", "The Linux display connector to capture, for example DP-1.");
    input("", "auto or DP-1", b->output, sizeof(b->output), 0, 0, 0);
    finish(width, padding);

    padding = row(width, "Capture GPU", "Select the GPU with the display you want to share.");
    const char *gpus[9];
    gpus[0] = "Automatic";
    int gpu_count = b->gpu_count > 8 ? 8 : b->gpu_count, gpu_selected = 0;
    for (int i = 0; i < gpu_count; ++i) {
        b->gpu_ids[i][31] = b->gpu_labels[i][95] = 0;
        gpus[i + 1] = b->gpu_labels[i];
        if (equal(b->gpu, b->gpu_ids[i])) gpu_selected = i + 1;
    }
    if (combo("", &gpu_selected, gpus, gpu_count + 1, -1))
        copy_gpu(b->gpu, gpu_selected ? b->gpu_ids[gpu_selected - 1] : "auto");
    finish(width, padding);

    padding = row(width, "Encoder", "Hardware video encoder. Software CPU encoding is unavailable.");
    const char *volatile encoders[] = {S("Automatic"), S("NVIDIA NVENC"), S("VA-API")};
    if (b->encoder < 0 || b->encoder > 2) b->encoder = 0;
    combo("", &b->encoder, (const char *const *)encoders, 3, -1);
    finish(width, padding);
    padding = row(width, "Capture Conversion", "Automatic selects the conversion path required by the encoder.");
    const char *volatile conversions[] = {S("Automatic"), S("Vulkan"), S("OpenGL")};
    if (b->conversion < 0 || b->conversion > 2) b->conversion = 0;
    combo("", &b->conversion, (const char *const *)conversions, 3, -1);
    finish(width, padding);

    padding = row(width, "Frame Rate", "Maximum capture rate in frames per second.");
    const char *volatile fps[] = {S("30 FPS"), S("60 FPS"), S("90 FPS"), S("120 FPS"), S("144 FPS"), S("240 FPS")};
    int rates[] = {30, 60, 90, 120, 144, 240}, selected = 1;
    for (int i = 0; i < 6; ++i) if (b->fps == rates[i]) selected = i;
    if (combo("", &selected, (const char *const *)fps, 6, -1)) b->fps = rates[selected];
    finish(width, padding);

    padding = row(width, "Bandwidth Limit", "Maximum video bitrate in megabits per second.");
    const char *volatile bitrates[] = {S("5 Mbps"), S("10 Mbps"), S("15 Mbps"), S("20 Mbps"), S("25 Mbps"), S("30 Mbps"), S("40 Mbps"), S("50 Mbps"), S("75 Mbps"), S("100 Mbps")};
    int values[] = {5, 10, 15, 20, 25, 30, 40, 50, 75, 100};
    selected = 1;
    for (int i = 0; i < 10; ++i) if (b->bitrate == values[i]) selected = i;
    if (combo("", &selected, (const char *const *)bitrates, 10, -1)) b->bitrate = values[selected];
    finish(width, padding);

    padding = row(width, "Codec", "The video compression format used by the Linux host.");
    const char *volatile codecs[] = {S("H.264"), S("H.265 / HEVC")};
    if (b->codec < 0 || b->codec > 1) b->codec = 0;
    combo("", &b->codec, (const char *const *)codecs, 2, -1);
    finish(width, padding);

    padding = row(width, "Encoder Quality", "Choose latency or encoding quality.");
    const char *volatile qualities[] = {S("Lowest latency"), S("Balanced"), S("Highest quality")};
    if (b->quality < 0 || b->quality > 2) b->quality = 0;
    combo("", &b->quality, (const char *const *)qualities, 3, -1);
    finish(width, padding);

    padding = row(width, "Start at Boot", "Automatically start the configured Linux host service.");
    const char *volatile booleans[] = {S("Disabled"), S("Enabled")};
    b->autostart = !!b->autostart;
    combo("", &b->autostart, (const char *const *)booleans, 2, -1);
    finish(width, padding);
    padding = row(width, "Constant FPS", "Capture continuously, even when the desktop is unchanged.");
    b->full_fps = !!b->full_fps;
    combo("", &b->full_fps, (const char *const *)booleans, 2, -1);
    finish(width, padding);
    padding = row(width, "Mute Speakers", "Mute local audio while a guest is listening.");
    b->mute_speakers = !!b->mute_speakers;
    combo("", &b->mute_speakers, (const char *const *)booleans, 2, -1);
    finish(width, padding);
    padding = row(width, "Clipboard", "Share clipboard text with the connected owner.");
    b->clipboard = !!b->clipboard;
    combo("", &b->clipboard, (const char *const *)booleans, 2, -1);
    finish(width, padding);

    /* No Save button: the launcher autosaves edited settings and refreshes
     * status on its own. Friend sharing is the only explicit action left. */
    b->share_link[255] = 0;
    padding = row(width, "Friend Access",
        "Create an invite link so a friend can connect to this computer.");
    if (b->share_enabled) {
        if (button(0, "Stop sharing", b->busy || !b->configured)) request(b, 7);
    } else if (button(0, "Create share link", b->busy || !b->configured)) {
        request(b, 6);
    }
    finish(width, padding);
    if (b->share_enabled) {
        if (b->share_link[0]) {
            text(b->share_link, 0);
            gap(6);
            if (button(0, "Copy link", b->busy)) request(b, 8);
        } else {
            text("Invite link not ready. Start hosting, then get the link.", 0);
            gap(6);
            if (button(0, "Get link", b->busy || !b->configured)) request(b, 9);
        }
    }
    gap(20);
}
