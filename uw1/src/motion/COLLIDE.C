/* target: seg026 */
/* opts: -mm -1 -G -O -Y -d */
/* Terrain and object collision for anything that moves or is placed: the floor height
   under the four corners of a mover's footprint, the walls between them, the objects
   it overlaps sorted by height, and the placing of objects in the map. The whole of DOS
   resident segment seg026, in original order. Seeded from UW2Decomp's
   src/motion/COLLIDE.C (UW2's seg028_2941): the same functions in the same order, less
   UW2's get_home_tile, which UW1 does not have. Function and global names are UW2's
   (the FM Towns originals); UW1 has no symbols of its own.

   UW1 differences, all from the bytes: GetHgt's steep flag, SolvePnt's result and the
   useflag, flier and Obj_Elem_Fate tests are signed chars (cbw), and a tile's terrain
   word takes the floor texture's whole terrain byte shifted left 4 (UW2 masks 0xC0 and
   shifts 2).

   All of it works on the collision record curP points at (struct MotionCalc, map.h):
   the caller sets x, y, z (1/8 tiles, z in object units), radius, height and the
   object's index, and the checks fill hits0, hits1, floor, top, slope, open, found,
   count and first. MOTION.C points curP at Ppd; can_place, obj_deal (OBJPHYS.C) and the
   player and critter code use a local record and restore curP.

   TerrainCheck looks at the 3x3 tiles around the centre (tiles[], one terrain word each:
   type, height and floor terrain class, read lazily) and at five points: the centre and
   the four corners of the square footprint radius wide. GetHgt gives the floor height at
   a point (0x80 for a wall, or the solid half of a diagonal; slopes rise one unit per
   cell). SolveCenter and SolvePnt turn each height into state bits against z and the
   step range (MOTION.C's header lists them): wall, too high, a drop, or on a floor of a
   given terrain class. A corner in another tile that a diagonal wall blocks (tile_walls)
   is a wall too. ComputeHeading then sums the corners into the direction of the blocking
   walls (slope) and of the open side (open), which MOTION.C's do_2dbounce uses.

   ObjectCheck finds the objects whose square overlaps the footprint, in the tiles around
   (at most 8, oCollisions), and process_objlist sorts them: those whose top is at or
   below z first (bottom of the list, by top), then those overlapping the mover's height
   (first and count), by bottom.

   can_place, put_at, near_mob_put_at and drop_around_place place objects: they are used
   all over the game to drop, create and move things, and drop_around_place tries up to
   24 random spots within range.

   File names mentioned (MOTION.C, OBJPHYS.C) are UW2Decomp's.
   name: descriptive, UW2Decomp's (its map/filenames.tsv: terrain and object collision,
   placing objects).

   Data owned: oCollisions (the objects in the way), curP (the collision record being
   filled), centptr, nvokHgt and nvokTerr (can_place's results), stay_centered (for
   drop_around_place), and the statics of the checks (tiles, pnt and the footprint
   extent). */

#include <mem.h>
#include <stdlib.h>
#include "event.h"
#include "map.h"
#include "motion.h"
#include "object.h"
#include "uw2.h"
#ifndef __TURBOC__
#include "sys.h"
#endif

/* Declared in each file that uses it, its own way (no header). */
unsigned char far IsMobElem(struct Object far *obj);

/* A tile's terrain word: type 0-3, height 4-7, the floor texture's terrain byte from
   bit 4 (its class, read back as bits 8-9). */
/* UW1: the terrain byte is shifted left 4 whole, so TERRAIN.DAT's 0x10 (water) and 0x20
   (lava) land in bits 8-9 as classes 1 and 2 (map.h's TERR_* and TERRAIN_*); UW2 masks
   its 0xC0 class bits and shifts 2. */
#define TILE_TERR(t)    (t)->type + ((t)->height << 4) + ((TxmTerr[(t)->floor] & 0xFF) << 4)

