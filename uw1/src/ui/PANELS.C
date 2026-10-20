/* target: seg036_3087 */
/* opts: -mm -1 -G -O -Y -d */
/* The screen furniture around the 3D view: the vitality and mana flasks, the compass, the
   power gem, the dragons, the eyes, the first-person weapon and its frames from
   DATA\weapons.dat, the rune shelf and active spell icons, the turning
   stat/inventory/rune panel, and the frame buffer send. The whole of UW1's DOS resident
   segment seg036_3087, in original order.

   What it does in the game: the rest of the game asks for a display element to change
   with set_screen_frame(which, value), which sets that element's goal; update_screen, run
   each frame, moves each element a step towards its goal through its adjust_ function,
   some at once, some every 32 ticks of the clock and some every 64. The elements, by
   index (adjust, goal, setting): 0 the vitality flask, 1 the mana flask, 2 the compass, 3
   the power gem, 4 and 5 the two dragons beside the view (set_screen_frame(4, n) picks
   whichever is free, 1 to 3 an animation), 6 the right-hand panel (it turns over when
   changed), 7 the gargoyle eyes and 8 the weapon (0..2 a swing of that kind, 3 drawing, 4
   ready, 5 sheathing, 6 sheathed). The first-person weapon's pictures come from
   WEAPONS.GR by the weapon kind and hand, with per-picture positions from
   DATA\weapons.dat, and are drawn into the frame buffer before it is sent to the screen
   (do_fbuf_bms, send_FB).

   UW1's differences from UW2's seg037_32C0: the dragons (adjust_dragons) take elements 4
   and 5; the panel does not slide but turns over, drawn squeezed column by column
   (flip_scale, flip_column) from copies kept in three EMS handles (init_panelflip,
   free_panelflip; plain redraws without EMS); the weapon file has positions only, the
   frame of each step coming from the step itself; the frame buffer gets three pictures
   on top; looking at a shaft or a grave plays a cutscene value.

   Data owned: the goal, setting and adjust tables, the flask, compass, dragon and
   weapon tables, RightPanel (0 inventory, 1 runes, 2 statistics, 4 while turning) and
   panel_dispatch, the weapon frame state (weapid, weap_frame ...). The element numbers
   and states are gfx.h's SCR_*, WEAP_* and PANEL_*.
   Function and global names are UW2's (FM Towns) where the routine is the same; UW1 has
   no symbol-bearing build. Names of UW1's own are descriptive, and the statics' names
   in _BSS were chosen for their layout keys (tools/bssorder.py). */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dos.h>
#include "combat.h"
#include "critter.h"
#include "gfx.h"
#include "inv.h"
#include "motion.h"
#include "player.h"
#include "sys.h"
#include "ui.h"
#include "view3d.h"

/* Declared in each file that uses it, its own way (no header). */
extern struct Spell far spells[53];

/* This file's _BSS, DS:359A-3635. The two function-level statics come first, in
   definition order and unaligned: update_screen's old_time (DS:359A) and
   adjust_dragons' dcount (DS:359B). The rest is laid out by name (tools/bssorder.py):
   flip_ch 318, flip_left 470, flip_high, flip_top and flip_page 478 (definition order),
   weap_x and weap_y 495, setting 675, weap_offs 743, flip_width_now 862, goal 879,
   pnl_wide 912, pnl_skew 920. The panel turn's names (UW1's own) were chosen for these
   keys. */
static int16 flip_ch;                   /* DS:35A0, the squeezed panel's height */
static int16 flip_left;                 /* DS:35A2, the panel's place and height */
static int16 flip_high;                 /* DS:35A4 */
static int16 flip_top;                  /* DS:35A6 */
static int16 flip_page;                 /* DS:35A8, the panel turned to */
unsigned char weap_x[0x1C];             /* DS:35AA */
unsigned char weap_y[0x1C];             /* DS:35C6 */
unsigned char setting[9];               /* DS:35E2 */
int16 weap_offs[0x1D];                  /* DS:35EC */
static int16 flip_width_now;            /* DS:3626, the squeezed panel's width */
unsigned char goal[9];                  /* DS:3628 */
static int16 pnl_wide;                  /* DS:3632, the panel's width */
static int16 pnl_skew;                  /* DS:3634, blank rows above and below a column */

/* The panel showing on the right, an index into panel_dispatch (FM Towns _RightPanel). */
unsigned char RightPanel = 0;           /* DS:784 */
/* Which screen elements to redraw: on every 64th tick, every 32nd tick, and now. One bit
   per element, by its index in `adjust`. */
static int16 slow_adjust = 0;           /* DS:785 */
static int16 fast_adjust = 0;           /* DS:787 */
static int16 now_adjust = 0;            /* DS:789 */
/* UW1: the dragons' animation states and sprites. */
static int16 dstate[2] = { 0, 0 };      /* DS:78B */
static int16 dspr[2] = { 0, 0 };        /* DS:78F */
char weapid = -1;                       /* DS:793 */
char last_weap = -1;
char wlstate = WEAP_SHEATHED;
int16 weap_frame = -1;                  /* DS:796 */
int16 wxo = 0;
int16 wfo = 1;
int16 FL_X[2] = { 0xF8, 0x11C };        /* DS:79C */
int16 liquid_y[14] = { 0x2C, 0x30, 0x32, 0x34, 0x36, 0x38, 0x3A, 0x3B, 0x3C, 0x3D, 0x3F, 0x41, 0x43, 0x45 };
int16 liquid_h[14] = { 0x0, 0x4, 0x5, 0x6, 0x7, 0x7, 0x7, 0x7, 0x6, 0x5, 0x4, 0x4, 0x4, 0x4 };
unsigned char bubbling[2] = { 0, 0 };   /* DS:7D8 */
unsigned char swishing[2] = { 0, 0 };   /* DS:7DA, the dragons' tails */
int16 nedl_x[16] = { 0x88, 0x80, 0x78, 0x74, 0x70, 0x70, 0x74, 0x7C, 0x88, 0x90, 0x9C, 0xA0, 0xA0, 0x9C, 0x98, 0x90 };
int16 nedl_y[16] = { 0x44, 0x42, 0x41, 0x3E, 0x3A, 0x36, 0x34, 0x31, 0x2F, 0x31, 0x34, 0x36, 0x3A, 0x3E, 0x41, 0x42 };
/* UW1: the dragons' parts: x of each dragon's two still parts and its tail, the place
   and size of each of its three animations, the tail's frames and the still parts'
   pictures. */
