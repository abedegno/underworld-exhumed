/* target: ovr092 */
/* opts: -mm -1 -G -O -Y -d */
/* The automap: drawing the player's map of each level and the map notes the player
   writes on it. The whole of UW1's DOS overlay ovr092, in original order.

   What it does in the game: PlayersMap is the map the player has made of the current
   level, a byte per tile, written by GRIDDB.C as the player sees tiles: bits 0..3 the
   tile type (0 never seen), bits 4..5 the floor's terrain (TxmTerr), which picks the
   shading, and bits 6..7 GRIDDB.C's curautocode: 1 a door, 2 and 3 drawn in the 0xE9..
   colours or dark (DoTile). Each level's map is block 0x1A + level of the save
   directory's LEV.ARK and its notes block 0x23 + level, up to 100 notes of 0x36 bytes
   (struct ATM). AutoMap opens the map screen (newscr; ExitAutoMap leaves it);
   ManageDungeonMap handles clicks: writing a note anywhere on the map, erasing one with
   the eraser, the up and down arrows for the levels, and the exit.

   UW1's differences from UW2's ovr094: no map scraps, gem or worlds (no automap_area,
   true_gem_region, show_gem_parts, map_section, update_map_scraps,
   make_terrain_unseen); the archive is UW1's ovr091, and SaveAutoMapLevel and
   GetAutoMapLevel take an open archive (struct Arc) rather than flags; GetTheWords
   draws the notes itself; the workspace for the map's background is lent by
   CRPAGES.C (swap_ws_out, unswap_ws); fonts by file name; no player position marker on
   level 9.

   Data owned: PlayersMap, the level shown and the drawing tables; the notes,
   ATM_Strings, are FARDATA's.
   Function and global names are UW2's (FM Towns), the routines being the same; UW1 has
   no symbol-bearing build. Every function is public, and their names' bssorder keys
   give the EXE's overlay stub order (verify.py agrees). symbols.tsv's provisional
   ovr092_97, ovr092_11E and ovr092_1D0 do not (keys 631, 631, 783 against the 85..138,
   36..85 and 525..731 their stub entries need); SaveAutoMapLevel (123),
   GetAutoMapLevel (47) and ClearAutoMap (587) do.
   Name: inferred (AutoMap is its first function, and System Shock's map display is
   AUTOMAP.C). */

#include <dos.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "event.h"
#include "file.h"
#include "gfx.h"
#include "map.h"
#include "object.h"
#include "player.h"
#include "sound.h"
#include "sys.h"
#include "ui.h"

/* Declared in each file that uses it, its own way (no header). */
unsigned char far bltfromdrive(char *name, void far *buf, unsigned n);
void far grfx_clear(void);

char far open_arc(struct Arc far *arc, char *name);           /* opens an archive into arc */
char far close_arc(struct Arc far *arc);                     /* closes it */
int far get_arc(struct Arc far *arc, int block, void far *buf);           /* reads a block */
char far put_arc(struct Arc far *arc, int block, void far *buf, int len); /* writes one */
/* UW1: CRPAGES.C lends the critter pages to hold the workspace (UW2's get_workspace and
   release_workspace). */
/* UW1: do_beep, called with (0x12C, 0xA) when a note will not take another
   character. */
/* UW1: seg015's pixel plot (UW2's gr_pixel). */
unsigned char far grfx_load_font(char *name);

/* Initialised data, DS:0ACA (the strings follow, to DS:0B49). */
/* name: none of it has an FM Towns name, so it was static. */

/* How each solid tile type is shaded, a 3x3 pattern per type: 1 shades the pixel with the
   tile's terrain, 2 darkens it. */
static unsigned char tile_pattern[5][3][3] = {
    { { 1, 1, 1 }, { 1, 1, 1 }, { 1, 1, 1 } },
    { { 2, 1, 1 }, { 0, 2, 1 }, { 0, 0, 2 } },
    { { 1, 1, 2 }, { 1, 2, 0 }, { 2, 0, 0 } },
    { { 0, 0, 2 }, { 0, 2, 1 }, { 2, 1, 1 } },
    { { 2, 0, 0 }, { 1, 2, 0 }, { 1, 1, 2 } },
};
static int16 registered = 0;            /* the automap's input handlers are in place */
static signed char diag_side[4] = { 1, 2, 0, 3 };
static signed char door_dy[4] = { -1, 0, -1, 1 };
static signed char door_dx[4] = { 0, -1, -1, -1 };

