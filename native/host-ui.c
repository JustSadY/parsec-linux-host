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
extern void vendor_card(float, float, unsigned char *, void *, void *, void *, void *, int);
extern void vendor_host_list(unsigned char *, void *, uint32_t *, void *);
extern unsigned char vendor_action_button(float, const char *, int, unsigned char);
extern const char *vendor_config_string(const char *, int);
extern unsigned char vendor_icon_button(const char *, uint32_t, uint32_t, int);
extern void vendor_clipboard_set(const char *);
extern void vendor_guest_strip(float, void *, void *, void *);
extern int vendor_guest_provider(void *, int, void **);
extern void *vendor_host_command(void *, const void *, uint32_t, uint32_t *);
extern void *vendor_calloc(size_t, size_t);
#pragma GCC visibility pop

/* Shared only with our unprivileged launcher. No account/session data. */
_Static_assert(sizeof(struct Bridge) == 4200, "bridge ABI");
static struct Bridge *bridge;
static int attempted, mismatched;

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
    /* Validate size before mapping, so a truncated file cannot cause SIGBUS.
     * A bridge of another size or version is a launcher from another build. */
    long size = syscall6(8, fd, 0, 2, 0, 0, 0);
    if (size > 0 && size != sizeof(struct Bridge)) mismatched = 1;
    long address = size == sizeof(struct Bridge)
        ? syscall6(9, 0, sizeof(struct Bridge), 3, 1, fd, 0) : -1;
    syscall6(3, fd, 0, 0, 0, 0, 0);
    if ((unsigned long)address >= (unsigned long)-4095) return 0;
    struct Bridge *b = (void *)address;
    if (b->magic != 0x50484f53 || b->version != BRIDGE_VERSION) {
        if (b->magic == 0x50484f53) mismatched = 1;
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
        text(mismatched
            ? "The launcher and this page are from different builds. Reinstall the package, then reopen Parsec Linux Host."
            : "Host controller unavailable. Open this test with the project launcher.", 0);
        return;
    }
    text("Changes save automatically. Restart hosting to apply capture settings.", 0);
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

    const char *gpus[9];
    gpus[0] = "Automatic";
    int gpu_count = b->gpu_count > 8 ? 8 : b->gpu_count, gpu_selected = 0, without_display = 0;
    for (int i = 0; i < gpu_count; ++i) {
        b->gpu_ids[i][31] = b->gpu_labels[i][95] = 0;
        gpus[i + 1] = b->gpu_labels[i];
        if (equal(b->gpu, b->gpu_ids[i])) gpu_selected = i + 1;
        if (!b->gpu_usable[i]) without_display = 1;
    }
    padding = row(width, "Capture GPU", without_display
        ? "Select the GPU with the display you want to share. A GPU marked (no display connected) cannot capture until a display is connected to it."
        : "Select the GPU with the display you want to share.");
    /* A GPU with no display has nothing to capture: choosing it changes
     * nothing, and the next frame shows the previous choice again. */
    if (combo("", &gpu_selected, gpus, gpu_count + 1, -1)) {
        if (!gpu_selected) copy_gpu(b->gpu, "auto");
        else if (b->gpu_usable[gpu_selected - 1]) copy_gpu(b->gpu, b->gpu_ids[gpu_selected - 1]);
    }
    finish(width, padding);

    padding = row(width, "Encoding GPU", "Separate AMD/Intel encoding needs no connected display. Cross-GPU encoding copies frames through RAM and can increase latency.");
    const char *encoding_gpus[9];
    int encoding_map[9], encoding_count=1, encoding_selected=0;
    encoding_gpus[0]="Same as capture GPU";
    for (int i=0;i<gpu_count;++i) if (b->gpu_encoding_ok[i]) {
        encoding_map[encoding_count]=i;
        encoding_gpus[encoding_count]=b->gpu_labels[i];
        if (equal(b->encoding_gpu,b->gpu_ids[i])) encoding_selected=encoding_count;
        ++encoding_count;
    }
    if (combo("",&encoding_selected,encoding_gpus,encoding_count,-1)) {
        copy_gpu(b->encoding_gpu,encoding_selected?b->gpu_ids[encoding_map[encoding_selected]]:"auto");
        b->encoder=encoding_selected?2:0;
        if (encoding_selected) b->conversion=0;
    }
    finish(width,padding);

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

    gap(20);
}

/* Reuse the shipped Computers card and guest menu, including their fonts,
 * crown, tooltips, inline link editor and Accept/Reject popup. Only this
 * backend's exact peer is marked local; other records use the original path.
 * Call-site hooks are confined to these two renderers and hash-pinned. */
