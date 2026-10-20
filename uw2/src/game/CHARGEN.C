/* target: ovr101 */
/* opts: -mm -1 -G -O -Y -d */
/* Character creation: a new player's starting record, the chargen screens (sex, hand,
   class, skills, portrait, difficulty, name, confirmation), the button pictures they are
   drawn from, and the entry point create_player: the whole of DOS overlay ovr101, in
   original order. Function and global names are the originals from the FM Towns symbol
   table.

   create_player (MAINMENU.C's start menu) blanks the record with init_char(1) and runs
   strt_chargen, which loads the background (screen image 1), the button pictures
   (CHRBTNS.GR), DATA\SKILLS.DAT and DATA\CHRGEN.DAT, then gen_char, which steps through
   eight questions: 0 sex, 1 handedness, 2 class, 3 skills, 4 portrait, 5 difficulty,
   6 name, 7 keep this character. Escape goes back to the start (or out, from the
   first question).
   Rules found here: choosing a class sets the attributes from SKILLS.DAT and spreads
   the class's bonus points over them (roll_stats, at most 30 each); each class then has
   five skill entries in SKILLS.DAT, each either a fixed skill or a menu of skills to
   pick one from, and each pick adds to that skill (SKILLS.C's add_to_skill).
   Data: sknow (skills chosen so far), chroff and chrbuf (the button pictures).
   SKILLS.DAT (191 bytes in the GOG data): 8 classes of 4 bytes (strength, dexterity,
   intelligence, bonus points), then for each class five entries, each a count and that
   many skill numbers. CHRGEN.DAT: 8 struct ChrOpt, then their answer string lists.
   Name: descriptive (character creation: strt_chargen, create_player). */

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <io.h>
#include <fcntl.h>
#include "combat.h"
#include "critter.h"
#include "file.h"
#include "gfx.h"
#include "inv.h"
#include "map.h"
#include "object.h"
#include "player.h"
#include "sound.h"
#include "sys.h"
#include "ui.h"
#include "view3d.h"

/* One chargen question from DATA\chrgen.dat, 0x12 bytes. */
HOST_LAYOUT_BEGIN
struct ChrOpt {
    int16 question;                     /* 0x00, string number, or 0 */
    char *name;                         /* 0x02, the name typed, for the name question */
    uint16 far *strings;                /* 0x04, the answers' strings, 0-terminated */
    int16 count;                        /* 0x08, how many answers */
    int16 pic;                          /* 0x0A, the button picture */
    int16 rows;                         /* 0x0C */
    int16 cols;                         /* 0x0E */
    int16 spacing;                      /* 0x10 */
};
HOST_LAYOUT_END

/* stdat holding DATA\SKILLS.DAT: four bytes a class, its three attributes and its skill points. */
#define CLASS_TAB ((unsigned char (far *)[4])stdat)
/* This file's _BSS, DS:47B8..47BF (ovr097's ends at 47B8). */
/* match: laid out by name: sknow 43, chroff 275, chrbuf 395. */
/* name: FM Towns keeps the three together too. */
int16 sknow;
uint16 *chroff;                         /* offsets of the button pictures in chrbuf: unsigned, as
                                           the female bodies start past 7FFFh (issue 8) */
unsigned char far *chrbuf;


/* The starting record of a new character. Fixed values: level 1, one skill point, the
   game clock at 0x465000, the time of day (X clock 0) at 15 of 72, the two moonstones
   on levels 3 and 45, hunger 0xC0, fatigue and food_heal 0x30, every quest, variable,
   X clock and lore entry zero, an empty rune shelf. Random: the portrait and sex, and
   unless blank every skill at 3d4 and every attribute at 2d10 + 10. Hit points start at
   the computed maximum less 6 to 11. */