static int16 dpart1_x[2] = { 0x24, 0xE4 };     /* DS:81C */
static int16 dpart2_x[2] = { 0x24, 0xCC };     /* DS:820 */
static int16 danim_x[2][3] = { { 0x28, 0x30, 0x24 }, { 0xCC, 0xCC, 0xC8 } };
static int16 danim_y[2][3] = { { 0x2C, 0x36, 0x36 }, { 0x2C, 0x36, 0x36 } };
static int16 danim_w[2][3] = { { 0x21, 0x18, 0x25 }, { 0x22, 0x18, 0x26 } };
static int16 danim_h[2][3] = { { 0x0E, 0x10, 0x17 }, { 0x0E, 0x10, 0x17 } };
static int16 dtail_x[2] = { 0x28, 0xE0 };      /* DS:854 */
static int16 dtail_seq[2][7] = {
    { ICON_DRAGONS + 0xE, ICON_DRAGONS + 0xF, ICON_DRAGONS + 0x10, ICON_DRAGONS + 0x11, ICON_DRAGONS + 0x10, ICON_DRAGONS + 0xF, ICON_DRAGONS + 0xE },
    { ICON_DRAGONS + 0x20, ICON_DRAGONS + 0x21, ICON_DRAGONS + 0x22, ICON_DRAGONS + 0x23, ICON_DRAGONS + 0x22, ICON_DRAGONS + 0x21, ICON_DRAGONS + 0x20 }
};
static int16 dpart1_pic[2] = { ICON_DRAGONS, ICON_DRAGONS + 0x12 };   /* DS:874 */
static int16 dpart2_pic[2] = { ICON_DRAGONS + 0x1, ICON_DRAGONS + 0x13 };   /* DS:878 */
/* UW1: the turning panel's height and width in percent, by step (flip_scale). */
static int16 flip_hpct[8] = { 0x64, 0x67, 0x69, 0x6A, 0x6A, 0x6A, 0x69, 0x67 };
static int16 flip_wpct[8] = { 0x64, 0x5C, 0x47, 0x26, 0x00, 0x26, 0x47, 0x5C };
/* UW1: three EMS handles for the turning panel's pictures (0 the old panel and the
   edge, 1 the new panel, 2 the work buffer). */
static uint16 flip_handle[3] = { 0, 0, 0 };     /* DS:89C */
/* The turning panel's progress. */
struct {
    unsigned char frame;
    unsigned char flag;
} pbuf = { 0, 0 };                      /* DS:8A2 */
/* The next free place in the weapon pictures' EMS buffer. */
unsigned char far *wbuf = 0;            /* DS:8A4 */
/* UW1: bit 0 set when the EMS handles are there and the panel turns. */
static char flip_flags = 0;             /* DS:8A8 */
int16 eyes_seq[5] = { ICON_EYES + 0x1, ICON_EYES + 0x2, ICON_EYES + 0x3, ICON_EYES + 0x2, ICON_EYES + 0x1 };
int16 eyes = 0;                         /* DS:8B3 */
int16 eyes_frame = 0;
int16 rune_x[3] = { 0xB0, 0xBF, 0xCE };
int16 spell_x[3] = { 0x56, 0x45, 0x34 };
void (far *panel_dispatch[3])(void) = { RedispInv, RedispRune, RedispStat };
int16 level[4] = { 0, 0, 0, 0 };        /* DS:8CF */
static int16 dpart1_spr[2] = { 0, 0 };  /* DS:8D7 */
static int16 dpart2_spr[2] = { 0, 0 };  /* DS:8DB */
static int16 dtail_spr[2] = { 0, 0 };   /* DS:8DF */
void (far *adjust[9])(int which) = { adjust_flasks, adjust_flasks,
    (void (far *)(int))adjust_compass, (void (far *)(int))adjust_power,
    adjust_dragons, adjust_dragons,
    (void (far *)(int))adjust_panel, (void (far *)(int))adjust_eyes,
    (void (far *)(int))adjust_weapon };

/* Draws flask which (0 vitality, 1 mana) at its goal level at once. */
void far set_flask(register int which)
{
    int frame;
    register int i;

    if (which == 0)
        frame = player->poison ? ICON_FLASKS + 0x32 : ICON_FLASKS;
    else if (which == 1)
        frame = ICON_FLASKS + 0x19;
    else
        return;
    pic_to_screen(ICON_FLASKS + 0x4B, FL_X[which], 0x4A, 1, 1);
    for (i = 0; i < goal[which]; i++) {
        move_sprite(level[which], FL_X[which], liquid_y[i + 1]);
        draw_sprite(level[which], frame + i);
        update_sprites();
    }
    setting[which] = goal[which];
}

/* Draws the compass at its goal heading at once. */
void far set_compass(void)
{
    register int dir;

    dir = goal[SCR_COMPASS];
    draw_sprite(level[2], (dir & 3) + ICON_COMPASS);
    move_sprite(level[3], nedl_x[dir], nedl_y[dir]);
    draw_sprite(level[3], dir + (ICON_COMPASS + 0x4));
    update_sprites();
}

/* Resets every display element (the weapon sheathed, the inventory panel, the dragons
   gone). */
void far reset_scrgr(void)
{
    register int i;

    for (i = 0; i < 9; i++)
        setting[i] = goal[i] = 0;
    erase_sprite(dspr[0]);
    erase_sprite(dspr[1]);
    dstate[0] = dstate[1] = 0;
    RightPanel = PANEL_INV;
    eyes_frame = 0;
    slow_adjust &= ~(1 << SCR_EYES);
    goal[SCR_EYES] = 4;
    setting[SCR_WEAPON] = goal[SCR_WEAPON] = WEAP_SHEATHED;
    wlstate = WEAP_SHEATHED;
    weap_frame = 0;
    update_sprites();
}

/* Creates the sprites of the flasks, dragons, compass, needle and eyes once, then draws
   the whole frame: flasks, dragons, compass, eyes, rune shelf and the right-hand panel
   from PANELS.GR. */
void far init_scrgr(void)
{
    static char inited = 0;
    register int i;

    if (!inited) {
        for (i = 0; i < 2; i++) {
            level[i] = create_sprite(0);
            change_sprite(level[i], 0, 0, 0x18, 4);
            dpart1_spr[i] = create_sprite(2, 0xD, 0xA);
            change_sprite(dpart1_spr[i], dpart1_x[i], 0x41, 0xD, 0xA);
            Transparency = 1;
            dpart2_spr[i] = create_sprite(2, 0x25, 0x17);
            Transparency = 0;
            change_sprite(dpart2_spr[i], dpart2_x[i], 0x36, 0x25, 0x17);
            dtail_spr[i] = create_sprite(0);
            change_sprite(dtail_spr[i], dtail_x[i], 0x86, 0xC, 0x1C);
            goal[i + 4] = setting[i + 4] = 0;
        }
        level[2] = create_sprite(0);
        change_sprite(level[2], 0x70, 0x44, 0x38, 0x20);
        level[3] = create_sprite(0);
        change_sprite(level[3], nedl_x[0], nedl_y[0], 3, 4);
        eyes = create_sprite(0);
        change_sprite(eyes, 0x80, 0xC3, 1, 1);
        goal[SCR_WEAPON] = setting[SCR_WEAPON] = WEAP_SHEATHED;
        inited = 1;
    }
    grSoftPageFlip();
    copy_visible_to_hidden();
    for (i = 0; i < 2; i++) {
        set_flask(i);
        draw_sprite(dpart1_spr[i], dpart1_pic[i]);
        draw_sprite(dpart2_spr[i], dpart2_pic[i]);
        draw_sprite(dtail_spr[i], (i ? 0x12 : 0) + (ICON_DRAGONS + 0xE));
    }
    set_compass();
    draw_sprite(eyes, ICON_EYES);
    set_runes(player->shelf);
    read_gr_far("panels", RightPanel, stdat);
    show(0xEC, 0xC0, stdat, 0x72, 0x53, 0, 0);
    panel_dispatch[RightPanel]();
    update_sprites();
    grPageFlip();
    grSoftPageFlip();
}

