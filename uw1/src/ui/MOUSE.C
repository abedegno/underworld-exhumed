/* target: seg013_1CC9 */
/* opts: -mm -1 -G -O -Y -d */
/* The mouse: the cursor, its save-under, the input queue, the mouse regions that pick the
   cursor shape, keyboard warping of the pointer, and the keyboard reader the input loop
   shares with it. The whole of UW1's DOS resident segment seg013_1CC9, in original order.
   UW1 has no symbol-bearing build; the names are UW2's (the FM Towns symbol table; UW2's
   MOUSE.C, seg015_1D7C), the code being the same routines.

   Coordinates: x runs 0 to 319 left to right and y 0 to 199 from the bottom of the screen
   up (moveMouse subtracts the driver's downward motion; the keypad 8 key warps to y 199).

   Main entry points: get_input (through mouse_get_input and mouse_get_input_sp) is the
   one place every input loop reads an event: it alternates between the mouse and the
   keyboard and returns 1 to 3 for the buttons held (1 left, 2 right), a key code
   (do_keyboard_input, with KEY_CTRL, KEY_ALT and KEY_SHIFT added, ui.h) or -1. moveMouse
   polls the driver and moves and redraws the cursor; it is called from every wait loop.
   mouse_release and mouse_dragged wait for the buttons to come up. defineMouseRegion and
   force_mouse_cursor choose the cursor picture; keyboard_mouse moves the pointer from the
   keypad.

   Data owned: the pointer position and cursor picture, the save-under bookkeeping, a
   one-deep queue of a button press seen while the game was busy (MousQUp records it from
   the 3D renderer and do_mouse_input replays it), up to 20 cursor regions, the
   force_mouse_cursor stack (three deep), the keyboard warp state and the 3D view's
   rectangle (m3dx ... m3dt) used to draw the cursor into the frame buffer.

   UW1's differences, marked "UW1:" where they are not obvious: no joystick and no
   left-handed button swap (so no joymovecur, fauxright or mouse_hand); when the cursor
   straddles the 3D view's edge outside screen mode 1 (inplist->mode), mous_3d_hide hides
   it on the whole screen and mous_3d_show (empty in UW2) shows it again, and
   MousReSave3d saves the background into the frame buffer in mode 1 only; Tab and
   Shift+Tab move the pointer between three places on the row y 130 (x 20, 130 and 280).

   Name: inferred (the mouse: init_mouse, mouse_getxy, mouse_putxy, mouse_constrain;
   System Shock's input library MOUSE.C has the same calls, mouse_init, mouse_get_xy,
   mouse_put_xy, mouse_constrain_xy). */

/* name: function names are UW2's, the FM Towns originals in one-for-one order there,
   apart from flush_keys, UW2's own name for a function FM Towns does not have. The target
   table lists some by IDA name; they are named from that order: mous_3d_set (IDA _F8),
   mous_3d_hide (_13A), mous_3d_show (_254), mouse_clearQ (_2F8), flush_keys (_30A),
   mouse_constrain (_46D), mouse_get_input (_651), moveMouse (_93C), drawMouse (_CF2),
   keyboard_mouse (_E29) and mouse_btns (_F8D). */

#include <stdlib.h>
#include <ctype.h>
#include "gfx.h"
#include "player.h"
#include "sys.h"
#include "ui.h"

/* Initialised data, DS:10E..132, in definition order. FM Towns (UW2) keeps the statics
   after _current_buttongroup (+4 onwards) and its publics in its small-data group, in this
   same order. UW1: no joymovecur, fauxright or mouse_hand. */
static int16 mouse_x = 100;             /* DS:10E */
static int16 mouse_y = 100;             /* DS:110 */
static int16 mouse_shown = 0;           /* DS:112, mouse_show/mouse_hide nesting count */
static unsigned char mouse_saved = 0;   /* DS:114, the background under the cursor is saved */
static int16 q_button = -1;             /* DS:115, the queued button press, -1 for none */
static int16 q_x = 0;                   /* DS:117, where it was pressed */
static int16 q_y = 0;                   /* DS:119 */
static int16 q_release = 0;             /* DS:11B */
static unsigned char q_taken = 0;       /* DS:11D, the last input came from the queue */
static int16 last_button = 0;           /* DS:11E */
static int16 in_x0 = -1;                /* DS:120, the region the pointer is in, -1 none */
static int16 num_regions = 0;           /* DS:122 */
static char force_depth = 0;            /* DS:124, force_mouse_cursor nesting */
static int16 warp_x = -1;               /* DS:125, keyboard warp target, -1 for none */
unsigned char calledfrom3d = 0;         /* DS:127, FM Towns _calledfrom3d */
static int16 key_index = 0;             /* DS:128, where the key array scan resumes */
static uint32 warp_time = 0;            /* DS:12A */
static uint32 key_time = 0;             /* DS:12E */
static char mouse_first = 1;            /* DS:132, get_input alternates mouse and keys */

