/* target: seg043_37F0 */
/* opts: -mm -1 -G -O -Y -d */
/* The message scroll: the strip of text at the bottom of the game screen (and its
   counterpart in conversations), printing to it with word wrap, colour and pause escapes,
   scrolling it up a line, its animated edges, and its interactive side (the [MORE] prompt,
   typed answers, yes or no questions). The whole of UW1's DOS resident segment
   seg043_37F0, in original order. In UW2 this is two files, SCROLL.C (resident) and
   SCROLLIO.C (overlay ovr139); UW1 has both in one segment, init_scroll and scroll_wait
   among SCROLL.C's routines and the rest after them. UW1 has no symbol-bearing build; the
   names are UW2's (the FM Towns symbol table), the code being the same routines.

   UW1's differences, marked "UW1:" where they are not obvious:
   - Two scrolls, not three: do_play_scroll selects main_scroll, and there is no
     pick_scroll (the answers are always typed into the main scroll).
   - The escapes \0..\6 set fixed colours in a switch; there is no spec_col table.
   - The paper colour is 0x2A (UW2 0x71), the [MORE] colour 0x60 (UW2 0x12).
   - The edges are drawn at fixed positions, and scroll_up and scroll_clear call PANELS.C's
     set_screen_frame(4, 1) before rolling the main scroll's edges.
   - scroll_wait does not keep the music going.
   - scroll_wrap is a different routine: it breaks the text once, at the last space that
     fits (or, for a word longer than the line, by cutting characters off the end), prints
     the first part with a newline appended and prints the rest through scroll_print3.

   Data owned: the two Scroll records, the current scroll and its mode, start_line, the
   edge animation phases, and where the typed answer starts (answer_start).

   Name: descriptive (the message scroll's input side: scroll_more, wyorn); the UW2
   seed's file name, as for UW2's SCROLLIO.C. */

#include <dos.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "gfx.h"
#include "sound.h"
#include "sys.h"
#include "ui.h"

/* This file's _BSS, DS:3650..3655. */
struct Scroll near *scroll;                      /* DS:3650 */
unsigned char mouse_in_scroll;                   /* DS:3654 */
/* UW2's answer_x (static there, so no original name): name chosen for layout, the _BSS
   order key putting it between scroll and mouse_in_scroll (DS:3652). */
static int16 answer_start;

/* This file's _DATA, DS:0A60..0AAC. */
struct Scroll main_scroll = { 0x1E, 1, 0x0F, 0x131, 0x0F, 0x1E, 0x0F, 0x1E, 0, 0, 0x2E };
/* DS:0A60 */
struct Scroll npc_scroll = { 0x94, 0x44, 0x38, 0xDB, 0x3B, 0x92, 0x3B, 0x92, 0, 0, 0x2E };
/* DS:0A75 */
int16 start_line = 0;                   /* DS:0A8A */
int16 scroll_mode = 0;                  /* DS:0A8C */
unsigned char menus_active = 0;         /* DS:0A8E */
unsigned char didMouseInput = 0;        /* DS:0A8F */
static int32 click_time = 0;            /* DS:0A90 */
static int16 edge_phase = 0;            /* DS:0A94 */
static int16 conv_edge_phase = 0;       /* DS:0A96 */
char scroll_esc = 1;                    /* DS:0A98 */

/* Selects and clears the message scroll. */
void far init_scroll(void)
{
    scroll = &main_scroll;
    scroll_mode = 0;
    draw_scroll(0x0F, 0x1E, 0x131, 1, 0);
    draw_edges();
}

/* Whether the cursor touches the current scroll's box, so printing must hide it first. */
void far set_mouse_in(void)
{
    mouse_in_scroll = mouse_check_reg(scroll->top, scroll->y0, scroll->bottom, scroll->x0);
}

/* Selects the game's message scroll for printing (scroll_mode 0). */
void far do_main_scroll(void)
{
    scroll_mode = 0;
    didMouseInput = 1;
    scroll = &main_scroll;
}

void far do_npc_scroll(void)
{
    scroll_mode = 1;
    didMouseInput = 1;
    scroll = &npc_scroll;
}

/* UW1: mode 2 prints to the main scroll too. */
void far do_play_scroll(void)
{
    scroll_mode = 2;
    didMouseInput = 1;
    scroll = &main_scroll;
}

/* Waits for the input to change from what it is on entry (a key or a click), or for
   ticks of *Time when ticks is not 0; the buttons must be released before and after.
   With mouse set, the cursor is shown during the wait if it is over the scroll. Used by
   the \p and \P escapes and the [MORE] prompt. */