void far init_char(char blank)
{
    register int i;

    SRAND(GAME_TIME());
    player->lefty = 1;
    player->exp = 0;
    player->skill_points = 1;
    player->skill_points_earned = 0;
    player->level = 1;
    player->map_scrap = 1;
    player->game_clock = 0x465000L;
    lastDurCheck = player->game_clock >> 8;
    player->xclock[XC_TIME] = 0xF;
    player->moonstones[0] = 3;
    player->moonstones[1] = 0x2D;
    player->automap = 1;
    player->b62_5 = 0;
    player->poison = 0;
    player->active_spells = 0;
    player->nrunes = 0;
    player->b60_11 = 0;
    player->drunk = 0;
    player->shrooms = 0;
    player->sleepbits = 0;
    player->in_void = 0;
    player->detail = 3;
    set_graphics_level();
    player->fps = 0;
    player->motion_state = 0;
    player->swim_count = 0;
    player->paralyzed = 0;
    player->fatigue = 0x30;
    player->food_heal = 0x30;
    player->b3C = 0;
    player->bF6 = 8;
    player->dreamflags = 0;
    memset(player->quests, 0, 0x10);
    memset(player->quest_bytes, 0, 0x10);
    memset(player->shelf, RUNE_NONE, 3);
    memset(player->runebag, 0, 3);
    memset(player->vars, 0, 0x100);
    memset(player->lore, 0, 0x50);
    memset(player->xclock, 0, 0x10);
    player->hunger = 0xC0;
    player->body = rand() % 5;
    player->female = rand() & 1;
    for (i = 0; i < NUM_SKILLS; i++)
        player->skills[i] = blank ? 0 : rollem(3, 4);
    for (i = 0; i < 3; i++)
        playerdat->attr[i] = blank ? 0 : rollem(2, 10) + 10;
    player_compute(1);
    player->weight = 0;
    ThePlayer->hp = playerdat->avghit - 6 - rand() % 6;
    PlayerLevel = 1;
    FixPlayerEquips();
}

/* Walks the class's five skill entries in SKILLS.DAT from entry *idx: an empty entry
   gives no skill (NUM_SKILLS), a one-skill entry gives that skill at once, and an entry
   of several skills becomes the skill menu (opt, their names as string numbers 0x1F
   on) and returns 1 to ask the player. Returns 0 when the five are done. */
unsigned char far set_sklmnu(unsigned char *idx, unsigned char *skills, struct ChrOpt far *opt,
                    unsigned char far *dat)
{
    int n;
    unsigned char cls;
    register int pos;
    register int i;

    cls = player->pclass;
    for (n = 0, pos = 0; cls * 5 + *idx > n; n++)
        pos += dat[pos] + 1;
    for (; *idx < 5; (*idx)++) {
        if (dat[pos] == 0)
            skills[*idx] = NUM_SKILLS;
        else if (dat[pos] == 1) {
            skills[*idx] = dat[pos + 1];
            pos += dat[pos] + 1;
        } else {
            opt->count = dat[pos];
            for (i = 0; dat[pos] > i; i++)
                opt->strings[i] = dat[pos + i + 1] + 0x1F;
            (*idx)++;
            return 1;
        }
    }
    return 0;
}

void far show_atts(void)
{
    char buf[10];

    set_the_color(0x106);
    rectangle(0x5D, 0x96, 0x8C, 0x4E);
    itoa(playerdat->attr[0], buf, 10);
    string_to_screen("Str:", 0x5D, 0x96);
    string_to_screen(buf, 0x8C - string_width(buf), 0x96);
    itoa(playerdat->attr[1], buf, 10);
    string_to_screen("Dex:", 0x5D, 0x84);
    string_to_screen(buf, 0x8C - string_width(buf), 0x84);
    itoa(playerdat->attr[2], buf, 10);
    string_to_screen("Int:", 0x5D, 0x72);
    string_to_screen(buf, 0x8C - string_width(buf), 0x72);
    itoa(playerdat->avghit, buf, 10);
    string_to_screen("Vit:", 0x5D, 0x60);
    string_to_screen(buf, 0x8C - string_width(buf), 0x60);
}

void far show_skills(void)
{
    char buf[10];
    char far *name;
    register int i;
    register int row;

    set_the_color(0x106);
    rectangle(0x1E, 0x43, 0x7D, 0x0C);
    row = 0;
    for (i = 0; i < 20 && row <= 5; i++) {
        if (player->skills[i] != 0) {
            name = get_string((i + 0x1F) | STR_CHARGEN);
            itoa(player->skills[i], buf, 10);
            string_to_screen(name, 0x1E, 0x43 - row * 11);
            string_to_screen(buf, 0x7D - string_width(buf), 0x43 - row * 11);
            row++;
        }
    }
}

/* Adds each chosen skill from skills[start] on (add_to_skill) and returns the new count
   done. */
int far set_skills(register int start, unsigned char *skills)
{
    register int i;

    for (i = start; i < 6; i++)
        if (skills[i] < NUM_SKILLS) {
            add_to_skill(skills[i]);
            start++;
        }
    return start;
}