/* Uninitialised data, DS:381C (PlayersMap DS:3820): the drawing state, the level shown, the note counts and
   the player's map. */
/* match: Turbo C lays _BSS out in an order set by the names, so the static names (none
   has an FM Towns name) were chosen by compiling probes to land where the EXE has them:
   door_dir, notes_dirty, level, old_strings, num_words and map_mouse are ours. */
static signed char door_dir;
static char notes_dirty;               /* UW1: char (cbw) */
static int16 level;
unsigned char PlayersMap[MAP_SIZE][MAP_SIZE];
static int16 old_strings;
static int16 num_words;
static int16 map_mouse;

/* Opens the map screen: registers its key handler once, plays theme 0xD, saves the
   current level's map and shows it, and adds the mouse handler (map_mouse). */
#ifndef __TURBOC__
void far port_map_key(int how);
#endif

void far AutoMap(void)
{
    if (!registered) {
        _input_addkey(KEY_ESC, MODE_GAME, MODE_MAP, (InputFn)newscr);
#ifndef __TURBOC__
        if (ENHANCED(ENH_MODERN_KEYS)) {     /* port only: modern-keys' map keys */
            _input_addkey('s', 1, MODE_MAP, (InputFn)port_map_key);
            _input_addkey('w', -1, MODE_MAP, (InputFn)port_map_key);
            _input_addkey('c', 2, MODE_MAP, (InputFn)port_map_key);
        }
#endif
        registered = 1;
    }
    set_new_music(MUSIC_MAPS);
    change_music_maybe();
    SaveAutoMapLevel(0, PlayerLevel);
    ShowAutoMapLevel(PlayerLevel);
    map_mouse = input_addmouse(0, 0, 0x13F, 0xC7, 0, MODE_MAP, (InputFn)ManageDungeonMap);
    mouse_constrain(0x16, 7, 0x13F, 0xC7);
    mouse_hide();
    force_mouse_cursor(ICON_CURSORS + 0xC);
    mouse_show();
    notes_dirty = 0;
}

/* Writes PlayersMap as level lev's automap block (0x1A + lev of LEV.ARK in the save
   directory), through the archive arcp, or opening and closing it when arcp is 0. */
unsigned char far SaveAutoMapLevel(struct Arc *arcp, int lev)
{
    char ok = 0;
    struct Arc arc;

    if (arcp == 0) {
        if (!open_arc(&arc, "SAVE0\\lev.ark"))
            return 0;
    } else
        arc = *arcp;
    ok = put_arc(&arc, LEVARK_AUTOMAP(lev), PlayersMap, MAP_TILES);
    if (arcp == 0)
        close_arc(&arc);
    else
        *arcp = arc;
    if (ok)
        return 1;
    return 0;
}

/* Reads level lev's automap into PlayersMap from the open archive arc. */
unsigned char far GetAutoMapLevel(struct Arc *arc, int lev)
{
    register int n;

    n = get_arc(arc, LEVARK_AUTOMAP(lev), PlayersMap);
    if (n != 0 && n != MAP_TILES)
        return 0;
    return 1;
}

/* Leaves the map screen: removes the mouse handler, saves the notes, reloads the
   current level's map if another was shown, and restores the level's music, the game
   palette and the pointer. */
void far ExitAutoMap(void)
{
    struct Arc arc;

    mouse_hide();
    input_del(map_mouse);
    unforce_mouse_cursor(0);
    SaveTheWords(level);
    if (level != PlayerLevel && open_arc(&arc, "SAVE0\\lev.ark")) {
        GetAutoMapLevel(&arc, PlayerLevel);
        close_arc(&arc);
    }
    seg014_1DC5_15C5();
    grfx_clear();
    grfx_quikpal(PAL_GAME);
    mouse_freereign();
    mouse_show();
}