void far scroll_wait(int ticks, char mouse)
{
    uint32 end;
    int key;

#ifndef __TURBOC__
    port_shared_story(1);
#endif
    mouse_release(1);
    key = mouse_get_input();
    end = GAME_TIME() + ticks;
    if (mouse_in_scroll && mouse)
        mouse_show();
    while (mouse_get_input() == key) {
        if (ticks && GAME_TIME() > end)
            break;
    }
    mouse_release(1);
    set_mouse_in();
    if (mouse_in_scroll & mouse)
        mouse_hide();
#ifndef __TURBOC__
    port_shared_story(0);
#endif
}

/* Advances the message scroll's rolled-paper edges, left and right of the text, by one of
   five animation frames (pictures 0x20D5.. and 0x20DA..). */
void far draw_edges(void)
{
    pic_to_screen(edge_phase + ICON_SCRLEDGE, 0x0B, 0x1E, 0x1C, 4);
    pic_to_screen(edge_phase + (ICON_SCRLEDGE + 0x5), 0x132, 0x1E, 0x1C, 4);
    edge_phase++;
    if (edge_phase == 5)
        edge_phase = 0;
}

/* The same for the conversation scroll's edges: three tiles each side, six frames. */
void far draw_conv_edges(void)
{
    register int i;

    for (i = 0; i < 3; i++) {
        pic_to_screen(conv_edge_phase + (ICON_SCRLEDGE + 0xA), 0x34, 0x94 - i * 0x1B, 0x1B, 5);
        pic_to_screen(conv_edge_phase + (ICON_SCRLEDGE + 0x10), 0xDC, 0x94 - i * 0x1B, 0x1B, 5);
    }
    conv_edge_phase++;
    if (conv_edge_phase == 6)
        conv_edge_phase = 0;
}

/* Scrolls the text up by one line height, clears the freed line in the paper colour and
   rolls the edges. */
void far scroll_up(int n)
{
    vcopy(scroll->top, scroll->last_y - cur_font->height,
          scroll->bottom - scroll->top + 1, scroll->last_y - cur_font->height - n + 1,
          scroll->top, scroll->last_y);
    set_the_color(0x2A);
    rectangle(scroll->top, scroll->cur_y, scroll->bottom, n + 1);
    if (scroll == &main_scroll) {
        set_screen_frame(SCR_DRAGON, 1);
        draw_edges();
    } else
        draw_conv_edges();
}

/* The [MORE] prompt: scrolls up a line, writes [MORE] in colour 0x60 at the start of the
   new line, waits for a key or click, then erases it and allows one more line before the
   next prompt. */
void far scroll_more(void)
{
    int color;
    int y;

    y = scroll->cur_y - cur_font->height;
    scroll_up(y);
    y = scroll->cur_y;
    color = *foreground_color;
    *foreground_color = 0x60;
    string_to_screen("[MORE]", scroll->left, y);
    scroll_wait(0, 1);
    set_the_color(0x2A);
    rectangle(scroll->left, y, scroll->bottom, scroll->y0);
    *foreground_color = color;
    start_line = scroll->start_line - 1;
    scroll->cur_x = scroll->left;
}

int far scroll_print(char far *s)
{
    /* match: see UW2's SCROLL.C: the 49-byte copy runs into the spill slot after it. */
    char copy[FRAME_LEN(47, 50)];
    register char *found;
    register int remaining;
    char saved;
    int chunklen;
    char pad1;                 /* match: unused; the EXE reserves it too (frame 0x36) */
    char sentinel;

    if (scrmode != MODE_GAME && scrmode != MODE_CONV)
        return -1;

    set_mouse_in();
    if (mouse_in_scroll)
        mouse_hide();
    if (menus_active && scroll_mode != 1) {
        menus_active = 0;
        scroll_clear(0);
    }
    start_line = scroll->start_line;
    didMouseInput = 0;
    *foreground_color = (unsigned char)scroll->font_color;
    *background_color = 0x2A;

    chunklen = 0;
    remaining = str_len(s);
    while (remaining > 0x31) {
        FAR_COPY(copy, s, 0x31);
        FRAME_TAIL(copy, 49, sentinel) = 0;
        found = strrchr(copy, ' ');
        if (found == 0)
            found = &FRAME_TAIL(copy, 49, sentinel);
        saved = *found;
        *found = 0;
        chunklen = found - copy;
        scroll_print1(copy, 1);
        s += chunklen;
        *found = saved;
        remaining -= chunklen;
    }
    FAR_COPY(copy, s, remaining + 1);
    scroll_print1(copy, 0);

    click_time = GAME_TIME();
    if (mouse_in_scroll)
        mouse_show();
    return scroll->start_line;
}