/* Stops a panel turn where it is, taking the target panel at once. */
void far hold_scrgr(void)
{
    RightPanel = goal[SCR_PANEL] = setting[SCR_PANEL];
    pbuf.frame = 0;
    pbuf.flag = 0;
}

void far free_scrgr(void)
{
    free_panelflip();
}

/* Sets display element which's goal to val (see the file comment). Flask values are hit
   points and mana, scaled to 12 steps; a dragon animation goes to whichever dragon is
   free (at random when both are); the weapon's goal is held back while another weapon's
   pictures are to be loaded. */
void far set_screen_frame(char which, int val)
{
    register int max;

    switch (which) {
    case SCR_VITALITY:
    case SCR_MANA:
        max = which == SCR_MANA ? player->max_mana : playerdat->avghit;
        if (max)
            goal[which] = val * 12 / max;
        else
            goal[which] = 0;
        if (goal[which] >= 12)
            goal[which] = 12;
        slow_adjust |= 1 << which;
        break;
    case SCR_COMPASS:
        if (setting[SCR_COMPASS] != val) {
            slow_adjust |= 1 << which;
            goal[SCR_COMPASS] = val;
        }
        break;
    case SCR_POWER:
        if (val != 9)
            now_adjust |= 1 << which;
        else
            slow_adjust |= 1 << which;
        goal[which] = val;
        break;
    case SCR_DRAGON:
        if (goal[4] == val)
            break;
        if (goal[5] == val)
            break;
        if (setting[4] == val)
            break;
        if (setting[5] == val)
            break;
        if (setting[4] == 0 && setting[5] == 0)
            which += rand() & 1;
        else if (setting[4] != 0) {
            if (setting[5] == 0)
                which++;
            else if (goal[4] == 0 && goal[5] == 0)
                which += rand() & 1;
            else if (goal[4] != 0 && goal[5] == 0)
                which++;
        }
        goal[which] = val;
        slow_adjust |= 1 << which;
        break;
    case SCR_WEAPON:
        if (weapid != last_weap && goal[SCR_WEAPON] == WEAP_SHEATHED)
            break;
        fast_adjust |= 1 << which;
        goal[SCR_WEAPON] = val;
        break;
    case SCR_PANEL:
#ifndef __TURBOC__
        if (port_shared_on()) {
            RightPanel = setting[SCR_PANEL] = goal[SCR_PANEL] = val;
            slow_adjust &= ~(1 << SCR_PANEL);
            pretty_panelagain();
            editchng(2);
            break;
        }
#endif
        if (RightPanel == PANEL_TURNING)
            break;
    default:
        slow_adjust |= 1 << which;
        goal[which] = val;
        break;
    }
}

/* Called every frame: steps the display elements whose redraw bits are set, the 'now'
   ones at once, the fast ones every 32 ticks, the slow ones every 64 (when the flasks may
   also start bubbling and the dragons swish their tails at random). */
void far update_screen(void)
{
    static unsigned char old_time;
    unsigned char now;
    int r;
    char did;
    register int i;
    register int bit;

#ifndef __TURBOC__
    /* The host and client animate in the same engine tick. Each view must
       consume that tick for its own weapon, flasks and panel state. */
    static unsigned char shared_time[2];
    old_time = shared_time[port_shared_guest()];
#endif
    now = GAME_TIME() & 0xFF;
    did = 0;
    if (now_adjust) {
        for (i = 0, bit = 1; i < 9; i++, bit <<= 1) {
            if (now_adjust & bit) {
                adjust[i](i);
                now_adjust &= ~bit;
                did = 1;
            }
        }
    }
    if ((now >> 5) != (old_time >> 5)) {
        for (i = 0, bit = 1; i < 9; i++, bit <<= 1)
            if (fast_adjust & bit)
                adjust[i](i);
        did = 1;
    }
    if ((now >> 6) != (old_time >> 6)) {
        if ((r = rand()) < 0x666 && bubbling[r & 1] == 0) {
            bubbling[r & 1] = 1;
            slow_adjust |= 1 << (r & 1);
        }
        if ((r = rand()) < 0x666 && swishing[r & 1] == 0) {
            swishing[r & 1] = 1;
            slow_adjust |= 1 << (r + 4 & 1);
        }
        for (i = 0, bit = 1; i < 9; i++, bit <<= 1)
            if (slow_adjust & bit)
                adjust[i](i);
        did = 1;
    }
    if (did) {
        update_sprites();
        old_time = now;
    }
#ifndef __TURBOC__
    shared_time[port_shared_guest()] = old_time;
#endif
}

/* Moves flask which one step towards its goal (filling or draining) and animates its
   bubbles; the vitality flask switches to the green set while the player is poisoned. */