static int local_card, linux_guests;
static uint32_t copy_after;
static uint64_t last_pending;
static char previous_peer[32];
static int available(struct Bridge *b) {
    return b && !b->busy && __atomic_load_n(&b->seq, __ATOMIC_ACQUIRE) == b->ack;
}
static void native_tick(struct Bridge *b) {
    if (b && copy_after && __atomic_load_n(&b->ack, __ATOMIC_ACQUIRE) == copy_after) {
        b->share_link[255] = 0;
        if (b->share_enabled && b->share_link[0]) {
            vendor_clipboard_set(b->share_link);
            copy_after = 0;
        } else if (!b->share_enabled) copy_after = 0;
    }
}
void linux_host_list(unsigned char *context, void *callbacks, uint32_t *user, void *opaque) {
    vendor_host_list(context, callbacks, user, opaque);
    struct Bridge *b = connect_bridge();
    unsigned count = *(uint32_t *)(context + 0x2f818);
    if (count > 256) return;
    int active = b && b->running && b->host_peer_id[0];
    unsigned char own[0x2f8] = {0};
    unsigned kept = 0;
    /* The vendor also synthesizes its local SDK host here. The service's
     * verified registration supplies the Linux equivalent even if the
     * separate client session has not received that row from the API yet. */
    for (unsigned i = 0; i < count; ++i) {
        unsigned char *r = context + 0x18 + i * 0x2f8;
        int current = active && equal((char *)r + 0x294, b->host_peer_id);
        if (current) {
            for (unsigned j = 0; j < sizeof own; ++j) own[j] = r[j];
        } else if (!previous_peer[0] || !equal((char *)r + 0x294, previous_peer)) {
            unsigned char *to = context + 0x18 + kept++ * 0x2f8;
            for (unsigned j = 0; j < sizeof own; ++j) to[j] = r[j];
        }
    }
    if (active) {
        if (kept == 256) kept = 255;
        unsigned char *records = context + 0x18;
        for (unsigned i = kept * 0x2f8; i; --i) records[i + 0x2f8 - 1] = records[i - 1];
        for (unsigned i = 0; i < sizeof own; ++i) records[i] = own[i];
        *(uint32_t *)records = *user;
        records[0x2f5] = 1;
        for (unsigned i = 0; i < 32; ++i) {
            records[0x294+i] = b->host_peer_id[i];
            previous_peer[i] = b->host_peer_id[i];
        }
        b->name[127] = 0;
        const char *name = b->name[0] ? b->name : "Linux Host";
        unsigned i = 0;
        for (; i < 127 && name[i]; ++i) records[0x154+i] = name[i];
        records[0x154+i] = 0;
        ++kept;
    } else previous_peer[0] = 0;
    *(uint32_t *)(context + 0x2f818) = kept;
}
void linux_card(float width, float height, unsigned char *context, void *opaque,
                void *callbacks, void *record, void *user, int index) {
    struct Bridge *b = connect_bridge();
    native_tick(b);
    unsigned char *host = record;
    /* A closed/missing host is never inferred from name or account owner. */
    if (!b || !b->running || !b->host_peer_id[0] ||
        !equal((char *)host + 0x294, b->host_peer_id)) {
        vendor_card(width, height, context, opaque, callbacks, record, user, index);
        return;
    }
    unsigned char own[0x2f8];
    for (unsigned i = 0; i < sizeof own; ++i) own[i] = host[i];
    own[0x2f5] = 1;
    unsigned char was_hosting = context[0];
    context[0] = 0; /* the native card selects Share instead of SDK Setup */
    local_card = 1;
    vendor_card(width, height, context, opaque, callbacks, own, user, index);
    local_card = 0;
    context[0] = was_hosting;
}
unsigned char linux_card_action(float width, const char *title, int style, unsigned char disabled) {
    struct Bridge *b = connect_bridge();
    if (!local_card) return vendor_action_button(width, title, style, disabled);
    unsigned char clicked = vendor_action_button(width, title, style, !available(b) || !b->configured);
    if (clicked && available(b)) {
        request(b, 6);
        copy_after = b->seq;
    }
    return 0; /* never enter the vendor's unsupported Linux host SDK */
}
const char *linux_card_secret(const char *key, int index) {
    struct Bridge *b = connect_bridge();
    if (local_card && b && equal(key, "ws_secret")) {
        b->share_secret[64] = 0;
        return b->share_enabled ? b->share_secret : "";
    }
    return vendor_config_string(key, index);
}
unsigned char linux_card_revoke(const char *icon, uint32_t normal, uint32_t hover, int kind) {
    unsigned char clicked = vendor_icon_button(icon, normal, hover, kind);
    if (!local_card) return clicked;
    struct Bridge *b = connect_bridge();
    if (clicked && available(b)) request(b, 7);
    return 0;
}