void far ClearAutoMap(void)
{
    memset(PlayersMap, 0, MAP_TILES);
}

/* Draws every seen tile, three pixels a tile, and shades its sides that face unseen or
   solid neighbours. */
void far ShowDungeonMap(void)
{
    int x;
    int y;
    int t;
    int px;
    int py;
    char side[4];
    register int i;
    register int d;

    for (y = 1; y < 0x3F; y++) {
        for (x = 1; x < 0x3F; x++) {
            t = PlayersMap[y][x] & 0xF;
            if (t <= 0 || t >= 10)
                continue;
            DoTile(t, x, y);
            memset(side, 0, 4);
            if (tile_walls[t] & TW_DIAG) {
                d = t - 2;
                d = diag_side[d];
                side[d] = ShadeSide(d, x, y);
                d = (d + 1) & 3;
                side[d] = ShadeSide(d, x, y);
            } else {
                for (i = 0; i < 4; i++)
                    side[i] = ShadeSide(i, x, y);
            }
            px = x * 3 + 10;
            py = y * 3 + 7;
            for (i = 0; i < 4; i++)
                if (side[i] && side[(i + 1) & 3])
                    PixelDarken(px - ((i & 2) << 1), py - ((i & 2) << 1), 3, 2);
        }
    }
}

/* Shades one side of the tile at x, y (0 south, 1 east, 2 north, 3 west, inferred) if the
   neighbour there is not an open tile; a neighbour of type 11 gets a lighter edge. Returns
   1 if it drew. */
char far ShadeSide(int side, int x, int y)
{
    int t;
    int px;
    int a;
    int b;
    register int i;
    register int py;

    px = (x - 1) * 3 + 10;
    py = (y - 1) * 3 + 7;
    switch (side) {
    case 0: y++; break;
    case 1: x++; break;
    case 2: y--; break;
    case 3: x--; break;
    }
    t = PlayersMap[y][x] & 0xF;
    if (t > 0 && t < 10)
        return 0;
    if (t == 11) {
        a = 0;
        b = 4;
    } else {
        a = 6;
        b = 2;
    }
    switch (side) {
    case 0:
        py += 4;
    case 2:
        py--;
        for (i = 0; i < 3; i++)
            PixelDarken(px + i, py, a, b);
        break;
    case 1:
        px += 4;
    case 3:
        px--;
        for (i = 0; i < 3; i++)
            PixelDarken(px, py + i, a, b);
        break;
    }
    return 1;
}

/* Adds base plus a random 0..n-1 to the colour at x, y: a rough, hand-drawn darkening. */
void far PixelDarken(int x, register int y, int base, unsigned n)
{
    register int d;

    d = 0x7FFF / n;
    *pixel_color = gr_read_pixel(x, y);
    *pixel_color += base + rand() / d;
    seg015_1F9B_25A(x, y, base + rand() / d + gr_read_pixel(x, y));
}

/* Draws one tile from its PlayersMap byte: the pattern of its type (diagonals half
   filled), coloured by the terrain (bits 4..5), then the kind (bits 6..7): dark, the
   0xE9.. colours, or a door. */
void far DoTile(int type, int x, int y)
{
    int px;
    int py;
    unsigned char shade;
    unsigned char kind;
    unsigned char b;
    register unsigned j;
    register unsigned i;

    if (type == 0)
        return;
    b = PlayersMap[y][x] >> 4;
    shade = b & 3;
    kind = b & 0xFC;
    px = (x - 1) * 3 + 10;
    py = (y - 1) * 3 + 7;
    if (type >= 6)
        type = 1;
    type--;
    for (i = 0; i < 3; i++) {
        for (j = 0; j < 3; j++) {
            switch (tile_pattern[type][j][i]) {
            case 1:
                switch (shade) {
                case 0:
                    PixelDarken(px + i, py + j, 2, 3);
                    break;
                case 1:
                    seg015_1F9B_25A(px + i, py + j, rand() % 2 + 0xB1);
                    break;
                case 2:
                    seg015_1F9B_25A(px + i, py + j, rand() % 2 + 0xB5);
                    break;
                }
                break;
            case 2:
                PixelDarken(px + i, py + j, 6, 2);
                break;
            }
        }
    }
    switch (kind) {
    case 0xC:
    case 0x10:
        for (i = 0; i < 3; i++)
            for (j = 0; j < 3; j++)
                PixelDarken(px + i, py + j, 6, 3);
        break;
    case 8:
        for (j = 0; j < 3; j++)
            for (i = 0; i < 3; i++)
                seg015_1F9B_25A(px + i, py + j, (int)((int32)rand() * 3 / 0x8000L) + 0xE9);
        break;
    case 4:
        DoDoorTile(x, y, px, py);
        break;
    }
}