/* A corner of the footprint, or (the fifth) its centre: which of the nine tiles around
   the centre tile it is in, and where in that tile. */
struct Pnt {
    unsigned char tile;
    unsigned char x;
    unsigned char y;
    uint16 flags;
};

/* Uninitialised data, DS:26C0..272D, 110 bytes as in UW2 (DS:251A..2587). nvokHgt and
   nvokTerr are can_place's results: the height an object placed there would rest at
   and the terrain byte there. */
/* match: Turbo C lays _BSS out by a hash of the names, ties in definition order, so the
   names fix the layout. */
/* name: the publics are the FM Towns names; the statics (_Valor+0x13 to +0x44 there)
   have none, and xlow, xhigh, ypos, ylow, yhigh, tiles, pnt, firstsolve and pos_x are
   UW2Decomp's, chosen by compiling candidates as probes because they land where UW2's
   EXE has them; every access here matches with UW1's addresses too. */
static char xlow;                       /* the mover's extent within its tile */
static char xhigh;
static char ypos;                       /* the mover's position within its tile */
static char ylow;
static char yhigh;
struct Collision oCollisions[8];
static int16 tiles[9];                  /* terrain words of the 3x3 tiles, 0x1111 unread */
struct MotionCalc near *curP;
static struct Pnt pnt[5];
struct Tile far *centptr;               /* the centre tile */
int16 nvokHgt;
static char firstsolve;
static char pos_x;
int16 nvokTerr;

/* Initialised data, DS:02BA..02D5 (with walls, side, dirs and stay_centered below).
   tile_off: the map offset of each of the 3x3 tiles from the centre, in tiles[] order
   (rows of 64). */
static signed char tile_off[9] = { -65, -64, -63, -1, 0, 1, 63, 64, 65 };

/* The floor height at point n (0 to 3 the corners, 4 the centre): the tile's height * 8,
   0x80 for a solid tile or the closed half of a diagonal, plus the rise across a slope.
   *steep is set in a diagonal tile. */
unsigned char far GetHgt(unsigned char n, char *steep)
{
    unsigned char h = (tiles[pnt[n].tile] & 0xF0) >> 1;

    *steep = 0;
    switch (tiles[pnt[n].tile] & 0xF) {
    case TILE_SOLID:
        h = 0x80;
        break;
    case TILE_DIAG_SE:
        if (pnt[n].y >= pnt[n].x)
            h = 0x80;
        *steep = 1;
        break;
    case TILE_DIAG_SW:
        if (pnt[n].x + pnt[n].y >= 7)
            h = 0x80;
        *steep = 1;
        break;
    case TILE_DIAG_NE:
        if (pnt[n].x + pnt[n].y <= 7)
            h = 0x80;
        *steep = 1;
        break;
    case TILE_DIAG_NW:
        if (pnt[n].y <= pnt[n].x)
            h = 0x80;
        *steep = 1;
        break;
    case TILE_SLOPE_N:
        h = h + (pnt[n].y & 7);
        break;
    case TILE_SLOPE_S:
        h = h + (7 - (pnt[n].y & 7));
        break;
    case TILE_SLOPE_E:
        h = h + (pnt[n].x & 7);
        break;
    case TILE_SLOPE_W:
        h = h + (7 - (pnt[n].x & 7));
        break;
    }
    return h;
}

/* name: GetSlopeHgt (IDA GetTileZOffset): FM Towns has it between GetHgt and SolvePnt,
   and it reads the centre tile's slope and height the same way. */
/* The floor height under x, y (1/256 tiles) in the centre tile, in the physics record's
   z units (1/8 of an object unit), following a slope exactly; back_to_space uses it to
   keep a walker on a slope. */
