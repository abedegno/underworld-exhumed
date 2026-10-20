/* target: ovr094 */
/* opts: -mm -1 -G -O -Y -d */
/* The automap: drawing the player's map of each level, the map notes the player writes on
   it, the gem that picks a level, and map scraps that reveal parts of other levels. The
   whole of DOS overlay ovr094, in original order.

   What it does in the game: PlayersMap is the map the player has made of the current
   level, a byte per tile: bits 0..3 the tile type (0 never seen; 1..9 the tile types; 0xA
   and up marks a tile known but not seen, inferred from make_terrain_unseen and
   update_map_scraps), bits 4..5 a kind (0x10 a door, 0x20 and 0x30 drawn as water or dark,
   inferred from DoTile) and bits 6..7 the floor's terrain class, which picks the shading.
   The motion and view code fills it in as the player sees tiles; spells and scraps fill in
   areas through automap_area. Each level's map is block 0x9F + level of the save
   directory's LEV.ARK and its notes block 0xEF + level (UW-Formats 4.1: the automap and the
   map notes), up to 100 notes of 0x36 bytes (struct ATM). AutoMap opens the map screen
   (screen 2 of UWEDIT.C's screen table, entered with newscr(2); ExitAutoMap leaves it);
   ManageDungeonMap handles clicks: writing a note anywhere on the map, erasing one with the
   eraser, the up and down arrows for the levels of a world, the gem's facets for the other
   worlds, and the exit.

   Data owned: PlayersMap, the note list ATM_Strings (FARDATA), the level shown and the
   drawing tables.
   Function and global names are the originals from the FM Towns symbol table where it has
   them.
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

#ifndef __TURBOC__
/* Port only (docs/PORT.md, "Host hazards"; UW1's issue 7): a note's text into a 50-byte buffer
   as str_copy copies it, but at most 49 characters and the 0. The game ends every note it
   writes within its 50 bytes (ManageDungeonMap takes at most 46 characters), and UW2's LEV.ARK
   has a length table, so the aliased block that gave UW1 an unended note cannot happen here;
   but a notes block the game did not write (a damaged or edited save) can hold one, and
   str_copy would write past the buffer, where DOS returns into low memory and the host's frame
   decides what the extra bytes hit. A note that ends in its 50 bytes is copied by str_copy, as
   DOS copies it. */
static char far *note_copy(char *dst, char far *src)
{
    int i;
    for (i = 0; i < 0x32; i++)
        if (src[i] == 0) return str_copy(dst, src);
    for (i = 0; i < 0x31; i++) dst[i] = src[i];
    dst[0x31] = 0;
    return dst;
}
#define NOTE_COPY(dst, src) note_copy(dst, src)
#else
#define NOTE_COPY(dst, src) str_copy(dst, src)
#endif

/* Initialised data, DS:09EE. */
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

/* Uninitialised data, DS:36F8: the drawing state, the level shown, the note counts and
   the player's map. */
/* match: Turbo C lays _BSS out in an order set by the names, so the static names (none
   has an FM Towns name) were chosen by compiling probes to land where the EXE has them:
   door_dir, notes_dirty, level, old_strings, num_words and map_mouse are ours. */
static signed char door_dir;
static unsigned char notes_dirty;
static int16 level;
unsigned char PlayersMap[MAP_SIZE][MAP_SIZE];
static int16 old_strings;
static int16 num_words;
static int16 map_mouse;


/* Opens the map screen: registers its key and mouse handlers once, saves the current
   level's map, and shows this level, or without a map of it (player->automap clear) the
   level of the last map scrap read (player->map_scrap; its high bit marks a scrap to
   switch to). Walking music plays if none of the map themes is. */
#ifndef __TURBOC__
void far port_map_key(int how);
#endif