/* Draws a door: a dark centre and two dark pixels across the passage, turned to face the
   open neighbours. */
void far DoDoorTile(int x, int y, register int px, register int py)
{
    PixelDarken(++px, ++py, 6, 3);
    for (door_dir = 0; door_dir < 4; door_dir++) {
        if ((PlayersMap[y + door_dy[door_dir]][x + door_dx[door_dir]] & 0xF) == 1
            || (PlayersMap[y - door_dy[door_dir]][x - door_dx[door_dir]] & 0xF) == 1) {
            PixelDarken(px + door_dy[door_dir], py + door_dx[door_dir], 6, 3);
            PixelDarken(px - door_dy[door_dir], py - door_dx[door_dir], 6, 3);
            break;
        }
    }
}

/* Darkens the pixel at one corner of a tile. Nothing calls it. */
/* name: DOS only; the name is provisional (IDA's ovr094_9D2), chosen so that its
   tools/bssorder.py key puts it in the EXE's overlay stub order. */
void far CornerShade_ovr094_9D2(int corner, int x, int y)
{
    register int px;
    register int py;

    px = x * 3 + 10 - (((corner + 1) & 1) << 2);
    py = (y - 1) * 3 + (corner >> 1 << 1) + 7;
    PixelDarken(px, py, 6, 2);
}

/* Of two notes under the mouse, the one whose middle is nearer. */
struct ATM far * far DetermineBetterHit(struct ATM far *a, struct ATM far *b, int x, int y)
{
    int ax;
    int bx;
    int ay;
    register int w;
    register int by;
    char buf[50];

    if (a == 0)
        return b;
    if (b == 0)
        return a;
    str_copy(buf, a->text);
    w = string_width(buf);
    ax = abs(a->x + w / 2 - x);
    ay = abs(a->y - 2 - y);
    str_copy(buf, b->text);
    w = string_width(buf);
    bx = abs(b->x + w / 2 - x);
    by = abs(b->y - 2 - y);
    if (by < ay)
        return b;
    if (bx < ax)
        return b;
    return a;
}

/* The map screen's mouse handler (see the file comment). Notes are typed in capitals in
   the small font, up to 46 characters and the screen edge, ended by Enter, Escape or a
   click. The level arrows move between levels 1 and 0x63 (ChangeAutoMapLevel loads
   only levels below 9). */