/* Uninitialised data, DS:24CE..25C9. */
/* match: Turbo C lays _BSS out by a hash of the names, ties in definition order. Measured
   from probe compiles, the bucket is (c[0] + 256*c[1] + 8*c[len-2] + 64*len) & 1023 and
   buckets go out in ascending order. The five m3d publics are the FM Towns names and share
   one bucket (909). The statics are FM Towns _mcurhndl+0x10 to +0xDA, which has no names
   for them, so these names are ours, chosen to land where the EXE has them (bucket in
   brackets). */
static int16 m_curs;                    /* DS:24CE (125), the cursor set_mouse_data chose */
static int16 rgn_ylo[20];               /* DS:24D0 (146), the mouse regions */
static int16 hotspot_x;                 /* DS:24F8 (160), the cursor's hot spot */
static int16 hotspot_y;                 /* DS:24FA (160) */
static int16 rgn_left[20];              /* DS:24FC (162), 10000 marks a free region */
static int16 con_x0;                    /* DS:2524 (163), mouse_constrain's box */
static int16 con_y0;                    /* DS:2526 (171) */
static int16 rgn_x1[20];                /* DS:2528 (178) */
static int16 m_warp_dx;                 /* DS:2550 (205), keyboard warp steps */
static int16 m_warp_dy;                 /* DS:2552 (205) */
static int16 rgn_ytop[20];              /* DS:2554 (234) */
static int16 cur_h;                     /* DS:257C (411), the cursor's size */
static int16 max_x;                     /* DS:257E (421) */
static int16 max_y;                     /* DS:2580 (421) */
static int16 m_cursor_pic;              /* DS:2582 (437), the cursor being drawn */
static int16 m_warp_rate;               /* DS:2584 (461) */
static int16 curs_w;                    /* DS:2586 (475) */
static int16 warp_y;                    /* DS:2588 (495) */
static int16 warp_key;                  /* DS:258A (671), the key driving the warp */
static int16 hit_y0;                    /* DS:258C (688), the bounds of the region in_x0 is */
static int16 reg_icon[20];              /* DS:258E (746), each region's cursor */
static int16 curs_stack[3];             /* DS:25B6 (763), force_mouse_cursor's saved cursors */
static int16 in_x1;                     /* DS:25BC (873) */
static int16 in_y1;                     /* DS:25BE (881) */
int16 m3dx;                             /* DS:25C0, the 3D view, from mous_3d_set */
int16 m3dy;                             /* DS:25C2 */
int16 m3dw;                             /* DS:25C4 */
int16 m3dh;                             /* DS:25C6 */
int16 m3dt;                             /* DS:25C8, cursor against it: 0 outside, 1 across, 2 inside */

/* The asm graphics module's pointer table (gfx.h; UW2's notes there): Color_data_ptr's
   words 0x100 and 0x101 are the save-under buffer. UW1's window-edge pointers are at
   DS:23EC (wtop), 23F0 (wbot), 23F4 (wright) and 23F8 (wleft), in UW2's order (21DC,
   21E0, 21E4, 21E8), and are read in UW2's order (wleft, wtop, wright, wbot) in moveMouse,
   mous_3d_hide and mous_3d_show. */

/* Sets the full-screen bounds and the default cursor (picture 0x106C, the arrow used
   whenever no region or forced cursor applies), allocates the 40 by 40 save-under buffer
   and clears the region table. Returns -1 if the buffer cannot be had. */
int far init_mouse(void)
{
    int i;

    con_x0 = con_y0 = 0;
    max_x = 0x13F;
    max_y = 0xC7;
    set_mouse_data(ICON_CURSORS);
    if ((Color_data_ptr[0x100] = valloc(0x28, 0x28)) == 0)
        return -1;
    Color_data_ptr[0x101] = Color_data_ptr[0x100];
    for (i = 0; i < 20; i++)
        rgn_left[i] = 10000;
    return 0;
}

/* Puts back the background saved under the cursor (colour 0x101 selects the save-under
   buffer as the source of the graphics library's rectangle), so the cursor disappears.
   Returns whether anything was saved. */
unsigned char far _actual_mhide(void)
{
    if (mouse_saved) {
        set_the_color(0x101);
        rectangle(mouse_x - hotspot_x, mouse_y + hotspot_y + 1,
                  mouse_x - hotspot_x + curs_w - 1, mouse_y + hotspot_y - cur_h + 1);
    }
    return mouse_saved;
}

/* mouse_show and mouse_hide nest: the cursor is drawn on the first show and removed on
   the matching last hide. */
void far mouse_show(void)
{
    if (++mouse_shown == 1)
        drawMouse();
}

/* Hides the cursor when the last of the nested mouse_show calls is undone: the cursor
   picture is taken off and the saved background put back (_actual_mhide); set_the_color(1)
   then leaves an ordinary pen selected. */
void far mouse_hide(void)
{
    if (--mouse_shown == 0) {
        if (_actual_mhide()) {
            mouse_saved = 0;
            set_the_color(1);
        }
    }
}