int far GetSlopeHgt(int x, int y)
{
    int h = 0;

    y = y & 0xFF;
    x = x & 0xFF;
    switch (tiles[4] & 0xF) {
    case TILE_SLOPE_N:
        h = y;
        break;
    case TILE_SLOPE_S:
        h = 0xFF - y;
        break;
    case TILE_SLOPE_E:
        h = x;
        break;
    case TILE_SLOPE_W:
        h = 0xFF - x;
        break;
    }
    h = h >> 2;
    h += (tiles[4] & 0xF0) << 2;
    return h;
}

/* Sets pnt[n].flags from the floor at corner n against curP->z, within range: 0x200 a
   wall, 0x100 too high, 0x800 a drop, else 8 << the floor's terrain class. Raises
   curP->top to the highest floor seen. Returns 0 if the corner is in a diagonal tile. */
char far SolvePnt(unsigned char n, unsigned char range)
{
    char steep;
    unsigned char h;
    unsigned flags = 0;

    h = GetHgt(n, &steep);
    if (h == 0x80)
        flags |= 0x200;
    else if (curP->z + range < h)
        flags |= 0x100;
    else if (curP->z - range > h)
        flags |= 0x800;
    else
        flags |= 8 << ((tiles[pnt[n].tile] & 0x300) >> 8);
    pnt[n].flags = flags;
    if (curP->top < h)
        curP->top = h;
    return !steep;
}

/* SolvePnt for the centre, into hits0, which also gets the terrain class (bits 0-1),
   4 for standing on the floor and 0x2000 for a slope. */
unsigned char far SolveCenter(unsigned char range)
{
    char steep;

    curP->hits0 = (tiles[4] & 0x300) >> 8;
    curP->floor = GetHgt(4, &steep);
    if (curP->floor == 0x80)
        curP->hits0 |= 0x200;
    else if (curP->z + range < curP->floor)
        curP->hits0 |= 0x100;
    else if (curP->z - range > curP->floor)
        curP->hits0 |= 0x800;
    else {
        curP->hits0 |= 4;
        curP->hits0 |= 8 << ((tiles[4] & 0x300) >> 8);
    }
    if ((tiles[4] & 0xF) >= TILE_SLOPE_N)
        curP->hits0 |= 0x2000;
    return !steep;
}

/* The terrain under curP's footprint: the centre into hits0 and floor, the four corners
   into hits1 (with the centre's bits), top the highest floor. range is how far up or down
   the floor may be and still count as underfoot (the mover's step height). */
void far TerrainCheck(unsigned char range)
{
    char off;
    char i;
    struct Tile far *t;
    int x;
    int y;
    char j;

    memset(tiles, 0x11, sizeof tiles);
    centptr = Map_GetAddr(curP->x >> 3, curP->y >> 3);
    x = curP->x & 7;
    y = curP->y & 7;
    off = 4;
    pnt[4].tile = off;
    pnt[4].x = x;
    pnt[4].y = y;
    if (tiles[4] == 0x1111)
        tiles[4] = TILE_TERR(centptr);
    SolveCenter(range);
    curP->hits1 = curP->hits0;
    curP->top = curP->floor;
    if (curP->radius == 0)
        return;
    for (y -= curP->radius; y < 0; y += 8)
        off = off - 3;
    for (x -= curP->radius; x < 0; x += 8)
        off -= 1;
    pnt[0].tile = off;
    pnt[0].x = x;
    pnt[0].y = y;
    for (x += curP->radius * 2; x > 7; x -= 8)
        off = off + 1;
    pnt[1].tile = off;
    pnt[1].x = x;
    pnt[1].y = y;
    for (y += curP->radius * 2; y > 7; y -= 8)
        off = off + 3;
    pnt[2].tile = off;
    pnt[2].x = x;
    pnt[2].y = y;
    for (x -= curP->radius * 2; x < 0; x += 8)
        off -= 1;
    pnt[3].tile = off;
    pnt[3].x = x;
    pnt[3].y = y;

    t = centptr + tile_off[pnt[0].tile];
    if (tiles[pnt[0].tile] == 0x1111)
        tiles[pnt[0].tile] = TILE_TERR(t);
    t = centptr + tile_off[pnt[1].tile];
    if (tiles[pnt[1].tile] == 0x1111)
        tiles[pnt[1].tile] = TILE_TERR(t);
    t = centptr + tile_off[pnt[2].tile];
    if (tiles[pnt[2].tile] == 0x1111)
        tiles[pnt[2].tile] = TILE_TERR(t);
    t = centptr + tile_off[pnt[3].tile];
    if (tiles[pnt[3].tile] == 0x1111)
        tiles[pnt[3].tile] = TILE_TERR(t);

    firstsolve = 1;
    for (i = 0; i < 4; i++) {
        if (!SolvePnt(i, range) && !(pnt[i].flags & 0x300)) {
            char walls[5] = { 4, 0x10, 2, 8, 4 };

            for (j = 0; j < 2; j++) {
                if (pnt[(i + (j << 1) + 1) & 3].tile != pnt[i].tile
                    && (tile_walls[tiles[pnt[i].tile] & 0xF] & walls[j + i])) {
                    pnt[i].flags = 0x200;
                    curP->top = 0x80;
                    j = 2;
                }
            }
            firstsolve = 0;
        }
    }
    curP->hits1 |= pnt[0].flags | pnt[1].flags | pnt[2].flags | pnt[3].flags;
}