/* The class's attributes: the three bases from SKILLS.DAT, then its bonus points given
   out 1 to 4 at a time to a random attribute, none above 30. Skills are cleared, and
   the maximum HP, mana and weight are recomputed with HP full. */
void far roll_stats(void)
{
    int pts;
    register int i;
    register int a;

    for (i = 0; i < 3; i++)
        playerdat->attr[i] = CLASS_TAB[player->pclass][i];
    for (i = 0; i < NUM_SKILLS; i++)
        player->skills[i] = 0;
    i = CLASS_TAB[player->pclass][3];
    while (i > 0) {
        pts = (rand() & 3) + 1;
        if (pts > i)
            pts = i;
        a = rand() % 3;
        if (playerdat->attr[a] + pts > 30)
            pts = 30 - playerdat->attr[a];
        playerdat->attr[a] += pts;
        i -= pts;
    }
    player_compute(1);
    ThePlayer->hp = playerdat->avghit;
}

/* Draws a question and its answer buttons, laid out in as many columns as fit in the
   right half of the screen. Button picture 0 carries the answer's text, picture 3 a
   picture (the portraits). */
void far drawopt(struct ChrOpt far *opt)
{
    int h, w, i;
    register int x;
    register int y;

    if (opt->strings != 0) {
        unsigned char far *pic;
        char hasq;

        pic = chrbuf + chroff[opt->pic];
        hasq = opt->question != 0 ? 1 : 0;
        set_cuts_ems(0);
        w = pic[-4];
        h = pic[-3];
        opt->cols = (opt->count * (h + 4) - 4) / (0xC4 - (hasq ? 20 : 0)) + 1;
        opt->rows = (opt->count + opt->cols - 1) / opt->cols;
        opt->spacing = (0xA0 - opt->cols * w) / (opt->cols + 1);
        y = 0xC5 - (0xC4 - (opt->rows + hasq) * (h + 4) + 4) / 2;
    } else
        y = 0x6A;
    if (opt->question != 0) {
        char far *s;

        s = get_string(opt->question | STR_CHARGEN);
        x = 0xA4;
        if (opt->name != 0) {
            show(x, y, chrbuf + chroff[6], 0x10, 0x91, 0, 0);
            string_to_screen(s, x += 4, y - 3);
        } else
            string_to_screen(s, x + (0x91 - string_width(s)) / 2, y - 3);
    } else
        y += h + 4;
    if (opt->strings != 0) {
        Transparency = 0;
        set_cuts_ems(0);
        for (i = 0; opt->count > i; i++) {
            if (i % opt->cols == 0) {
                x = 0xA0 - w;
                y -= h + 4;
            }
            x += opt->spacing + w;
            show(x, y, chrbuf + chroff[opt->pic], h, w, 0, 0);
            switch (opt->pic) {
            case 0: {
                char far *s;

                s = get_string(opt->strings[i] | STR_CHARGEN);
                string_to_screen(s, x + (w - string_width(s)) / 2, y - 3);
                break;
            }
            case 3: {
                int k;

                k = opt->strings[0] + i;
                Transparency = 1;
                show(x, y, chrbuf + chroff[k], h, w, 0, 0);
                Transparency = 0;
                break;
            }
            }
        }
    }
}

void far selopt(struct ChrOpt far *opt, unsigned char new, unsigned char old)
{
    int xbase, ybase, h;
    unsigned char far *pic;
    char hasq;
    unsigned char sel[2];
    int x, y;
    register int j;
    register int w;

    pic = chrbuf + chroff[opt->pic];
    hasq = opt->question != 0 ? -1 : 0;
    if (new == old)
        return;
    sel[0] = old;
    sel[1] = new;
    w = pic[-4];
    h = pic[-3];
    xbase = opt->spacing + 0xA0;
    ybase = 0xC5 - (0xC4 - (opt->rows + hasq) * (h + 4) + 4) / 2;
    for (j = 0; j < 2; j++) {
        if (sel[j] < 0)                 /* match: never true, but compiled (a jae over a
                                           jmp) */
            continue;
        if (opt->count <= sel[j])
            continue;
        {
            y = ybase - sel[j] / opt->cols * (h + 4);
            x = xbase + sel[j] % opt->cols * (w + opt->spacing);
            pic = chrbuf + chroff[opt->pic + 2 - j];
            mouse_hide();
            set_cuts_ems(0);
            show(x, y, pic, h, w, 0, 0);
            switch (opt->pic) {
            case 0: {
                char far *s;

                s = get_string(opt->strings[j ? new : old] | STR_CHARGEN);
                string_to_screen(s, x + (w - string_width(s)) / 2, y - 3);
                break;
            }
            case 3: {
                int k;

                k = opt->strings[0] + (j ? new : old);
                Transparency = 1;
                show(x, y, chrbuf + chroff[k], h, w, 0, 0);
                break;
            }
            }
            mouse_show();
        }
    }
    Transparency = 0;
}

