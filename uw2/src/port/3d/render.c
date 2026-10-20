/* render.c: replaces C3DENTRY.ASM's cRender and gives the renderer the port's interface
   (render.h): the frame buffer it draws into and the per-object sprite hook. The rendering
   itself is seg004 and seg003 translated from the assembly (x86/asmrt.h, tools/asm2c.py). */
#include <stdio.h>
#include <stdlib.h>
#include "compat.h"
#include "motion.h"
#include "object.h"
#include "player.h"
#include "port.h"
#include "sys.h"
#include "view3d.h"
#include "x86/asmrt.h"
#include "3d/render.h"

#define FD71_SEG (0x60B9u + PORT_LOAD_SEG)
#define G370D (seg_370D - 8)
#define W370D(o) ((uint16_t)(G370D[(uint16_t)(o)] | G370D[(uint16_t)((o) + 1)] << 8))
#define SEG004_DS ((uint8_t *)seg052_519C)

void port_render_framebuffer(struct uw_framebuffer *fb)
{
    uint16_t w = W370D(0xAEE);
    fb->pixels = port_mk_fp(W370D(0x958), 0) + 2;
    fb->width = w;
    fb->height = (int)W370D(0xAF0) + 1;
    fb->pitch = w + 2;
}

static const struct uw_framebuffer *render_fb;

/* cRender (C3DENTRY.ASM): on seg021's private stack (FD71:0510) with DS = ES = FD71, it points
   int 0 at FD71:05A0 (the port's divide faults go to asm_divfault, which reads the handler the
   renderer keeps at FD71:05A5), then clear_fbuf (GRENTRY.ASM), render_3d (INSTANCE.ASM),
   GRMISC.ASM's _18 and update_cache (PGCACHE.ASM), each far-called. The registers are as
   cRender leaves them for the first call: AX 2500h and DX 05A0h from the int 21h, BX and ES 0. */
void port_render_3d(const struct uw_framebuffer *fb)
{
    render_fb = fb;
    if (fb->pixels != port_mk_fp(W370D(0x958), 0) + 2)
        port_halt("port_render_3d: the faithful renderer draws only into stdat's frame buffer");
    SET_SS(FD71_SEG);
    SP = 0x510;
    SET_DS(FD71_SEG);
    SET_ES(0);
    AX = 0x2500;
    BX = 0;
    DX = 0x05A0;
    DF = 0;
    asm_run_far(0x0085, 0x06E2);
    asm_run_far(0x065C, 0x3007);
    asm_run_far(0x0085, 0x0018);
    asm_run_far(0x065C, 0x7F12);
    render_fb = NULL;
}

/* UW2PORT_SPRITEHOOK=1 in the environment installs a hook that only counts the sprites it is
   shown and lets each be drawn as in DOS, and reports them now and then: a check that every
   sprite goes through the hook and that a hook returning 0 leaves the output alone. */
static long hook_seen[2];

static int counting_hook(const struct uw_sprite_draw *d)
{
    hook_seen[d->kind]++;
    if ((hook_seen[0] + hook_seen[1]) % 500 == 1)
        fprintf(stderr, "sprite hook: %ld objects and %ld critter frames so far (the last %s %d at %d,%d, %dx%d, shade %d)\n",
                        hook_seen[0], hook_seen[1], d->object ? "an object of type" : "type", d->item,
                        d->screen_x, d->screen_y, d->screen_w, d->screen_h, d->light);
    return 0;
}

/* The 3D frames drawn, those drawn under PORT_FRAME_TICKS ticks after the one before, and the
   game clock at the first and the last (port_report_motion). */
static unsigned long frames, short_frames;
static uint32_t first_tick, last_tick;

/* UW2PORT_POS_LOG in the environment (main.c, at the end of a run): the player's position
   (PN's x and y in 1/256 tiles, and z), the way they face (PlayerFacing, a full turn in
   0x10000) and the way they last moved (PN.heading), and the 3D frames drawn over how many
   ticks of the 1/256 s clock and how many of them came early, for Exhume's tools/movecheck.py. */
void port_report_motion(void)
{
    fprintf(stderr, "uw2port: motion: x %d y %d z %d facing %u heading %u frames %lu ticks %lu short %lu\n",
            PN.x, PN.y, PN.z, (unsigned)(uint16_t)PlayerFacing, (unsigned)(uint16_t)PN.heading,
            frames, frames ? (unsigned long)(last_tick - first_tick) : 0ul, short_frames);
}

