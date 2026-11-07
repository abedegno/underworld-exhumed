/* target: ovr147 */
/* opts: -mm -1 -G -O -Y -d */
/* The main menu: loading its button pictures, drawing the buttons or the list of saved
   games, following the mouse and the keys, and acting on the choice. The whole of DOS
   overlay ovr147, in original order.

   What it does in the game: real_start (called from UWEDIT.C's main) shows the menu on
   screen 5 of the art with four buttons from OPBTN.GR: introduction, create character,
   acknowledgements and journey onward; journey onward is offered only when a saved game
   exists, and with none the introduction plays first. Creating a character (CHARGEN.C's
   create_player) clears the SAVE0 directory, copies in the starting files, starts the
   conversation system and loads level 1 with the player at (0x13, 0x30). Journeying onward
   lists the saved games' descriptions and restores the one chosen (GAMEWRAP.C). Escape on
   the buttons (key 0x278) quits to DOS.

   Data owned: the button pictures (opbuf, buttons).
   Function names are the originals from the FM Towns symbol table.
   Name: descriptive (the main menu: draw_start_buttons, real_start). */
/* name: adr_opbtn and move_opbtn (IDA ovr147_0 and ovr147_22) are the FM Towns functions
   just before do_intro_scene, and gronk_gr's callbacks for "opbtn" in both builds.
   UnknownAutomapLoop_ovr147_A56 and its callback have no FM Towns counterpart and no
   caller in DOS. The loop keeps its IDA name; the callback (IDA's
   UnknownCallBackFunctionForAutomap_ovr147_A73) has a provisional name chosen so that its
   tools/bssorder.py key puts it in the EXE's overlay stub order. */

#include <dos.h>
#include <stdlib.h>
#include <string.h>
#include "conv.h"
#include "critter.h"
#include "file.h"
#include "gfx.h"
#include "player.h"
#include "sound.h"
#include "sys.h"
#include "ui.h"
#ifndef __TURBOC__
#include "event.h"
#include "map.h"
#include "object.h"
#endif

/* name: FM Towns keeps these as statics, so their names are not known. */
static unsigned char far *opbuf = 0;    /* where gronk_gr puts the next button picture */
static struct Button *buttons;          /* the menu's buttons, on real_start's stack */

/* gronk_gr's callbacks while loading the menu buttons: where to put the next picture, and
   recording it. Pictures alternate: a button's normal picture, then its selected one. */
unsigned char far * far adr_opbtn(int size)
{
    unsigned char far *p;

    p = opbuf;
    opbuf += size;
    return p;
}

int far move_opbtn(unsigned char far *p, int size, register int n)
{
    buttons[n >> 1].img[n & 1] = p + 5;
    if (!(n & 1)) {
        n >>= 1;
        buttons[n].w = p[1];
        buttons[n].h = p[2];
    }
    return size != 0;
}

/* The introduction plays before the menu when there are no saved games. */
void far do_intro_scene(int intro)
{
    int16 found;
    char descs[4][40];

#ifndef __TURBOC__
    if (ENHANCED(ENH_SKIP_INTRO)) return;   /* port only: --enhance skip-intro; the menu still has it */
#endif
    if (intro) {
        get_save_descs(descs, &found);
        if (found == 0) {
            show_cutscene(0);
            show_cutscene(1);
        }
    }
}

/* Draws n buttons, or n strings (the saved games) when text is set, sel highlighted. */
void far draw_start_buttons(int n, struct Button far *b, unsigned char text, int sel)
{
    int w;
    struct Button far *p;
    unsigned char far *img;
    register int i;
    register char **s;

    if (text == 0) {
        mouse_hide();
        for (i = 0; i < n; i++) {
            p = b;
            img = p[i].img[i == sel];
            show(p[i].x, p[i].y, img, p[i].h, p[i].w, 0, 0);
        }
    } else {
        mouse_hide();
        for (i = 0; i < n; i++) {
            s = (char **)b;
            if (i == sel) {
                *foreground_color = 7;
                *background_color = 7;
            } else {
                *foreground_color = 0xCD;
                *background_color = 0xCD;
            }
            while ((w = string_width(s[i])) > 0x13E)
                s[i][strlen(s[i]) - 1] = 0;
            string_to_screen(s[i], 0xA0 - w / 2, 100 - i * 0x16);
        }
    }
    mouse_show();
}

