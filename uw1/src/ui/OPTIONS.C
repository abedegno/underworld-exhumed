/* target: ovr130 */
/* opts: -mm -1 -G -O -Y -d */
/* The options panel: the left-hand panel's pages of buttons (save, restore, music, sound
   effects, detail, quit) drawn from the "optbtns" art, the keyboard and mouse loop that
   moves the highlight and presses buttons, and each page's handler. The whole of DOS
   overlay ovr130, in original order.

   What it does in the game: a click on the options icon (INTERACT.C) calls
   run_options_panel(1), which sets LeftPanel to 1, draws the top page and runs until a
   choice closes the panel (close_option_panel sets gameopts_done). While the panel is up,
   INTERACT.C passes clicks in it to options_mouse_click. The Ctrl shortcuts (PLAYER.C)
   call do_option_shortcut, which presses that button of the top page before running the
   panel; options_quick then makes a music, sound or detail page close the panel after
   the one choice instead of going back to the top page.

   A page is a number, options_page: 0 save, 1 restore, 2 music, 3 sound effects, 4
   detail, 5 quit, 6 the top page (7 while closed). Each has a drawing function
   (page_draw) and a handler for a pressed button (page_press), and page_pics gives each
   of its seven rows (row 0 at the bottom) the picture of its button, 0 where there is no
   button. A button's lit picture is the next one in OPTBTNS.GR. The panel is the
   rectangle from (4, 0x52) to (0x27, 0xBE) on screen; each row is 15 pixels high.

   UW1 against UW2: UW2's WRAPPER.C (ovr136) and ICONS.C's do_option_shortcut do the same
   job with struct buttongroup tables, button regions and another picture layout. Nothing
   here shares UW2's bytes (kin 1%, by chance), and UW2's other names
   (busywaiting_new_options, new_hilit_button, deal_with_button, the *_group_fun and *_opt
   functions) do not fit this overlay's stub order. LeftPanel, defined in UW2's
   INTERACT.C, is defined here in UW1. UW1's shortcuts press the button at once and do not
   try the save or restore handler first.

   File name: descriptive (the original UW1 name is unknown).
   name: do_option_shortcut and gameopts_done are UW2's (FM Towns) names, the same entry
   and the same flag; their keys fit (908 between choose_detail 747 and
   draw_music_and_sound_page 988; 23, first of the _BSS). The other names are
   descriptive, chosen so that their bssorder keys give the EXE's overlay stub order
   (close_option_panel 11, options_handle_key 23, options_mouse_click 71,
   set_options_page 171, run_options_panel 218, draw_top_page 220, draw_save_page 284,
   draw_detail_page 412, draw_options_back 444, draw_quit_yn_page 476, draw_options_btn
   516, press_options_btn 592, choose_top 603, choose_quit 619, hilite 648, choose_music
   683, choose_sounds 707, choose_save 723, choose_detail 747) and the _BSS layout
   (hilite_pic 816, hilite_row 864). The listing called run_options_panel and
   options_mouse_click ovr130_0 and ovr130_6D6. */

#include "file.h"
#include "gfx.h"
#include "player.h"
#include "sound.h"
#include "sys.h"
#include "ui.h"
#include "view3d.h"

/* match: this file's _DATA is DS:1A30..1AA5, from LeftPanel to "optbtns"; DS:1AA6
   ("bad tmap ids size") starts the next file. */