static signed char side[4] = { 1, -1, -1, 1 };
static unsigned char dirs[3][3] = { { 5, 4, 3 }, { 6, 9, 2 }, { 7, 0, 1 } };

/* name: ComputeHeading (IDA seg028_2941_803): the FM Towns function after TerrainCheck,
   with the same tables, the same sums over the four corners and the same 8-way switch. */
/* From the last TerrainCheck: curP->slope, the direction (0 to 7, eighths of a turn, 9
   for none) of the corners that hit a wall or a high floor, and curP->open, that of the
   corners that are clear. A single blocking corner on a diagonal (on the first solve)
   is turned into a wall direction by the mover's heading and, head on, by which side of
   the corner the point lies. */
void far ComputeHeading(void)
{
    char ox;
    char oy;
    char sy;
    char sx;
    unsigned char want;
    unsigned char head;
    unsigned char k;
    unsigned char a;
    unsigned char b;
    int nopen;
    int nsteep;
    char i;

    oy = ox = sx = sy = nopen = nsteep = 0;
    for (i = 0; i < 4; i++) {
        if ((pnt[i].flags & 0xF8) == 0) {
            oy += side[i];
            ox += side[(i + 3) & 3];
            nopen++;
        }
        if (pnt[i].flags & 0x300) {
            sx += side[i];
            sy += side[(i + 3) & 3];
            nsteep++;
        }
    }
    if (nsteep) {
        curP->slope = dirs[sx / nsteep + 1][sy / nsteep + 1];
        if (nsteep == 1 && curP->slope % 2 && firstsolve) {
            want = (9 - curP->slope) & 7;
            head = curP->heading >> 13;
            switch ((head - want) & 7) {
            case 0:
            case 1:
                curP->slope = 9;
                break;
            case 2:
            case 3:
                curP->slope = (curP->slope + 1) & 7;
                break;
            case 4:
            case 5:
                k = (curP->slope - 1) >> 1;
                curP->slope--;
                switch (k) {
                case 0:
                    a = pnt[k].x;
                    b = pnt[k].y;
                    break;
                case 1:
                    a = pnt[k].x;
                    b = 8 - pnt[k].y;
                    break;
                case 2:
                    a = pnt[k].y;
                    b = pnt[k].x;
                    break;
                case 3:
                    a = 8 - pnt[k].x;
                    b = pnt[k].y;
                    break;
                }
                if (a < b)
                    curP->slope = (curP->slope + 2) & 7;
                if (a == b)
                    curP->slope++;
                break;
            case 6:
            case 7:
                curP->slope = (curP->slope + 7) & 7;
                break;
            }
        }
    } else
        curP->slope = 9;
    if (nopen == 1 || nopen == 2)
        curP->open = dirs[oy / -nopen + 1][ox / -nopen + 1];
    else
        curP->open = 9;
}