void far AutoMap(void)
{
    char noauto;

    noauto = !player->automap;
    if (!registered) {
        _input_addkey(KEY_ESC, 1, 2, (InputFn)newscr);
#ifndef __TURBOC__
        if (ENHANCED(ENH_MODERN_KEYS)) {     /* port only: modern-keys' map keys */
            _input_addkey('s', 1, 2, (InputFn)port_map_key);
            _input_addkey('w', -1, 2, (InputFn)port_map_key);
            _input_addkey('c', 2, 2, (InputFn)port_map_key);
            _input_addkey('d', 3, 2, (InputFn)port_map_key);
            _input_addkey('a', 4, 2, (InputFn)port_map_key);
        }
#endif
        map_mouse = input_addmouse(0, 0, 0x13F, 0xC7, 0, 2, (InputFn)ManageDungeonMap);
        registered = 1;
    }
    kill_all_effects();
    if (get_current_music() < 8 || get_current_music() > 0xF) {
        set_random_walking_music(0);
        change_music_maybe();
    }
    SaveAutoMapLevel(0, PlayerLevel);
    if (noauto) {
        level = player->map_scrap;
        GetAutoMapLevel(0, level);
    } else
        level = PlayerLevel;
    if (player->map_scrap & 0x80) {
        SaveAutoMapLevel(0, level);
        player->map_scrap = ~player->map_scrap;
        level = player->map_scrap;
        GetAutoMapLevel(0, level);
    }
    fadeout((unsigned char far *)0, 0, 0);
    ShowAutoMapLevel(level);
    mouse_constrain(9, 3, 0x13F, 0xC7);
    mouse_hide();
    force_mouse_cursor(0x1078);
    mouse_show();
    notes_dirty = 0;
}

/* Fills PlayersMap from the level for each tile x0..x1, y0..y1 that fn(x, y, arg) accepts:
   the tile type and its floor's terrain class. The loop over the tile's objects checks
   bridges, doors and wall decals but records nothing for them (assignments of terr to
   itself, as in DOS). Used by Map Area (SPELLS.C) and the whole-level map below. */
void far automap_area(int x0, int y0, int x1, int y1, int16 *arg,
                      char (far *fn)(int x, int y, int16 *arg))
{
    int y;
    unsigned char terr;
    struct Tile far *tile;
    union Link far *link;
    struct Object far *obj;
    register int x;
    register int t;

    for (y = y0; y <= y1; y++) {
        for (x = x0; x <= x1; x++) {
            if (fn(x, y, arg) == 0)
                continue;
            if (x < 1)
                x = 1;
            if (x >= 0x3F)
                break;
            if (y < 1)
                break;
            if (y >= 0x3F)
                break;
            tile = Map_GetAddr(x, y);
            terr = tile->type;
            terr |= TxmTerr[tile->floor] & TERR_CLASS;
            for (link = &tile->objects; (obj = Obj_PtrTMem(link)) != 0; link = &obj->qn.link) {
                if (OBJ_ITEM(obj) == ITEM_BRIDGE && OBJ_FLAGS(obj) < 2)
                    terr = terr;
                if (OBJ_CLASS(obj) == CLASS_DOOR)
                    terr = terr;
                if (OBJ_INCLASS(obj) == 0xE || OBJ_INCLASS(obj) == 0xF) {
                    t = TxmTerr[OBJ_OWNER(obj)] & 7;
                    if (t == 3)
                        terr = terr;
                    if (t == 4)
                        terr = terr;
                }
            }
            PlayersMap[y][x] = terr;
        }
    }
}

/* automap_area's callback for the whole level. */
/* name: DOS only; the name is provisional (IDA's SetALTo1_ovr094_2A1), chosen so that its
   tools/bssorder.py key puts it in the EXE's overlay stub order. */
char far ReturnOne_ovr094_2A1(int x, int y, int16 *arg)
{
    return 1;
}

/* DOS only: map the whole level and draw it. */
void far ovr094_2A8(void)
{
    automap_area(1, 1, 0x3F, 0x3F, 0, ReturnOne_ovr094_2A1);
    mouse_hide();
    ShowDungeonMap();
    mouse_show();
}