void far mous_3d_set(int x, int y, int w, int h)
{
    m3dx = x;
    m3dy = y;
    m3dw = w;
    m3dh = h;
}

/* Whether the cursor touches the 3D view's rectangle (m3dx, m3dy is its bottom left). */
char far mous_in_3d_p(void)
{
    return mouse_check_reg(m3dx, m3dy - m3dh, m3dx + m3dw, m3dy);
}

/* Called by the 3D renderer (VIEW3D.C's send_db) before the frame buffer goes to the
   screen. Sets m3dt to 0 if the cursor is outside the 3D view, 2 if wholly inside, 1 if
   across its edge, and when it overlaps draws the cursor into the frame buffer
   (draw3dMouse) so the copy to the screen carries it; a nested show count is reduced
   instead. UW1: across the edge outside mode 1, the cursor is hidden on the whole screen
   instead, and mous_3d_show, the matching call after the copy, shows it again. */
void far mous_3d_hide(void)
{
    int x0, x1;
    int16 y1, y0;
    int wl, wt, wr, wb;

    x0 = mouse_x - curs_w + hotspot_x;
    x1 = mouse_x + curs_w - hotspot_x;
    y1 = mouse_y + cur_h - hotspot_y;
    y0 = mouse_y - cur_h + hotspot_y;
    if (m3dx + m3dw < x0 || x1 < m3dx || m3dy - m3dh > y1 || y0 > m3dy)
        m3dt = 0;
    else {
        if (x0 > m3dx && m3dx + m3dw > x1 && y1 < m3dy && m3dy - m3dh < y0)
            m3dt = 2;
        else {
            m3dt = 1;
            if (inplist->mode != MODE_GAME) {   /* UW1: outside mode 1 the cursor is hidden instead */
                wl = *wleft;
                wt = *wtop;
                wr = *wright;
                wb = *wbot;
                set_the_window(0, 0xC7, 0x13F, 0);
                mouse_hide();
                set_the_window(wl, wt, wr, wb);
            }
        }
        if (mouse_shown == 1)
            draw3dMouse();
        else if (mouse_shown > 1)
            mouse_shown--;
    }
}

/* UW1: shows the cursor again after mous_3d_hide hid it outside screen mode 1, with the
   clip window opened to the whole screen for the moment (UW2's is empty). */
void far mous_3d_show(void)
{
    int wl, wt, wr, wb;

    if (m3dt == 1 && inplist->mode != MODE_GAME) {
        wl = *wleft;
        wt = *wtop;
        wr = *wright;
        wb = *wbot;
        set_the_window(0, 0xC7, 0x13F, 0);
        mouse_show();
        set_the_window(wl, wt, wr, wb);
    }
}

void far mouse_getxy(int16 *x, int16 *y)
{
    *x = mouse_x;
    *y = mouse_y;
}

/* Where the last button event happened: the queued position if that event came from the
   queue (q_taken), else the pointer now. */
void far mouse_Qgetxy(int16 *x, int16 *y)
{
    if (q_taken) {
        *x = q_x;
        *y = q_y;
    } else {
        *x = mouse_x;
        *y = mouse_y;
    }
}

void far mouse_clearQ(void)
{
    if (q_button != -1)
        q_button = -1;
}

/* Not in FM Towns and never called: empties the keyboard buffer.
   name: UW2's (provisional there, where it is static); UW1's target table has it as a
   function of its own, so it is public here. */
void far flush_keys(void)
{
    while (KEY())
        ;
}

void far mouse_putxy(int x, int y)
{
    mouse_hide();
    checkMouse();
    if (*MouseOn)
        MOUSE();
    mouse_x = x;
    mouse_y = y;
    mouse_show();
}

/* The buttons held now, also into *b; no button cancels a queued press (q_button). */
int far mouse_getbut(int16 *b)
{
    if ((*b = mouse_btns()) == 0)
        q_button = -1;
    last_button = *b;
    return *b;
}

/* Waits until the button that was last seen down is released, keeping the pointer and
   the keyboard warp alive, and with how set also running do_changes so the game goes on
   updating (MAINLOOP.C). If MousQUp saw the button come up meanwhile (q_release cleared)
   the wait ends at once; a press still held at the end is queued for do_mouse_input. */
void far mouse_release(char how)
{
    int want;
    int b;

    q_release = -1;
    want = last_button == 1 ? 1 : 2;
    while ((mouse_get_input_sp() & (want | 0xFFFC)) == want && q_release == -1) {
        if (how)
            do_changes();
        keyboard_mouse(do_keyboard_input(1));
        moveMouse();
    }
    if (q_release == -1) {
        b = mouse_btns();
        if (b) {
            q_button = b;
            q_x = mouse_x;
            q_y = mouse_y;
        }
    }
}

/* While a button stays down, returns 1 as soon as the pointer has moved more than six
   pixels (x plus y distance) from where it was, 0 if the button is released first. A
   look click on an object that turns into a drag picks the object up (INTERACT.C's
   player_3dlook). */
