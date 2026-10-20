/* Shared UW1/UW2 adapter. Included only by src/port/sys/coop.c, never by DOS.
   Game globals are read on the game thread. Scanout takes a copied, locked snapshot. */
#include <pthread.h>
#include "compat.h"
#include "port.h"
#include "plat.h"
#include "player.h"
#include "motion.h"
#include "ui.h"
#include "sys.h"
#include "coop.h"

static enum coop_role requested_role;
static const char *requested_endpoint;
int port_coop_role(void) { return requested_role; }
const char *port_coop_endpoint(void) { return requested_endpoint; }
static struct coop_session session;
static int shown_map_level;
static pthread_mutex_t view_lock = PTHREAD_MUTEX_INITIALIZER;
static struct {
    enum coop_status status;
    struct coop_pose remote;
    int have_remote, screen, map_level;
    uint8_t reason;
    uint64_t sampled_at, received_at;
} view;

static uint64_t now_ms(void)
{
    uint64_t t = plat_counter(), hz = plat_counter_hz();
    return (t / hz) * 1000 + (t % hz) * 1000 / hz;
}

int port_coop_configure(int role, const char *endpoint)
{
#ifdef __EMSCRIPTEN__
    (void)role; (void)endpoint;
    fprintf(stderr, "%s: co-op presence requires a native build\n", PORT_NAME);
    return -1;
#else
    uint32_t address;
    uint16_t port;
    if (requested_role || coop_endpoint((enum coop_role)role, endpoint, &address, &port)) {
        fprintf(stderr, "%s: use one of --coop-host PORT or --coop-join IPV4:PORT\n", PORT_NAME);
        return -1;
    }
    requested_role = (enum coop_role)role; requested_endpoint = endpoint;
    return 0;
#endif
}

int port_coop_enabled(void) { return requested_role != COOP_OFF; }

int port_coop_start(void)
{
    if (!requested_role) return 0;
    if (coop_open(&session, requested_role, COOP_GAME, requested_endpoint)) {
        fprintf(stderr, "%s: co-op: %s\n", PORT_NAME, session.error); return -1;
    }
    fprintf(stderr, "%s: co-op presence: %s %s; worlds are not synchronised yet\n",
            PORT_NAME, requested_role == COOP_HOST ? "host" : "join", requested_endpoint);
    return 0;
}

void port_coop_tick(void)
{
    struct coop_pose pose;
    enum coop_status old_status;
    uint64_t now;
    if (!requested_role) return;
    now = now_ms();
    memset(&pose, 0, sizeof pose);
    if (player && GrSq >= 0 && scrnum >= 0 && scrnum <= 2 &&
        PlayerLevel >= 1 && PlayerLevel <= (COOP_GAME == 1 ? 9 : 80) &&
        PN.x >= 0 && PN.x < 16384 && PN.y >= 0 && PN.y < 16384 &&
        player->maxhealth && player->health <= player->maxhealth) {
        pose.active = 1; pose.level = (uint16_t)PlayerLevel;
        pose.x = (uint16_t)PN.x; pose.y = (uint16_t)PN.y; pose.z = PN.z;
        pose.facing = (uint16_t)PlayerFacing;
        pose.health = player->health; pose.max_health = player->maxhealth;
    }
    old_status = session.status;
    coop_tick(&session, &pose, now);
    if (session.status != old_status) {
        fprintf(stderr, "%s: co-op: %s", PORT_NAME, coop_status_text(session.status));
        if (session.status == COOP_REJECTED) fprintf(stderr, " (reason %u)", session.reason);
        fputc('\n', stderr);
    }
    pthread_mutex_lock(&view_lock);
    view.status = session.status; view.remote = session.remote; view.have_remote = session.have_remote;
    view.reason = session.reason; view.screen = scrnum; view.map_level = shown_map_level;
    view.sampled_at = now; view.received_at = session.last_receive;
    pthread_mutex_unlock(&view_lock);
}

void port_coop_map_level(int level) { shown_map_level = level; }