/* Writes PlayersMap as level lev's automap block (0x9F + lev of LEV.ARK in the save
   directory). flags bit 2: the archive is already open. */
unsigned char far SaveAutoMapLevel(int flags, int lev)
{
    unsigned char ok = 0;

    if (!(flags & 4) && !open_arc(1, HomeDir))
        return ok;
    ok = put_arc(1, lev + 0x9F, (char far *)PlayersMap, MAP_TILES);
    if (!(flags & 4))
        close_arc(1);
    return ok;
}

/* Reads level lev's automap into PlayersMap (all unseen if the archive cannot be opened). */
unsigned char far GetAutoMapLevel(int flags, int lev)
{
    unsigned char ok = 0;
    register int n;

    if (!(flags & 4) && !open_arc(1, HomeDir))
        memset(PlayersMap, 0, MAP_TILES);
    else {
        n = get_arc(1, lev + 0x9F, (char far *)PlayersMap);
        ok = n == 0 || n == MAP_TILES;
        if (!(flags & 4))
            close_arc(1);
    }
    return ok;
}

/* Leaves the map screen: saves the notes, remembers the level shown in map_scrap, reloads
   the current level's map and restores the game palette and pointer. */
void far ExitAutoMap(void)
{
    mouse_hide();
    unforce_mouse_cursor(0);
    SaveTheWords(level);
    player->map_scrap = level;
    if (level != PlayerLevel)
        GetAutoMapLevel(0, PlayerLevel);
    if (player->drawn) {
        set_new_music(MUSIC_ARMED);
        change_music_maybe();
    }
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
    gr_pixel(x, y, base + rand() / d + gr_read_pixel(x, y));
}

/* Draws one tile from its PlayersMap byte: the pattern of its type (diagonals half
   filled), coloured by the terrain class (bits 6..7), then the kind (bits 4..5): dark, the
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

    b = PlayersMap[y][x];
    shade = b >> 6;
    kind = b & 0x30;
    if (kind == 0x30 || kind == 0x20)
        shade = 0;
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
                    gr_pixel(px + i, py + j, rand() % 3 + 0xB1);
                    break;
                case 2:
                    gr_pixel(px + i, py + j, rand() % 3 + 0xB5);
                    break;
                case 3:
                    gr_pixel(px + i, py + j, rand() % 3 + 0xB9);
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
    case 0x30:
    case 0x40:
        for (i = 0; i < 3; i++)
            for (j = 0; j < 3; j++)
                PixelDarken(px + i, py + j, 6, 3);
        break;
    case 0x20:
        for (j = 0; j < 3; j++)
            for (i = 0; i < 3; i++)
                gr_pixel(px + i, py + j, (int)((int32)rand() * 3 / 0x8000L) + 0xE9);
        break;
    case 0x10:
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
    NOTE_COPY(buf, a->text);
    w = string_width(buf);
    ax = abs(a->x + w / 2 - x);
    ay = abs(a->y - 2 - y);
    NOTE_COPY(buf, b->text);
    w = string_width(buf);
    bx = abs(b->x + w / 2 - x);
    by = abs(b->y - 2 - y);
    if (by < ay)
        return b;
    if (bx < ax)
        return b;
    return a;
}

/* Which part of the gem the mouse is over: 0 the middle, 1 to 8 round the edge. */
int far true_gem_region(int x, int y)
{
    register int region = 0;

    x -= 0x116;
    y -= 0x77;
    x += 3;
    y++;
    if (abs(y) > abs(x) * 2)
        region = y > 0 ? 1 : 5;
    else if (abs(x) > abs(y) + 9)
        region = x > 0 ? 3 : 7;
    else
        region = ((4 - (octant(x, y) >> 1)) & 3) * 2 + 2;
    if (region & 1) {
        if (abs(x) <= 0xF && abs(y) <= 0xF)
            region = 0;
    } else if (abs(x) + abs(y) <= 0x15)
        region = 0;
    return region;
}

