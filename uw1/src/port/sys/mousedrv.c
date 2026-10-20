/* mousedrv.c: replaces src/sys/MOUSEDRV.ASM (seg019 module 10) and the DOS mouse driver under
   it (int 33h). The driver's state is built from the platform layer's pointer events: the
   position, in the driver's 640 by 200 range for mode 13h, the buttons, and the motion counters
   in mickeys, at the driver's default of 8 mickeys per 8 pixels across and 16 per 8 down, so a
   pointer moving one screen pixel gives two mickeys either way. The game reads only the motion
   and the buttons (MOUSE.C keeps its own cursor), so a touch screen drives it as a mouse does.

   The game's cursor follows the host's pointer. Fed the host's motion, it drifts away from
   it: the scaling drops half a pixel whenever the mickeys are odd, and the game stops its
   cursor at the edges of the box it confines it to while the host's pointer goes on. So
   while the pointer is not captured (an event with absolute set), function 0Bh reports
   the motion that puts the game's cursor (mouse_getxy) where the host's pointer is, once per
   move of the host's pointer, and none while it stays still, which leaves the game's own
   moves (the keyboard's warps, mouse_putxy) alone. Captured (the pointer lock option,
   platform/sdl3), the host's motion goes through as mickeys, in whole pixels so that none
   is lost to the scaling.

   MOUSEDRV.ASM's routines are kept: _6C8 resets the driver and stores the motion divisor (200)
   in FD72:0360 and MouseOn (FD72:0362); _6E7 reads the motion and scales it by 100 / 200;
   _703 gives the buttons. */
#include <stdatomic.h>
#include <string.h>
#include "port.h"
#include "plat.h"

#define D seg063
#define W(o) ((int16_t)(D[(o)] | D[(o) + 1] << 8))
#define SETW(o, v) port_setw(&D[(o)], (uint16_t)(v))

static _Atomic int mick_x, mick_y, pos_x, pos_y, buttons, abs_x, abs_y, moved;
static float frac_x, frac_y;

void mouse_getxy(int16_t *x, int16_t *y);       /* ui/MOUSE.C: the game's cursor */

/* mouse-look (ui/MOUSE.C, --enhance mouse-look): while it is on, every event's motion counts,
   scaled by look-speed (the settings file's, a percentage), and none moves the game's cursor to
   the host's pointer, which the platform captures. The scaling is here, before the motion is
   recorded, so that a recording replays as it was played. */
static _Atomic int look_on, look_pct = 100;

void mouse_look_mode(int on)
{
    atomic_store(&look_on, on);
    if (!port_shared_guest()) plat_pointer_capture(on);
}

void mouse_look_speed(int pct)
{
    atomic_store(&look_pct, pct < 10 ? 10 : pct > 400 ? 400 : pct);
}

void mouse_event(const PlatPointer *ev)
{
    int x = (int)(ev->x * 2.0f), y = (int)ev->y;
    int look = atomic_load(&look_on);
    float k = look ? (float)atomic_load(&look_pct) / 100.0f : 1.0f;
    if (ev->absolute && !look) {
        int ax = (int)ev->x, ay = (int)ev->y;
        atomic_store(&abs_x, ax < 0 ? 0 : ax > 319 ? 319 : ax);
        atomic_store(&abs_y, ay < 0 ? 0 : ay > 199 ? 199 : ay);
        atomic_store(&moved, 1);
    } else {
        float mx = ev->dx * k + frac_x, my = ev->dy * k + frac_y;
        int ix = (int)mx, iy = (int)my;
        frac_x = mx - (float)ix;
        frac_y = my - (float)iy;
        atomic_fetch_add(&mick_x, ix * 2);
        atomic_fetch_add(&mick_y, iy * 2);
    }
    if (x < 0) x = 0;
    if (x > 639) x = 639;
    if (y < 0) y = 0;
    if (y > 199) y = 199;
    atomic_store(&pos_x, x);
    atomic_store(&pos_y, y);
    atomic_store(&buttons, (int)(ev->buttons & 3));
}