void far adjust_flasks(int which)
{
    static int16 bub_frame[2] = { ICON_FLASKS + 0xD, ICON_FLASKS + 0x26 };
    static int16 bub_spr[2] = { 0, 0 };
    static int16 drain_spr[2] = { 0, 0 };
    static int16 mask_spr[2] = { 0, 0 };
    /* match: a register copy of the parameter, as in UW2 */
    register int f = which;
    register int n;
    int diff;
    int base;
    int bub_first;
    int bub_last;
    int y;

    switch (f) {
    case 0:
        if (player->poison) {
            base = ICON_FLASKS + 0x32;
            bub_first = ICON_FLASKS + 0x3F;
            bub_last = ICON_FLASKS + 0x4A;
            if (bub_frame[0] >= ICON_FLASKS + 0xD && bub_frame[0] <= ICON_FLASKS + 0x18) {
                set_flask(0);
                bub_frame[0] += 0x32;
            }
        } else {
            base = ICON_FLASKS;
            bub_first = ICON_FLASKS + 0xD;
            bub_last = ICON_FLASKS + 0x18;
            if (bub_frame[0] >= ICON_FLASKS + 0x3F && bub_frame[0] <= ICON_FLASKS + 0x4A) {
                set_flask(0);
                bub_frame[0] -= 0x32;
            }
        }
        break;
    case 1:
        base = ICON_FLASKS + 0x19;
        bub_first = ICON_FLASKS + 0x26;
        bub_last = ICON_FLASKS + 0x31;
        break;
    default:
        return;
    }
    if (drain_spr[f] == 0) {
        drain_spr[f] = create_sprite(0);
        change_sprite(drain_spr[f], FL_X[f], 0x4A, 0x18, 0x21);
        bub_spr[f] = create_sprite(0);
        change_sprite(bub_spr[f], 0, 0, 0x18, 4);
        mask_spr[f] = create_sprite(0);
        change_sprite(mask_spr[f], FL_X[f], 0x4A, 0x18, 4);
    }
    diff = goal[f] - setting[f];
    if (diff > 0) {
        n = ++setting[f];
        move_sprite(level[f], FL_X[f], liquid_y[n]);
        draw_sprite(level[f], base + n - 1);
    } else if (diff < 0) {
        n = --setting[f];
        y = liquid_y[n + 1];
        change_sprite(drain_spr[f], FL_X[f], y, 0x18, liquid_h[n + 1]);
        set_yoff(drain_spr[f], 0x4A - y);
        draw_sprite(drain_spr[f], ICON_FLASKS + 0x4B);
        if (n > 0) {
            move_sprite(level[f], FL_X[f], liquid_y[n]);
            draw_sprite(level[f], base + n - 1);
        }
    } else {
        if (bubbling[f] == 0)
            slow_adjust &= ~(1 << which);
        n = setting[f];
    }
    if (bubbling[f] == 1 && n < 8 && n > 0) {
        if (bub_frame[f] == bub_last) {
            bub_frame[f] = bub_first;
            draw_sprite(level[f], base + n - 1);
            bubbling[f] = 0;
        } else {
            move_sprite(bub_spr[f], FL_X[f], liquid_y[n]);
            draw_sprite(bub_spr[f], ++bub_frame[f]);
            set_yoff(mask_spr[f], 0x4A - liquid_y[n]);
            move_sprite(mask_spr[f], FL_X[f], liquid_y[n]);
            draw_mask(mask_spr[f], ICON_FLASKS + 0x4C);
        }
    }
}

/* UW1: animates dragon which - 4 (elements 4 and 5): its tail when swishing, and the
   animation its setting names (1 to 3), in states 1 (start) to 5 (end): each plays its
   frames from first to last a number of times (dcount) or until a new goal comes. */
void far adjust_dragons(int which)
{
    static int16 frame[2] = { 0, 0 };
    static int16 tail = 0;
    static int16 dcount[2];
    int16 first[2][3] = { { ICON_DRAGONS + 0x2, ICON_DRAGONS + 0x6, ICON_DRAGONS + 0xA }, { ICON_DRAGONS + 0x14, ICON_DRAGONS + 0x18, ICON_DRAGONS + 0x1C } };
    int16 last[2][3] = { { ICON_DRAGONS + 0x5, ICON_DRAGONS + 0x9, ICON_DRAGONS + 0xD }, { ICON_DRAGONS + 0x17, ICON_DRAGONS + 0x1B, ICON_DRAGONS + 0x1F } };
    register int d;
    register int a;

    if (which != 4 && which != 5)
        return;
    d = which - 4;
    if (dspr[d] == 0) {
        Transparency = 1;
        dspr[d] = create_sprite(3, 0x28, 0x18);
        Transparency = 0;
    }
    if (swishing[d] == 1) {
        draw_sprite(dtail_spr[d], dtail_seq[d][tail++]);
        if (tail >= 7) {
            tail = 0;
            swishing[d] = 0;
        }
    }
    if (dstate[d] == 0 && goal[d + 4] != 0) {
        setting[d + 4] = goal[d + 4];
        dstate[d] = 1;
    }
    a = setting[d + 4] - 1;
    switch (setting[d + 4]) {
    case 0:
        if (swishing[d] == 0)
            slow_adjust &= ~(1 << which);
        break;
    case 1:
        switch (dstate[d]) {
        case 1:
            goal[d + 4] = 0;
            change_sprite(dspr[d], danim_x[d][a], danim_y[d][a], danim_w[d][a], danim_h[d][a]);
            frame[d] = first[d][a];
            dcount[d] = 1;
            dstate[d] = 3;
        case 3:
            draw_sprite(dspr[d], frame[d]++);
            if (last[d][a] < frame[d]) {
                frame[d] = first[d][a];
                dcount[d]--;
            }
            if (goal[d + 4] != 0 || dcount[d] == 0)
                dstate[d] = 4;
            break;
        case 4:
            draw_sprite(dspr[d], frame[d]++);
            if (last[d][a] < frame[d])
                dstate[d] = 5;
            break;
        case 5:
            erase_sprite(dspr[d]);
            setting[d + 4] = 0;
            dstate[d] = 0;
            break;
        }
        break;
    case 2:
        switch (dstate[d]) {
        case 1:
            goal[d + 4] = 0;
            change_sprite(dspr[d], danim_x[d][a], danim_y[d][a], danim_w[d][a], danim_h[d][a]);
            frame[d] = first[d][a];
            dcount[d] = 3;
            dstate[d] = 2;
        case 2:
            draw_sprite(dspr[d], frame[d]++);
            if (first[d][a] + 1 < frame[d])
                dstate[d] = 3;
            break;
        case 3:
            draw_sprite(dspr[d], frame[d]++);
            if (last[d][a] < frame[d]) {
                frame[d] = first[d][a] + 2;
                dcount[d]--;
            }
            if (goal[d + 4] != 0 || dcount[d] == 0)
                dstate[d] = 4;
            break;
        case 4:
            draw_sprite(dspr[d], frame[d]--);
            if (first[d][a] > frame[d])
                dstate[d] = 5;
            break;
        case 5:
            erase_sprite(dspr[d]);
            setting[d + 4] = 0;
            dstate[d] = 0;
            break;
        }
        break;
    case 3:
        switch (dstate[d]) {
        case 1:
            goal[d + 4] = 0;
            erase_sprite(dpart2_spr[d]);
            change_sprite(dspr[d], danim_x[d][a], danim_y[d][a], danim_w[d][a], danim_h[d][a]);
            frame[d] = first[d][a];
            dcount[d] = 6;
            dstate[d] = 2;
        case 2:
            draw_sprite(dspr[d], frame[d]++);
            if (last[d][a] < frame[d]) {
                frame[d]--;
                dstate[d] = 3;
            }
            break;
        case 3:
            if (goal[d + 4] != 0 || --dcount[d] == 0)
                dstate[d] = 4;
            break;
        case 4:
            draw_sprite(dspr[d], --frame[d]);
            if (first[d][a] == frame[d])
                dstate[d] = 5;
            break;
        case 5:
            erase_sprite(dspr[d]);
            draw_sprite(dpart2_spr[d], dpart2_pic[d]);
            setting[d + 4] = 0;
            dstate[d] = 0;
            break;
        }
        break;
    }
}