unsigned char far mouse_dragged(char how)
{
    int16 x0, y0;
    int16 b;
    int16 x1, y1;
    char moved;                         /* UW1: signed (cbw) */

    moved = 0;
    mouse_getxy(&x0, &y0);
    while (mouse_getbut(&b) && !moved) {
        if (how)
            do_changes();
        keyboard_mouse(do_keyboard_input(1));
        moveMouse();
        mouse_getxy(&x1, &y1);
        if (abs(x1 - x0) + abs(y1 - y0) > 6)
            moved = 1;
    }
    return moved;
}

void far mouse_constrain(int x0, int y0, int x1, int y1)
{
    con_x0 = x0;
    con_y0 = y0;
    max_x = x1;
    max_y = y1;
}

/* Lets the pointer go anywhere on the 320 by 200 screen again (see mouse_constrain). */
void far mouse_freereign(void)
{
    con_x0 = con_y0 = 0;
    max_x = 0x13F;
    max_y = 0xC7;
}

/* Moves the pointer and returns the buttons held, or -1 for none. A press queued by
   MousQUp or mouse_release is returned once when the buttons are now up, so a quick click
   during a long frame is not lost. */
int far do_mouse_input(void)
{
    int b = 0;

    moveMouse();
    if (q_button == -1) {
        q_taken = 0;
        b = mouse_btns();
    } else {
        if ((b = mouse_btns()) == 0) {
            b = q_button;
            q_taken = 1;
        }
        q_button = -1;
    }
    last_button = b;
    if (b == 0)
        return -1;
    return b;
}

/* Key repeat from the held-key array: no more often than every 30 ticks of *Time since
   the last key, scans key_on (indexed by scan code) round from just after the last key
   found, and returns the character Asc gives for the first key held (the shifted table
   half when Shift is down), or 0. Used when the caller asks for held keys (array set). */
int far do_keyarray_input(void)
{
    int c;
    int start;

    if (GAME_TIME() - key_time < 30)
        return 0;
    c = 0;
    start = key_index = (key_index + 1) & 0x7F;
    do {
        if (key_on[key_index]) {
            if (*Shift)
                key_index |= 0x80;
            c = Asc[key_index];
            if (c)
                break;
        }
        key_index = (key_index + 1) & 0x7F;
    } while (key_index != start);
    return c;
}

/* Reads one key (or with array set, a held key from do_keyarray_input instead) and
   returns its input code, -1 for none: the low byte is the character, or 0x80 and up for
   the special keys (the keypad keys are 0x8C to 0x94, Tab is 9); Shift adds KEY_SHIFT to
   special keys only, Caps Lock flips the case of letters, and Alt and Ctrl add KEY_ALT
   and KEY_CTRL. So Ctrl+S is 0x173 (ICONS.C's do_option_shortcut). */
#ifndef __TURBOC__
static char port_lshift_was;
#endif
int far do_keyboard_input(char array)
{
    int c;

#ifndef __TURBOC__
    {
        /* port only: --enhance modern-keys binds a lone left Shift (UltimaHacks' jump). It has no
           character, so KEY never gives it; a new press is seen in key_on (0x2A, its scan code),
           which replays restore, and given as PORT_KEY_LSHIFT with the modifiers held */
        char now = key_on[0x2A] != 0;
        char pressed = now && !port_lshift_was;
        port_lshift_was = now;
        if (pressed && ENHANCED(ENH_MODERN_KEYS)) {
            key_time = GAME_TIME();
            c = PORT_KEY_LSHIFT | KEY_SHIFT;
            if (*Alt)
                c |= KEY_ALT;
            if (*Ctrl)
                c |= KEY_CTRL;
            return c;
        }
    }
#endif
    c = KEY();
    if (array)
        c = do_keyarray_input();
    c &= 0xFF;
    if (c == 0)
        return -1;
    key_time = GAME_TIME();
    if (c & 0x80) {
        if (*Shift)
            c |= KEY_SHIFT;
    } else if (*CapsLock && isalpha(c))
        c = !*Shift ? c - 0x20 : c + 0x20;
    if (*Alt)
        c |= KEY_ALT;
    if (*Ctrl)
        c |= KEY_CTRL;
    return c;
}

/* The input reader under every loop: tries the mouse and the keyboard in turn, starting
   with whichever was not tried first last time, so neither can starve the other. Returns
   1 to 3 for mouse buttons, a key code above 3, or -1. Sets didMouseInput (SCROLLIO.C). */
int far get_input(char array)
{
    int c;

#ifndef __TURBOC__
    if ((c = port_shared_click(&q_x, &q_y)) > 0) {
        q_taken = 1; didMouseInput = 1; return c;
    }
#endif

    didMouseInput = 1;
    if (mouse_first) {
        mouse_first = 0;
        if ((c = do_mouse_input()) < 0)
            return do_keyboard_input(array);
        return c;               /* match: the dead jump the EXE has before the else */
    } else {
        mouse_first = 1;
        if ((c = do_keyboard_input(array)) < 0)
            return do_mouse_input();
    }
    return c;
}