/* int 33h: AX the function; returns its results in the registers, as the driver did. */
int mouse_int33(uint16_t *ax, uint16_t *bx, uint16_t *cx, uint16_t *dx)
{
    port_shared_poll_input();
    int x, y;
    switch (*ax) {
    case 0x00:                          /* reset: installed, two buttons */
        *ax = 0xFFFF;
        *bx = 2;
        return 1;
    case 0x03:                          /* position and buttons */
        *bx = (uint16_t)atomic_load(&buttons);
        *cx = (uint16_t)atomic_load(&pos_x);
        *dx = (uint16_t)atomic_load(&pos_y);
        return 1;
    case 0x0B:                          /* motion since the last call, in mickeys */
        x = atomic_exchange(&mick_x, 0);
        y = atomic_exchange(&mick_y, 0);
        if (atomic_exchange(&moved, 0)) {
            /* to the host's pointer: the game's y counts up from the bottom row, and _6F8
               scales mickeys by 100 / [0360] */
            int16_t gx, gy, d = W(0x360);
            mouse_getxy(&gx, &gy);
            x = (atomic_load(&abs_x) - gx) * d / 100;
            y = (atomic_load(&abs_y) - (199 - gy)) * d / 100;
        }
        *cx = (uint16_t)(int16_t)x;
        *dx = (uint16_t)(int16_t)y;
        return 1;
    default:
        port_log("int 33h function %04Xh not emulated\n", *ax);
        return 0;
    }
}

/* _6C8 (initmouse): reset the driver; a driver that answers sets 0360 and 0362 to 200. */
void seg019_6C8(void)
{
    uint16_t ax = 0, bx = 0, cx = 0, dx = 0;
    mouse_int33(&ax, &bx, &cx, &dx);
    if (ax) ax = 0xC8;
    SETW(0x360, ax);
    SETW(0x362, ax);
}

/* _6F8: AX * 100 / [0360], as imul and idiv do it (a 16-bit quotient). */
int port_shared_mouse_look(void) { return atomic_load(&look_on); }

void port_shared_mouse_seat(int seat)
{
    static struct { int mx,my,px,py,buttons,ax,ay,moved,look;float fx,fy; } saved[2];
    static int current;
    if (seat<0) { memset(&saved[1],0,sizeof saved[1]);return; }
    if (seat==current) return;
#define M(member,global) saved[current].member=atomic_load(&global);atomic_store(&global,saved[seat].member)
    M(mx,mick_x);M(my,mick_y);M(px,pos_x);M(py,pos_y);M(buttons,buttons);
    M(ax,abs_x);M(ay,abs_y);M(moved,moved);M(look,look_on);
#undef M
    saved[current].fx=frac_x;saved[current].fy=frac_y;frac_x=saved[seat].fx;frac_y=saved[seat].fy;
    current=seat;
}

static int16_t scale(int16_t v)
{
    int32_t p = (int32_t)v * 100;
    int16_t d = W(0x360);
    if (d == 0) port_fatal("mouse: divide by zero in _6F8 (no mouse driver)");
    return (int16_t)(p / d);
}

/* _6E7: the motion (function 0Bh), each axis scaled by _6F8. */
void Read_Mouse_Motion_seg019_6E7(int16_t *x, int16_t *y)
{
    uint16_t ax = 0x0B, bx = 0, cx = 0, dx = 0;
    mouse_int33(&ax, &bx, &cx, &dx);
    *x = scale((int16_t)cx);
    *y = scale((int16_t)dx);
}

/* _703: the buttons (function 3): left and right, bits 0 and 1. */
int seg019_703(void)
{
    uint16_t ax = 3, bx = 0, cx = 0, dx = 0;
    mouse_int33(&ax, &bx, &cx, &dx);
    return bx & 3;
}