/* Turns the compass one step (of 16) the short way towards its goal. */
void far adjust_compass(void)
{
    int cur;
    register int diff;

    cur = setting[SCR_COMPASS];
    diff = goal[SCR_COMPASS] - cur;
    if (diff == 0) {
        slow_adjust &= ~(1 << SCR_COMPASS);
        return;
    }
    if (diff < 0)
        diff += 16;
    if (diff <= 8)
        cur++;
    else
        cur--;
    cur &= 0xF;
    draw_sprite(level[2], (cur & 3) + ICON_COMPASS);
    move_sprite(level[3], nedl_x[cur], nedl_y[cur]);
    draw_sprite(level[3], cur + (ICON_COMPASS + 0x4));
    setting[SCR_COMPASS] = cur;
}

/* Shows the power gem: frame p (0 at rest, a blow's charge while it charges); 9 pulses
   through frames 9 to 13. */
void far adjust_power(void)
{
    static int16 spr = 0;
    static int16 last = 0;
    static int16 frame = 9;
    char p;

    p = goal[SCR_POWER];
    if (p >= 0 && p <= 13) {
        if (spr == 0) {
            spr = create_sprite(0);
            change_sprite(spr, 4, 0x3C, 1, 1);
        }
        if (p == 9) {
            draw_sprite(spr, frame++ + ICON_POWER);
            if (frame > 13)
                frame = 9;
            slow_adjust |= 1 << SCR_POWER;
        } else {
            if (last == 9)
                frame = 9;
            draw_sprite(spr, p + ICON_POWER);
            slow_adjust &= ~(1 << SCR_POWER);
        }
        last = p;
    }
}

/* Turns the right-hand panel over towards the goal panel, a frame at a time
   (do_panel_frame), with RightPanel 4 while it turns. */