/* Splits at backslash escapes, so each escape starts a piece. */
void far scroll_print1(char *text, int flag)
{
    char *di;
    char *found;

    di = text;
    while ((found = strchr(di + 1, '\\')) != 0) {
        *found = 0;
        if (found[2] != 0 || found[1] == 'm')
            scroll_print2(di, 1);
        else
            scroll_print2(di, flag);
        *found = '\\';
        di = found;
    }
    scroll_print2(di, flag);
}

/* Splits at newlines, keeping each newline at the end of its piece. */
void far scroll_print2(char *text, int flag)
{
    char saved, unused;
    char *si;
    char *di;

    di = text;
    while ((si = strchr(di, '\n')) != 0) {
        if (si[1] == 0)
            break;
        saved = si[1];
        si[1] = 0;
        scroll_print3(di, 1);
        si[1] = saved;
        di = si + 1;
    }
    scroll_print3(di, flag);
}

/* Prints one piece; see UW2's SCROLL.C. UW1: the colour escapes \0..\6 are fixed colours. */
void far scroll_print3(char *text, int flag)
{
    int width;
    int idx;
    int code_val;
    char *si;
    int y;

    si = text;
    code_val = 0;
    if (scroll_esc && *si == '\\') {
        si++;
        switch (*si++) {
        case 'p':
            code_val = 0x190;
        case 'P':
            code_val += 0xC8;
            scroll_wait(code_val, 1);
            break;
        case 'm':
            scroll_more();
            break;
        case '0':
            *foreground_color = 0x2E;
            break;
        case '1':
            *foreground_color = 0x26;
            break;
        case '2':
            *foreground_color = 0xF1;
            break;
        case '3':
            *foreground_color = 0x60;
            break;
        case '4':
            *foreground_color = 0xB4;
            break;
        case '5':
            *foreground_color = 0xC4;
            break;
        case '6':
            *foreground_color = 0xD4;
            break;
        }
        scroll->font_color = *foreground_color;
    }

    if (scroll->more_pending) {
        y = scroll->cur_y - cur_font->height;
        if (start_line - flag < 0) {
            if (y - cur_font->height < scroll->y0 - 1) {
                scroll_more();
                y = scroll->cur_y;
            } else {
                scroll->start_line++;
            }
        } else {
            if (y - cur_font->height < scroll->y0 - 1) {
                scroll_up(y);
                y = scroll->cur_y;
                start_line--;
            } else {
                scroll->start_line++;
            }
        }
        scroll->cur_y = y;
        scroll->cur_x = scroll->left;
        scroll->more_pending = 0;
    }

    width = scroll->cur_x + string_width(si);
    if (scroll->bottom <= width) {
        scroll_wrap(si, flag);
    } else {
        idx = strlen(si) - 1;
        if (si[idx] == '\n') {
            si[idx] = 0;
            scroll->more_pending = 1;
        }
        string_to_screen(si, scroll->cur_x, scroll->cur_y);
        scroll->cur_x += string_width(si);
    }
}

/* UW1: word wrap, unlike UW2's. Finds the last space at which the text before it fits the
   line; failing that, cuts characters off the end until it fits (if nothing fits, starts
   a new line and prints the whole text there). Prints the part that fits with a newline
   appended, then the rest (without the space at the break). */
void far scroll_wrap(char *text, int flag)
{
    char saved;
    char saved2;
    char *next;
    char *si;

    si = strrchr(text, ' ');
    while (si) {
        *si = 0;
        if (scroll->cur_x + string_width(text) < scroll->bottom) {
            *si = ' ';
            break;
        }
        next = strrchr(text, ' ');
        *si = ' ';
        si = next;
    }
    if (si == 0) {
        si = text + strlen(text) - 1;
        saved = *si--;
        do {
            si[1] = saved;
            saved = *--si;
            *si = 0;
        } while (si > text && scroll->cur_x + string_width(text) >= scroll->bottom);
        if (si <= text) {
            /* scroll_print3 cuts the newline off: in DOS this literal is "" from the
               first time on (the byte before it is "[MORE]"'s 0) */
            scroll_print3(PERSISTENT_STR("\n"), 1);
            scroll_print3(text, flag);
            return;
        }
        *si = saved;
    }
    saved = *si;
    saved2 = si[1];
    *si = '\n';
    si[1] = 0;
    scroll_print3(text, 1);
    *si = saved;
    si[1] = saved2;
    scroll_print3(si + (saved == ' ' ? 1 : 0), flag);
}

/* Clears a scroll box to the paper colour; with flag set a frame in colour 0xF1 is drawn
   first, 14 pixels in on the left. The four values are the box's left x, top y, right x
   and bottom y, as rectangle takes them. */