int16 LeftPanel = 2;                    /* what the left panel shows: 1 the options */
int16 options_page = -1;
char options_quick = 0;                 /* opened by do_option_shortcut: close after one choice */
void (far *page_draw[7])(void) = {
    draw_save_page, draw_save_page, draw_music_and_sound_page, draw_music_and_sound_page,
    draw_detail_page, draw_quit_yn_page, draw_top_page
};
void (far *page_press[7])(int) = {
    choose_save, choose_save, choose_music, choose_sounds, choose_detail, choose_quit,
    choose_top
};
/* The button picture of each row of each page, 0 where there is no button. */
char page_pics[7][7] = {
    { 0, 0x18, 0x24, 0x22, 0x20, 0x1E, 0 },
    { 0, 0x18, 0x24, 0x22, 0x20, 0x1E, 0 },
    { 0, 0, 0x1A, 0x16, 0x14, 0, 0 },
    { 0, 0, 0x1A, 0x16, 0x14, 0, 0 },
    { 0x1C, 0x2C, 0x2A, 0x28, 0x26, 0, 0 },
    { 0, 0, 0, 0x3B, 0x39, 0, 0 },
    { 0x12, 0x10, 0x0E, 0x0C, 0x0A, 0x08, 0x06 }
};

/* This file's _BSS, DS:7172..7177. */
char gameopts_done;                      /* set by close_option_panel: ends the loop */
int16 hilite_pic;                       /* the lit picture shown, -1 none */
int16 hilite_row;                       /* the row of the lit button */

/* Runs the options panel until a choice closes it: draws the top page first if show is
   nonzero. Mouse buttons press the button under the pointer; space, 0x93 and 0xAB move
   the highlight down, 0xA6 and 0x8D up, Enter presses the lit button, Escape closes. */
void far run_options_panel(int show)
{
#ifndef __TURBOC__
    port_shared_story(1);
#endif
    int16 x, y;
    int input;

    LeftPanel = 1;
    if (show) {
        mouse_hide();
        grSoftPageFlip();
        set_options_page(6);
        grSoftPageFlip();
        set_the_color(0x106);
        rectangle(4, 0xBD, 0x26, 0x52);
        mouse_show();
        mouse_release(0);
    }
    gameopts_done = 0;
    do {
        while ((input = mouse_get_input()) < 0)
            loop_music_maybe();
        switch (input) {
        case 1:
        case 2:
        case 3:
            mouse_getxy(&x, &y);
            x -= 4;
            y -= 0x52;
            if (x >= 0 && x <= 0x23 && y >= 0 && y <= 0x6C)
                options_mouse_click(x, y);
            break;
        case ' ':
        case KEY_DOWN:
        case KEY_GDOWN:
            options_handle_key(0);
            break;
        case '\r':
            options_handle_key(1);
            break;
        case KEY_GUP:
        case KEY_UP:
            options_handle_key(2);
            break;
        case KEY_ESC:
            close_option_panel();
            break;
        }
    } while (!gameopts_done);
#ifndef __TURBOC__
    port_shared_story(0);
#endif
}

/* Draws picture pic of OPTBTNS.GR as the panel's background. */
void far draw_options_back(int pic)
{
    reload_gr_vpic(ICON_OPTB, "optbtns", pic);
    pic_to_screen(ICON_OPTB, 4, 0xBD, 0x6C, 0x23);
}

/* Draws picture pic as the button of row row. */
void far draw_options_btn(int row, int pic)
{
    reload_gr_vpic(ICON_OPTB + 0x1, "optbtns", pic);
    pic_to_screen(ICON_OPTB + 0x1, 5, 0xBD - (6 - row) * 15 - 2, 0x0E, 0x1F);
}

/* Puts out the lit button and lights button pic at row row. */
void far hilite(int row, int pic)
{
    if (hilite_pic >= 0)
        draw_options_btn(hilite_row, hilite_pic - 1);
    draw_options_btn(row, pic + 1);
    hilite_pic = pic + 1;
    hilite_row = row;
}

/* Closes the panel: the left panel's normal picture, the icon the right button had. */
void far close_option_panel(void)
{
    mouse_hide();
    LeftPanel = 0;
    options_page = 7;
    reload_gr_vpic(ICON_OPTB, "optbtns", 0);
    pic_to_screen(ICON_OPTB, 4, 0xBD, 0x6C, 0x23);
    if (RightButtonThing > IMODE_DEFAULT)
        new_IconSelect(RightButtonThing);
    gameopts_done = 1;
    mouse_show();
}

void far draw_top_page(void)
{
    draw_options_back(1);
    hilite_pic = -1;
    hilite(6, 6);
}