#ifndef __TURBOC__
/* port-only: where the player fits at or near a mod's start square, for port_new_game_start:
   the squares 0, 1, 2, 3 and 4 away in turn (the box find_good_x_and_y searches), each ring from
   its south-west corner row by row, asking can_place as find_good_x_and_y asks it. Not
   find_good_x_and_y itself, whose visited table is half cleared (FINDINGS), so the square it
   picks hangs on what the stack held: one start, or its replay, could differ from another.
   Returns 0 when the player fits nowhere there. */
static int port_start_fit(int x, int y, int16 *nx, int16 *ny)
{
    int r, tx, ty;
    struct Tile far *tile;

    for (r = 0; r <= 4; r++)
        for (ty = y - r; ty <= y + r; ty++)
            for (tx = x - r; tx <= x + r; tx++) {
                if ((tx != x - r && tx != x + r && ty != y - r && ty != y + r)
                    || tx < 1 || tx > 62 || ty < 1 || ty > 62)
                    continue;
                tile = Map_GetAddr(tx, ty);
                if (can_place(ThePlayer->id & ID_ITEM, Obj_MemTPtr(ThePlayer), tx * 8 + 3, ty * 8 + 3,
                              (tile->height << 3) + ((tile_walls[tile->type] & 0x20) ? 4 : 0),
                              0, 8)) {
                    *nx = tx;
                    *ny = ty;
                    return 1;
                }
            }
    return 0;
}

/* port-only (sys/mods.c, docs/MODDING.md): a new game's first level and square. Without a
   mod, the original's GetLevel(1), load_dl(), player_setup(0x13, 0x30, -1) and
   do_level_hacks(1, 0). With a mod's start level or square: that level (GetLevel sets
   PlayerLevel and reads its light, which load_dl reads again, as in the original) and square,
   the player moved to the nearest square it fits on (port_start_fit), or left on the square
   given when it fits nowhere near, as the original's start leaves it. Returns 0 if the level
   cannot be loaded. */
static int port_new_game_start(void)
{
    int level = port_start_level(1);
    int x = port_start_square(0, 0x13), y = port_start_square(1, 0x30);
    int16 nx = x, ny = y;

    if (GetLevel(level) <= 0)
        return 0;
    load_dl();
    if (port_start_level(0) || port_start_square(0, 0)) {
        int found = port_start_fit(x, y, &nx, &ny);
        if (!found) {
            nx = x;
            ny = y;
        }
        port_start_placed(level, x, y, nx, ny, found);
    }
    player_setup(nx, ny, -1);
    do_level_hacks(level, 0);
    return 1;
}
#endif

/* The main menu loop (intro set: play the introduction when there are no saves). Returns
   once a game has been started or restored, with the game screen set up; a restore that
   fails shows 'Error: Bad save file' and returns to the menu. */