void far ManageDungeonMap(void)
{
    int16 mx;
    int16 my;
    int w;
    int i;
    int16 but;
    struct ATM far *note;
    struct ATM far *best;
    char ch[2];
    int curx;
    int hit;
    char text[50];
    register int cmd;
    register int len;

    *foreground_color = 0x2D;
    *background_color = 0x2D;
    mx = inplist->x - 0x16;
    my = inplist->y - 7;
    cmd = inplist->cmd;
    if (cmd >= 4)
        return;
    mouse_release(1);
    if (mx > 0x104 && mx < 0x140 && my > 0x13 && my < 0x33) {
        cmd = 0xFF;
        newscr(MODE_GAME);
    } else if (mx > 0x104 && mx < 0x140 && my > 0x33 && my < 0x52) {
        cmd = 0xFD;
        mouse_constrain(0, 0, 0x13F, 0xC7);
        mouse_hide();
        force_mouse_cursor(ICON_CURSORS + 0xD);
        mouse_show();
        while (mouse_get_input() != 1)
            ;
        mouse_getxy(&mx, &my);
        mouse_constrain(0x16, 7, 0x13F, 0xC7);
        unforce_mouse_cursor(1);
    } else if (mx > 0x113 && mx < 0x140 && my > 0xB7 && my < 0xC7)
        cmd = 0xFC;
    else if (mx > 0x113 && mx < 0x140 && my > 0 && my < 0x12)
        cmd = 0xFB;
    else
        cmd = 0xFE;
    switch (cmd) {
    case 0xFE:
        ch[1] = 0;
        if (num_words != 100) {
            note = &ATM_Strings[num_words];
            grfx_load_font("font4x5p.sys");
            force_mouse_cursor(ICON_CURSORS + 0xE);
            note->x = mx;
            note->y = my + 4;
            text[0] = 0;
            len = -1;
            curx = note->x - 1;
            mouse_putxy(curx + 10, my + 0x12);
            for (;;) {
                while ((cmd = do_keyboard_input(0)) < 0)
                    if ((cmd = mouse_getbut(&but)) > 0)
                        break;
                if (cmd == KEY_ENTER || cmd == KEY_ESC || cmd < 4)
                    break;
                if (cmd >= 0x20 && cmd <= 0x7A) {
                    ch[0] = toupper(cmd);
                    len++;
                    w = string_width(text) + string_width(ch);
                    if (note->x + w > 0x13B) {
                        do_beep(0x12C, 0xA);
                        len--;
                    } else if (len > 0x2D) {
                        do_beep(0x12C, 0xA);
                        len = 0x2D;
                    } else
                        text[len] = ch[0];
                } else if (cmd == KEY_BACKSPACE) {
                    if (--len < -1)
                        len = -1;
                    w = string_width(text);
                    if (w > 0) {
                        set_the_color(0x106);
                        rectangle(note->x, note->y, note->x + w, note->y - 6);
                    }
                }
                text[len + 1] = 0;
                curx = note->x + string_width(text) - 1;
                mouse_putxy(curx + 10, my + 0x12);
                string_to_screen(text, note->x, note->y);
            }
            grfx_load_font("font5x6p.sys");
            if (text[0]) {
                notes_dirty = 1;
                str_copy(note->text, text);
                num_words++;
            }
        }
        mouse_hide();
        RedisplayStrings();
        mouse_putxy(curx + 0x16, my + 7);
        unforce_mouse_cursor(2);
        break;
    case 0xFD:
        if (num_words > 0) {
            best = 0;
            for (i = 0; i < num_words; i++) {
                note = &ATM_Strings[i];
                str_copy(text, note->text);
                w = string_width(text);
                if (note->x <= mx && note->x + w >= mx && note->y - 5 <= my && note->y >= my) {
                    best = DetermineBetterHit(best, note, mx, my);
                    if (best == note)
                        hit = i;
                }
            }
            if (best) {
                notes_dirty = 1;
                str_copy(text, best->text);
                w = string_width(text);
                set_the_color(0x106);
                rectangle(best->x, best->y, best->x + w, best->y - 5);
                if (num_words - 1 == hit)
                    num_words--;
                else
                    ATM_Strings[hit].x = -1;
                RedisplayStrings();
            }
        }
        mouse_show();
        break;
    case 0xFC:
        if (level > 1)
            ChangeAutoMapLevel(level - 1);
        break;
    case 0xFB:
        if (level < 0x63)
            ChangeAutoMapLevel(level + 1);
        break;
    }
    mouse_release(1);
}

/* Draws the notes that have not been erased. */
void far RedisplayStrings(void)
{
    char buf[50];
    register int i;

    grfx_load_font("font4x5p.sys");
    *foreground_color = 0x2D;
    *background_color = 0x2D;
    for (i = 0; i < num_words; i++) {
        str_copy(buf, ATM_Strings[i].text);
        if (ATM_Strings[i].x >= 0) {
            str_copy(buf, ATM_Strings[i].text);
            string_to_screen(buf, ATM_Strings[i].x, ATM_Strings[i].y);
        }
    }
    grfx_load_font("font5x6p.sys");
}