/* The next input event (get_input, see the file comment); mouse_get_input_sp is the same
   with get_input's flag set, the form INPUT.C's dispatcher uses. */
int far mouse_get_input(void)
{
    return get_input(0);
}

int far mouse_get_input_sp(void)
{
    return get_input(1);
}

/* Claims a free slot of the 20 cursor regions: while the pointer is inside the box the
   cursor is picture id (checkMouse). Returns the slot, the handle for undefineMouseRegion,
   or -1 if all are in use. */
int far defineMouseRegion(int x0, int y0, int x1, int y1, int id)
{
    int i;

    for (i = 0; i < 20; i++)
        if (rgn_left[i] == 10000)
            break;
    if (i == 20)
        return -1;
    rgn_left[i] = x0;
    rgn_x1[i] = x1;
    rgn_ytop[i] = y1;
    rgn_ylo[i] = y0;
    reg_icon[i] = id;
    if (i >= num_regions)
        num_regions = i + 1;
    checkMouse();
    return i;
}

/* Frees cursor region handle: its left edge becomes 10000, which no pointer reaches, and
   the count shrinks past free slots at the end. */
void far undefineMouseRegion(int handle)
{
    int i;

    if (handle < num_regions) {
        rgn_left[handle] = 10000;
        if (reg_icon[handle] == m_cursor_pic)
            in_x0 = 10000;
        if (num_regions - 1 == handle) {
            for (i = num_regions - 2; i >= 0; i--)
                if (rgn_left[i] != 10000)
                    break;
            num_regions = i + 1;
        }
        checkMouse();
    }
}

/* Overrides the cursor picture regardless of regions, saving the old one on a stack up to
   three deep (a fourth force is ignored). Used for the mode cursors (INTERACT.C's
   deal_with_icons, 0x1077), the automap's pen and eraser and an object held on the
   cursor. */
void far force_mouse_cursor(int id)
{
    if (force_depth != 3) {
        mouse_hide();
        curs_stack[force_depth] = m_curs;
        force_depth++;
        set_mouse_data(id);
        mouse_show();
    }
}

/* Pops force_mouse_cursor's stack, back to the arrow if it underflows. Bit 0 of how hides
   the cursor first and bit 1 shows it after, so 3 does both. */
void far unforce_mouse_cursor(int how)
{
    if (how & 1)
        mouse_hide();
    if (--force_depth < 0) {
        curs_stack[0] = ICON_CURSORS;
        force_depth = 0;
    }
    set_mouse_data(curs_stack[force_depth]);
    checkMouse();
    if (how & 2)
        mouse_show();
}

/* Whether the cursor touches the box: the pointer may lie outside it by up to half the
   cursor's width and height. Used to decide whether drawing in an area needs the mouse
   hidden (SCROLLIO.C's set_mouse_in) and whether the pointer is over the 3D view. */
char far mouse_check_reg(int x0, int y0, int x1, int y1)
{
    int16 dy, dx;

    dy = (cur_h + 1) >> 1;
    dx = (curs_w + 1) >> 1;
    return y0 - dy <= mouse_y && y1 + dy >= mouse_y &&
           x0 - dx <= mouse_x && x1 + dx >= mouse_x;
}

/* Makes picture id the cursor: its width and height come from the picture's header and
   the hot spot is its middle. */
void far set_mouse_data(int id)
{
    struct Bitmap far *p;

    _actual_mhide();
    p = seg009_1(grs_which1(id));
    curs_w = p->width;
    cur_h = p->height;
    hotspot_x = curs_w / 2 - 1;
    hotspot_y = cur_h / 2;
    m_cursor_pic = id;
    m_curs = id;
    if (mouse_saved)
        MousReSave();
}

/* Picks the cursor picture from the region under the pointer: nothing to do while a
   cursor is forced or the pointer is still in the region found last time; otherwise the
   first region containing it (in slot order) sets its picture, and leaving every region
   restores the arrow (0x106C). */
void far checkMouse(void)
{
    int i;

    if (force_depth > 0)
        return;
    if (in_x0 != -1 && mouse_x >= in_x0 && mouse_x <= in_x1 &&
        mouse_y >= hit_y0 && mouse_y <= in_y1)
        return;
    for (i = 0; i < num_regions; i++) {
        if (rgn_left[i] <= mouse_x && rgn_x1[i] >= mouse_x &&
            rgn_ylo[i] <= mouse_y && rgn_ytop[i] >= mouse_y) {
            in_x0 = rgn_left[i];
            in_x1 = rgn_x1[i];
            hit_y0 = rgn_ylo[i];
            in_y1 = rgn_ytop[i];
            set_mouse_data(reg_icon[i]);
            break;
        }
    }
    if (in_x0 != -1 && i == num_regions) {
        in_x0 = -1;
        set_mouse_data(ICON_CURSORS);
    }
}