void far real_start(int intro)
{
    int16 found;
    unsigned char done;
    char ok;
    int n;
    int sel;
    unsigned char far *pal;
    register int choice = 0;
    register int r;
    char descs[4][40];
    struct Button btns[4] = {
        { { 0, 0 }, 0x62, 0x76, 1, 1 },
        { { 0, 0 }, 0x51, 0x5F, 1, 1 },
        { { 0, 0 }, 0x48, 0x47, 1, 1 },
        { { 0, 0 }, 0x55, 0x2E, 1, 1 },
    };

    buttons = btns;
    get_save_descs(descs, &found);
    n = found == 0 ? 3 : 4;
    sel = found == 0 ? 1 : 3;
    do_intro_scene(intro);
    force_mouse_cursor(0x106C);
    mouse_show();
    done = 0;
    while (!done) {
        set_new_music(MUSIC_THEME);
        change_music_maybe();
        if (choice < 4 && choice > -1) {
            opbuf = MK_FP(get_workspace(), 0);
            pal = opbuf + 0xFA00;
            mouse_hide();
            disk_to_vid(5, opbuf);
            mouse_show();
            set_workspace();
            if (opbuf == 0 || !gronk_gr("opbtn", 0, -1, (ArtAllocFn)adr_opbtn, (ArtMoveFn)move_opbtn))
                pfatal_code(ERR_READ | 0x00D);
            if (choice != 3) {
                draw_start_buttons(n, buttons, 0, sel);
                read_quikpal(2, pal);
                fadein(pal, 2, 0);
            }
        }
        choice = parse_start_input(n, buttons, 0, sel);
        if (choice < 4 && choice > -1)
            release_workspace();
        switch (choice) {
        case 2:
            show_credits();
            break;
        case 3:
            r = do_journey();
            done = r == 1;
            if (r == -1) {
                char far *msg;

                msg = get_string(0x2B8);  /* 'Error: Bad save file' */
                disk_to_vid(5, 0);
                grfx_quikfont(FONT_BIG);
                *foreground_color = 7;
                *background_color = 7;
                string_to_screen(msg, 0xA0 - string_width(msg) / 2, 0x5A);
                mouse_show();
                while (mouse_get_input() < 0)
                    ;
                grfx_quikfont(FONT_5X6P);
            } else if (done)
                punt_fightmode();
            break;
        case 1:
            fadeout(pal, 2, 0);
            ok = create_player();
            if (ok) {
                int err;

                clear_dir(HomeDir);
                move_initial_files();
                if ((err = init_babl()) != 0)
                    pfatal_code(err);
#ifndef __TURBOC__
                /* port-only: a mod's start level and square (sys/mods.c), else the game's own */
                done = port_new_game_start();
                if (done) {
#else
                done = GetLevel(1) > 0;
                if (done) {
                    load_dl();
                    player_setup(0x13, 0x30, -1);
                    do_level_hacks(1, 0);
#endif
                    preload_cr(0);
                    PreLoadCritPages();
                }
            }
            break;
        case 0:
            show_cutscene(0);
            show_cutscene(1);
            break;
        case -1:
            free_world(0);
            exit(1);
        }
    }
    unforce_mouse_cursor(3);
    mouse_show();
    change_screen(1);
    editchng(0x7FFE);
    LeftPanel = 0;
    set_random_walking_music(0);
}

/* Follows the mouse over the buttons (or strings) until it is clicked; returns the one
   clicked, plus n if the click was on none of them. */
int far parse_start_mouse(int n, struct Button far *b, unsigned char text)
{
    int none;
    int16 x, y;
    struct Button far *p;
    int left, bottom, w, h;
    char **s;
    register int i;
    register int last = -1;

    none = 1;
    p = b;
    if (text == 0) {
        while (mouse_get_input() > 0) {
            mouse_getxy(&x, &y);
            for (i = 0; i < n; i++)
                if (p[i].x <= x && p[i].x + p[i].w - 1 >= x &&
                    p[i].y - p[i].h + 1 <= y && p[i].y >= y) {
                    none = 1;
                    if (i != last) {
                        draw_start_buttons(n, b, text, i);
                        last = i;
                    }
                    break;
                }
            if (i == n)
                none = last == -1;
        }
    } else {
        s = (char **)b;
        grfx_quikfont(FONT_BIG);
        while (mouse_get_input() > 0) {
            mouse_getxy(&x, &y);
            for (i = 0; i < n; i++) {
                w = string_width(s[i]);
                left = (0x140 - w) / 2;
                bottom = 100 - i * 0x16;
                h = cur_font->height;
                if (x >= left && left + w > x && y <= bottom && bottom - h < y) {
                    none = 1;
                    if (i != last) {
                        draw_start_buttons(n, b, text, i);
                        last = i;
                    }
                    break;
                }
            }
            if (i == n)
                none = last == -1;
        }
        grfx_quikfont(FONT_5X6P);
    }
    return last + (none ? 0 : n);
}

/* Runs the menu until a choice is made; returns it, or -1 for Escape where allowed. */
int far parse_start_input(register int n, struct Button far *b, int text, int sel)
{
    int result = -2;
    int key;
    int hit;
    register int cur = sel;

    while (result < -1) {
        grfx_quikfont(FONT_BIG);
        draw_start_buttons(n, b, text, cur);
        grfx_quikfont(FONT_5X6P);
        while ((key = mouse_get_input()) < 0)
            loop_music_maybe();
        switch (key) {
        case 1:
        case 2:
        case 3:
            hit = parse_start_mouse(n, b, text);
            if (hit >= 0 && hit < n && hit != result)
                result = cur = hit;
            else if (hit >= n)
                cur = hit - n;
            break;
        case KEY_RIGHT: case KEY_DOWN: case 0xA9: case 0xAB: case KEY_CTRL | 'f': case KEY_CTRL | 'n':
            cur++;
            break;
        case KEY_UP: case KEY_LEFT: case 0xA6: case 0xA8: case KEY_CTRL | 'b': case KEY_CTRL | 'p':
            cur--;
            break;
        case 0x0D:
            result = cur;
            break;
        case KEY_HOME: case KEY_PGUP: case 0xA5: case 0xA7: case KEY_ALT | '<':
            cur = 0;
            break;
        case KEY_END: case KEY_PGDN: case 0xAA: case 0xAC: case KEY_ALT | '>':
            cur = n - 1;
            break;
        case KEY_ESC:
            if (text == 0)
                break;
        case KEY_ALT | 'x':
            result = -1;
            break;
        }
#ifndef __TURBOC__
        if (ENHANCED(ENH_WRAP_MENU) && n > 0) {
            if (cur < 0)                /* port only: --enhance wrap-menu */
                cur = n - 1;
            else if (cur >= n)
                cur = 0;
        } else
#endif
        if (cur < 0)
            cur = 0;
        else if (cur >= n)
            cur = n - 1;
    }
    return result;
}

/* Lists the saved games and restores the one chosen: 1 if restored, -1 if that failed,
   0 if none was chosen. */
int far do_journey(void)
{
    int i;
    int16 found;
    int count;
    int j;
    char *descs[4];
    char far *msg;
    char names[4][40];
    register char *p;
    register char *s;

    mouse_hide();
    display_screen(-1, 5);
    mouse_show();
    get_save_descs(names, &found);
    for (i = count = 0; i < 4; i++)
        if (found & (1 << i)) {
            s = names[i];
            p = &names[i][38];
            while (*p == ' ' && p > s)
                p--;
            p[1] = 0;
            descs[count] = s;
            count++;
        }
    i = parse_start_input(count, (struct Button far *)descs, 1, 0);
    if (i >= 0) {
        for (count = j = -1; j != i; count++)
            if (found & (1 << (count + 1)))
                j++;
        i = count;
        mouse_hide();
        display_screen(-1, 5);
        msg = get_string(0x311);  /* 'You reenter the Underworld . . .' */
        grfx_quikfont(FONT_BIG);
        *foreground_color = 7;
        *background_color = 7;
        string_to_screen(msg, (0x140 - string_width(msg)) / 2 + 10, 0x5A);
        grfx_quikfont(FONT_5X6P);
        if (RestoreGame(i + 1)) {
            load_weapcm();
            return 1;
        }
        return -1;
    }
    return 0;
}

/* Marks an irregular area of the automap (x 0x12..0x2C, y 0x1E..0x34, cut by
   Region_ovr147_A73) as seen. Never called. */
void far UnknownAutomapLoop_ovr147_A56(void)
{
    automap_area(0x12, 0x1E, 0x2C, 0x34, 0, (AreaMapFn)Region_ovr147_A73);
}

char far Region_ovr147_A73(register int x, register int y)
{
    if (x == 0x23)
        return y > 0x21;
    if (y < 0x20)
        return x >= 0x1E && x <= 0x20;
    if (x < 0x14)
        return y <= 0x31;
    return 1;
}