/* Follows the mouse while a button is held, highlighting the answer under it. Returns
   the answer released on, or -1 - cur when released over none. */
/* name: called jname_input in the target table, but this is FM Towns mousopt: it calls
   selopt, mouse_getxy and mouse_get_input, and pickopt calls it on a mouse button. FM
   Towns jname_input is the Japanese name entry and has no DOS counterpart. */
int far mousopt(struct ChrOpt far *opt, int cur)
{
    int16 mx, my, h, w, sel, hstep, wstep, q;
    unsigned char far *pic;
    register int xleft;
    register int ytop;

    sel = cur;
    q = opt->question;
    pic = chrbuf + chroff[opt->pic];
    w = pic[-4];
    h = pic[-3];
    wstep = w + opt->spacing;
    hstep = h + 4;
    xleft = opt->spacing + 0xA0;
    ytop = 0xC5 - (0xC4 - (opt->rows + (q != 0)) * (h + 4) + 4) / 2 - (q ? hstep : 0);
    while (mouse_get_input() > 0) {
        if (sel != cur && sel != -1) {
            mouse_hide();
            selopt(opt, sel, cur);
            mouse_show();
            cur = sel;
        }
        mouse_getxy(&mx, &my);
        sel = (ytop - my) / hstep * opt->cols + (mx - xleft) / wstep;
        if (sel < 0 || opt->count <= sel || my > ytop || mx < xleft)
            sel = -1;
        else {
            mx -= xleft + wstep * (sel % opt->cols);
            my = ytop - my - hstep * (sel / opt->cols);
            if (mx >= w || my >= h)
                sel = -1;
        }
    }
    if (sel == -1)
        sel = -1 - cur;
    return sel;
}

/* Gets one answer. For the name question it reads typed characters (up to 29, leading
   spaces skipped, backspace deletes) until Enter with a name not empty. Otherwise the
   mouse or the cursor keys move the selection and Enter or a click chooses; Escape or
   Alt+X returns -1. */
int far pickopt(struct ChrOpt far *opt)
{
    int key;
    int sel = 0;
    int old = 0;
    unsigned char done = 0;
    char empty = 1;

    if (opt->question != 0 && opt->name != 0) {
        int c;
        char buf[2];
        char far *s;
        int y;
        register int len;
        register int x;

        len = 0;
        s = get_string(opt->question | STR_CHARGEN);
        x = string_width(s) + 0xA8;
        y = 0x6B;
        buf[1] = 0;
        while ((c = mouse_get_input()) != '\r' || empty) {
            if (isspace(c) && len == 0)
                continue;
            if (c >= ' ' && c <= '~' && x < 0x12E && len < 0x1D) {
                buf[0] = c;
                mouse_hide();
                string_to_screen(buf, x, y - 3);
                mouse_show();
                x += string_width(buf);
                opt->name[len] = c;
                len++;
                empty = 0;
            } else if ((c == 8 || c == 0x91) && len > 0) {
                len--;
                buf[0] = opt->name[len];
                x -= string_width(buf);
                mouse_hide();
                show(x, y, chrbuf + chroff[6], 0x10, 0x91, x - 0xA4, 0);
                mouse_show();
                if (len == 0)
                    empty = 1;
            }
        }
        opt->name[len] = 0;
        return 0;
    }
    while (!done) {
        do
            loop_music_maybe();
        while ((key = mouse_get_input()) < 0);
        switch (key) {
        case 1:
        case 2:
        case 3:
            sel = mousopt(opt, sel);
            done = sel > -1;
            if (sel < 0)
                sel = -1 - sel;
            old = sel;
            break;
        case 0x93:
        case 0xAB:
        case 0x16E:
            sel += opt->cols;
            break;
        case 0x8D:
        case 0xA6:
        case 0x170:
            sel -= opt->cols;
            break;
        case 0x91:
        case 0xA9:
        case 0x166:
            sel++;
            break;
        case 0x8F:
        case 0xA8:
        case 0x162:
            sel--;
            break;
        case 0x8C:
        case 0x8E:
        case 0xA5:
        case 0xA7:
        case 0x23C:
            sel = 0;
            break;
        case 0x92:
        case 0x94:
        case 0xAA:
        case 0xAC:
        case 0x23E:
            sel = opt->count - 1;
            break;
        case '\r':
            done = 1;
            break;
        case 0x1B:
        case 0x278:
            return -1;
        }
        if (sel < 0)
            sel = 0;
        else if (opt->count <= sel)
            sel = opt->count - 1;
        else
            selopt(opt, sel, old);
        old = sel;
    }
    return sel;
}