/* The save and restore pages: the slots' descriptions come from ShowSaveRest. */
void far draw_save_page(void)
{
    draw_options_back(2);
    hilite_pic = -1;
    hilite(5, 0x1E);
    if (options_page == 1)
        draw_options_btn(6, 0x2E);
    grSoftPageFlip();
    set_the_color(0x106);
    rectangle(4, 0xBD, 0x26, 0x52);
    ShowSaveRest();
    grSoftPageFlip();
}

void far draw_quit_yn_page(void)
{
    draw_options_back(3);
    hilite_pic = -1;
    hilite(3, 0x3B);
}

/* The music and the sound effects pages: a title, the state, and on and off buttons. */
void far draw_music_and_sound_page(void)
{
    int title;
    int row, state;

    draw_options_back(4);
    hilite_pic = -1;
    if (options_page == 2) {
        title = 0x33;
        state = (music_is_on() ? 0 : 1) + 0x2F;
        row = (music_is_on() ? 1 : 0) + 3;
    } else {
        title = 0x34;
        state = (fx_is_on() ? 0 : 1) + 0x31;
        row = (fx_is_on() ? 1 : 0) + 3;
    }
    draw_options_btn(6, state);
    draw_options_btn(5, title);
    hilite(row, (row == 3 ? 2 : 0) + 0x14);
}

void far draw_detail_page(void)
{
    int level;

    level = player->detail;
    hilite_pic = -1;
    draw_options_back(5);
    reload_gr_vpic(ICON_OPTB + 0x2, "optbtns", level + 0x35);
    pic_to_screen(ICON_OPTB + 0x2, 5, 0xBC, 0x12, 0x22);
    hilite(4 - level, level * 2 + 0x26);
}

/* Music page: row 4 on, row 3 off, row 2 done. */
void far choose_music(int row)
{
    char leave = 1;

    if (row == 4) {
        turn_music(1);
        draw_music_and_sound_page();
    } else if (row == 3) {
        turn_music(0);
        draw_music_and_sound_page();
    }
    if (!options_quick && row != 2)
        leave = 0;
    if (leave) {
        if (options_quick)
            close_option_panel();
        else
            set_options_page(6);
    }
}

/* Sound effects page: row 4 on, row 3 off, row 2 done. */
void far choose_sounds(int row)
{
    char leave = 1;

    if (row == 4) {
        turn_fx(1);
        draw_music_and_sound_page();
    } else if (row == 3) {
        turn_fx(0);
        draw_music_and_sound_page();
    }
    if (!options_quick && row != 2)
        leave = 0;
    if (leave) {
        if (options_quick)
            close_option_panel();
        else
            set_options_page(6);
    }
}

/* Detail page: rows 4 to 1 set detail 0 to 3 and redraw the view; row 0 is done. */
void far choose_detail(int row)
{
    char leave = 1;
    int level;

    if (row >= 1 && row <= 4) {
        /* match: -(row - 4), not 4 - row: add ax,-4; neg ax */
        player->detail = level = -(row - 4);
        set_graphics_level();
        grSoftPageFlip();
        render_FB();
        send_FB();
        grSoftPageFlip();
        reload_gr_vpic(ICON_OPTB + 0x2, "optbtns", level + 0x35);
        pic_to_screen(ICON_OPTB + 0x2, 5, 0xBC, 0x12, 0x22);
        hilite(4 - level, level * 2 + 0x26);
    }
    if (row)
        leave = 0;
    if (leave) {
        if (options_quick)
            close_option_panel();
        else
            set_options_page(6);
    }
}

/* The top page, from the top: save, restore, music, sound, detail, return to the game,
   quit. */
void far choose_top(int row)
{
    switch (row) {
    case 6:
        if (check_save())
            set_options_page(0);
        break;
    case 5:
        if (check_rest())
            set_options_page(1);
        break;
    case 4:
        set_options_page(2);
        break;
    case 3:
        set_options_page(3);
        break;
    case 1:
        close_option_panel();
        break;
    case 2:
        set_options_page(4);
        break;
    case 0:
        set_options_page(5);
        break;
    }
}