void far draw_scroll(int x, int y, int w, int h, char flag)
{
    if (flag) {
        set_the_color(0xF1);
        rectangle(x + 0xE, y - 1, w - 0xF, h + 1);
    }
    set_the_color(0x2A);
    rectangle(x, y, w, h);
}

/* UW2's seg043_3619_669, with UW1's colours: four corner pictures, two nested boxes and a
   filled rectangle, probably a framed text box. Nothing in UW1.EXE calls it either; the
   target table runs it into draw_scroll's row (IDA made no procedure of it), so it is
   static here. */
/* name: UW2's provisional name. */
static void far seg043_3619_669(int x, int y, int r, int b)
{
    Transparency = 1;
    pic_to_screen(ICON_BUTTONS + 0x68, x, y, 10, 0x28);
    pic_to_screen(ICON_BUTTONS + 0x6A, r - 0x28, y, 10, 0x28);
    pic_to_screen(ICON_BUTTONS + 0x69, x, b + 10, 10, 0x28);
    pic_to_screen(ICON_BUTTONS + 0x6B, r - 0x28, b + 10, 10, 0x28);
    Transparency = 0;
    set_the_color(0x2D);
    box(x + 0xD, y, r - 0xD, b);
    set_the_color(0x2B);
    box(x + 0xE, y - 1, r - 0xE, b + 1);
    set_the_color(0x2A);
    rectangle(x + 0xF, y - 2, r - 0xF, b + 2);
}

/* Empties the current scroll and resets its cursor and line count; redraw hides the
   mouse around it when the cursor is over the scroll. */
void far scroll_clear(char redraw)
{
    if (redraw) {
        set_mouse_in();
        if (mouse_in_scroll)
            mouse_hide();
    }
    set_the_color(0x2A);
    rectangle(scroll->top, scroll->x0, scroll->bottom, scroll->y0);
    scroll->cur_y = scroll->last_y;
    scroll->cur_x = scroll->left;
    scroll->more_pending = 0;
    scroll->start_line = 0;
    if (scroll == &main_scroll) {
        set_screen_frame(SCR_DRAGON, 1);
        draw_edges();
    } else
        draw_conv_edges();
    if (redraw && mouse_in_scroll)
        mouse_show();
}

/* Replaces the answer typed after the prompt with the number n. */
void far wd_replace(int n)
{
    char buf[6];
    int y;
    int x;

    itoa(n, buf, 10);
    x = answer_start;
    y = scroll->cur_y;
    do_main_scroll();
    *foreground_color = scroll->font_color;
    set_the_color(0x2A);
    rectangle(x, y, scroll->cur_x, y - cur_font->height + 1);
    scroll->cur_x = answer_start;
    scroll_print(buf);
}

/* Replaces the answer of a yes-or-no question on the main scroll with "Yes" or "No". */
void far wd_bool(char yes)
{
    int y;
    int x;

    x = answer_start;
    y = scroll->cur_y;
    do_main_scroll();
    *foreground_color = scroll->font_color;
    set_the_color(0x2A);
    rectangle(x, y, scroll->cur_x, y - cur_font->height + 1);
    scroll->cur_x = answer_start;
    if (yes)
        scroll_print("Yes");
    else
        scroll_print("No");
}