/* name: obj_coll_check (IDA CreateCollisionRecord): the FM Towns function before
   ObjectCheck, testing the same four bounds and filling an oCollisions record. */
/* Adds obj, in tile x, y relative to the centre, to oCollisions if its square (a whole
   tile for radius 4) overlaps the mover's: bottom and top from its z and height, the
   link's low bits 9, plus 0x10 if the mover's centre is inside it. Critters shrink each
   other's radius by one so they can pass close. */
void far obj_coll_check(struct Object far *obj, int link, char x, char y, char isnpc)
{
    char x0;
    char y0;
    char x1;
    char y1;
    struct ComObj com;
    register struct Collision *c;

    if (curP->found > 8)
        return;
    com = ComObjData[OBJ_ITEM(obj)];
    if (com.radius == 4) {
        x1 = (x0 = x << 3) + 7;
        y1 = (y0 = y << 3) + 7;
    } else {
        register int r;

        x0 = (x << 3) + OBJ_FINEX(obj);
        y0 = (y << 3) + OBJ_FINEY(obj);
        r = com.radius;
        if (OBJ_MAJOR(obj) == MAJOR_CREATURE && r > 0 && isnpc)
            r--;
        x1 = x0 + r;
        y1 = y0 + r;
        x0 = x0 - r;
        y0 = y0 - r;
    }
    if (x1 < xlow)
        return;
    if (x0 > xhigh)
        return;
    if (y1 < ylow)
        return;
    if (y0 > yhigh)
        return;
    c = &oCollisions[curP->found++];
    c->bottom = OBJ_Z(obj);
    c->top = c->bottom + com.height;
    if (com.height == 0)
        c->top = c->top + 1;
    c->link.f.index = link;
    c->link.f.low = 9;
    if (pos_x >= x0 && pos_x <= x1 && ypos >= y0 && ypos <= y1)
        c->link.f.low |= 0x10;
    c->offset = x + (y << 6);
}

/* Collects the objects around curP into oCollisions (at most 8, and at most 64 per
   tile's list). Skips the mover itself, flat static objects, mobile non-creatures whose
   b15 bit 7 is set (OBJPHYS.C's do_objhit sets it when two mobile objects hit, so
   probably an object that has already struck something) and, for critters, objects
   ComObjData marks c3_2. flat with no height uses a point footprint; useflag keeps only
   touchable objects. */