/* Writes the level's notes (block 0x23 + lev) if they changed, dropping the erased ones. */
void far SaveTheWords(int lev)
{
    struct Arc arc;
    register int i;

    if (!notes_dirty)
        return;
    if (!num_words)
        return;
    for (i = 0; i < num_words; i++) {
        if (ATM_Strings[i].x < 0) {
            FAR_COPY(&ATM_Strings[i], &ATM_Strings[i + 1], (100 - i) * sizeof(struct ATM));
            num_words--;
        }
    }
    if (open_arc(&arc, "SAVE0\\lev.ark")) {
        if (!put_arc(&arc, LEVARK_NOTES(lev), ATM_Strings, num_words * sizeof(struct ATM)))
            ;
        close_arc(&arc);
    }
}

/* Reads the level's notes into ATM_Strings and draws them. */
void far GetTheWords(int lev)
{
    struct Arc arc;

    old_strings = num_words = 0;
    if (open_arc(&arc, "SAVE0\\lev.ark")) {
        old_strings = num_words = get_arc(&arc, LEVARK_NOTES(lev), ATM_Strings) / sizeof(struct ATM);
        RedisplayStrings();
        close_arc(&arc);
    }
}

/* Draws the map screen for level lev: the background (DATA\\blnkmap.byt), the map, the
   player's position (only on his own level, not level 9), the notes and the level
   number. If the background cannot be read, leaves the map screen. */
void far ShowAutoMapLevel(register int lev)
{
    int w;
    int px;
    int py;
    char num[6];
    char far *buf;
    register unsigned ws;

    ws = swap_ws_out();
    buf = MK_FP(conv_ws_seg, 0);
    mouse_hide();
    if (bltfromdrive("DATA\\blnkmap.byt", buf, 0xFA00)) {
        grSoftPageFlip();
        set_the_window(0, 0xC7, 0x13F, 0);
        show(0, 0xC7, buf, 0xC8, 0x140, 0, 0);
        unswap_ws(ws);
        ShowDungeonMap();
        if (lev == PlayerLevel && lev != 9) {
            px = (OBJ_HOMEX(ThePlayer) - 1) * 3 + 10 - 1;
            py = (OBJ_HOMEY(ThePlayer) - 1) * 3 + 12;
            Transparency = 1;
            pic_to_screen(ICON_BUTTONS + 0x3F, px, py, 5, 8);
            Transparency = 0;
        }
        level = lev;
#ifndef __TURBOC__
        port_coop_map_level(lev);
#endif
        grfx_quikpal(PAL_MAP);
        grPageFlip();
        grSoftPageFlip();
        copy_hidden_to_visible();
        GetTheWords(lev);
        *foreground_color = 0x2D;
        *background_color = 0x2D;
        grfx_load_font("fontbig.sys");
        itoa(lev, num, 10);
        w = string_width(num);
        string_to_screen(num, 0x121 - w / 2, 0xC2);
        grfx_load_font("font5x6p.sys");
    } else {
        unswap_ws(ws);
        mouse_show();
        ExitAutoMap();
    }
    mouse_show();
}

/* Shows another level's map (levels 1 to 8), saving this one's notes first. */
void far ChangeAutoMapLevel(register int lev)
{
    struct Arc arc;

    SaveTheWords(level);
    ClearAutoMap();
    if (lev < 9 && open_arc(&arc, "SAVE0\\lev.ark")) {
        GetAutoMapLevel(&arc, lev);
        close_arc(&arc);
    }
    ShowAutoMapLevel(lev);
}

/* The automap screen's idle work: keeps the music going. */
/* name: IDA ChangeThemeToBrittania_ovr094_158E: FM Towns automap_scr_ sits at this position and
   is the same one call, to loop_music_maybe_. */
void far automap_scr(void)
{
    loop_music_maybe();
}

#ifndef __TURBOC__
/* --enhance modern-keys' map keys (UltimaHacks' mapControl, John Glassmyer, MIT): S a level on
   (as the map's 0xFB button), W a level back (its 0xFC button), C the player's own level */
void far port_map_key(int how)
{
    if (how == 1 && level < 0x63)
        ChangeAutoMapLevel(level + 1);
    else if (how == -1 && level > 1)
        ChangeAutoMapLevel(level - 1);
    else if (how == 2 && level != PlayerLevel)
        ChangeAutoMapLevel(PlayerLevel);
}
#endif