/* Private 150-105c guest record ABI, observed by REA. The native renderer
 * takes ownership of the array and frees it with the vendor's libc. Pending
 * decision IDs remain full u64 decimal strings, separate from popup IDs. */
static void put32(unsigned char *p, uint32_t n) {
    for (unsigned i = 0; i < 4; ++i) p[i] = n >> (i * 8);
}
static void guest_name(unsigned char *p, const char *s) {
    unsigned i = 0;
    while (i < 31 && s[i]) { p[i] = s[i]; ++i; }
    p[i] = 0;
}
static void decimal(unsigned char *p, uint64_t n) {
    char reverse[20]; unsigned i = 0, j = 0;
    do { reverse[i++] = '0' + n % 10; n /= 10; } while (n);
    while (i) p[j++] = reverse[--i];
    p[j] = 0;
}
void linux_guest_strip(float width, void *context, void *opaque, void *callbacks) {
    struct Bridge *b = connect_bridge();
    native_tick(b);
    if (!b || !b->running) {
        vendor_guest_strip(width, context, opaque, callbacks);
        return;
    }
    /* Only this renderer reads up to byte 0x52 of its callback record. A
     * private copy keeps the synthetic host marker out of shared app state. */
    uintptr_t local[11];
    for (unsigned i = 0; i < sizeof local; ++i)
        ((unsigned char *)local)[i] = ((unsigned char *)callbacks)[i];
    local[1] = (uintptr_t)b;
    uint64_t pending = b->pending_count ? b->pending_ids[0] : 0;
    if (pending != last_pending) ((unsigned char *)context)[2] = 0;
    last_pending = pending;
    linux_guests = 1;
    vendor_guest_strip(width, context, opaque, local);
    linux_guests = 0;
}
int linux_guest_provider(void *host, int operation, void **out) {
    if (!linux_guests) return vendor_guest_provider(host, operation, out);
    struct Bridge *b = connect_bridge();
    *out = 0;
    if (!b || operation != 5) return 0;
    unsigned guests = b->guest_count > 8 ? 8 : b->guest_count;
    unsigned pending = b->pending_count > 8 ? 8 : b->pending_count;
    unsigned n = guests + pending;
    if (!n) return 0;
    unsigned char *records = vendor_calloc(n, 0x1b8);
    if (!records) return 0;
    for (unsigned i = 0; i < n; ++i) {
        unsigned char *r = records + i * 0x1b8;
        if (i < guests) {
            for (unsigned j = 0; j < 3; ++j) r[j] = !!b->guest_permissions[i][j];
            put32(r + 0x11c, 4);
            put32(r + 0x124, b->guest_ids[i]);
            put32(r + 0x128, b->guest_user_ids[i]);
            guest_name(r + 0x190, b->guest_names[i]);
        } else {
            unsigned j = i - guests;
            put32(r + 0x11c, 1);
            put32(r + 0x124, 0x80000000u + j);
            put32(r + 0x128, b->pending_user_ids[j]);
            decimal(r + 0x158, b->pending_ids[j]);
            guest_name(r + 0x190, b->pending_names[j]);
        }
    }
    *out = records;
    return (int)n;
}
void *linux_guest_command(void *host, const void *data, uint32_t size, uint32_t *reply_size) {
    if (!linux_guests) return vendor_host_command(host, data, size, reply_size);
    struct Bridge *b = connect_bridge();
    const unsigned char *p = data;
    static unsigned char reply[12];
    *reply_size = 0;
    if (!available(b) || size < 5) return 0;
    unsigned op = *(const uint32_t *)p;
    if (op == 0x11 && size >= 7 && size <= 26) {
        uint64_t id = 0;
        unsigned i = 5;
        for (; i < size && p[i]; ++i) {
            if (p[i] < '0' || p[i] > '9' || id > (UINT64_MAX - (p[i] - '0')) / 10) return 0;
            id = id * 10 + p[i] - '0';
        }
        if (i == 5 || i == size || !id) return 0;
        b->decision_id = id;
        request(b, p[4] ? 11 : 12);
    } else if (op == 0x10 && size == 12) {
        b->kick_guest = *(const uint32_t *)(p + 4);
        request(b, 10);
    } else if (op == 0xf && size == 12) {
        b->permissions_guest = *(const uint32_t *)(p + 4);
        for (unsigned i = 0; i < 3; ++i) b->permissions_value[i] = !!p[8+i];
        request(b, 13);
    } else return 0;
    for (unsigned i = 0; i < sizeof reply; ++i) reply[i] = 0;
    put32(reply, op);
    *reply_size = sizeof reply;
    return reply;
}