/* The map screen's mouse handler (see the file comment). Notes are typed in capitals in
   the small font, up to 46 characters and the screen edge, ended by Enter, Escape or a
   click. The level arrows move within the current world (level & 7), the gem's facets
   pick a world at the same depth. */
void far ManageDungeonMap(void)
{
    int16 mx;
    int16 my;
    int w;
    int i;
    int16 but;
    int key;
    struct ATM far *note;
    struct ATM far *best;
    char ch[2];
    int curx;
    int hit;
    int gem;
    int lev;
    char text[50];
    register int cmd;
    register int len;

    gem = -1;
    *foreground_color = 0x4E;
    *background_color = 0x4E;
    mx = inplist->x - 0x13;
    my = inplist->y - 7;
    cmd = inplist->cmd;
    if (cmd >= 4)
        return;
    mouse_release(1);
    if (mx > 0xFC && mx < 0x140 && my > 0x1B && my < 0x3B) {
        cmd = 0xFF;
        newscr(1);
    } else if (mx > 0xFC && mx < 0x140 && my > 0x3B && my < 0x5A) {
        cmd = 0xFD;
        mouse_constrain(0, 0, 0x13F, 0xC7);
        mouse_hide();
        force_mouse_cursor(0x1079);
        mouse_show();
        do {
            key = mouse_get_input_sp();
            if (key > 3)
                keyboard_mouse(key);
        } while (key < 1 || key > 3);
        mouse_getxy(&mx, &my);
        mouse_constrain(9, 3, 0x13F, 0xC7);
        unforce_mouse_cursor(1);
    } else if (mx > 0x10C && mx < 0x140 && my > 0xAC && my < 0xC7)
        cmd = 0xFC;
    else if (mx > 0x10C && mx < 0x140 && my > 0 && my < 0x1B)
        cmd = 0xFB;
    else if (mx > 0xFC && mx < 0x131 && my > 0x5D && my < 0x92) {
        gem = true_gem_region(mx, my);
        if ((gem & 6) == 6)
            gem = 0xD - gem;
        cmd = 0xFA;
    } else if (mx < 0 || my > 0xC7)
        return;
    else
        cmd = 0xFE;
    switch (cmd) {
    case 0xFE:
        ch[1] = 0;
        if (num_words != 100) {
            note = &ATM_Strings[num_words];
            grfx_quikfont(FONT_4X5P);
            force_mouse_cursor(0x107A);
            note->x = mx + 2;
            note->y = my + 2;
            text[0] = 0;
            len = -1;
            curx = note->x - 1;
            mouse_putxy(curx + 9, my + 0xF);
            for (;;) {
                while ((cmd = do_keyboard_input(0)) < 0)
                    if ((cmd = mouse_getbut(&but)) > 0)
                        break;
                if (cmd == 0xD || cmd == 0x1B || cmd < 4)
                    break;
                if (cmd >= 0x20 && cmd <= 0x7A) {
                    ch[0] = toupper(cmd);
                    len++;
                    w = string_width(text) + string_width(ch);
                    if (note->x + w > 0x13B)
                        len--;
                    else if (len > 0x2D)
                        len = 0x2D;
                    else
                        text[len] = ch[0];
                } else if (cmd == 8) {
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
                mouse_putxy(curx + 9, my + 0xF);
                string_to_screen(text, note->x, note->y);
            }
            grfx_quikfont(FONT_5X6P);
            if (text[0]) {
                notes_dirty = 1;
                str_copy(note->text, text);
                num_words++;
            }
        }
        mouse_hide();
        RedisplayStrings();
        mouse_putxy(curx + 0x13, my + 7);
        unforce_mouse_cursor(2);
        break;
    case 0xFD:
        if (num_words > 0) {
            best = 0;
            my -= 5;
            for (i = 0; i < num_words; i++) {
                note = &ATM_Strings[i];
                NOTE_COPY(text, note->text);
                w = string_width(text);
                if (note->x <= mx && note->x + w >= mx && note->y - 5 <= my && note->y >= my) {
                    best = DetermineBetterHit(best, note, mx, my);
                    if (best == note)
                        hit = i;
                }
            }
            if (best) {
                notes_dirty = 1;
                NOTE_COPY(text, best->text);
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
        if ((level & 7) != 1)
            ChangeAutoMapLevel(level - 1);
        break;
    case 0xFB:
        if (level & 7)
            ChangeAutoMapLevel(level + 1);
        break;
    case 0xFA:
        if (gem != -1) {
            lev = ((level - 1) & 7) + (gem << 3) + 1;
            ChangeAutoMapLevel(lev);
        }
        break;
    }
    mouse_release(1);
}

/* Draws the notes that have not been erased. */
void far RedisplayStrings(void)
{
    char buf[50];
    register int i;

    grfx_quikfont(FONT_4X5P);
    *foreground_color = 0x4E;
    *background_color = 0x4E;
    for (i = 0; i < num_words; i++) {
        NOTE_COPY(buf, ATM_Strings[i].text);
        if (ATM_Strings[i].x >= 0) {
            NOTE_COPY(buf, ATM_Strings[i].text);
            string_to_screen(buf, ATM_Strings[i].x, ATM_Strings[i].y);
        }
    }
    grfx_quikfont(FONT_5X6P);
}

/* Writes the level's notes (block 0xEF + lev) if they changed, dropping the erased ones. */
void far SaveTheWords(int lev)
{
    register int i;

    if (!notes_dirty)
        return;
    if (!(num_words | old_strings))
        return;
    for (i = 0; i < num_words; i++) {
        if (ATM_Strings[i].x < 0) {
            FAR_COPY(&ATM_Strings[i], &ATM_Strings[i + 1], (100 - i) * sizeof(struct ATM));
            num_words--;
        }
    }
    if (open_arc(1, HomeDir)) {
        if (!put_arc(1, lev + 0xEF, (char far *)ATM_Strings, num_words * sizeof(struct ATM)))
            ;
        close_arc(1);
    }
}

/* Reads the level's notes into ATM_Strings; returns 0 if the archive cannot be opened. */
unsigned char far GetTheWords(int lev)
{
    old_strings = num_words = 0;
    if (open_arc(1, HomeDir)) {
        old_strings = num_words = get_arc(1, lev + 0xEF, (char far *)ATM_Strings) / sizeof(struct ATM);
        close_arc(1);
        return 1;
    }
    return 0;
}

/* Draws one facet of the gem (part 0..7 a world, 8 the middle): how 1 lit, 2 the current
   world's picture. */
void far show_a_part(register int part, int how)
{
    struct { unsigned char x, y; } pos[9] = {
        { 0x0B, 0x8D }, { 0x1B, 0x8C }, { 0x23, 0x83 }, { 0x1B, 0x71 }, { 0x0B, 0x69 },
        { 0x00, 0x83 }, { 0x01, 0x71 }, { 0x01, 0x8D }, { 0x07, 0x85 }
    };
    register int pic = part + 0x107F;

    if (how) {
        if (how == 2)
            pic += 8;
        pic_to_screen(pic, pos[part].x + 0x101, pos[part].y, 0, 0);
    }
}

/* Lights the gem facets of the worlds the player has visited (QB_WORLDS_VISITED) and marks
   the world of the level shown (world 0, Britannia, is the middle). */
void far show_gem_parts(int lev)
{
    int i;
    int old;
    register int k;

    old = Transparency;
    Transparency = 1;
    for (i = 0; i < 8; i++)
        if (player->quest_bytes[QB_WORLDS_VISITED] & (1 << i))
            show_a_part(i, 1);
    k = (lev - 1) / 8;
    if (k == 0)
        k = 8;
    else
        k--;
    show_a_part(k, 2);
    Transparency = old;
}

/* Draws the map screen for level lev: the background, the map, the gem, the player's
   position (only on his own level and with player->automap set), the notes and the level
   number within its world. */
void far ShowAutoMapLevel(int lev)
{
    int px;
    char num[2];
    char far *buf;
    char ok;
    register int py;
    register unsigned ws;

    mouse_hide();
    ws = get_workspace();
    if (ws == 0)
        return;
    buf = MK_FP(ws, 0);
    grSoftPageFlip();
    set_workspace();
    ok = disk_to_vid(0, buf);
    release_workspace();
    if (ok) {
        ShowDungeonMap();
        show_gem_parts(lev);
        if (lev == PlayerLevel && player->automap) {
            px = (OBJ_HOMEX(ThePlayer) - 1) * 3 + 10 - 1;
            py = (OBJ_HOMEY(ThePlayer) - 1) * 3 + 12;
            Transparency = 1;
            pic_to_screen(0x103F, px, py, 5, 8);
            Transparency = 0;
        }
        level = lev;
#ifndef __TURBOC__
        port_coop_map_level(lev);
#endif
        grPageFlip();
        grfx_quikpal(PAL_MAP);
        grSoftPageFlip();
        copy_hidden_to_visible();
        if (GetTheWords(lev))
            RedisplayStrings();
        *foreground_color = 0x4E;
        *background_color = 0x4E;
        grfx_quikfont(FONT_BIG);
        num[0] = (lev - 1 & 7) + '1';
        num[1] = 0;
        string_to_screen(num, 0x115, 0xBF);
        grfx_quikfont(FONT_5X6P);
    } else {
        grSoftPageFlip();
        mouse_show();
        ExitAutoMap();
    }
    release_workspace();
    mouse_show();
}

/* Shows another level's map, saving this one's notes first. Level 0x47 (the first of the
   map scraps' slots) is never loaded and shows blank. */
void far ChangeAutoMapLevel(register int lev)
{
    SaveTheWords(level);
    ClearAutoMap();
    if (lev < NUM_LEVELS && lev != 0x47)
        GetAutoMapLevel(0, lev);
    ShowAutoMapLevel(lev);
}

/* The automap screen's idle work: keeps the music going. */
/* name: IDA ChangeThemeToBrittania_ovr094_158E: FM Towns automap_scr_ sits at this position and
   is the same one call, to loop_music_maybe_. */
void far automap_scr(void)
{
    loop_music_maybe();
}

/* Which of the map's sections a point (relative to the map's middle) lies in: 0 the
   middle (within 9 tiles), 1 to 7 round it. */
/* name: IDA GetMapPieceSegmentNo_ovr094_1598: FM Towns map_section_ at this position is
   the same code. */
int far map_section(register int x, register int y)
{
    int t;
    register int s = 0;

    if (x * x + y * y <= 0x51)
        return 0;
    if (y < 0) {
        y = -y;
        x = -x;
        s = 0x1C;
    }
    if (x < 0) {
        t = y;
        y = -x;
        x = t;
        s += 0xE;
    }
    if (y < x)
        s += y * 7 / x;
    else
        s += 0xE - x * 7 / y;
    s = s / 8 + 1;
    if (s > 7)
        return 7;
    return s;
}

/* Copying a map scrap to the player's map (USEITEMS.C): the scrap's map is stored as the
   automap of level number scrap (0x47 + the scrap object's quality) and covers level lev
   (its owner field); the sections it shows are a bit mask (its link). The scrap's map is
   loaded and every tile outside those sections is marked unseen; then each tile it adds to
   lev's map gives experience (lev / 8 + 1 per tile, a twentieth of the total in the end),
   and the scrap's notes in those sections are copied too. */
void far update_map_scraps(int scrap, int lev, unsigned char sections)
{
    unsigned char far *buf;
    int sect;
    unsigned char flag;
    unsigned char old;
    unsigned char cur;
    int count;
    int xp;
    struct ATM far *note;
    int nx;
    int ny;
    register int x;
    register int y;

    xp = 0;
    SaveAutoMapLevel(0, PlayerLevel);
    GetAutoMapLevel(0, scrap);
    if (!get_workspace())
        return;
    buf = MK_FP(set_workspace(), 0);
    for (y = 0x3F; y >= 1; y--) {
        for (x = 1; x <= 0x3F; x++) {
            sect = map_section(x - 0x20, y - 0x20);
            if (((sections >> sect) & 1) == 0) {
                cur = 0xF & PlayersMap[y][x];
                if (cur >= 6 && cur <= 9)
                    cur = 0xB;
                else if (cur <= 5)
                    cur = cur + 0xA;
                PlayersMap[y][x] |= cur;
            }
        }
    }
    FAR_COPY(buf, PlayersMap, MAP_TILES);
    GetAutoMapLevel(0, lev);
    for (y = 0x3F; y >= 1; y--) {
        for (x = 1; x < 0x3F; x++) {
            old = 0xF & (buf + (y << 6))[x];
            cur = 0xF & PlayersMap[y][x];
            flag = 0;
            if (cur == 0)
                flag = 1;
            else if (old < 0xA && cur >= 0xA)
                flag = 1;
            if (flag) {
                xp += lev / 8 + 1;
                PlayersMap[y][x] = (buf + (y << 6))[x];
            }
        }
    }
    player_get_exp(xp / 20);
    SaveAutoMapLevel(0, lev);
    GetTheWords(scrap);
    count = num_words;
    FAR_COPY(buf, ATM_Strings, count * sizeof(struct ATM));
    GetTheWords(lev);
    for (x = 0; x < count; x++) {
        note = (struct ATM far *)buf + x;
        nx = (note->x - 10) / 3;
        ny = (note->y - 7) / 3;
        sect = map_section(nx - 0x20, ny - 0x20);
        if ((sections >> sect) & 1 && num_words < 100) {
            ATM_Strings[num_words] = *note;
            num_words++;
            notes_dirty = 1;
        }
    }
    SaveTheWords(lev);
    GetAutoMapLevel(0, PlayerLevel);
    release_workspace();
}

/* Marks a block of the map as not seen (tile types 0..5 become 0xA..0xF); hack trap 45. */
/* name: IDA AutoMapTrap_ovr094_1878: FM Towns make_terrain_unseen_ at this position is
   the same code. */
void far make_terrain_unseen(int x, int y, unsigned w, unsigned h)
{
    unsigned char hi;
    register int i;
    register int j;
    unsigned char t;

    for (i = x; i <= x + w; i++) {
        for (j = y; j <= y + h; j++) {
            t = 0xF & PlayersMap[j][i];
            hi = 0xF0 & PlayersMap[j][i];
            if (t <= 5)
                t = t + 0xA;
            PlayersMap[j][i] = hi | t & 0xF;
        }
    }
}

#ifndef __TURBOC__
/* --enhance modern-keys' map keys (UltimaHacks' mapControl, John Glassmyer, MIT): S a level on
   within the world (as the map's 0xFB button), W a level back (its 0xFC button), C the player's
   own level, D and A the previous and next world at the same depth (UltimaHacks'
   adjacentRealmTable: from each world, the previous one and the next) */
static const unsigned char adjacent_world[9][2] = {
    { 8, 1 }, { 0, 2 }, { 1, 3 }, { 2, 4 }, { 3, 5 }, { 4, 7 }, { 7, 8 }, { 5, 6 }, { 6, 0 }
};

void far port_map_key(int how)
{
    if (how == 1 && (level & 7))
        ChangeAutoMapLevel(level + 1);
    else if (how == -1 && (level & 7) != 1)
        ChangeAutoMapLevel(level - 1);
    else if (how == 2 && level != PlayerLevel)
        ChangeAutoMapLevel(PlayerLevel);
    else if (how == 3 || how == 4)
        ChangeAutoMapLevel((adjacent_world[(level - 1) >> 3][how - 3] << 3) + ((level - 1) & 7) + 1);
}
#endif