void far ObjectCheck(unsigned char flat, unsigned char useflag)
{
    struct Tile far *tile;
    union Link far *link;
    struct Object far *obj;
    char x0;
    char y0;
    char x1;
    char y1;
    char i;
    char j;
    char radius;
    char isnpc;
    struct ComObj far *com;
    int n;
    int off;

    isnpc = 0;
    tile = Map_GetAddr(curP->x >> 3, curP->y >> 3);
    if (curP->index != 0)
        isnpc = OBJ_MAJOR(Obj_IntTMem(curP->index)) == MAJOR_CREATURE;
    curP->found = 0;
    pos_x = curP->x & 7;
    ypos = curP->y & 7;
    if (flat && curP->height == 0)
        radius = 0;
    else {
        radius = curP->radius;
        if (isnpc && radius > 0)
            radius--;
    }
    xhigh = pos_x + radius;
    yhigh = ypos + radius;
    xlow = pos_x - radius;
    ylow = ypos - radius;
    x0 = (xlow - 11) / 8;
    y0 = (ylow - 11) / 8;
    x1 = (xhigh + 4) / 8;
    y1 = (yhigh + 4) / 8;
    for (i = x0; i <= x1; i++) {
        for (j = y0; j <= y1; j++) {
            n = 0;
            off = (j << 6) + i;
            for (link = &tile[off].objects; link->f.index != 0 && n < 0x40;
                 link = &Obj_PtrTMem(link)->qn.link, n++) {
                if (link->f.index == curP->index)
                    continue;
                obj = Obj_PtrTMem(link);
#ifndef __TURBOC__
                /* Companions can pass in the Abyss's one-person corridors.
                   Projectiles, item placement and NPCs keep normal collisions. */
                if (curP->height == ComObjData[ITEM_ADVENTURER].height
                    && curP->radius == ComObjData[ITEM_ADVENTURER].radius
                    && port_shared_avatar(Obj_IntTMem(curP->index))
                    && port_shared_avatar(obj))
                    continue;
#endif
                com = &ComObjData[OBJ_ITEM(obj)];
                if (isnpc && com->c3_2)
                    continue;
                if (com->height == 0 && obj >= (struct Object far *)objdata)
                    continue;
                if (obj < (struct Object far *)objdata && OBJ_MAJOR(obj) != MAJOR_CREATURE && OBJ_B15_7(obj) != 0)
                    continue;
                if (!(char)useflag || com->touch)   /* UW1: tested as a signed char */
                    obj_coll_check(obj, link->f.index, i, j, isnpc);
            }
            if (n == 0x40)
                return;
        }
    }
}

/* name: oCswap (IDA SwapCollisionRecords): the FM Towns function between ObjectCheck
   and process_objlist, swapping two oCollisions records. */
void far oCswap(unsigned char i)
{
    struct Collision t;

    t = oCollisions[i];
    oCollisions[i] = oCollisions[i + 1];
    oCollisions[i + 1] = t;
}

/* Sorts oCollisions (bubble sorts): by top up to the first object whose top is above
   curP->z, which becomes curP->first, then the rest by bottom; curP->count is how many
   of those reach into the mover's height. So oCollisions[0..first-1] are underfoot and
   [first..first+count-1] are in the way. */
void far process_objlist(void)
{
    char k;
    char m;
    char n;
    char flat;

    flat = 0;
    if (curP->height == 0)
        flat = 1;
    for (n = 0; curP->found > n; n++) {
        for (k = curP->found - 2; k >= n; k--)
            if (oCollisions[k].top > oCollisions[k + 1].top)
                oCswap(k);
        if (oCollisions[n].top > curP->z)
            break;
    }
    for (k = n; curP->found > k; k++)
        for (m = curP->found - 2; m >= k; m--)
            if (oCollisions[m].bottom > oCollisions[m + 1].bottom)
                oCswap(m);
    curP->first = n;
    for (curP->count = 0; n + curP->count < curP->found
         && curP->z + curP->height + flat > oCollisions[n + curP->count].bottom; curP->count++)
        ;
}

/* Whether an object of type item (index its object number, for ObjectCheck to skip)
   fits at x, y, z (1/8 tiles): not through the ceiling, no wall or high floor under its
   footprint, nothing in its way, and, unless flier, not left hanging more than range
   above what it would rest on. Sets nvokHgt to that height (the floor or the top of a
   solid object below) and nvokTerr to the terrain byte there. Uses its own collision
   record and restores curP. */