/* cRender waits first until 8 ticks have passed since the last 3D frame (the runtime's
   sys/pace.c, which says why: at a frame a tick the game lost every slow move towards +x or
   +y, rc8's "struggling to walk back whilst in the corridors"). */
void cRender(void)
{
    struct uw_framebuffer fb;
    static int checked;
    if (!port_shared_guest()) port_pace_frame((volatile uint32_t *)Time);
    if (!frames++) first_tick = *Time;
    else if (*Time - last_tick < PORT_FRAME_TICKS) short_frames++;
    last_tick = *Time;
    if (!checked) {
        checked = 1;
        if (getenv("UW2PORT_SPRITEHOOK")) port_set_sprite_hook(counting_hook);
    }
    port_render_framebuffer(&fb);
    port_render_3d(&fb);
}

/* RENDER_TAG: the objects of this frame's sprite opcodes, by the opcode's offset in the render
   database (FD58). The frame's database is written before it is run, and a tag is looked up
   by the offset of the opcode being drawn. */
#define MAXTAG 512
static struct { uint16_t at; const struct Object *o; int16_t tile; } tags[MAXTAG];
static int ntags;
static uint16_t last_at = 0xFFFF;

void port_render_tag(const void *object, const void *db)
{
    uint16_t at = (uint16_t)((const uint8_t *)db - SEG004_DS);
    if (at <= last_at) ntags = 0;            /* a new frame's database starts again at DbEntry */
    last_at = at;
    if (ntags == MAXTAG) return;
    tags[ntags].at = at;
    tags[ntags].o = object;
    tags[ntags].tile = (int16_t)((tmptr - mlowptr) + mptrmod);
    ntags++;
}

static uw_sprite_fn sprite_hook;

void port_set_sprite_hook(uw_sprite_fn fn)
{
    sprite_hook = fn;
}

/* TMAPOPS.ASM's in_scalebm at 407Bh, in place of `call far ptr _seg003_0272_D1E`: the sprite
   is projected, its rectangle is at seg_370D:0B20..0B26, SI is past the record it copied (C856
   for an object, C848 for a critter) and the database offset after the opcode's operands is
   under ES on the stack. The hook sees the sprite; unless it draws the object itself, the
   sprite is drawn as in DOS. */
uint32_t port_sprite_draw(void)
{
    if (sprite_hook) {
        struct uw_sprite_draw d;
        uint16_t after = rw(pSS, SP + 2), op;
        int i;
        d.kind = SI == 0xC85C ? 0 : 1;
        op = (uint16_t)(after - (d.kind == 0 ? 8 : 12));
        d.item = (int16_t)rw(SEG004_DS, op + 2);
        d.frame = d.kind ? (int16_t)rw(SEG004_DS, op + 8) : 0;
        d.light = (int16_t)rw(SEG004_DS, op + (d.kind ? 6 : 4));
        d.object = NULL;
        d.tile_x = d.tile_y = -1;
        d.x = d.y = d.z = d.heading = 0;
        for (i = 0; i < ntags; i++)
            if (tags[i].at == op) {
                const struct Object *o = tags[i].o;
                d.object = o;
                d.tile_x = tags[i].tile & 63;
                d.tile_y = tags[i].tile >> 6 & 63;
                d.x = OBJ_FINEX(o);
                d.y = OBJ_FINEY(o);
                d.z = OBJ_Z(o);
                d.heading = OBJ_HEADING(o);
                break;
            }
        /* BX, CX: the corner opposite the one projected first, BP its depth */
        d.view_x = (int16_t)(uint16_t)(BX - rw(SEG004_DS, 0x14AC));
        d.view_y = (int16_t)(uint16_t)(CX - rw(SEG004_DS, 0x14AE));
        d.view_z = (int16_t)BP;
        d.screen_x = (int16_t)W370D(0xB20);
        d.screen_y = (int16_t)W370D(0xB22);
        d.screen_h = (int16_t)W370D(0xB24);
        d.screen_w = (int16_t)W370D(0xB26);
        d.fb = render_fb;
        if (sprite_hook(&d)) return 0;
    }
    return asm_callf(ASM_JMP(0x0085, 0x0D1E), 0x065C + PORT_LOAD_SEG, 0x4080);
}