/* Runs the eight questions (see the file comment) and fills in the record: sex (which
   also picks the portrait set, pictures 7 or 12), lefty, class with its attributes and
   fixed skills, the skill picks, the portrait (body), easy, the name (at most 29
   characters), and on "Yes" to keeping the character the final HP and mana. "No"
   starts again. Returns 0 if Escape is pressed at the first question. */
char far gen_char(unsigned char far *buf, unsigned char far *dat, struct ChrOpt far *opts)
{
    int x, y, w, h;
    int choice;
    unsigned char idx;
    unsigned char skills[6];
    unsigned char far *dat32;
    char far *done;
    uint16 far *strs;
    char far *s;
    char name[30];
    register int stage;
    register int k;

    stage = 0;
    idx = 0;
    dat32 = dat + 0x20;
    memset(skills, 20, 6);
    opts[6].name = name;
    mouse_hide();
    copy_hidden_to_visible();
    mouse_show();
    while (stage < 8) {
        strs = opts[stage].strings;
        mouse_hide();
        grSoftPageFlip();
        set_the_color(0x106);
        rectangle(0x11, 0, 0x8E, 0xC7);
        drawopt(&opts[stage]);
        selopt(&opts[stage], 0, 0xFF);
        grPageFlip();
        set_cuts_ems(1);
        show(0, 0xC7, chrbuf, 0xC8, 0x140, 0, 0);
        grSoftPageFlip();
        mouse_show();
        set_cuts_ems(0);
        choice = pickopt(&opts[stage]);
        if (choice < 0) {
            if (stage == 0)
                return 0;
            mouse_hide();
            copy_visible_to_hidden();
            goto restart;
        }
        switch (stage) {
        case 0:
            s = get_string(strs[choice] | STR_CHARGEN);
            opts[4].strings[0] = choice ? 12 : 7;
            player->female = choice;
            mouse_hide();
            string_to_screen(s, 0x11, 0xB2);
            mouse_show();
            stage++;
            break;
        case 1:
            player->lefty = 1 - choice;
            stage++;
            break;
        case 2:
            s = get_string(strs[choice] | STR_CHARGEN);
            player->pclass = choice;
            roll_stats();
            if (!set_sklmnu(&idx, skills, &opts[3], dat32))
                stage++;
            sknow = set_skills(0, skills);
            mouse_hide();
            string_to_screen(s, 0x8F - string_width(s), 0xB2);
            show_atts();
            show_skills();
            mouse_show();
            stage++;
            break;
        case 3:
            skills[idx - 1] = opts[3].strings[choice] + 0xE1;
            sknow = set_skills(sknow, skills);
            mouse_hide();
            show_skills();
            mouse_show();
            if (!set_sklmnu(&idx, skills, &opts[3], dat32))
                stage++;
            break;
        case 4:
            Transparency = 1;
            k = chroff[player->female * 5 + choice + 0x11];
            set_cuts_ems(0);
            w = buf[k - 4];
            h = buf[k - 3];
            x = (0x41 - w) / 2 + 0x0F;
            y = 0xA5 - (0x5E - h) / 2;
            mouse_hide();
            show(x, y, buf + k, h, w, 0, 0);
            mouse_show();
            Transparency = 0;
            player->body = choice;
            stage++;
            break;
        case 5:
            player->easy = choice;
            stage++;
            break;
        case 6:
            s = opts[6].name;
            mouse_hide();
            string_to_screen(s, (0x7E - string_width(s)) / 2 + 0x11, 0xBD);
            mouse_show();
            if (*s)
                strncpy(player->name, (char *)s, 29);
            player->name[29] = 0;
            stage++;
            break;
        case 7:
            if (choice == 0) {
                stage++;
                player_compute(1);
            } else {
                mouse_hide();
                set_the_color(0x106);
                rectangle(0x11, 0xC7, 0x8F, 0);
restart:
                mouse_show();
                stage = sknow = idx = 0;
                memset(skills, 20, 6);
            }
            break;
        }
    }
    done = get_string(0x310);           /* "awakens . . ." */
    mouse_hide();
    set_cuts_ems(1);
    grSoftPageFlip();
    show(0, 0xC7, chrbuf, 0xC8, 0x140, 0, 0);
    string_to_screen(name, (0xA0 - string_width(name)) / 2 + 0xA0, cur_font->height + 0x62);
    string_to_screen(done, (0xA0 - string_width(done)) / 2 + 0xA0, 0x62);
    grSoftPageFlip();
    set_the_color(0x106);
    rectangle(0xA0, 0, 0x13F, 0xC7);
    return 1;
}