/* Save or restore page: rows 5 to 2 are the slots 1 to 4, row 1 cancels. */
void far choose_save(int row)
{
    int slot = 4;

    if (row >= 1 && row <= 5) {
        hilite(row, (5 - row) * 2 + 0x1E);
        grSoftPageFlip();
        set_the_color(0x106);
        rectangle(4, 0xBD, 0x26, 0x52);
        scroll_clear(0);
        switch (row) {
        case 5:
            slot--;
        case 4:
            slot--;
        case 3:
            slot--;
        case 2:
            DoSaveRest(options_page == 1, slot);
            if (options_page == 1) {
                RightButtonThing = IMODE_DEFAULT;
                punt_fightmode();
            }
        case 1:
            grSoftPageFlip();
            close_option_panel();
        }
    }
}

/* Quit page: row 4 quits the game, row 3 goes back. */
void far choose_quit(int row)
{
    switch (row) {
    case 4:
        hilite(4, 0x39);
        grSoftPageFlip();
        set_the_color(0x106);
        rectangle(4, 0xBD, 0x26, 0x52);
        mouse_show();
        editexit(0);
        mouse_hide();
        grSoftPageFlip();
    case 3:
        close_option_panel();
    }
}

/* Shows options page page (page_draw); press_options_btn runs the page's press
   handler for row on the hidden page, then flips and redraws the panel's frame. */
void far set_options_page(int page)
{
    options_page = page;
    (*page_draw[options_page])();
}

void far press_options_btn(int row)
{
    mouse_hide();
    grSoftPageFlip();
    (*page_press[options_page])(row);
    grSoftPageFlip();
    set_the_color(0x106);
    rectangle(4, 0xBD, 0x26, 0x52);
    mouse_show();
    mouse_release(0);
}

/* A click at (x, y) in the panel presses the row under it. x is incremented and never
   used (the bytes have the inc). */
void far options_mouse_click(int x, int y)
{
    int row;

    row = y / 15;
    x++;
    press_options_btn(row);
}

/* A key on the panel: 0 moves the highlight down a row, 2 up, 1 presses the lit button;
   the Ctrl shortcuts press a top page button: Ctrl+S (0x173) save, Ctrl+R (0x172)
   restore, Ctrl+M (0x16D) music, Ctrl+F (0x166) sound, Ctrl+Q (0x171) quit, Ctrl+D
   (0x164) detail. */
void far options_handle_key(int key)
{
    int row;

    switch (key) {
    case 0:
    case 2:
        row = hilite_row + (key == 0 ? -1 : 1);
        if (row >= 0 && row < 7 && page_pics[options_page][row]) {
            mouse_hide();
            grSoftPageFlip();
            hilite(row, page_pics[options_page][row]);
            grSoftPageFlip();
            set_the_color(0x106);
            rectangle(4, 0xBD, 0x26, 0x52);
            mouse_show();
        }
        break;
    case 1:
        press_options_btn(hilite_row);
        break;
    case 0x173:
        press_options_btn(6);
        break;
    case 0x172:
        press_options_btn(5);
        break;
    case 0x16D:
        press_options_btn(4);
        break;
    case 0x166:
        press_options_btn(3);
        break;
    case 0x171:
        press_options_btn(0);
        break;
    case 0x164:
        press_options_btn(2);
        break;
    }
}

/* The Ctrl shortcuts to the options panel: presses keycode's button on the top page and
   runs the panel (drawing the top page only if the button left it there). Refused while
   an action is in progress (GameInputMode): string 0xA0, "You cannot select options
   partway through an action." */
void far do_option_shortcut(int keycode)
{
    if (GameInputMode)
        game_sprint(0xA0);
    else {
        options_quick = 1;
        options_page = 6;
        options_handle_key(keycode);
        run_options_panel(options_page == 6);
        options_quick = 0;
    }
}