/* Reads a line typed into the scroll after prompt (or ">"); see UW2's SCROLLIO.C. */
int far wdialog(char *prompt, char *initial, char *result, char anychar, int maxlen)
{
    int width;
    int blink;
    int x;
    int y;
    int key;
    char text[52];
    char tmp[52];
    int pos;
    int i;

#ifndef __TURBOC__
    port_shared_story(1);
#endif
    if (maxlen > 50)
        maxlen = 50;
    do_main_scroll();
    *foreground_color = scroll->font_color;
    if (prompt) {
        width = scroll->bottom - (string_width(prompt) + scroll->left) - 20;
        scroll_print(prompt);
    } else {
        width = scroll->bottom - (string_width(">") + scroll->left) - 20;
        scroll_print(">");
    }
    answer_start = scroll->cur_x;
    if (initial) {
        scroll_esc = 0;
        scroll_print(initial);
        scroll_esc = 1;
        strcpy(text, initial);
        pos = strlen(initial) * -1;
    } else
        pos = 0;
    text[abs(pos)] = 0;
    blink = 3000;
    x = answer_start + string_width(text);
    y = scroll->cur_y;
    mouse_hide();
    mouse_release(0);
    while ((key = mouse_get_input()) != 13 && key != 27 && key != 1 && key != 2 && key != 3) {
        strcpy(tmp, text);
        tmp[abs(pos)] = 0;
        x = answer_start + string_width(tmp);
        if (blink >= 2000 && blink < 4000) {
            set_the_color(scroll->font_color);
            rectangle(x, y, x + 4, y - cur_font->height + 1);
        } else if (blink == 4000) {
            set_the_color(0x2A);
            blink = 0;
            rectangle(x, y, x + 4, y - cur_font->height + 1);
            if (strlen(text) > pos)
                string_to_screen(text, answer_start, scroll->cur_y);
        }
        blink++;
        switch (key) {
        case -1:
            continue;
        case KEY_CTRL | 'k':
            if (pos < 0)
                pos = -pos;
            text[pos] = 0;
            break;
        case KEY_HOME: case KEY_GHOME: case KEY_CTRL | 'a':
            pos = 0;
            break;
        case KEY_END: case KEY_GEND: case KEY_CTRL | 'e':
            pos = strlen(text);
            break;
        case KEY_RIGHT: case KEY_GRIGHT: case KEY_CTRL | 'f':
            if (pos < 0)
                pos = -pos;
            if (strlen(text) > pos)
                pos++;
            break;
        case KEY_LEFT: case KEY_GLEFT: case KEY_CTRL | 'b':
            if (pos < 0)
                pos = -pos;
            if (pos > 0)
                pos--;
            break;
        case KEY_BACKSPACE:
            if (pos < 0)
                pos = -pos;
            if (pos > 0) {
                for (i = pos; i <= strlen(text); i++)
                    text[i - 1] = text[i];
                pos--;
            }
            break;
        case KEY_DEL: case KEY_CTRL | 'd':
            if (pos < 0)
                pos = -pos;
            if (strlen(text) > pos)
                for (i = pos; i < strlen(text); i++)
                    text[i] = text[i + 1];
            break;
        default:
            if (pos < 0) {
                pos = 0;
                text[0] = 0;
            }
            if (key >= 0x20 && key <= 0x7E && string_width(text) < width
                    && strlen(text) < maxlen && (anychar || isdigit(key))) {
                i = strlen(text);
                text[i + 1] = 0;
                for (; i > pos; i--)
                    text[i] = text[i - 1];
                text[pos] = key;
                pos++;
            }
            break;
        }
        set_the_color(0x2A);
        rectangle(answer_start, scroll->cur_y, scroll->bottom, scroll->cur_y - cur_font->height + 1);
        *foreground_color = scroll->font_color;
        string_to_screen(text, answer_start, scroll->cur_y);
    }
    if (blink >= 2000) {
        set_the_color(0x2A);
        rectangle(x, y, x + 4, y - cur_font->height + 1);
        if (strlen(text) > pos)
            string_to_screen(text, answer_start, scroll->cur_y);
    }
    mouse_show();
    if (key == KEY_ESC) {
        strcpy(result, NULLTRAP(initial));  /* initial is 0 for a conversation's answer */
        set_the_color(0x2A);
        rectangle(answer_start, scroll->cur_y, scroll->bottom, scroll->cur_y - cur_font->height + 1);
        scroll->cur_x = answer_start;
        scroll_print("-");
    } else {
        strcpy(result, text);
        scroll->cur_x = x;
    }
#ifndef __TURBOC__
    port_shared_story(0);
#endif
    return key;
}

/* Asks a yes or no question; see UW2's SCROLLIO.C. */
int far wyorn(char *question, int id, char *answer)
{
    char cur;
    char sel;
    int key;

#ifndef __TURBOC__
    port_shared_story(1);
#endif
    cur = *answer;
    sel = *answer;
    do_main_scroll();
    *foreground_color = scroll->font_color;
    if (question == 0)
        game_sprint(id);
    else
        scroll_print(question);
    answer_start = scroll->cur_x;
    if (*answer)
        scroll_print("Yes");
    else
        scroll_print("No");
    mouse_hide();
    mouse_release(0);
    while ((key = mouse_get_input()) != 13 && key != 27 && key != 1 && key != 2 && key != 3) {
        switch (key) {
        case 'Y': case 'y':
            sel = 1;
            break;
        case 'N': case 'n':
            sel = 0;
            break;
        default:
            continue;
        }
        if (sel != cur) {
            wd_bool(sel);
            cur = sel;
        }
    }
    mouse_show();
    if (key == KEY_ESC) {
        wd_bool(0);
        *answer = 0;
#ifndef __TURBOC__
        port_shared_story(0);
#endif
        return -1;
    }
    *answer = cur;
#ifndef __TURBOC__
    port_shared_story(0);
#endif
    return key;
}