#ifndef __TURBOC__
/* Port only: --enhance mouse-look (docs/ENHANCEMENTS.md), UltimaHacks' setMouseLookState and
   mouseLookOrMoveCursor in our names (John Glassmyer, MIT). While it is on, the pointer stays at
   the 3D view's centre as a crosshair, where clicks act, and the mouse's motion turns the view:
   across the heading, 64 units a pixel, and up and down the pitch, 128 a pixel, within the
   pitch's bound (PLAYER.C's port_pitch_bound, wide-pitch's or the original's). The mouse driver
   (src/port/sys/mousedrv.c) is told, so that it passes the motion, scaled by the settings file's
   look-speed, and the platform captures the pointer. */
#include "object.h"
void mouse_look_mode(int on);
int port_pitch_bound(void);

static char look_on;                    /* mouse-look is on */
static char look_was;                   /* it was when the 3D view gave way (port_mouse_look_screen) */
static int16 look_x, look_y;            /* the pointer before it */

int port_mouse_look(void)
{
    return look_on;
}

static void mouse_look_set(int on)
{
    if (on == look_on)
        return;
    look_on = (char)on;
    mouse_look_mode(on);
    if (on) {
        if (_actual_mhide())            /* the cursor off the screen where it was */
            mouse_saved = 0;
        look_x = mouse_x;
        look_y = mouse_y;
        mouse_x = m3dx + m3dw / 2;      /* the view's centre (m3dy its bottom edge) */
        mouse_y = m3dy - m3dh / 2;
        in_x0 = in_x1 = mouse_x;        /* a region of one point, so checkMouse keeps the cursor */
        hit_y0 = in_y1 = mouse_y;
        MousReSave();
        if (GameInputMode == GIM_NONE)  /* not while carrying something: its picture stays */
            set_mouse_data(ICON_CURSORS);
    } else {
        mouse_x = look_x;               /* the view's next frame clears the crosshair */
        mouse_y = look_y;
        MousReSave();
        in_x0 = -1;
        checkMouse();
        drawMouse();
    }
}

/* the ` key (PLAYER.C's init_player binds it with the flag on) */
void far port_mouse_look_toggle(void)
{
    mouse_look_set(!look_on);
    scroll_print(look_on ? "Mouse look enabled.\n" : "Mouse look disabled.\n");
}

/* change_screen (UWEDIT.C): off while the 3D view gives way (the map, a conversation), and back
   as it was when it returns; leaving is_view 1 for 0 remembers, 0 for 1 restores */
void port_mouse_look_screen(int leaving, int is_view)
{
    if (!is_view)
        return;
    if (leaving) {
        look_was = look_on;
        mouse_look_set(0);
    } else
        mouse_look_set(look_was);
}

static void mouse_look_step(int dx, int dy)
{
    long p;
    int bound = port_pitch_bound();

    PlayerFacing += (int16)(dx << 6);
    p = (long)PlayerPitch - (long)(ENHANCED(ENH_INVERT_LOOK) ? -dy : dy) * 128;
    PlayerPitch = (int16)(p > bound ? bound : p < -bound ? -bound : p);
    SET_HEADING(ThePlayer, PlayerFacing >> 13);     /* as PHYSICS.C keeps them */
    SET_FINEHEAD(ThePlayer, PlayerFacing >> 8);
    editchng(2);                                    /* redraw the view */
}
#endif

/* Moves the pointer by the driver's motion, or, with no motion, one step of a keyboard warp toward
   (warp_x, warp_y): every 10 ticks, up to m_warp_rate pixels per axis, the rate growing by
   8 to 40 while warping freely and halving once a held arrow key is released. Real
   motion cancels a warp. The pointer is clamped to mouse_constrain's box and the cursor
   redrawn. With calledfrom3d (MousQUp's argument; VIEW3D.C passes 0) the clip window is
   opened to the whole screen around the redraw. */