unsigned char far can_place(int item, int index, int x, int y, int z, unsigned char flier, unsigned char range)
{
    int radius;
    struct MotionCalc near *oldP;
    unsigned char ok;
    struct MotionCalc calc;

    oldP = curP;
    curP = &calc;
    curP->index = index;
    curP->radius = ComObjData[item].radius;
    curP->height = ComObjData[item].height;
    curP->x = x;
    curP->y = y;
    curP->z = z;
    if (curP->height != 0x80 && curP->height + curP->z > 0x7F) {
        ok = 0;
        goto out;
    }
    TerrainCheck(range);
    if ((curP->hits0 | curP->hits1) & 0x300) {
        ok = 0;
        goto out;
    }
    nvokHgt = curP->z + range >= curP->top ? curP->top : curP->floor;
    radius = curP->radius;
    if (range > radius)
        radius = range;
    if (curP->z <= curP->floor + radius)
        nvokTerr = 1 << (curP->hits0 & 3);
    else
        nvokTerr = FOOT_AIR;
    ObjectCheck(nvokTerr != FOOT_AIR && index >= NUM_MOBILE, 1);
    if (curP->found != 0) {
        int i;
        int best;

        best = -1;
        process_objlist();
        if (curP->count != 0) {
            ok = 0;
            goto out;
        }
        if (curP->found != 0)
            for (i = 0; i < curP->first; i++)
                if (oCollisions[i].top > nvokHgt)
                    nvokHgt = oCollisions[best = i].top;
        if (best > -1) {
            if (!ComObjData[OBJ_ITEM(Obj_PtrTMem(&oCollisions[best].link))].solid) {
                ok = 0;
                goto out;
            }
            nvokTerr = FOOT_FLOOR;
        }
    }
    if (!(char)flier && (curP->hits0 | curP->hits1) & 0x800 && curP->z - range > nvokHgt)
        ok = 0;
    else
        ok = 1;
out:
    curP = oldP;
    return ok;
}

/* Puts obj near x, y, z (1/8 tiles) with drop_around_place, or failing that exactly at
   x, y unless Obj_Elem_Fate culls it (nocull skips that); a culled object is freed.
   Returns 1 if the object is in the map. */
unsigned char far put_at(int x, int y, int z, struct Object far *obj, int range, unsigned char nocull)
{
    if (drop_around_place(obj, x, y, z, range))
        return 1;
    if (nocull || !(char)Obj_Elem_Fate(10, obj)) {   /* UW1: tested as a signed char */
        SET_FINEX_UNSIGNED(obj, x & 7);
        SET_FINEY(obj, y & 7);
        Obj_Add(&Map_GetAddr(x >> 3, y >> 3)->objects, obj);
        return 1;
    }
    Obj_FreeLinkChain(0L, obj);
    return 0;
}

/* put_at at src's position. */
unsigned char far near_mob_put_at(struct Object far *src, struct Object far *obj, int range, unsigned char nocull)
{
    return put_at((OBJ_HOMEX(src) << 3) + OBJ_FINEX(src), (OBJ_HOMEY(src) << 3) + OBJ_FINEY(src),
                  OBJ_Z(src), obj, range, nocull);
}

/* When set, drop_around_place tries the exact spot first. */
char stay_centered = 0;

/* Tries up to 24 random spots within range of x, y for obj with can_place, and adds it
   at the end of the first one's tile list; a mobile object gets its home tile, a static
   one is settled by obj_deal. Returns 0 if none fits. */
unsigned char far drop_around_place(struct Object far *obj, int x, int y, int z, int range)
{
    unsigned char tries;
    int span;
    int zz;
    struct Tile far *tile;
    int tx;
    int ty;

    span = range * 2 + 1;
    for (tries = 0; tries < 24; tries++) {
        if (tries == 0 && stay_centered) {
            tx = x;
            ty = y;
        } else {
            tx = x - range + rand() % span;
            ty = y - range + rand() % span;
        }
        zz = z;
        if (can_place(OBJ_ITEM(obj), Obj_MemTPtr(obj), tx, ty, zz, 1, 0)) {
            tile = Map_GetAddr(tx >> 3, ty >> 3);
            SET_FINEX_UNSIGNED(obj, tx & 7);
            SET_FINEY(obj, ty & 7);
            SET_Z(obj, zz);
            Obj_AddEnd(&tile->objects, obj);
            if (IsMobElem(obj)) {
                SET_HOMEX(obj, tx >> 3);
                SET_HOMEY(obj, ty >> 3);
            } else
                obj_deal(obj, tx >> 3, ty >> 3, 1);
            return 1;
        }
    }
    return 0;
}