/* gronk_gr's callbacks while loading the chargen buttons: where to put the next picture,
   and recording its offset. */
unsigned char far * far adr_chrpic(int size)
{
    unsigned char far *p;

    p = chrbuf;
    chrbuf += size;
    return p;
}

int far move_chrpic(unsigned char far *p, int size, register int n)
{
    if (n == 0)
        chroff[0] = 5;
    chroff[n + 1] = chroff[n] + size;
    return size != 0;
}

/* Loads the chargen screen and its data and runs gen_char. A missing file is fatal
   (error 5). */
char far strt_chargen(void)
{
    int n;
    unsigned char far *pic;
    unsigned char far *pal;
    unsigned char far *buf;
    uint16 far *strs;
    struct ChrOpt far *opts;
    unsigned char result;
    unsigned char ok;
    uint16 offs[28];
    register int i;
    register int fd;

    buf = (unsigned char far *)stdat;
    get_cuts_ems();
    pic = chrbuf = set_cuts_ems(1);
    set_cuts_ems(1);
    pal = pic + 0xFA00;
    mouse_hide();
    ok = disk_to_vid(1, pic);
    ok &= read_quikpal(PAL_CHARGEN, pal);
    set_cuts_ems(0);
    chroff = offs;
    if (!gronk_gr("chrbtns", 0, -1, (ArtAllocFn)adr_chrpic, (ArtMoveFn)move_chrpic))
        goto fail;
    chrbuf = pic;
    if ((fd = open("DATA\\skills.dat", O_RDONLY | O_BINARY)) == -1)
        goto fail;
    n = intoFarBuffer_ovr167_5DA(fd, stdat, 0x348);
    close(fd);
    if (n < 0x28 || n == -1)
        goto fail;
    if ((fd = open("DATA\\chrgen.dat", O_RDONLY | O_BINARY)) == -1)
        goto fail;
    intoFarBuffer_ovr167_5DA(fd, buf + n, 10000);
    close(fd);
    opts = FILE_RECORDS(struct ChrOpt, buf + n, 8, "wnfwwwww");
    strs = (uint16 far *)FILE_RECORDS_END(opts, 8);
    for (i = 0; i < 8; i++) {
        opts[i].strings = strs;
        while (*strs++ != 0)
            ;
    }
    grfx_quikfont(FONT_CHAR);
    *foreground_color = *background_color = 0xC4;
    set_cuts_ems(0);
    drawopt(opts);
    selopt(opts, 0, 0xFF);
    mouse_show();
    set_cuts_ems(1);
    if (!ok)
        goto fail;
    fadein(pal, 2, 0);
    result = gen_char(chrbuf, buf, opts);
    grfx_quikfont(FONT_5X6P);
    free_cuts_ems();
    if (in_game)
        load_txtmaps();
    if (!result) {
        set_cuts_ems(1);
        ok &= read_quikpal(PAL_CHARGEN, pal);
        fadeout(pal, 2, 0);
    }
    return result;
fail:
    free_cuts_ems();
    if (in_game)
        load_txtmaps();
    init_char(0);
    grfx_clear();
    pfatal_code(5);
    return 1;
}

char far create_player(void)
{
    char r;

    init_char(1);
    r = strt_chargen();
    load_weapcm();
    return r;
}

/* Puts obj in inventory slot slot, if there is an object. Not referenced in DOS. */
/* name: has no FM Towns counterpart. The name is provisional (IDA's ovr101_18CB), chosen
   so that its tools/bssorder.py key puts it in the EXE's overlay stub order. */
void far AddObjIfAny_ovr101_18CB(struct Object far *obj, int slot)
{
    if (obj != 0)
        AddToInventory(obj, slot);
}