void far moveMouse(void)
{
    int wl, wt, wr, wb;
    int16 dx, dy;
    int axes, n;

    dx = 0;
    dy = 0;
    if (*MouseOn) {             /* UW1: no joystick */
        MOUSE();
        dx = *MouseDx;
        dy = *MouseDy;
    }
    if (dx == 0 && dy == 0) {
        if (warp_x < 0)
            return;
        if (warp_key) {
            if (key_on[warp_key] == 0) {
                m_warp_rate >>= 1;
                if (m_warp_rate == 0) {
                    warp_x = -1;
                    warp_key = 0;
                    return;
                }
            } else if (GAME_TIME() - warp_time < 10)
                return;
        } else {
            if (GAME_TIME() - warp_time < 10)
                return;
            m_warp_rate += 8;
            if (m_warp_rate > 40)
                m_warp_rate = 40;
        }
        warp_time = GAME_TIME();
        axes = 3;
        n = m_warp_rate;
        while (n-- && axes) {
            if (mouse_x + dx > warp_x - 5 && mouse_x + dx < warp_x + 5 && (axes & 1)) {
                axes ^= 1;
                dx = warp_x - mouse_x;
            } else if (axes & 1)
                dx += m_warp_dx;
            if (mouse_y - dy > warp_y - 5 && mouse_y - dy < warp_y + 5 && (axes & 2)) {
                axes ^= 2;
                dy = mouse_y - warp_y;
            } else if (axes & 2)
                dy += m_warp_dy;
        }
    } else
        warp_x = -1;
    if (calledfrom3d) {
        wl = *wleft;
        wt = *wtop;
        wr = *wright;
        wb = *wbot;
        set_the_window(0, 0xC7, 0x13F, 0);
    }
    if (_actual_mhide())
        mouse_saved = 0;
#ifndef __TURBOC__
    if (look_on && !(inplist->mode & 0x11)) {
        /* port only: the view gone without change_screen (real_death sets the mode itself, for
           the main menu): mouse-look off, remembered for the view's return, so the pointer moves */
        look_was = 1;
        mouse_look_set(0);
    }
    if (look_on) {                      /* port only: mouse-look turns the view instead */
        mouse_look_step(dx, dy);
        dx = dy = 0;
    }
#endif
    mouse_x += dx;
    mouse_y -= dy;
    if (mouse_x < con_x0)
        mouse_x = con_x0;
    else if (mouse_x > max_x)
        mouse_x = max_x;
    if (mouse_y < con_y0)
        mouse_y = con_y0;
    else if (mouse_y > max_y)
        mouse_y = max_y;
    if (warp_x == mouse_x && warp_y == mouse_y) {
        warp_key = 0;
        warp_x = -1;
    }
    checkMouse();
    if (mouse_shown > 0)
        drawMouse();
    if (calledfrom3d)
        set_the_window(wl, wt, wr, wb);
}

/* Called while the game is busy (VIEW3D.C, between building the vision grid and the
   frame's database) to keep the pointer moving and remember a button press for
   do_mouse_input; a release clears q_release so a pending mouse_release ends. */
void far MousQUp(char from3d)
{
    int b;

    calledfrom3d = from3d;
    moveMouse();
    calledfrom3d = 0;
    b = mouse_btns();
    if (b) {
        if (q_button == -1) {
            q_button = b;
            q_x = mouse_x;
            q_y = mouse_y;
        }
    } else
        q_release = 0;
}

/* Saves the screen under the cursor: a rectangle in pen 0x100, which probably copies to
   the save-under area in video memory rather than drawing (VIDMODE.ASM's pens; not
   traced further). */
void far MousReSave(void)
{
    set_the_color(0x100);
    rectangle(mouse_x - hotspot_x, mouse_y + hotspot_y + 1,
              mouse_x - hotspot_x + curs_w - 1, mouse_y + hotspot_y - cur_h + 1);
    mouse_saved = 1;
}

/* The same for the cursor drawn into the 3D view's frame buffer (m3dt 2), or, while it
   straddles the view in screen mode 1 (m3dt 1), a copy of the frame buffer area from
   video memory through vcopyfb. */
void far MousReSave3d(void)
{
    if (m3dt == 2) {
        fbuf_setcolor(0x100);
        rectangle(mouse_x - hotspot_x - m3dx, mouse_y + hotspot_y - m3dy + m3dh,
                  mouse_x + curs_w - hotspot_x - m3dx - 1, mouse_y - cur_h + hotspot_y - m3dy + m3dh);
    } else if (m3dt == 1 && inplist->mode == MODE_GAME) {     /* UW1: in mode 1 only */
        ShowClip = 1;
        vcopyfb(mouse_x - hotspot_x - m3dx, mouse_y + hotspot_y - m3dy + m3dh, curs_w, cur_h + 1,
                Color_data_ptr[0x100]);
        ShowClip = 0;
    } else
        return;
    mouse_saved = 1;
}

/* Draws the cursor on the screen at the pointer, transparent and clipped, after saving
   what it covers. */
void far drawMouse(void)
{
    MousReSave();
    ShowClip = Transparency = 1;
    pic_to_screen(m_cursor_pic, mouse_x - hotspot_x, mouse_y + hotspot_y, cur_h, curs_w);
    ShowClip = Transparency = 0;
    set_the_color(0);
}

/* Draws the cursor into the 3D view's frame buffer, relative to the view. */
void far draw3dMouse(void)
{
    int x, y;

    MousReSave3d();
    x = mouse_x - hotspot_x - m3dx;
    y = mouse_y + hotspot_y - m3dy + m3dh - 1;
    ShowClip = Transparency = 1;
    pic_to_fbuf(m_cursor_pic, x, y);
    ShowClip = Transparency = 0;
}

/* Starts a keyboard warp of the pointer toward (x, y), clamped to the constraint box;
   moveMouse carries it out. */
