/* c3dentry.c: replaces the C side of src/sys/C3DENTRY.ASM (seg019 module 17): the far pointers C
   reaches the 3D renderer's data through (its _DATA), and the C entry points into the renderer,
   the frame buffer and the maths. C3DENTRY.ASM is translated (sys/c3dentry_x.c), so each entry
   pushes the C arguments as Turbo C's far call did and runs the translation (x86/entry.c); the
   near pointers cFstSinCos and cSinCos write through are slots of the C stack, copied back. The
   offsets are symbols.tsv's (the link's map). */
#include <stdio.h>
#include "compat.h"
#include "motion.h"
#include "player.h"
#include "port.h"
#include "sys.h"
#include "view3d.h"
#include "x86/asmrt.h"
#include "x86/entry.h"

#define SEG019 0x1F3Au

extern unsigned char lightabs[];

/* C3DENTRY.ASM's _DATA (DS:2370..23A3): the zero doubleword nothing reads, the far pointer to
   seg_5DFD, then the pointers C uses. */
struct Camera *cPlayer = (struct Camera *)(seg063 + 0x970);
int16 *cDbase = (int16 *)(seg051 + 0x2938);
int16 *cEntryStrt = (int16 *)(seg051 + 0x73F2);
unsigned char *cLightTabs = lightabs;
int16 *cDbbase = (int16 *)(seg051 + 0x161C);
int16 *cPixXferStuff = (int16 *)(seg048 + 0xB12);

#define W W16

void cZoom(unsigned zoom)
{
    uint16_t w[1] = { (uint16_t)zoom };
    port_far_entry(SEG019, 0x0C34, w, 1);
}

void cFBtoScreen(void) { port_far_entry(SEG019, 0x0C7E, 0, 0); }

void cFillFB(int c)
{
    uint16_t w[1] = { W(c) };
    port_far_entry(SEG019, 0x0C84, w, 1);
}

void seg019_CB7(int shade)
{
    uint16_t w[1] = { W(shade) };
    port_far_entry(SEG019, 0x0CB7, w, 1);
}

void seg019_CEA(int count)
{
    uint16_t w[1] = { W(count) };
    port_far_entry(SEG019, 0x0CEA, w, 1);
}

void cPlaceFB(int x, int y, int w_, int h)
{
    uint16_t w[4] = { W(x), W(y), W(w_), W(h) };
    port_far_entry(SEG019, 0x0D1D, w, 4);
}

/* The 3D frames drawn, those drawn under PORT_FRAME_TICKS ticks after the one before, and the
   game clock at the first and the last (port_report_motion). */
static unsigned long frames, short_frames;
static uint32_t first_tick, last_tick;

/* cRender waits first until 8 ticks have passed since the last 3D frame (the runtime's
   sys/pace.c, which says why): UW1's physics rounds once a frame, and at a frame a tick it
   lost every slow move towards +x or +y, so that sidestepping and walking backwards stalled or
   drifted by the way the player faced (issue 6). UW1's port had drawn as fast as the host
   could since it began; UW2's paced from rc8. */
void cRender(void)
{
    if (!port_shared_guest()) port_pace_frame((volatile uint32_t *)Time);
    if (!frames++) first_tick = *Time;
    else if (*Time - last_tick < PORT_FRAME_TICKS) short_frames++;
    last_tick = *Time;
    port_far_entry(SEG019, 0x0D7A, 0, 0);
}

/* UW1PORT_POS_LOG in the environment (main.c, at the end of a run): the player's position
   (PN's x and y in 1/256 tiles, and z), the way they face (PlayerFacing, a full turn in
   0x10000) and the way they last moved (PN.heading), and the 3D frames drawn over how many
   ticks of the 1/256 s clock and how many of them came early, for Exhume's tools/movecheck.py. */
void port_report_motion(void)
{
    fprintf(stderr, "uw1port: motion: x %d y %d z %d facing %u heading %u frames %lu ticks %lu short %lu\n",
            PN.x, PN.y, PN.z, (unsigned)(uint16_t)PlayerFacing, (unsigned)(uint16_t)PN.heading,
            frames, frames ? (unsigned long)(last_tick - first_tick) : 0ul, short_frames);
}

void cInit3d(void) { port_far_entry(SEG019, 0x0E1A, 0, 0); }

void cFstSinCos(int angle, int16 *a, int16 *b)
{
    uint16_t w[3] = { W(angle), port_near_slot(0), port_near_slot(1) };
    port_far_entry(SEG019, 0x0E63, w, 3);
    *a = port_near_get(0);
    *b = port_near_get(1);
}

void cSinCos(int angle, int16 *x, int16 *y)
{
    uint16_t w[3] = { W(angle), port_near_slot(0), port_near_slot(1) };
    port_far_entry(SEG019, 0x0EAE, w, 3);
    *x = port_near_get(0);
    *y = port_near_get(1);
}

int cAtan2(int x, int y)
{
    uint16_t w[2] = { W(x), W(y) };
    return (int16_t)port_far_entry(SEG019, 0x0EFB, w, 2);
}

int cSqRt(int32 v)
{
    uint16_t w[2] = { (uint16_t)v, (uint16_t)((uint32_t)v >> 16) };
    return (int16_t)port_far_entry(SEG019, 0x0F3F, w, 2);
}

void *cFrmtoRaw(void *data, unsigned char *pal, unsigned char mode)
{
    uint16_t w[5];
    port_far_arg(data, w);
    port_far_arg(pal, w + 2);
    w[4] = mode;
    return port_far_result(port_far_entry(SEG019, 0x0F83, w, 5));
}