void far adjust_panel(void)
{
    if (setting[SCR_PANEL] != goal[SCR_PANEL]) {
        if (pbuf.flag == 0) {
            pbuf.flag = 1;
            init_panelflip(goal[SCR_PANEL], 0xEC, 0xC0, 0x53, 0x72);
            RightPanel = PANEL_TURNING;
        }
#ifndef __TURBOC__
        /* port only: --enhance fast-panels, two steps a frame, never one past the end */
        if (do_panel_frame() == 1 || (ENHANCED(ENH_FAST_PANELS) && do_panel_frame() == 1)) {
#else
        if (do_panel_frame() == 1) {
#endif
            RightPanel = setting[SCR_PANEL] = goal[SCR_PANEL];
            pbuf.flag = 0;
            slow_adjust &= ~(1 << SCR_PANEL);
        }
    }
}

/* Animates the gargoyle's eyes above the view: a blink sequence of five frames, the set
   chosen by the goal. */
void far adjust_eyes(void)
{
    static unsigned char wait = 0;

    if (setting[SCR_EYES] == goal[SCR_EYES]) {
        goal[SCR_EYES] += 4;
        eyes_frame = 2;
        wait = 0;
    } else if (setting[SCR_EYES] != goal[SCR_EYES] - 4) {
        if (wait != 0) {
            eyes_frame = 2;
            wait = 0;
        }
        setting[SCR_EYES] = goal[SCR_EYES];
        goal[SCR_EYES] += 4;
    }
    if (eyes_frame == 3 && wait < 0x10)
        wait++;
    else
        draw_sprite(eyes, eyes_seq[eyes_frame++] + (setting[SCR_EYES] - 1) * 3);
    if (eyes_frame > 5) {
        setting[SCR_EYES] = eyes_frame = wait = 0;
        draw_sprite(eyes, ICON_EYES);
        slow_adjust &= ~(1 << SCR_EYES);
    }
}

/* gronk_gr callback: the next place in the EMS weapon buffer for a picture. */
unsigned char far * far adr_weapon(int size)
{
    unsigned char far *p;

    if (wbuf == 0) {
        seg012_10F(2, seg051_C377);
        wbuf = MK_FP(ems_frame, 0x8000);
        obj_inpage1 = 0xFF;
    }
    p = wbuf;
    wbuf += size;
    return p;
}

/* gronk_gr callback: records where each weapon picture starts. */
char far move_weapon(unsigned char far *p, int size, register int n)
{
    if (n == 0)
        weap_offs[0] = 0;
    weap_offs[n + 1] = weap_offs[n] + size;
    return 1;
}

/* Asks for weapon kind id's pictures (0..3; -1 none) to be loaded at the next weapon redraw. */
void far load_weapon(char id)
{
    weapid = id;
    if (id >= 0 && id <= 3 || last_weap != weapid)
        fast_adjust |= 1 << SCR_WEAPON;
}

/* Loads the pictures of the weapon kind weapid for the player's hand (lefty) from
   WEAPONS.GR, 0x1C a kind, and their x and y from DATA\weapons.dat. */
char far do_weapload(void)
{
    char ok;
    int start;
    register int off;
    register FILE *fp;

    if (last_weap == weapid)
        return 1;
    last_weap = weapid;
    if (weapid > 3 || weapid < 0)
        return 0;
    wbuf = 0;
    start = (1 - player->lefty) * 0x70 + weapid * 0x1C;
    ok = gronk_gr("weapons", start, 0x1C, (ArtAllocFn)adr_weapon, (ArtMoveFn)move_weapon);
    off = (1 - player->lefty) * 0xE0 + weapid * 0x38;
    fp = fopen("DATA\\weapons.dat", "rb");
    ok &= fp != 0;
    if (fp) {
        ok &= fseek(fp, off, 0) == 0;
        ok &= fread(weap_x, 1, 0x1C, fp) == 0x1C;
        ok &= fread(weap_y, 1, 0x1C, fp) == 0x1C;
        ok &= fclose(fp) == 0;
    }
    return ok;
}

/* Sways the weapon sideways as the player moves: how 0 still, 1 a little, 2 more. */
void far jiggle_weapon(int how)
{
    switch (how) {
    case 0:
        wxo = 0;
        wfo = 1;
        break;
    case 1:
        if (wxo > 0)
            wxo = rand() % 5;
        else
            wxo = -rand() % 5;
        wfo = 1;
        break;
    case 2:
        wxo = (wxo > 0 ? rand() : -rand()) % 10;
        wfo = 1;
    }
}

/* Steps the first-person weapon: drawing and sheathing play their three frames, a swing
   plays nine steps (the blow lands at step 3), then it is ready again; a new weapon's
   pictures are loaded while sheathed. */
void far adjust_weapon(void)
{
#ifdef __TURBOC__
    static char swung = 0;
#else
    static char shared_swung[2];
    char *seat_swung = &shared_swung[port_shared_guest()];
#define swung (*seat_swung)
#endif

    editchng(2);
    if (goal[SCR_WEAPON] > WEAP_SHEATHED)
        goal[SCR_WEAPON] = WEAP_SHEATHED;
    if (goal[SCR_WEAPON] == WEAP_SHEATHED) {
        if (setting[SCR_WEAPON] != WEAP_SHEATHED && setting[SCR_WEAPON] != WEAP_SHEATHING) {
            setting[SCR_WEAPON] = WEAP_SHEATHING;
            weap_frame = -1;
        }
    } else if (goal[SCR_WEAPON] == WEAP_READY) {
        if (setting[SCR_WEAPON] == WEAP_SHEATHED || setting[SCR_WEAPON] == WEAP_SHEATHING) {
            setting[SCR_WEAPON] = WEAP_DRAWING;
            weap_frame = 3;
        } else if (swung) {
            setting[SCR_WEAPON] = WEAP_READY;
            weap_frame = -1;
        } else if (setting[SCR_WEAPON] >= 0 && setting[SCR_WEAPON] <= 2) {
            if ((weap_frame -= 2) < -1)
                setting[SCR_WEAPON] = WEAP_READY;
        } else if (setting[SCR_WEAPON] == WEAP_READY && weapid == last_weap) {
            fast_adjust &= ~(1 << SCR_WEAPON);
            return;
        }
    } else
        setting[SCR_WEAPON] = goal[SCR_WEAPON];
    switch (setting[SCR_WEAPON]) {
    case WEAP_SHEATHING:
        if (++weap_frame > 2)
            setting[SCR_WEAPON] = WEAP_SHEATHED;
        break;
    case WEAP_DRAWING:
        if (--weap_frame < 0)
            setting[SCR_WEAPON] = WEAP_READY;
        break;
    case WEAP_SHEATHED:
        if (weapid != last_weap) {
            do_weapload();
            goal[SCR_WEAPON] = wlstate;
            wlstate = WEAP_SHEATHED;
            break;
        }
        weap_frame = -1;
        fast_adjust &= ~(1 << SCR_WEAPON);
        break;
    case WEAP_READY:
        if (weapid != last_weap) {
            goal[SCR_WEAPON] = WEAP_SHEATHED;
            wlstate = WEAP_READY;
            break;
        }
        weap_frame = -1;
        fast_adjust &= ~(1 << SCR_WEAPON);
        break;
    default:
        switch (++weap_frame) {
        case 0:
            swung = 0;
            break;
        case 1:
        case 2:
            break;
        case 3:
            fast_adjust &= ~(1 << SCR_WEAPON);
            swung = 1;
            break;
        case 9:
            goal[SCR_WEAPON] = setting[SCR_WEAPON] = WEAP_READY;
            weap_frame = -1;
            fast_adjust &= ~(1 << SCR_WEAPON);
            break;
        }
        break;
    }
}
#ifndef __TURBOC__
#undef swung
#endif

/* Reads the weapon's colour map for the player's skin (DATA\weapons.cm, 16 bytes, the
   second set for body type 1). */
char far load_weapcm(void)
{
    char ok;
    register FILE *fp;
    register int off;

    ok = (fp = fopen("DATA\\weapons.cm", "rb")) != 0;
    if (ok) {
        off = player->body == 1 ? 0x10 : 0;
        ok &= fseek(fp, off, 0) == 0;
        ok &= fread(Palettes[30], 1, 0x10, fp) == 0x10;
        ok &= fclose(fp) == 0;
    }
    return ok;
}

/* Shows the three runes on the shelf (0x18 and up leaves a place empty). */
void far set_runes(register unsigned char *runes)
{
    static int16 spr[3] = { 0, 0, 0 };
    register int i;

    if (spr[0] == 0) {
        Transparency = 1;
        for (i = 0; i < 3; i++) {
            spr[i] = create_sprite(1, 0x10, 0x10);
            change_sprite(spr[i], rune_x[i], 0x3D, 0x10, 0x10);
        }
        Transparency = 0;
    }
    for (i = 0; i < 3; i++) {
        if (runes[i] >= 0 && runes[i] < 0x18)
            draw_sprite(spr[i], runes[i] + 0xE8);
        else
            erase_sprite(spr[i]);
    }
    update_sprites();
}

/* Shows the icons of up to three active spells (0..0x14) in the 3D view mode. */
void far active_spells(register unsigned char *spells)
{
    static int16 spr[3] = { 0, 0, 0 };
    register int i;

    if (inplist->mode == MODE_GAME) {
        if (spr[0] == 0) {
            Transparency = 1;
            for (i = 0; i < 3; i++) {
                spr[i] = create_sprite(1, 0x10, 0x12);
                change_sprite(spr[i], spell_x[i], 0x3F, 0x10, 0x12);
            }
            Transparency = 0;
        }
        for (i = 0; i < 3; i++) {
            if (spells[i] >= 0 && spells[i] < 0x15)
                draw_sprite(spr[i], spells[i] + ICON_SPELLS);
            else
                erase_sprite(spr[i]);
        }
        update_sprites();
    }
}

/* UW1: prepares a panel turn to panel, the panel's place being x, y, w, h: the first
   time, the three EMS handles (without them the panel is redrawn in two steps); then
   the new panel and the edge picture (panel 3) into EMS, and the new panel drawn there
   with its contents. */
void far init_panelflip(int panel, int x, int y, int w, int h)
{
    static char inited = 0;
    char ok = 1;
    int old;
    register int i;

    flip_left = x;
    flip_top = y;
    pnl_wide = w;
    flip_high = h;
    if (!inited) {
        for (i = 0; i < 3; i++)
            if (flip_handle[i] == 0)
                ok &= seg012_141(&flip_handle[i]);
        if (!ok)
            free_panelflip();
        else
            flip_flags |= 1;
        inited = 1;
    }
    if (flip_flags & 1) {
        ok &= (char)read_gr_far("panels", panel, seg012_15E(flip_handle[1]));
        ok &= (char)read_gr_far("panels", PANEL_EDGE, (unsigned char far *)seg012_15E(flip_handle[0]) + 0x2800);
        if (!ok)
            pfatal_code(ERR_READ | 0x00E);
        mouse_hide();
        grSoftPageFlip();
        show(0xEC, 0xC0, seg012_15E(flip_handle[1]), 0x72, 0x53, 0, 0);
        old = RightPanel;
        RightPanel = panel;
        inv_refresh = 0;
        panel_dispatch[panel]();
        RightPanel = old;
        inv_refresh = 1;
        grab(seg012_15E(flip_handle[1]), 0xEC, 0xC0, 0x53, 0x72);
        grSoftPageFlip();
        mouse_show();
    }
    flip_page = panel;
}

/* Redraws the right-hand panel in full. */
void far pretty_panelagain(void)
{
    if (read_gr_far("panels", RightPanel, stdat)) {
        mouse_hide();
        grSoftPageFlip();
        show(0xEC, 0xC0, stdat, 0x72, 0x53, 0, 0);
        panel_dispatch[RightPanel]();
        grSoftPageFlip();
        set_the_color(0x106);
        rectangle(0xEC, 0xC0, 0x13E, 0x4F);
        mouse_show();
    }
}

void far free_panelflip(void)
{
    register int i;

    for (i = 0; i < 3; i++)
        if (flip_handle[i] != 0) {
            seg012_1B1(flip_handle[i]);
            flip_handle[i] = 0;
        }
}

/* One frame of the panel turn. Without the EMS copies: the edge at frame 3, the new
   panel at 6, done at 8. With them: the old panel squeezed narrower over frames 1 to 3,
   the edge at 4, the new panel widening over 5 to 7, and in place at 8. Returns 1 when
   done. */
char far do_panel_frame(void)
{
    unsigned char far *src;
    unsigned char far *dst;
    char old;

    pbuf.frame++;
    if (!(flip_flags & 1)) {
        switch (pbuf.frame) {
        case 3:
            src = stdat;
            read_gr_far("panels", PANEL_EDGE, src);
            mouse_hide();
            set_the_color(0xF1);
            rectangle(0xEC, 0xC0, 0x13F, 0x4E);
            pic_to_screen(ICON_CHAINS + 0xC, 0x110, 0xC4, 1, 1);
            pic_to_screen(ICON_CHAINS + 0x4, 0x110, 0x4E, 1, 1);
            show(0x114, 0xC3, src, 0x78, 3, 0, 0);
            mouse_show();
            break;
        case 6:
            src = stdat;
            read_gr_far("panels", flip_page, src);
            mouse_hide();
            set_the_color(0xF1);
            rectangle(0x114, 0xC3, 0x117, 0x4B);
            old = RightPanel;
            RightPanel = flip_page;
            grSoftPageFlip();
            show(0xEC, 0xC0, src, 0x72, 0x53, 0, 0);
            panel_dispatch[flip_page]();
            grSoftPageFlip();
            RightPanel = old;
            pic_to_screen(ICON_CHAINS + 0x8, 0x110, 0xC4, 1, 1);
            pic_to_screen(ICON_CHAINS, 0x110, 0x4E, 1, 1);
            set_the_color(0x106);
            rectangle(0xEC, 0xC0, 0x13E, 0x4F);
            mouse_show();
            break;
        case 8:
            pbuf.frame = 0;
            break;
        }
        return pbuf.frame == 0;
    } else {
        mouse_hide();
        dst = seg012_15E(flip_handle[2]);
        switch (pbuf.frame) {
        case 1:
            src = seg012_15E(flip_handle[0]);
            grab(src, flip_left, flip_top, pnl_wide, flip_high);
            flip_scale(src, dst, pbuf.frame);
            set_the_color(0xF1);
            rectangle(flip_left, flip_top + (flip_ch - flip_high) / 2,
                      flip_left + (pnl_wide - flip_width_now) / 2, flip_top - (flip_ch + flip_high) / 2);
            rectangle(flip_left + (pnl_wide + flip_width_now) / 2, flip_top + (flip_ch - flip_high) / 2,
                      flip_left + pnl_wide, flip_top - (flip_ch + flip_high) / 2);
            show(flip_left + (pnl_wide - flip_width_now) / 2, flip_top + (flip_ch - flip_high) / 2, dst,
                 flip_ch, flip_width_now, 0, 0);
            break;
        case 2:
        case 3:
            src = seg012_15E(flip_handle[0]);
            flip_scale(src, dst, pbuf.frame);
            set_the_color(0xF1);
            rectangle(flip_left, flip_top + (flip_ch - flip_high) / 2,
                      flip_left + (pnl_wide - flip_width_now) / 2, flip_top - (flip_ch + flip_high) / 2);
            rectangle(flip_left + (pnl_wide + flip_width_now) / 2, flip_top + (flip_ch - flip_high) / 2,
                      flip_left + pnl_wide, flip_top - (flip_ch + flip_high) / 2);
            show(flip_left + (pnl_wide - flip_width_now) / 2, flip_top + (flip_ch - flip_high) / 2, dst,
                 flip_ch, flip_width_now, 0, 0);
            break;
        case 4:
            src = (unsigned char far *)seg012_15E(flip_handle[0]) + 0x2800;
            set_the_color(0xF1);
            rectangle(flip_left + (pnl_wide - flip_width_now) / 2, flip_top + (flip_ch - flip_high) / 2,
                      flip_left + (pnl_wide + flip_width_now) / 2, flip_top - (flip_ch + flip_high) / 2);
            show(flip_left + (pnl_wide - 3) / 2, flip_top + (0x78 - flip_high) / 2, src, 0x78, 3, 0, 0);
            break;
        case 5:
        case 6:
        case 7:
            src = seg012_15E(flip_handle[1]);
            set_the_color(0xF1);
            rectangle(flip_left, flip_top + (flip_ch - flip_high) / 2, flip_left + pnl_wide, flip_top);
            rectangle(flip_left, flip_top - flip_high, flip_left + pnl_wide, flip_top - (flip_ch + flip_high) / 2);
            flip_scale(src, dst, pbuf.frame);
            show(flip_left + (pnl_wide - flip_width_now) / 2, flip_top + (flip_ch - flip_high) / 2, dst,
                 flip_ch, flip_width_now, 0, 0);
            break;
        case 8:
            src = seg012_15E(flip_handle[1]);
            set_the_color(0xF1);
            rectangle(flip_left, flip_top + (flip_ch - flip_high) / 2, flip_left + pnl_wide, flip_top);
            rectangle(flip_left, flip_top - flip_high, flip_left + pnl_wide, flip_top - (flip_ch + flip_high) / 2);
            show(flip_left, flip_top, src, flip_high, pnl_wide, 0, 0);
            pbuf.frame = 0;
            break;
        }
        pic_to_screen(pbuf.frame + (ICON_CHAINS + 0x8), 0x110, 0xC4, 1, 1);
        pic_to_screen(pbuf.frame + ICON_CHAINS, 0x110, 0x4E, 1, 1);
        mouse_show();
        return pbuf.frame == 0;
    }
}

/* UW1: draws the panel picture src squeezed into dst for turn step frame: its width to
   flip_wpct[frame] percent (flip_width_now) and its height to flip_hpct[frame], skewed so
   that the nearer edge grows (flip_ch, pnl_skew), one column at a time. */
void far flip_scale(unsigned char far *src, unsigned char far *dst, int frame)
{
    int runs;
    int per;
    int tall;
    int rest;
    int step;
    int d1;
    int k;
    int d2;
    register int err;
    register int j;

    flip_width_now = pnl_wide * flip_wpct[frame] / 100;
    tall = flip_high * flip_hpct[frame] / 100;
    step = 100 / flip_wpct[frame];
    if (step == 1) {
        runs = pnl_wide - flip_width_now;
        per = pnl_wide / (runs + 1) - 1;
    } else {
        runs = flip_width_now;
        per = 1;
    }
    if (frame < 4) {
        flip_ch = tall;
        d1 = (flip_high - tall) << 1;
        d2 = (flip_high - tall + flip_width_now) << 1;
        err = ((flip_high - tall) << 1) + flip_width_now;
        pnl_skew = 0;
        for (k = 0; k < runs; k++) {
            for (j = 0; j < per; j++) {
                if (err > 0)
                    err += d1;
                else {
                    pnl_skew++;
                    flip_ch -= 2;
                    err += d2;
                }
                flip_column(src++, dst++);
            }
            src += step;
        }
    } else {
        flip_ch = (flip_high << 1) - tall;
        pnl_skew = tall - flip_high;
        d1 = (tall - flip_high) << 1;
        d2 = (tall - flip_high - flip_width_now) << 1;
        err = ((tall - flip_high) << 1) - flip_width_now;
        for (k = 0; k < runs; k++) {
            for (j = 0; j < per; j++) {
                if (err < 0)
                    err += d1;
                else {
                    pnl_skew--;
                    flip_ch += 2;
                    err += d2;
                }
                flip_column(src++, dst++);
            }
            src += step;
        }
    }
    rest = flip_width_now - runs * per;
    while (rest-- > 0)
        flip_column(src++, dst++);
    flip_ch = tall;
}

/* UW1: copies one column of the panel picture (flip_high tall, rows pnl_wide apart) into dst
   (rows flip_width_now apart) at height flip_ch, pnl_skew blank pixels above and below,
   dropping or doubling rows evenly. */
void far flip_column(unsigned char far *src, unsigned char far *dst)
{
    int diff;
    int rest;
    register int i;
    register int j;

    diff = flip_ch - flip_high;
    for (i = 0; i < pnl_skew; i++) {
        *dst = 0;
        dst += flip_width_now;
    }
    if (diff == 0) {
        for (i = 0; i < flip_high; i++) {
            *dst = *src;
            dst += flip_width_now;
            src += pnl_wide;
        }
        rest = 0;
    } else if (diff > 0) {
        register int n;

        n = flip_high / (diff + 1);
        for (i = 0; i < diff; i++) {
            for (j = 0; j < n; j++) {
                *dst = *src;
                dst += flip_width_now;
                src += pnl_wide;
            }
            *dst = *src;
            dst += flip_width_now;
        }
        rest = flip_high - diff * n;
    } else {
        register int n;

        n = flip_high / (diff - 1) + 1;
        for (i = 0; i > diff; i--) {
            for (j = 0; j > n; j--) {
                *dst = *src;
                dst += flip_width_now;
                src += pnl_wide;
            }
            src += pnl_wide;
        }
        rest = flip_ch - diff * n - 1;
    }
    while (rest-- > 0) {
        *dst = *src;
        dst += flip_width_now;
        src += pnl_wide;
    }
    for (i = 0; i < pnl_skew; i++) {
        *dst = 0;
        dst += flip_width_now;
    }
    *dst = 0;
}

char ShowStupidFirstPersonWeapon = 1;

/* Draws the first-person weapon into the frame buffer (unless sheathed or turned off),
   swaying with the player's speed, and three pictures on top. */
void far do_fbuf_bms(void)
{
    unsigned char far *p;
    register int n;
    register int jig;

    Transparency = 1;
    if (setting[SCR_WEAPON] != WEAP_SHEATHED && weap_frame < 0x1C && last_weap > -1 && ShowStupidFirstPersonWeapon) {
        jig = PN.speed ? PN.speed * 2 / 0x31F + 1 : 0;
        jiggle_weapon(jig);
        if (setting[SCR_WEAPON] == WEAP_DRAWING || setting[SCR_WEAPON] == WEAP_SHEATHING)
            n = weap_frame + 0x12;
        else if (weap_frame < 0 || setting[SCR_WEAPON] == WEAP_READY)
            n = 0x1C - wfo;
        else {
            n = setting[8] * 9 + weap_frame;
            wxo = 0;
        }
        seg012_10F(2, seg051_C377);
        wbuf = (unsigned char far *)MK_FP(ems_frame, 0x8000) + weap_offs[n];
        obj_inpage1 = 0xFF;
        p = grs_unpack((struct Bitmap far *)wbuf);
        fbshow(p, weap_x[n] + wxo, weap_y[n], wbuf[1], wbuf[2]);
    }
    pic_to_fbuf(ICON_3DWIN, 0x3E, 3);
    pic_to_fbuf(ICON_3DWIN + 0x1, 0, 0xD);
    pic_to_fbuf(ICON_3DWIN + 0x2, 0xAB, 0xD);
    Transparency = 0;
}

/* Copies the 3D frame buffer to the screen, after drawing the weapon in demo mode. */
void far send_FB(void)
{
    if (demo_mode)
        do_fbuf_bms();
    cFBtoScreen();
}

/* Looking at a shaft or a gravestone (LOOK.C): UW1 records the level, or the grave, in
   cutscene value 0x100 or 0x101. */
void far player_look_shaft(void)
{
    value_cuts(0x100, PlayerLevel);
}

/* Looking at gravestone grave (nonzero) records it in cutscene value 0x101. */
void far player_look_grave(int grave)
{
    if (grave)
        value_cuts(0x101, grave);
}

#ifndef __TURBOC__
/* Native co-op switches the persistent weapon animation and panel clocks.
   last_weap describes the art actually loaded, so it remains a global cache. */
void port_shared_panel_state(int which,int save)
{
    static struct {
        unsigned char setting[9],goal[9],panel;
        int16 slow,fast,now,frame,xo,fo;
        char weapon,state;
    } seats[2];
    static int initialized;
    if(which<0){memset(&seats[1],0,sizeof seats[1]);seats[1].weapon=-1;seats[1].frame=-1;seats[1].fo=1;
        seats[1].setting[SCR_WEAPON]=seats[1].goal[SCR_WEAPON]=seats[1].state=WEAP_SHEATHED;return;}
#define FIELD(member,global) do{if(save)seats[which].member=global;else global=seats[which].member;}while(0)
    if(!initialized){seats[1].weapon=-1;seats[1].frame=-1;seats[1].fo=1;initialized=1;}
    if(save){memcpy(seats[which].setting,setting,9);memcpy(seats[which].goal,goal,9);}
    else{memcpy(setting,seats[which].setting,9);memcpy(goal,seats[which].goal,9);}
    FIELD(slow,slow_adjust);FIELD(fast,fast_adjust);FIELD(now,now_adjust);
    FIELD(frame,weap_frame);FIELD(xo,wxo);FIELD(fo,wfo);FIELD(weapon,weapid);FIELD(state,wlstate);FIELD(panel,RightPanel);
    if(!save) {
        static int hand=-1;
        if(hand!=player->lefty){last_weap=-1;hand=player->lefty;}
        /* Keep the global art cache in this character's weapon and hand before
           adjust_weapon compares it, preserving the independent swing frame. */
        do_weapload();
    }
#undef FIELD
}
#endif
