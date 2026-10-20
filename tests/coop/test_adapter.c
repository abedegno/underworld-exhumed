/* Exercise the actual adapter against game-global stubs, with no copyrighted assets. */
#include "test_platform.h"
#include <pthread.h>
#include <assert.h>
#include <time.h>
#include "../../coop/port.c"
#undef main
#undef close
#undef fopen
#undef fwrite

static uint64_t clock_ms = 1000;
uint64_t plat_counter(void) { return clock_ms; }
uint64_t plat_counter_hz(void) { return 1000; }
void vga_scanout(uint8_t *pixels, int *w, int *h, uint8_t rgb6[768])
{
    *w = 320; *h = 200; memset(pixels, 77, 320 * 200);
    memset(rgb6, 0, 768); memset(rgb6 + 765, 63, 3);
}
struct Phys PN;
static struct Player character;
struct Player *player = &character;
int16 GrSq = 4, PlayerLevel = 3, PlayerFacing = 0, scrnum = 1;

static void pump_adapter(struct coop_session *guest, const struct coop_pose *pose)
{
    int i;
    for (i = 0; i < 100; i++) {
        clock_ms += 10; port_coop_tick(); coop_tick(guest, pose, clock_ms); test_pause();
    }
}
int main(void)
{
    struct sockaddr_in addr;
    test_socklen size = sizeof addr;
    struct coop_session guest;
    struct coop_pose pose = {3, 3072, 4096, 0, 1234, 20, 30, 1};
    uint8_t pixels[320 * 200], palette[768];
    test_socket fd;
    int w, h, i;
    char port[10], endpoint[40];
    test_sockets_start();
    port_coop_scanout(pixels, &w, &h, palette);
    for (i = 0; i < 320 * 200; i++) assert(pixels[i] == 77); /* solo is unchanged */
    fd = socket(AF_INET, SOCK_STREAM, 0); assert(test_valid(fd));
    memset(&addr, 0, sizeof addr); addr.sin_family = AF_INET;
    assert(inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr) == 1);
    assert(!bind(fd, (struct sockaddr *)&addr, sizeof addr));
    assert(!getsockname(fd, (struct sockaddr *)&addr, &size)); test_close(fd);
    snprintf(port, sizeof port, "%u", ntohs(addr.sin_port));
    snprintf(endpoint, sizeof endpoint, "127.0.0.1:%s", port);
    assert(!port_coop_configure(COOP_HOST, port)); assert(port_coop_enabled());
    assert(!port_coop_start()); assert(!coop_open(&guest, COOP_JOIN, COOP_GAME, endpoint));
    character.health = 25; character.maxhealth = 40; PN.x = 1024; PN.y = 2048;
    port_coop_map_level(3); pump_adapter(&guest, &pose);
    assert(guest.remote.active && guest.remote.x == 1024 && guest.remote.health == 25);
    port_coop_scanout(pixels, &w, &h, palette);
    assert(pixels[141 * 320 + 44] == 255); /* live partner on level 3 */
    assert(pixels[8 * 320] == 77); /* game image under the status strip unchanged */
    pose.level = 4; pump_adapter(&guest, &pose);
    port_coop_scanout(pixels, &w, &h, palette);
    assert(pixels[141 * 320 + 44] == 77); /* other level is hidden, previous mark erased */
    port_coop_map_level(4); port_coop_tick();
    port_coop_scanout(pixels, &w, &h, palette); assert(pixels[141 * 320 + 44] == 255);
    pose.active = 0; memset(&pose, 0, sizeof pose); pump_adapter(&guest, &pose);
    port_coop_scanout(pixels, &w, &h, palette); assert(pixels[141 * 320 + 44] == 77);
    GrSq = -1; pump_adapter(&guest, &pose); assert(!guest.remote.active);
    coop_close(&guest); port_coop_tick();
    port_coop_scanout(pixels, &w, &h, palette); assert(pixels[141 * 320 + 44] == 77);
    /* Write a representative scanout as a portable pixmap for optional visual inspection. */
    if (getenv("COOP_TEST_PPM")) {
        FILE *f = fopen(getenv("COOP_TEST_PPM"), "wb"); assert(f);
        fprintf(f, "P6\n320 200\n255\n");
        for (i = 0; i < 320 * 200; i++) {
            uint8_t rgb[3]; int j;
            for (j = 0; j < 3; j++) rgb[j] = palette[pixels[i] * 3 + j] * 4;
            fwrite(rgb, 1, 3, f);
        }
        fclose(f);
    }
    coop_close(&session);
    test_sockets_stop();
    printf("UW%d game adapter, solo scanout, player capture and map-marker lifecycle: PASS\n", COOP_GAME);
    return 0;
}