/* A small original 3x5 pixel alphabet, independent of the game's data/fonts/palette. */
static const uint16_t letters[] = {
    0x2BED, 0x6BAE, 0x3923, 0x6B6E, 0x79A7, 0x79A4, 0x396B, 0x5BED, 0x7497,
    0x126A, 0x5BAD, 0x4927, 0x5FED, 0x5F6D, 0x2B6A, 0x6BA4, 0x2B7B, 0x6BAD,
    0x388E, 0x7492, 0x5B6F, 0x5B6A, 0x5BFD, 0x5AAD, 0x5A92, 0x72A7
};
static const uint16_t digits[] = {0x7B6F, 0x2597, 0x73E7, 0x73CF, 0x5BC9, 0x79CF, 0x79EF, 0x7249, 0x7BEF, 0x7BCF};
static uint16_t glyph(char c)
{
    if (c >= 'A' && c <= 'Z') return letters[c - 'A'];
    if (c >= '0' && c <= '9') return digits[c - '0'];
    if (c == '/') return 0x12A4;
    if (c == '-') return 0x01C0;
    return 0;
}
static void dot(uint8_t *pixels, int w, int h, int x, int y, uint8_t color)
{ if (x >= 0 && x < w && y >= 0 && y < h) pixels[y * w + x] = color; }
static void label(uint8_t *pixels, int w, int h, const char *text, uint8_t color)
{
    int x = 2, row, col;
    for (; *text && x + 3 < w; text++, x += 4) {
        uint16_t bits = glyph(*text);
        for (row = 0; row < 5; row++) for (col = 0; col < 3; col++)
            if (bits & (1u << (14 - row * 3 - col))) dot(pixels, w, h, x + col, 1 + row, color);
    }
}

void port_coop_banner(unsigned char *pixels,int w,int h,unsigned char rgb6[768],const char *text)
{
    int i,light=0,dark=0,brightest=-1,darkest=1000;
    for(i=0;i<256;i++) {
        int sum=rgb6[i*3]+rgb6[i*3+1]+rgb6[i*3+2];
        if(sum>brightest){brightest=sum;light=i;}
        if(sum<darkest){darkest=sum;dark=i;}
    }
    if(!brightest){light=255;rgb6[765]=rgb6[766]=rgb6[767]=63;}
    if(h>=7){memset(pixels,dark,(size_t)w*7);label(pixels,w,h,text,(uint8_t)light);}
}

void port_coop_scanout(unsigned char *pixels, int *w, int *h, unsigned char rgb6[768])
{
    struct coop_pose remote;
    enum coop_status status;
    int have_remote, screen, map_level, i, light = 0, dark = 0, brightest = -1, darkest = 1000;
    uint8_t reason;
    uint64_t sampled_at, received_at, now;
    char text[100];
    vga_scanout(pixels, w, h, rgb6);
    if (!requested_role || *w != 320 || *h != 200) return;
    pthread_mutex_lock(&view_lock);
    status = view.status; remote = view.remote; have_remote = view.have_remote;
    reason = view.reason; screen = view.screen; map_level = view.map_level;
    sampled_at = view.sampled_at; received_at = view.received_at;
    pthread_mutex_unlock(&view_lock);
    now = now_ms();
    if (!sampled_at) status = requested_role == COOP_HOST ? COOP_WAITING : COOP_CONNECTING;
    else if (now - sampled_at >= COOP_TIMEOUT_MS) status = COOP_CONNECTING;
    if (now - received_at >= COOP_TIMEOUT_MS || status != COOP_CONNECTED) have_remote = 0;
    /* Use the current palette's extremes. Never change the emulated VGA or its palette. */
    for (i = 0; i < 256; i++) {
        int sum = rgb6[i * 3] + rgb6[i * 3 + 1] + rgb6[i * 3 + 2];
        if (sum > brightest) { brightest = sum; light = i; }
        if (sum < darkest) { darkest = sum; dark = i; }
    }
    if (status == COOP_REJECTED)
        snprintf(text, sizeof text, "COOP REJECTED - %s", reason == COOP_WRONG_GAME ? "WRONG GAME" : "WRONG VERSION");
    else if (have_remote && remote.active)
        snprintf(text, sizeof text, "COOP P2 L%u X%u Y%u HP%u/%u - PRESENCE ONLY",
                 remote.level, remote.x / 256, remote.y / 256, remote.health, remote.max_health);
    else snprintf(text, sizeof text, "COOP %s - PRESENCE ONLY", coop_status_text(status));
    memset(pixels, dark, (size_t)*w * 7);
    label(pixels, *w, *h, text, (uint8_t)light);
    if (have_remote && remote.active && screen == 1 && remote.level == map_level) {
        /* Map drawing uses bottom-origin Y: tile (1,1) is at (10,12).
           Pixel scanout uses top-origin Y. Cross is a party marker, not a world sprite. */
        int x = ((int)remote.x / 256 - 1) * 3 + 11;
        int y = 199 - (((int)remote.y / 256 - 1) * 3 + 13);
        for (i = -3; i <= 3; i++) {
            dot(pixels, *w, *h, x + i, y - 1, (uint8_t)dark);
            dot(pixels, *w, *h, x + i, y + 1, (uint8_t)dark);
            dot(pixels, *w, *h, x - 1, y + i, (uint8_t)dark);
            dot(pixels, *w, *h, x + 1, y + i, (uint8_t)dark);
        }
        for (i = -2; i <= 2; i++) {
            dot(pixels, *w, *h, x + i, y, (uint8_t)light);
            dot(pixels, *w, *h, x, y + i, (uint8_t)light);
        }
    }
}