void far warpMouse(int x, int y)
{
    m_warp_rate = 1;
    if (x < con_x0)
        x = con_x0;
    else if (x > max_x)
        x = max_x;
    warp_x = x;
    if (y < con_y0)
        y = con_y0;
    else if (y > max_y)
        y = max_y;
    warp_y = y;
    m_warp_dx = x > mouse_x ? 1 : -1;
    m_warp_dy = y > mouse_y ? -1 : 1;
}

/* Moves the pointer from the keyboard. Tab moves it along the row y 130 between three
   places, x 20, 130 and 280 (0x4A3, probably Shift+Tab, goes the other way). The keypad
   keys 1 to 9 except 5 (codes 0x8C to 0x94) warp it toward the matching screen edge or
   corner; pressing the same key again while it moves speeds it up. k is the key's scan
   code, which moveMouse watches in key_on to slow the warp when the key is let go. */
void far keyboard_mouse(int key)
{
    register int k = 0;
    register int x = mouse_x;
    int y = mouse_y;

    switch (key) {
    case KEY_SHIFT | KEY_BACKTAB:
        if (warp_x >= 0)
            return;
#ifndef __TURBOC__
        if (look_on)                    /* port only: mouse-look holds the pointer */
            return;
#endif
        y = 0x82;
        if (mouse_x < 0x2E)
            x = 0x118;
        else if (mouse_x < 0xE1)
            x = 0x14;
        else
            x = 0x82;
        break;
    case 9:
        if (warp_x >= 0)
            return;
#ifndef __TURBOC__
        if (look_on)                    /* port only: mouse-look holds the pointer */
            return;
#endif
        y = 0x82;
        if (mouse_x < 0x2E)
            x = 0x82;
        else if (mouse_x < 0xE1)
            x = 0x118;
        else
            x = 0x14;
        break;
    case KEY_LEFT:
        x = 0;
        k = 0x4B;
        break;
    case KEY_RIGHT:
        x = 0x13F;
        k = 0x4D;
        break;
    case KEY_UP:
        y = 0xC7;
        k = 0x48;
        break;
    case KEY_DOWN:
        y = 0;
        k = 0x50;
        break;
    case KEY_HOME:
        x = 0;
        y = 0xC7;
        k = 0x47;
        break;
    case KEY_PGUP:
        x = 0x13F;
        y = 0xC7;
        k = 0x49;
        break;
    case KEY_END:
        x = 0;
        y = 0;
        k = 0x4F;
        break;
    case KEY_PGDN:
        x = 0x13F;
        y = 0;
        k = 0x51;
        break;
    default:
        return;
    }
    if (k == warp_key && warp_x >= 0) {
        m_warp_rate++;
        if (m_warp_rate > 40)
            m_warp_rate = 40;
    } else {
        warp_key = k;
        if (x != mouse_x || y != mouse_y)
            warpMouse(x, y);
    }
}

/* The buttons held: the driver's, or when it reports none, INS (scan code 0x52) as the
   left button and DEL (0x53) as the right. */
int far mouse_btns(void)
{
    int b = 0;

    if (!*MouseOn || (b = MBUTTONS()) == 0) {
        if (key_on[0x52])
            b |= 1;
        if (key_on[0x53])
            b |= 2;
    }
    return b;
}

#ifndef __TURBOC__
/* Each seat has its own input repeat deadline and pending mouse press. */
void port_shared_input_seat(int seat)
{
    static struct {
        int16 x,y,button,qx,qy,release,last,index;
        unsigned char taken;char first,shift;uint32 time;
        char depth,look,look_was;int16 cursor,stack[3],lookx,looky;
        int16 x0,y0,x1,y1,rx0,rx1,ry0,ry1;
    } saved[2];
    static int current,ready;
    if (!ready || seat<0) {
        memset(&saved[1],0,sizeof saved[1]);saved[1].button=-1;saved[1].first=1;
        saved[1].x=saved[1].y=100;saved[1].x1=319;saved[1].y1=199;saved[1].rx0=-1;
        ready=1;if(seat<0)return;
    }
    if (seat==current)return;
#define S(member,global) saved[current].member=(global);(global)=saved[seat].member
    S(x,mouse_x);S(y,mouse_y);S(button,q_button);S(qx,q_x);S(qy,q_y);
    S(release,q_release);S(last,last_button);S(index,key_index);S(taken,q_taken);
    S(first,mouse_first);S(shift,port_lshift_was);S(time,key_time);
    S(depth,force_depth);S(look,look_on);S(look_was,look_was);S(lookx,look_x);S(looky,look_y);
    S(x0,con_x0);S(y0,con_y0);S(x1,max_x);S(y1,max_y);
    S(rx0,in_x0);S(rx1,in_x1);S(ry0,hit_y0);S(ry1,in_y1);
#undef S
    memcpy(saved[current].stack,curs_stack,sizeof curs_stack);memcpy(curs_stack,saved[seat].stack,sizeof curs_stack);
    saved[current].cursor=m_cursor_pic;set_mouse_data(saved[seat].cursor ? saved[seat].cursor : ICON_CURSORS);
    current=seat;
}
#endif
