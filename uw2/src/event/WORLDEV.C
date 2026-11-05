/* target: ovr110 */
/* opts: -mm -1 -G -O -Y -d */
/* World events: teleports, terrain changes, traps, the castle guards, the pits, item repair,
   quest hacks and Killorn's fall: the whole of DOS overlay ovr110, in original order.

   What it does in the game: the actions behind the traps (TRIGGER.C's UseTrap and its hack
   traps), the schedules (SCDEVENT.C) and a number of plot events. The general part:
   do_teleport (and find_good_x_and_y, which finds room near the destination), changing
   tiles with everything standing on them (change_terrain, raise_up, put_down, filter),
   damage and spell traps, passing time (pass_time), deaths that matter to the plot
   (death_check, instant_kill, genocide), repairing items (repair_item, from the Repair
   skill and from conversations), fishing (go_fish), and the castle guards. The rest are
   single-purpose hacks for places in the game: the Britannia courtyard drying up and the
   castle schedule, the Pits of Carnage arena, the blackrock gem's facets, the vending
   machines, the Scintillus Academy's pillars, wand and potions, Bliy Skup Ductosnore's
   chamber, the q*bert floor puzzle, jail, and Killorn Keep's crash.

   Quest flags are read and written with GET_QUEST and SET_QUEST: bit (q & 3) of
   player->quests[q / 4], the same packing as TRIGGER.C's numbered variables 0x100 on. NPCs
   are named here by whoami, with the name the conversation strings give them (block 7,
   whoami + 16).

   Data owned: TK_wand, and the statics of courtyard_hacking, black_gem_trip, do_qbert and
   go_vend.
   Function and global names are the originals from the FM Towns symbol table where it has
   them. Each function's first comment gives its explanation; the second, tagged name:,
   its number in the file and IDA's name.
   Name: descriptive (world events and quest hacks: do_teleport, change_terrain, death_check). */
#include <stdlib.h>
#include <string.h>
#include "combat.h"
#include "conv.h"
#include "critter.h"
#include "event.h"
#include "file.h"
#include "gfx.h"
#include "inv.h"
#include "map.h"
#include "motion.h"
#include "object.h"
#include "player.h"
#include "sound.h"
#include "sys.h"
#include "ui.h"

/* match: declared here, not in event.h: TRIGGER.C defines set_numbered_variable with an
   unsigned char op, and this file's callers push an int. */
void far set_numbered_variable(int var, int how, int val);

#define GET_QUEST(q)    ((int)((player->quests[(q) >> 2] & (1 << ((q) & 3))) >> ((q) & 3)))
#define SET_QUEST(q, v) (player->quests[(q) >> 2] = \
            (player->quests[(q) >> 2] & ~(1 << ((q) & 3))) + ((v) << ((q) & 3)))

/* name: 1: TeleportCharToTile_ovr110_0. A breadth-first search outward from (x, y), at most
   20 tiles a ring, for a tile within the 9x9 box around it where the object fits. */
#define VISITED(tx, ty) (visited[(tx) - xmin] & (1 << ((ty) - ymin)))
#define VISIT(tx, ty) \
    if (!VISITED(tx, ty) && nnext < 20 \
        && (tx) >= xmin && (tx) <= xmax && (ty) >= ymin && (ty) <= ymax) { \
        next[nnext][0] = (tx); \
        next[nnext][1] = (ty); \
        nnext++; \
        visited[(tx) - xmin] |= 1 << ((ty) - ymin); \
    }
unsigned char far find_good_x_and_y(struct Object far *obj, int x, int y,
                                    int16 *nx, int16 *ny, char clear)
{
    unsigned char ncur, nnext;
    signed char xmin, ymin, xmax, ymax;
    unsigned char i;
    int tx, ty;
    struct Tile far *tile;
    union Link far *link;
    struct Object far *o;
    char cur_list[20][2], next_list[20][2];
    uint16 visited[9];
    register char (*next)[2];
    register char (*cur)[2];

    memset(cur_list, 0, 0x14);
    memset(next_list, 0, 0x14);
    memset(visited, 0, 9);
    xmin = max(1, x - 4);
    xmin = min(xmin, 0x3A);
    ymin = max(1, y - 4);
    ymin = min(ymin, 0x3A);
    xmax = xmin + 9;
    xmax = min(xmax, 0x3E);
    ymax = ymin + 9;
    ymax = min(ymax, 0x3E);
    ncur = 1;
    nnext = 0;
    cur = cur_list;
    next = next_list;
    cur[0][0] = x;
    cur[0][1] = y;
    visited[x - xmin] |= 1 << (y - ymin);
    while (ncur > 0) {
        for (i = 0; i < ncur; i = i + 1) {
            tx = cur[i][0];
            ty = cur[i][1];
            tile = Map_GetAddr(tx, ty);
            if (clear) {
                o = 0L;
                for (link = &tile->objects; link->f.index;
                     link = &o->qn.link) {
                    o = Obj_PtrTMem(link);
                    if (ComObjData[o->id & ID_ITEM].height || IsMobElem(o))
                        Obj_Punt(&tile->objects, o, 0);
                }
            }
            if (can_place(obj->id & ID_ITEM, Obj_MemTPtr(obj), tx * 8 + 3, ty * 8 + 3,
                          (tile->height << 3) + ((tile_walls[tile->type] & 0x20) ? 4 : 0),
                          0, 8)) {
                *nx = tx;
                *ny = ty;
                return 1;
            }
            switch (tile->type) {
            case TILE_SOLID:
                break;
            case TILE_DIAG_SE:
                VISIT(tx + 1, ty);
                VISIT(tx, ty - 1);
                break;
            case TILE_DIAG_SW:
                VISIT(tx - 1, ty);
                VISIT(tx, ty - 1);
                break;
            case TILE_DIAG_NE:
                VISIT(tx + 1, ty);
                VISIT(tx, ty + 1);
                break;
            case TILE_DIAG_NW:
                VISIT(tx - 1, ty);
                VISIT(tx, ty + 1);
                break;
            case TILE_OPEN:
            default:
                VISIT(tx - 1, ty);
                VISIT(tx, ty - 1);
                VISIT(tx + 1, ty);
                VISIT(tx, ty + 1);
                break;
            }
        }
        {
            char (*t)[2];
            t = cur;
            cur = next;
            next = t;
            ncur = nnext;
            nnext = 0;
        }
    }
    return 0;
}

/* Lets seconds of game time pass: the game clock (1/256 s units, inferred from the shift)
   moves on, the day clock XC_TIME becomes clock / 0x4B000 modulo 72 (so a step is 20
   minutes and the cycle a day), and the schedules catch up. */
/* name: 2: AddTimeToClock_ovr110_949, target size 0x79. */
void far pass_time(int32 seconds)
{
    int32 elapsed = seconds << 8;
    player->game_clock += elapsed;
    player->xclock[XC_TIME] = (player->game_clock / 0x4B000L) % 0x48L;
    Sched_SetAllClocks(1);
    lastDurCheck = player->game_clock >> 8;
}

/* Teleports who to square x, y of level (0 or this level: a nearby free square is found).
   Only the player can change level, and not while asleep in the void unless the destination
   is the Ethereal Void. A player leaving jail (quest 112, the cell at 0x2A..0x2B, 0x26 of
   levels 0 and 1) has escaped: quest 112 is cleared and quest 124 set. A fighter in the
   Pits who teleports out of his part of the arena has run away (arena_player_runs). The
   player's move is only recorded (NewPlayerLevel, NewPlayerX, NewPlayerY) for the main
   loop to carry out. Returns 0x10 on success, 2 otherwise. */
/* name: 3: Teleport_ovr110_9C2, target size 0x211. */
int far do_teleport(struct Object far *who, int x, int y, int level)
{
    int16 new_x, new_y;
    unsigned char ok;
    int old_x, old_y;
    register int dest = level;
    ok = 1;
    if (who == ThePlayer) {
        if (player->sleepbits && player->in_void && dest && (dest - 1) / 8 != 8)
            return 2;
        if (GET_QUEST(112)) {
            if (!((dest == 0 || dest == 1) && y == 0x26 && x >= 0x2A && x <= 0x2B)) {
                SET_QUEST(112, 0);
                SET_QUEST(124, 1);
            }
        }
    }
    if (dest != PlayerLevel && who != ThePlayer)
        return 2;
    if ((dest == 0 || dest == PlayerLevel) && x != 0x3F && y != 0x3F) {
        dest = PlayerLevel;
        if (!find_good_x_and_y(who, x, y, &new_x, &new_y, 0))
            ok = 0;
        else {
            x = new_x;
            y = new_y;
        }
    }
    if (ok) {
        old_x = OBJ_HOMEX(ThePlayer);
        old_y = OBJ_HOMEY(ThePlayer);
        if (who == ThePlayer) {
            if (player->in_pits) {
                if (player->in_pits) {
                    if (dest != PlayerLevel || !in_arena(x, y)
                        || (mpos((char)(x - 0x1F), (char)(y - 0x1F)) >> 1)
                         != (mpos((char)(old_x - 0x1F), (char)(old_y - 0x1F)) >> 1))
                        arena_player_runs();
                }
            }
            NewPlayerLevel = dest;
            NewPlayerX = x;
            NewPlayerY = y;
            editchng(0x20);
        } else
            trap_teleport_data = -1;
        return 0x10;
    }
    trap_teleport_data = -1;
    return 2;
}

/* change_terrain helper for a rising tile: lifts an object standing below the new floor
   height (mobiles keep their fine height in step, the player's physics too) and, on a tile
   that has become solid, removes static objects; objects pushed above the ceiling are
   destroyed. Returns the link to continue the list walk from. */
/* name: 4: RaiseZposOfObjectInChangingTile_ovr110_BD3, target size 0x135. */
union Link far * far raise_up(struct Tile far *tile, union Link far *head,
                            int old_height, int new_height, int tile_x,
                            int tile_y, char solid, int adjust)
{
    struct Object far *obj;
    unsigned char removed;
    register int height = new_height;
    removed = 0;
    obj = Obj_PtrTMem(head);
    if ((unsigned)(height * 8 + ComObjData[obj->id & ID_ITEM].height) > 0x7F)
        damage_item(obj, 0L, tile_x, tile_y, 0xFF, 0);
    if (OBJ_Z(obj) < (height << 3)) {
        SET_Z(obj, height << 3);
        if (IsMobElem(obj) && OBJ_MAJOR(obj) != MAJOR_CREATURE)
            obj->b0F = height << 6;
        else if (obj == ThePlayer)
            PN.z = height << 6;
        else if (!IsMobElem(obj) && solid)
            removed = Obj_Punt(&tile->objects, obj, 0) == 0L;
    }
    if (removed)
        return head;
    return &obj->qn.link;
}

/* change_terrain helper for a sinking tile: objects resting on the old floor drop with
   it (a critter is marked to fall; the player is re-settled on his terrain). Returns the
   link to continue from. */
/* name: 5: LowerZposOfObjectInChangingTile_ovr110_D08, target size 0x10B. */
union Link far * far put_down(struct Tile far *tile, union Link far *head,
                           int old_height, int new_height, int tile_x,
                           int tile_y, char remove_obj, int adjust)
{
    unsigned char removed;
    struct Object far *obj;
    register int height = new_height;
    removed = 0;
    obj = Obj_PtrTMem(head);
    if (OBJ_Z(obj) == (old_height << 3)) {
        SET_Z(obj, height << 3);
        if (obj == ThePlayer) {
            if (adjust == 1 || adjust == 3)
                PN.z = height << 6;
            else
                parse_player_terr(0x10, 1);
        } else if (IsMobElem(obj)) {
            if (OBJ_MAJOR(obj) != MAJOR_CREATURE)
                obj->b0F = height << 6;
            else
                SET_GRAVITY(obj, 1);
        } else if (remove_obj)
            removed = Obj_Punt(&tile->objects, obj, 0) == 0L;
    }
    if (removed)
        return head;
    return &obj->qn.link;
}

/* Whether an object on a tile next to a changing one moves with it: anything on the
   changing tile, or an object at the right height whose footprint reaches over the edge.
   Traps and MAJOR_RECT objects never move. */
/* name: 6: WillObjectMoveWithTileHeightChange_ovr110_E13, target size 0x124. */
unsigned char far filter(int tile_x, int tile_y, int cur_x, int cur_y,
                         int old_height, int new_height, union Link far *link)
{
    struct Object far *obj;
    int xpos;
    int zpos;
    register int ypos;
    register int radius;
    obj = Obj_PtrTMem(link);
    if (OBJ_MAJOR(obj) == MAJOR_TRAP || OBJ_MAJOR(obj) == MAJOR_RECT)
        return 0;
    if (tile_x == cur_x && tile_y == cur_y)
        return 1;
    zpos = obj->pos & POS_Z;
    old_height <<= 3;
    new_height <<= 3;
    if (old_height > new_height) {
        if (zpos != old_height)
            return 0;
    } else if (zpos < old_height || zpos >= new_height)
        return 0;
    xpos = ((tile_x - cur_x) << 3) + OBJ_FINEX(obj);
    ypos = ((tile_y - cur_y) << 3) + OBJ_FINEY(obj);
    radius = ComObjData[obj->id & ID_ITEM].radius;
    if (xpos < 0 && xpos + radius >= 0 || xpos > 7 && xpos - radius <= 7
        || tile_x == cur_x) {
        if (ypos < 0 && ypos + radius >= 0 || ypos > 7 && ypos - radius <= 7
            || tile_y == cur_y)
            return 1;
    }
    return 0;
}

/* Changes the tiles from x, y to x + dx, y + dy: wall and floor textures (0x3F and 0xF
   mean unchanged), type (10 or more unchanged) and height (adjust 1 and 3 raise or lower
   by one instead; 4 allows height 15). Objects on the tiles and overlapping from their
   neighbours rise or fall with the floor. A new water floor, or (at random) lava, makes the
   change remove static objects. Returns 2. */
/* name: 7: ChangeTile_ovr110_F37, target size 0x2F6. */
int far change_terrain(int x, int y, int wall, int floor, int height,
                       int type, int dx, int dy, int adjust)
{
    int endx, endy, xmin, xmax, tx, ymin, ymax, ty;
    struct Tile far *tile;
    struct Tile far *near_tile;
    int old_height;
    union Link far *head;
    unsigned char solid, raised;
    int terrain;
    register int cy, cx;
    solid = 0;
    endx = x + dx;
    endy = y + dy;
    for (cx = x; cx <= endx; cx++) {
        for (cy = y; cy <= endy; cy++) {
            tile = Map_GetAddr(cx, cy);
            old_height = tile->height;
            if (adjust == 1 || adjust == 3)
                height = old_height + 2 - adjust;
            if (height >= 0 && height <= 0xE)
                tile->height = height;
            if (height == 0xF && adjust == 4)
                tile->height = 0xF;
            if (floor < 0xF) {
                terrain = (TxmTerr[floor] & TERR_CLASS) >> 6;
                tile->floor = floor;
                if (terrain == TERRAIN_LAVA)
                    solid = rand() & 1;
                else if (terrain == TERRAIN_WATER)
                    solid = 1;
            }
            if (wall < 0x3F)
                TILE_WALL(tile) = wall;
            if (type < 10) {
                tile->type = type;
                if (type == TILE_SOLID)
                    solid = 1;
            }
            xmin = xmax = cx;
            ymin = ymax = cy;
            if (cx == x && cx > 1) xmin--;
            if (cx == endx && cx < 0x3F) xmax++;
            if (cy == y && cy > 1) ymin--;
            if (cy == endy && cy < 0x3F) ymax++;
            if (tile->height == old_height)
                continue;
            height = tile->height;
            raised = tile->height > old_height;
            for (tx = xmin; tx <= xmax; tx++) {
                for (ty = ymin; ty <= ymax; ty++) {
                    near_tile = Map_GetAddr(tx, ty);
                    if (raised) {
                        for (head = &near_tile->objects;
                             head->f.index != 0; ) {
                            if (filter(tx, ty, cx, cy, old_height, height, head))
                                head = raise_up(near_tile, head, old_height, height,
                                                cx, cy, solid, adjust);
                            else
                                head = &Obj_PtrTMem(head)->qn.link;
                        }
                    } else {
                        for (head = &near_tile->objects;
                             head->f.index != 0; ) {
                            if (filter(tx, ty, cx, cy, old_height, height, head))
                                head = put_down(near_tile, head, old_height, height,
                                                cx, cy, solid, adjust);
                            else
                                head = &Obj_PtrTMem(head)->qn.link;
                        }
                    }
                }
            }
        }
    }
    editchng(6);
    return 2;
}

/* The change-from trap (TRAP_CHANGE_FROM) and its linked change-to trap: over the area
   given by the trap's fine position (all of the map when 0), every tile whose wall, floor,
   type and height match the change-from values (a field at its limit matches anything)
   gets the change-to values (fields at their limit are left). */
/* name: 8: ChangeFromTrap_ovr110_122D, target size 0x1EE. */
void far do_change_grokking(struct Object far *trap, struct Object far *link,
                            int x, int y)
{
    int xstart, ystart, xend, yend, tx;
    int16 trapv[4];
    int16 linkv[4];
    int16 cur[4];
    unsigned char limits[4] = {0x3F, 0x0F, 0x0A, 0x0F};
    struct Tile far *tile;
    unsigned char ok;
    register int i, y_;
    xend = OBJ_FINEX(trap);
    yend = OBJ_FINEY(trap);
    trapv[0] = trap->qn.f.quality;
    trapv[1] = OBJ_HEADING(trap) + ((trap->pos & 0x10) >> 1);
    trapv[2] = trap->ol.f.owner;
    trapv[3] = trap->pos & 0xF;
    linkv[0] = link->qn.f.quality;
    linkv[1] = OBJ_HEADING(link) + ((link->pos & 0x10) >> 1);
    linkv[2] = link->ol.f.owner;
    linkv[3] = link->pos & 0xF;
    if (xend == 0) {
        xstart = 1;
        ystart = 1;
        xend = 0x3E;
        yend = 0x3E;
    } else {
        xstart = x;
        ystart = y;
        if (yend == 0)
            yend = xend;
    }
    for (tx = xstart; tx < xstart + xend; tx++) {
        for (y_ = ystart; y_ < ystart + yend; y_++) {
            tile = mapdata + (y_ << 6) + tx;
            cur[0] = TILE_WALL(tile);
            cur[1] = tile->floor;
            cur[2] = tile->type;
            cur[3] = tile->height;
            ok = 1;
            for (i = 0; i < 4; i++) {
                if (linkv[i] < limits[i]) {
                    ok &= trapv[i] == cur[i] || trapv[i] == limits[i];
                    cur[i] = linkv[i];
                }
            }
            if (ok)
                change_terrain(tx, y_, cur[0], cur[1], cur[3], cur[2], 0, 0, 0);
        }
    }
}

/* The damage trap: damage to object index with damage type 4 (physical). Negative damage
   poisons the player at that strength instead (unless he resists poison) or, for others,
   is plain damage. Returns 0x10 when the object was destroyed, else 2. */
/* name: 9: DamageTrap_ovr110_141B, target size 0xC0. */
int far whack_thing(int index, int damage, int how, int extra)
{
    struct Object far *target;
    target = Obj_IntTMem(index);
    if (damage < 0) {
        if (target == ThePlayer) {
            if ((unsigned)(-damage) > player->poison) {
                if (check_res(ThePlayer, 1, 0x10)) {
                    player->poison = -damage;
                }
            }
        } else {
            damage = -damage;
        }
    }
    if (damage > 0) {
        if (damage_item(target, 0L,
                        OBJ_HOMEX(target),
                        OBJ_HOMEY(target),
                        (unsigned char)damage, 4))
            return 0x10;
    }
    return 2;
}

/* A spell cast by a trap at x, y: by class and minor (major >= 0) or by spell number. */
/* name: 10: SpellTrap_ovr110_14DB, target size 0x50. */
int far inanimate_spell(int x, int y, struct Object far *trap,
                        struct Object far *who, int major, int effect)
{
    inanmMapX = x;
    inanmMapY = y;
    if (major >= 0)
        do_spell((unsigned char)major, (unsigned char)effect, trap, who);
    else
        cast((unsigned char)effect, trap, who);
    return 2;
}

/* Fishing: the player must face water (a non-solid tile of the water terrain class
   whose floor is below him) about a square ahead. Then a fish bites when a roll of 0..4 is at
   most (Track + 7) / 8, and is caught if he can carry it.
   Returns 1 for a catch, which the caller adds (USEITEMS.C, inferred). */
/* name: 11: UseFishingPole_ovr110_152B, target size 0xF7. */
unsigned char far go_fish(void)
{
    int16 x;
    int16 y;
    struct Tile far *tile;
    x = PN.x >> 5;
    y = PN.y >> 5;
    move_along(PlayerFacing >> 8, 0xB, &x, &y);
    tile = Map_GetAddr(x >> 3, y >> 3);
    if (tile->type != TILE_SOLID) {
        if (((TxmTerr[tile->floor] & TERR_CLASS) >> 6) != TERRAIN_WATER)
            goto bad_place;
        if ((OBJ_Z(ThePlayer) >> 3) <= (uint16)(tile->height - 1))
            goto bad_place;
    } else
        goto bad_place;
    if (rand() % 5 > (player->skills[SKILL_TRACK] + 7) / 8)
        goto no_luck;
    if ((uint16)(player->weight + ComObjData[ITEM_FISH].mass)   /* 16 bits, as Turbo C's */
        >= player->max_weight)
        goto no_room;
    game_sprint(0x70);  /* 'You catch a lovely fish.' */
    return 1;
no_room:
    game_sprint(0x73);  /* 'You feel a nibble, but the fish gets away.' */
    return 0;
no_luck:
    game_sprint(0x71);  /* 'No luck this time.' */
    return 0;
bad_place:
    game_sprint(0x72);  /* 'You cannot fish there. Perhaps somewhere else.' */
    return 0;
}

/* A conversation with no body: a rotworm is made at the player's square with the given
   whoami, talked to and removed again. */
/* name: 12: SpawnATalkingRotworm_ovr110_1622, target size 0xB2. */
void far talk_to_disembodied(char whoami)
{
    struct Object far *worm;
    struct Tile far *tile;
    worm = CreateObj(ITEM_ROTWORM, 1);
    worm->whoami = whoami;
    SET_ATTITUDE(worm, 3);
    SET_GOAL(worm, 0xA);
    tile = Map_GetAddr(OBJ_HOMEX(ThePlayer),
                       OBJ_HOMEY(ThePlayer));
    Obj_Add(&tile->objects, worm);
    TalkTo(worm);
    Obj_Rem(&tile->objects, worm);
    Obj_Free(worm);
}

/* The player has done something the owner (a race number) would object to: the owners
   nearby are told, as if he had taken their property (player_grabbed, CRITTIME.C). */
/* name: 13: HackTrapTrespass_ovr110_16D4, target size 0x51. */
void far player_did_bad(int owner)
{
    register int oldx, oldy;
    if (owner != 0) {
        oldx = MapObj_X;
        oldy = MapObj_Y;
        MapObj_X = OBJ_HOMEX(ThePlayer);
        MapObj_Y = OBJ_HOMEY(ThePlayer);
        player_grabbed(ThePlayer, (unsigned char)owner);
        MapObj_X = oldx;
        MapObj_Y = oldy;
    }
}

/* Hack 3 and 4, the eight-position switch: with the switch's position (flags) times 8
   added to the trap's height, hack 3 sets the height of the tile, hack 4 the height of the
   trap's linked object. */
/* name: 14: HackTrapChangeHeight_ovr110_1725, target size 0x84. */
void far eight_pos_switch(int flags, struct Object far *trap, int x, int y)
{
    struct Object far *obj;
    register int height;
    height = OBJ_Z(trap) + (flags << 3);
    if (trap->qn.f.quality == 3) {
        if (height < 0x68)
            change_terrain(x, y, 0xFF, 0xFF, height >> 3, 0xFF, 0, 0, 0);
    } else {
        obj = Obj_IntTMem((trap->ol.word >> 6) & 0x3FF);
        SET_Z(obj, height);
    }
}

/* Hacks 21 and 22: sets the linked object (and with multiple the next one in its group
   of five) to the trap's height, or that plus owner if it is not above it already: a
   platform that moves up and down. */
/* name: 15: HackTrapChangeObjectZPos_ovr110_17A9, target size 0xAA. */
void far toggle_object_height(struct Object far *trap, char multiple)
{
    struct Object far *obj;
    int i;
    register int link;
    register int z;
    for (i = 0; i <= (multiple != 0 ? 1 : 0); i++) {
        link = (trap->ol.word >> 6) & 0x3FF;
        link = (link / 5) * 5 + (link + i) % 5;
        obj = Obj_IntTMem(link);
        z = trap->pos & POS_Z;
        if (OBJ_Z(obj) <= z)
            z = z + trap->ol.f.owner;
        SET_Z(obj, z);
    }
    editchng(2);
}

/* A special-effect trap: types 5..8 flash the screen in colour arg + 0x40 * (8 - type),
   type 2 plays sound arg + 0x64, type 4 shakes the screen by 2 * arg. */
/* name: 16: SpecialEffects_ovr110_1853, target size 0x66. */
void far do_sfx(int type, int arg)
{
    switch (type) {
    case 5: arg += 0x40;
    case 6: arg += 0x40;
    case 7: arg += 0x40;
    case 8: fill_FB(arg); break;
    case 2:
        if (play_effect((unsigned char)arg + 0x64, 0, 0, 0) == 0xFF) ;
        break;
    case 4: set_effect(0x40, (unsigned char)arg << 1); break;
    }
}

/* Removes an NPC and its contents from the map (a gronk callback). */
/* name: 17: RemoveNPC_ovr110_18B9, target size 0x47. */
unsigned char far remove_whoami(struct Object far *obj)
{
    union Link far *head;
    head = &Map_GetAddr(OBJ_HOMEX(obj), OBJ_HOMEY(obj))->objects;
    Obj_FreeLinkChain(head, obj);
    return 1;
}

/* A pit fighter has lost: removes it from player->pit_fighters; when at most one was
   left, the fight is over (in_pits cleared). Returns 1 if it was one of them. */
/* name: 18: PitWarriorLosesFight_ovr110_1900, target size 0x5C. */
unsigned char far remove_opponent(struct Object far *npc)
{
    unsigned char removed;
    register int count = 0;
    register int i;
    removed = 0;
    for (i = 0; i < 5; i++) {
        if (player->pit_fighters[i] > 0) {
            count++;
            if (Obj_MemTPtr(npc) == player->pit_fighters[i]) {
                player->pit_fighters[i] = 0;
                removed = 1;
            }
        }
    }
    if (count <= 1)
        player->in_pits = 0;
    return removed;
}

/* The plot's special deaths. mode 0 is asked before a critter with a conversation starts
   to die (go_into_dying_sequence, AI.C): returning 0 keeps it alive. mode 1 is the death
   itself. Killing one of race 0xB (a trilkhun by the owner race strings of block 1, inferred)
   makes the rest of that race hostile (gronkify_attitude with 0); bloodworms killed on
   level 4 count in quest byte 7; a pit fighter's death counts towards the player's pit
   record (QB_PIT_RECORD), and XC_PIT_KILLS keeps the best. The castle's people (whoami
   0x81..0x8F, Syria 0xA8 and guard 0x95) cannot be killed: they drop to a third of their
   hit points and the guards are called, except Lady Tory (0x8D) once the castle plot has
   reached 8. By whoami: Altara 0x2D (quest 69), Mokpo 0x48 (quest 53), Mystell 0x2C (66),
   Bliy Skup Ductosnore 0x98 (fires two triggers, quest 122), a guard 0x4B (turns into a
   hordling the first time), Freemis 0x0B (10), Zaria 0x62 (25), Dorstag 0x63 (121), Blog
   0x65 (65), Bishop 6 (4), Relk 0x31 (123), Lord Umbria 0x1F (variable 0xF3 and a crime
   against race 0x15, golems by the race strings), Praecor Loth 0x20 (his liches die with
   him, quest 7), Mors Gotha 0x2F (in Killorn Keep she talks instead of dying, or vanishes
   after the crash, quest 54; elsewhere she talks and then dies, quest 64), the Listener
   0x91 (dies only to Altara's dagger, which breaks: castle plot + 1, quest 11), Krilner
   0x64 and Fissif 0x80 (yield and talk), Patterson 0x8C (talks as he dies; the castle plot
   moves to 0xD, or 0xE after the djinn), Nelson 0x8B (calls the guards in the castle after
   plot stage 0xB), and whoami 0x3A (quest byte 6 counts to 2, then quest 50). Many set
   XC_CHANGED so the schedules notice. Returns 1 to let the death go on. */
/* name: 19: SpecialDeathCases_ovr110_195C, target size 0x8B6. */
unsigned char far death_check(struct Object far *obj, unsigned char mode)
{
    unsigned char pit;
    pit = 0;
    if (mode && is_my_race(obj, 0xB))
        gronk_race(0xB, 1, 0, (char (far *)(struct Object far *, NEARPTR))gronkify_attitude);
    if (mode && OBJ_ITEM(obj) == ITEM_BLOODWORM && PlayerLevel == 4
        && player->quest_bytes[QB_WORMS_KILLED] < 0xC8)
        player->quest_bytes[QB_WORMS_KILLED]++;
    if (player->in_pits && mode)
        pit = remove_opponent(obj);
    if (pit) {
        player->quest_bytes[QB_PIT_RECORD]++;
        if (obj->whoami != 0x64)
            SET_QUEST(24, 1);
    }
    if (!mode && (obj->whoami >= 0x81 && obj->whoami <= 0x8F
                  || obj->whoami == 0xA8 || obj->whoami == 0x95)
        && !OBJ_LONER(obj)) {
        int hp;
        if (obj->whoami == 0x8D && player->xclock[XC_CASTLE] >= 8)
            return 1;
        hp = Creature[obj->id & ID_INMAJOR].avghit / 3 - 1;
        obj->hp = hp;
        call_out_the_guards(OBJ_HOMEX(obj), OBJ_HOMEY(obj));
        return 0;
    }
    switch (obj->whoami) {
    case 0x2D:
        SET_QUEST(69, mode);
        break;
    case 0x48:
        if (mode) {
            SET_QUEST(53, 1);
            player->xclock[XC_CHANGED]++;
        }
        break;
    case 0x2C:
        if (mode) {
            SET_QUEST(66, 1);
            player->xclock[XC_CHANGED]++;
        }
        break;
    case 0x98:
        if (mode) {
            fire_trigger_at(0x3A, 0x12);
            fire_trigger_at(0x3C, 0x12);
            SET_QUEST(122, 1);
        }
        break;
    case 0x4B:
        if (!mode && OBJ_ITEM(obj) != ITEM_HORDLING) {
            SET_ITEM(obj, ITEM_HORDLING);
            obj->hp = 0x5C;
            SET_GOAL(obj, 5);
            return 0;
        }
        break;
    case 0x0B:
        SET_QUEST(10, 1);
        break;
    case 0x62:
        SET_QUEST(25, 1);
        if (pit)
            player->quest_bytes[QB_PIT_RECORD] = player->quest_bytes[QB_PIT_RECORD] + 3;
        break;
    case 0x63:
        if (mode) {
            SET_QUEST(121, 1);
            if (!GET_QUEST(23))
                player->quest_bytes[QB_PIT_RECORD] = player->quest_bytes[QB_PIT_RECORD] + 6;
        }
        break;
    case 0x65:
        if (mode) {
            SET_QUEST(65, 1);
            player->xclock[XC_CHANGED]++;
            if (GET_QUEST(22)
                && !GET_QUEST(23))
                SET_QUEST(22, 0);
            if (GET_QUEST(23))
                player->quest_bytes[QB_PIT_RECORD] = player->quest_bytes[QB_PIT_RECORD] + 6;
        }
        break;
    case 6:
        if (mode)
            SET_QUEST(4, 1);
        break;
    case 0x31:
        if (mode)
            SET_QUEST(123, 1);
        break;
    case 0x1F:
        if (mode) {
            set_numbered_variable(0xF3, 2, 1);
            player_did_bad(0x15);
        }
        break;
    case 0x20:
        if (mode) {
            genocide(0x17);
            SET_QUEST(7, 1);
            fire_trigger_at(1, 2);
        }
        break;
    case 0x2F:
        if (!mode) {
            if ((PlayerLevel - 1) / 8 == 2) {
                struct Tile far *tile;
                tile = Map_GetAddr(OBJ_HOMEX(obj), OBJ_HOMEY(obj));
                if (!GET_QUEST(54))
                    stop_and_talk(obj);
                else
                    Obj_Punt(&tile->objects, obj, 1);
            } else {
                stop_and_talk(obj);
                SET_QUEST(64, 1);
                player->xclock[XC_CHANGED]++;
                return 1;
            }
            return 0;
        }
        break;
    case 0x91:
        if (!mode) {
            struct Object far *dagger;
            if (!using_altaras_dagger) {
                obj->hp = 1;
                SET_GOAL(obj, 5);
                return 0;
            }
            dagger = AskInventory(8 - player->lefty);
            InvRemoveOneObject(dagger);
            SET_ITEM(dagger, ITEM_BROKEN_DAGGER);
            near_mob_put_at(ThePlayer, dagger, 6, 0);
            player->xclock[XC_CASTLE]++;
            SET_QUEST(11, 1);
        }
        break;
    case 0x64:
        if (pit)
            player->quest_bytes[QB_PIT_RECORD]--;
        if (GET_QUEST(28))
            return 1;
    case 0x80:
        if (obj->whoami == 0x80 && GET_QUEST(119))
            return 1;
        if (!mode) {
            if (!OBJ_TERRAIN(obj)) {
                SET_TERRAIN(obj, 1);
                player_get_exp(0x32);
                stop_and_talk(obj);
                return 0;
            }
            return 1;
        }
        break;
    case 0x8C:
        if (mode) {
            int old;
            old = player->xclock[XC_CASTLE];
            stop_and_talk(obj);
            player->xclock[XC_CASTLE] = 0xD;
            if (player->xclock[XC_DJINN] >= 6)
                player->xclock[XC_CASTLE] = 0xE;
            if (player->xclock[XC_CASTLE] < old) {
                player->xclock[XC_CASTLE] = old;
                return 1;
            }
        }
        return 1;
    case 0x8B:
        if (mode) {
            if (player->xclock[XC_CASTLE] >= 0xB
                && OBJ_HOMEX(obj) >= 0x14
                && OBJ_HOMEY(obj) >= 0x25
                && OBJ_HOMEX(obj) <= 0x16
                && OBJ_HOMEY(obj) <= 0x27) {
                call_out_the_guards(0x12, 0x28);
                return 1;
            }
            return 1;
        }
        break;
    case 0x8D:
        if (mode && player->xclock[XC_CASTLE] < 8)
            return 0;
        return 1;
    case 0x3A:
        if (mode) {
            player->quest_bytes[6]++;
            if (player->quest_bytes[6] >= 2) {
                SET_QUEST(50, 1);
                do_sfx(4, 0x2C);
                player->quest_bytes[6] = 0x18;
                return 1;
            }
        }
        return 1;
    case 3:
        return 1;
    }
    if (player->quest_bytes[QB_PIT_RECORD] > player->xclock[XC_PIT_KILLS])
        player->xclock[XC_PIT_KILLS] = player->quest_bytes[QB_PIT_RECORD];
    return 1;
}

/* Makes a critter stop fighting and talk: friendly, goal 8, the player's fight state
   cleared, time running, then the conversation. */
/* name: 20: TalkToDyingNPC_ovr110_2212, target size 0x6C. */
void far stop_and_talk(struct Object far *obj)
{
    obj->last_hit = 0;
    clear_fight_state();
    SET_SEQ(obj, 0);
    SET_FRAME(obj, 0);
    SET_ATTITUDE(obj, 2);
    SET_GOAL(obj, 8);
    TimeStop = 0;
    TalkTo(obj);
    FixPlayerEquips();
    SET_NOHEAL(obj, 0);
}

/* The castle guards: every scheduled trigger (mode 0xC) in the guard post square
   (0xF, 0x1D) sends its linked guard to home_x, home_y, hostile and powerful, by firing
   the trigger. */
/* name: 21: RunScheduleTriggersInTile_15_29_ovr110_227E, target size 0x12D. */
void far call_out_the_guards(int home_x, int home_y)
{
    struct Object far *scheduled;
    struct Object far *linked;
    int dx;
    int dy;
    register int x;
    register int y;
    scheduled = Obj_FindInMapSquare(MAJOR_TRAP, 2, 0xC, 0xF, 0x1D);
    while (scheduled) {
        x = (scheduled->qn.f.quality << 3) + 3;
        y = (scheduled->ol.f.owner << 3) + 3;
        dx = x - home_x;
        dy = y - home_y;
        if (abs(dx) <= 6 && abs(dy) <= 6)
            if (!player_looking(x, y)) ;
        linked = Obj_PtrTMem(&Obj_PtrTMem(&scheduled->ol.link)->ol.link);
        if (linked) {
            linked->qn.f.quality = home_x;
            linked->ol.f.owner = home_y;
            SET_GOAL(linked, 1);
            SET_TEMP(linked, 1);
            SET_POWERFUL(linked, 1);
        }
        UseTrigger(0L, 0L, scheduled, -1);
        scheduled = Obj_PtrTMem(&scheduled->qn.link);
    }
}

/* True when x, y is inside the Pits of Carnage arena, a ring around (0x1F, 0x1F) whose
   outer edge depends on the quarter. */
/* name: 22: PitArenaRelated_ovr110_23AB, target size 0x79. */
unsigned char far in_arena(int x, int y)
{
    register int oct = 0;
    unsigned char limits[4] = {0xF, 0x10, 0xE, 0xF};
    x -= 0x1F;
    y -= 0x1F;
    oct = mpos((char)x, (char)y) >> 1;
    x = abs(x);
    y = abs(y);
    if (x < 3 || y < 3)
        return 0;
    if (limits[oct] < x || limits[oct] < y)
        return 0;
    return 1;
}

/* An item's durability for repair: from the Weapons or Armour tables; -1 for anything
   else (missiles included), which cannot be repaired. */
/* name: 23: GetItemDurability_ovr110_2424, target size 0x6D. */
int far get_rep_diff(struct Object far *obj)
{
    register int item;
    switch OBJ_MAJOR(obj) {
    case MAJOR_HACK:
        item = obj->id & ID_INCLASS;
        switch OBJ_MINOR(obj) {
        case 0: return (char)Weapons[item].durability;
        case 1: return -1;
        case 2: ;
        case 3: return (char)Armor[OBJ_INMAJOR(obj) - FIRST_ARMOR].durability;
        }
    }
    return -1;
}

/* One repair attempt with skill: it takes durability * 3 - skill - quality / 2 minutes
   (at least 15). skill_check(skill, durability): a critical restores the item fully, a
   success adds 3 + skill / 5 quality, a plain result does nothing, a failure takes off 4..11
   quality, or destroys the item when a random 0..63 beats quality + skill. Returns 3 fully
   repaired, 2 partly, 1 no effect, -1 damaged, -2 destroyed, 0 not repairable. */
/* name: 24: RepairItem_ovr110_2491, target size 0x118. */
int far do_repair(struct Object far *obj, int skill, int16 *time)
{
    int repair_time, result, durability;
    register int new_quality, quality;
    if ((durability = get_rep_diff(obj)) == -1) return 0;
    quality = obj->qn.f.quality;
    repair_time = durability * 3 - skill - quality / 2;
    if (repair_time < 15) repair_time = 15;
    *time = repair_time;
    result = skill_check(skill, durability);
    switch (result) {
    case 2:
        new_quality = 64;
        break;
    case 1:
        new_quality = 3 + skill / 5;
        break;
    case 0:
        return 1;
    case -1:
        if ((unsigned)(rand() & 0x3F) > obj->qn.f.quality + skill) return -2;
        new_quality = -(4 + (rand() & 7));
        break;
    }
    if (quality + new_quality > 63) {
        obj->qn.f.quality = 63;
        return 3;
    }
    if (quality + new_quality > 0) {
        obj->qn.f.quality = quality + new_quality;
        if (new_quality > 0) return 2;
        return -1;
    }
    obj->qn.f.quality = 0;
    return -2;
}

/* Repairs obj with skill, by the player (who set) or by an NPC in a conversation. The
   player is told the difficulty first, by durability - skill + 15: trivial, simple,
   possible, hard, very difficult, and asked to confirm. The attempt is noisy and takes its
   time; a destroyed item may survive by its fate. The result is printed from string 0x9C +
   result. */
/* name: 25: ItemRepairLogic_ovr110_25A9, target size 0x1F0. */
void far repair_item(struct Object far *obj, int skill, char who)
{
    int16 time;
    int durability;
    int reply;
    unsigned char answer;
    char name[80] = "giSoink";
    register int estimate, result;
    answer = 1;
    get_name(name, obj, 0, 0);
    if (who) {
        durability = get_rep_diff(obj);
        if (durability >= 0) {
            estimate = durability - skill + 15;
            if (estimate < 0)
                estimate = 0;
            else if (estimate > 30)
                estimate = 4;
            else
                estimate = estimate / 10 + 1;
            game_sprint(0xE7);  /* 'You think it will be ' */
            game_sprint(estimate + 0xEA);  /* 'trivial', 'simple', 'possible', 'hard', 'very difficult' */
            game_sprint(0xE8);  /* ' to repair the ' */
            scroll_print(name);
            reply = wyorn(0, 0xE9, &answer);  /* 'Make an attempt? ' */
            if (reply != 0 && reply < 4)
                wd_bool(answer = reply == 2);
            scroll_print("\n");
            if (!answer)
                return;
        } else {
            game_sprint(0x9C);  /* 'You cannot repair that.' */
            return;
        }
    }
    result = do_repair(obj, skill, &time);
    if (who) {
        playerdat->noise = 0xF;
        pass_time(time * 60L);
        if (result == -2) {
            if (Obj_Elem_Fate(10, obj)) {
                InvRemoveOneObject(obj);
                Obj_Punt(0L, obj, 1);
            } else
                result = 0;
        }
        game_sprint(result + 0x9C);  /* -2 'You destroy the ' .. 0 'You cannot repair that.' .. 3 'You fully repair the ' */
        if (result) {
            scroll_print(name);
            game_sprint(0x60);  /* '.' */
        }
        FixPlayerEquips();
        editchng(0x200);
    } else if (result == -2)
        Obj_Punt(&Map_GetAddr(MapObj_X, MapObj_Y)->objects, obj, 0);
}

/* Clears an object's "already examined" bit (heading bit 2), so a better Lore skill can
   identify it again. Not for mobiles, MAJOR_RECT, traps or objects drawn as 3D models
   (the Guide, on the lore skill). */
/* name: 26: ClearHeadingBit2ovr110_2799, target size 0x76. Named clear_loretry_ in FM
   Towns, at the same position among its neighbours, and the code corresponds. */
unsigned char far clear_loretry(struct Object far *obj)
{
    if (IsMobElem(obj)) return 0;
    if (OBJ_MAJOR(obj) == MAJOR_RECT || OBJ_MAJOR(obj) == MAJOR_TRAP
        || ComObjData[obj->id & ID_ITEM].render == 2) return 0;
    SET_HEADING(obj, OBJ_HEADING(obj) & 3);
    return 0;
}

/* Clears the examined bit on every object on the map (after the Lore skill rises). */
/* name: 27: ClearHeadingBit2FromAllObjects_ovr110_280F, target size 0x6D. */
void far clear_all_loretries(void)
{
    struct Tile far *tile;
    union Link far *head;
    register int x, y;
    tile = mapdata;
    for (y = 0; y < MAP_SIZE; y++) {
        for (x = 0; x < MAP_SIZE; x++, tile++) {
            head = &tile->objects;
            if (head->f.index > 0)
                Obj_Check(Obj_PtrTMem(head), clear_loretry);
        }
    }
}

/* Hack 20, the Scintillus pillars: toggles the height of the pillars marked in mask, a
   5 by 3 grid of tiles three apart from x, y, between lower and upper. */
/* name: 28: ScintillusPlatformsTrap_ovr110_287C, target size 0xA8. */
void far toggle_pillars_hack(int x, int y, int lower, int upper, int mask)
{
    int tile_x, tile_y, j;
    register int new_height, i;
    for (i = 0; i < 5; i++) {
        for (j = 0; j < 3; j++) {
            if (mask & (1 << (i * 3 + j))) {
                tile_x = x + i * 3;
                tile_y = y + j * 3;
                new_height = Map_GetAddr(tile_x, tile_y)->height;
                new_height = new_height <= lower ? upper : lower;
                change_terrain(tile_x, tile_y, 0x3F, 0xF, new_height,
                               0xF, 0, 0, 0);
            }
        }
    }
}

/* Hack 36, the castle NPCs' day: while quest 109 is set and the castle plot is below
   16, each castle person (whoami 0x81..0x8F and Syria) may go to their next place
   (maybe_go_hang_out, CRITTIME.C), except Lord British once jailed (quest 112), Lady Tory
   from plot stage 8 and Nelson and Patterson from stage 11. */
/* name: 29: HackTrapCastleSchedule_ovr110_2924, target size 0x99. */
void far move_folks_around(void)
{
    register int who, hour;
    hour = player->xclock[XC_CASTLE];
    if (GET_QUEST(109) && hour < 16) {
        for (who = 0x81; who <= 0x8F; who++) {
            if (who == 0x8E && GET_QUEST(112)) continue;
            if (who == 0x8D && hour >= 8) continue;
            if ((who == 0x8B || who == 0x8C) && hour >= 11) continue;
            gronk_whoami(who, 0, 0, (WhoamiFn)maybe_go_hang_out);
        }
        gronk_whoami(0xA8, 0, 0, (WhoamiFn)maybe_go_hang_out);
    }
}

/* Courtyard callback: with chance one in three, a plant wilts (0xD9 to 0xDA), a wilted
   plant becomes debris, grass or a mushroom, and it shifts a little. */
/* name: 30: GrowMushroomsInBritannia_ovr110_29BD, target size 0x1B1. Named kill_plants_ in
   FM Towns, at the same position among its neighbours, and the code corresponds. */
unsigned char far kill_plants(struct Object far *obj)
{
    int x;
    register int y;
    register int item = -1;
    if ((int)(((int32)rand() * 3) / 0x8000L) == 0) {
        switch OBJ_ITEM(obj) {
        case ITEM_PLANT_D9:
            item = ITEM_PLANT_DA;
            break;
        case ITEM_PLANT_DA:
            item = ITEM_PILE_OF_DEBRIS_D6;
            if ((int)(((int32)rand() * 2) / 0x8000L))
                item = ITEM_GRASS;
            if ((int)(((int32)rand() * 2) / 0x8000L))
                item = ITEM_MUSHROOM;
            break;
        case ITEM_GRASS:
            switch ((int)(((int32)rand() * 0xF) / 0x8000L)) {
            case 0: item = ITEM_MUSHROOM; return 0;     /* the new item is never used (as in DOS) */
            case 1: item = ITEM_PILE_OF_DEBRIS_D6; return 0;
            }
        default:
            return 0;
        }
        if (item >= 0)
            SET_ITEM(obj, item);
        x = OBJ_FINEX(obj);
        y = OBJ_FINEY(obj);
        if (x != 0 && x != 7)
            SET_FINEX(obj, x + (int)(((int32)rand() * 2) / 0x8000L)
                           + (int)(((int32)rand() * 2) / 0x8000L) - 1);
        if (y != 0 && y != 7)
            SET_FINEY(obj, y + (int)(((int32)rand() * 2) / 0x8000L)
                           + (int)(((int32)rand() * 2) / 0x8000L) - 1);
    }
    return 0;
}

/* Hack 34, Britannia goes dry: once per stage of the castle plot, the plants in the area
   wilt (kill_plants); from stage 3 the fountains' water animations go and their floor
   changes, and from stage 13 mushrooms spring up in 43% of the squares. */
/* name: 31: BritanniaGoesDry_ovr110_2B6E, target size 0x277. */
void far courtyard_hacking(int x, int y, struct Object far *trap)
{
    /* the castle plot stages already handled */
    /* name: no FM Towns name (static) */
    static int16 dried = 0;
    struct Tile far *tile;
    int hour;
    int xmax, ymax;
    struct Object far *obj;
    union Link far *head;
    int nx, ny;
    register int i, j;
    hour = player->xclock[XC_CASTLE];
    if (dried & (1 << hour))
        return;
    dried |= 1 << hour;
    xmax = x + OBJ_FINEX(trap);
    ymax = y + OBJ_FINEY(trap);
    for (i = x; i <= xmax; i++) {
        for (j = y; j <= ymax; j++) {
            tile = Map_GetAddr(i, j);
            head = &tile->objects;
            if (head->f.index > 0)
                Obj_Check(Obj_PtrTMem(head), kill_plants);
            if (hour >= 13) {
                if ((int)(((int32)rand() * 100) / 0x8000L) < 0x2B) {
                    nx = (int)(((int32)rand() * 6) / 0x8000L) + 1;
                    ny = (int)(((int32)rand() * 6) / 0x8000L) + 1;
                    if (can_place(ITEM_MUSHROOM, 0, (i << 3) + nx, (j << 3) + ny,
                                  tile->height << 3, 0, 0)) {
                        obj = CreateObj(ITEM_MUSHROOM, 0);
                        SET_FINEX(obj, nx);
                        SET_FINEY(obj, ny);
                        SET_Z(obj, tile->height << 3);
                        Obj_Add(&tile->objects, obj);
                    }
                }
            } else if (hour >= 3) {
                obj = Obj_FindInMapSquare(MAJOR_ANIMOBJ, 0, 9, i, j);
                if (obj) {
                    Obj_Punt(head, obj, 1);
                    tile->floor = trap->ol.f.owner;
                }
                obj = Obj_FindInMapSquare(MAJOR_SPEC, 2, 0xE, i, j);
                if (obj)
                    obj->qn.f.quality = 0;
            }
        }
    }
}

/* Hack 25, Bliy Skup Ductosnore's chamber: after his death (quest 122), with the right
   objects in place on squares (0x3A, 4), (0x39, 4) and (0x3B, 4) of quality 2 or 6, the two
   crystals are moved to the middle and the trigger at (0x3A, 5) fires; the quest flag is
   cleared. */
/* name: 32: HackTrapBlySkupChamber_ovr110_2DE5, target size 0x1BB. */
void far skup_ductosnore(void)
{
    struct Object far *obj;
    struct Object far *crystal;
    union Link far *head;
    if (!GET_QUEST(122))
        return;
    obj = Obj_FindInMapSquare(MAJOR_MISC, 2, 0xE, 0x3A, 4);
    if (obj == 0)
        return;
    obj = Obj_FindInMapSquare(MAJOR_MISC, 2, 1, 0x39, 4);
    if (obj == 0)
        return;
    if (obj->qn.f.quality != 2 && obj->qn.f.quality != 6)
        return;
    crystal = obj;
    obj = Obj_FindInMapSquare(MAJOR_MISC, 2, 1, 0x3B, 4);
    if (obj == 0)
        return;
    if (obj->qn.f.quality != 2 && obj->qn.f.quality != 6)
        return;
    head = &Map_GetAddr(0x39, 4)->objects;
    if (Obj_Rem(head, crystal))
        put_at(0x1D3, 0x1B, Map_GetAddr(0x3A, 3)->height << 3, crystal, 3, 1);
    head = &Map_GetAddr(0x3B, 4)->objects;
    if (Obj_Rem(head, obj))
        put_at(0x1D3, 0x1B, Map_GetAddr(0x3A, 3)->height << 3, obj, 3, 1);
    fire_trigger_at(0x3A, 5);
    SET_QUEST(122, 0);
}

/* Hack 12, a standing wave: the seven tiles north of x, y take heights that follow a
   wave whose phase is the trap's owner (counted up each time by the caller). */
/* name: 33: HackTrapOscillateTiles_ovr110_2FA0, target size 0xC2. */
void far standing_wave(int x, int y, int owner)
{
    struct Tile far *tile;
    int adjustment, height;
    register int i, tile_y = y;
    int16 oscillator[8] = {0, 1, 2, 1, 0, -1, -2, -1};
    tile = Map_GetAddr(x, tile_y);
    height = tile->height;
    adjustment = owner < 8 ? 4 - abs(4 - owner) : abs(12 - owner) - 4;
    for (i = 1; i < 8; i++) {
        tile = Map_GetAddr(x, tile_y - i);
        change_terrain(x, tile_y - i, 0x3F, 0xF,
                       height + adjustment * oscillator[i] / 2,
                       0xF, 0, 0, 0);
    }
}

/* Hack 14: cycles the floor (and with wall, the wall) textures first..last one step in
   the rectangle. */
/* name: 34: TileTextureCycle_ovr110_3062, target size 0xC9. */
void far cycle_floor(int x, int y, int width, int height,
                     int first, int last, char wall)
{
    struct Tile far *tile;
    int xmax, ymax;
    register int texture, row;
    xmax = x + width;
    ymax = y + height;
    for (; x <= xmax; x++) {
        for (row = y; row <= ymax; row++) {
            tile = Map_GetAddr(x, row);
            texture = tile->floor;
            if (texture <= last && texture >= first) {
                if (++texture > last) texture = first;
                tile->floor = texture;
            }
            if (wall != 0) {
                texture = TILE_WALL(tile);
                if (texture <= last && texture >= first) {
                    if (++texture > last) texture = first;
                    TILE_WALL(tile) = texture;
                }
            }
        }
    }
    editchng(6);
}

/* Hack 24, graffiti: in the 9 by 9 area, writings (MAJOR_RECT minor 2 class 0xE) showing
   one text change to another with a chance; five pairs by owner. */
/* name: 35: HackTrapGraffiti_ovr110_312B, target size 0x14E. */
void far do_graffiti(int x, register int y, int owner)
{
    struct Object far *obj;
    union Link far *next;
    int xmax, ymax;
    int startx, starty;
    int from;
    int prob;
    register int to;
    from = 0x3E;
    to = 0x25;
    switch (owner) {
    case 1: from = 0x3E; to = 0x25; prob = 0x10; break;
    case 2: from = 0x3F; to = 0x2A; prob = 10; break;
    case 3: from = 0x3D; to = 0x3C; prob = 6; break;
    case 4: from = 0x3B; to = 0x3A; prob = 6; break;
    case 5: from = 0x29; to = 0x2A; prob = 0x10; break;
    default: return;
    }
    xmax = x + 8;
    ymax = y + 8;
    startx = x;
    starty = y;
    for (x = startx; x <= xmax; x++) {
        for (y = starty; y <= ymax; y++) {
            for (obj = Obj_FindInMapSquare(MAJOR_RECT, 2, 0xE, x, y); obj; ) {
                if (obj->ol.f.owner == from && rand() % 16 < prob)
                    obj->ol.f.owner = to;
                next = &obj->qn.link;
                if (Obj_PtrTMem(next))
                    obj = Obj_InList(&next, 0, MAJOR_RECT, 2, 0xE);
                else
                    obj = 0;
            }
        }
    }
}

/* Hack 19: sets each arrow pillar's tile (marked by a trigger of class 4) back to its
   trigger's height, along the rows from x, y to the first wall, choosing a floor texture
   by direction when owner is set. */
/* name: 36: HackTrapPlatformReset_ovr110_3279, target size 0x135. */
void far reset_arrow_pillars(int x, int y, char owner)
{
    struct Object far *trig;
    union Link far *head;
    struct Tile far *tile;
    int height;
    int floor;
    int dy, dx;
    register int i, j;
    for (i = x; i < 0x3F; i++) {
        for (j = y; j < 0x3F; j++) {
            tile = Map_GetAddr(i, j);
            if (tile->type == TILE_SOLID) {
                if (j == y)
                    i = 0x3F;
                j = 0x3F;
                continue;
            }
            head = &tile->objects;
            trig = Obj_InList(&head, 0, MAJOR_TRAP, 2, 4);
            if (trig == 0)
                trig = Obj_InList(&head, 0, MAJOR_TRAP, 3, 4);
            if (trig == 0)
                continue;
            height = OBJ_Z(trig) >> 3;
            if (tile->height == height)
                continue;
            dx = trig->qn.f.quality - i;
            dy = trig->ol.f.owner - j;
            if (owner)
                floor = (dy == 0) | (dx < 0 || dy < 0) << 1;
            else
                floor = 0xF;
            change_terrain(i, j, 0x3F, floor, height, 0x10, 0, 0, 0);
        }
    }
}

/* Hack 10, the arena's prize (inferred): turns the weapon of class owner on x, y into
   something for the player's best skill: a mani stone (or a wooden shield with no magic)
   for a caster, leather gloves for a brawler, 30..41 sling stones for a missile user,
   otherwise a short sword, hand axe or cudgel. */
/* name: 37: HackTrap_ClassItem_ovr110_33AE, target size 0x15F. */
void far change_weapon_playerbest(int x, int y, int owner)
{
    struct Object far *obj;
    int magic;
    int combat;
    int type;
    int quality;
    int qty;
    register int i;
    register int item;
    quality = -1;
    qty = 0;
    combat = -1;
    for (i = SKILL_BAREHAND; i <= SKILL_MISSILE; i++) {
        if (player->skills[i] > combat) {
            combat = player->skills[i];
            type = i;
        }
    }
    if (player->skills[SKILL_CASTING] > player->skills[SKILL_MANA])
        magic = player->skills[SKILL_CASTING];
    else
        magic = player->skills[SKILL_MANA];
    if (magic >= combat) {
        if (magic > 0)
            item = ITEM_MANI_STONE;
        else
            item = ITEM_WOODEN_SHIELD;
        quality = 0x3F;
    } else {
        switch (type) {
        case SKILL_BAREHAND:
            item = ITEM_LEATHER_GLOVES;
            quality = 0x3F;
            break;
        case SKILL_MISSILE:
            item = ITEM_SLING_STONE;
            qty = (int)(((int32)rand() << 3) / 0x8000L) + (int)(((int32)rand() << 2) / 0x8000L) + 0x1E;
            break;
        case SKILL_SWORD:
            item = ITEM_SHORTSWORD;
            break;
        case SKILL_AXE:
            item = ITEM_HAND_AXE;
            break;
        case SKILL_MACE:
            item = ITEM_CUDGEL;
            break;
        }
    }
    obj = Obj_FindInMapSquare(MAJOR_HACK, 0, owner, x, y);
    if (obj) {
        SET_ITEM(obj, item);
        if (quality != -1)
            obj->qn.f.quality = quality;
        if (qty > 0) {
            SET_ISQUANT(obj, 1);
            obj->ol.f.link = qty;
        }
    }
}

/* Hack 11, fraznium: the force field on x, y is lifted out of the way (height 0x7F) when
   the player wears the fraznium gauntlets or circlet, else lowered; owner set lowers it
   regardless. */
/* name: 38: HackTrap_ForceField_ovr110_350D, target size 0xDC. */
void far check_fraznium(int x, int y, unsigned char owner)
{
    union Link far *head;
    struct Object far *field;
    struct Object far *obj;
    unsigned char gloves;
    gloves = 0;
    if (!owner) {
        obj = Obj_PtrTMem(&Inventory[2]);
        if (!(gloves = obj != 0 && OBJ_ITEM(obj) == ITEM_FRAZNIUM_GAUNTLETS)) {
            obj = Obj_PtrTMem(&Inventory[0]);
            gloves = obj != 0 && OBJ_ITEM(obj) == ITEM_FRAZNIUM_CIRCLET;
        }
    }
    head = &Map_GetAddr(x, y)->objects;
    field = Obj_InList(&head, 0, MAJOR_RECT, 2, 0xD);
    if (field)
        SET_Z(field, gloves ? 0x7F : 0);
}

/* Hack 18: flips the switch on x, y to position owner and fires its use triggers. */
/* name: 39: FindAndUseSwitch_ovr110_35E9, target size 0x59. */
void far switch_flip_hack(int x, int y, int owner)
{
    struct Object far *obj;
    obj = Obj_FindInMapSquare(MAJOR_RECT, 3, -1, x, y);
    if (obj && flip_switch(obj, owner))
        checkTrap(0L, obj, 4, x, y);
}

/* play_with_switches callback: flips a switch at random. */
/* name: 40: RandomSwitchFlicker_ovr110_3642, target size 0x44. Named maybe_flip_a_switch_
   in FM Towns, at the same position among its neighbours, and the code corresponds. */
unsigned char far maybe_flip_a_switch(struct Object far *obj)
{
    switch OBJ_CLASS(obj) {
    case CLASS_SWITCH:
        if ((int)(((int32)rand() * 2) / 0x8000L))
            flip_switch(obj, 3);
    }
    return 0;
}

/* Hack 29: flips switches at random in the 5 by 2 area from x, y. */
/* name: 41: FlickSwitchesInTileRandomly_ovr110_3686, target size 0x73. */
void far play_with_switches(int x, int y)
{
    union Link far *head;
    register int dy, dx;
    for (dx = 0; dx < 5; dx++) {
        for (dy = 0; dy < 2; dy++) {
            head = &Map_GetAddr(x + dx, y + dy)->objects;
            if (head->f.index > 0)
                Obj_Check(Obj_PtrTMem(head), maybe_flip_a_switch);
        }
    }
    editchng(2);
}

/* recharge_lightbulbs callback: a light sphere gets full quality. */
/* name: 42: RechargeLightSphere_ovr110_36F9, target size 0x24. Named recharge_a_lightbulb_
   in FM Towns, at the same position among its neighbours, and the code corresponds. */
unsigned char far recharge_a_lightbulb(struct Object far *obj)
{
    switch OBJ_ITEM(obj) {
    case ITEM_LIGHT_SPHERE:
        obj->qn.f.quality = 0x3F;
    }
    return 0;
}

/* Hack 35: recharges the light spheres on x, y, with a sparkle on each. */
/* name: 43: RechargeLightSpheresInTile_ovr110_371D, target size 0xAB. */
void far recharge_lightbulbs(int x, int y)
{
    union Link far *head;
    struct Object far *obj;
    register int tx = x;
    register int ty = y;
    head = &Map_GetAddr(tx, ty)->objects;
    if (head->f.index != 0) {
        obj = Obj_PtrTMem(head);
        Obj_Check(obj, recharge_a_lightbulb);
        while (obj) {
            if (HasOrIsObj(obj, 0x93))
                put_effect(obj, 0xC, 4, 0, 0, tx, ty);
            obj = Obj_PtrTMem(&obj->qn.link);
        }
    }
}

/* redeem_all_bottles callback: an empty bottle (0x13D) becomes a coin. */
/* name: 44: BottleRecycler_ovr110_37C8, target size 0x26. Named redeem_a_bottle_ in FM
   Towns, at the same position among its neighbours, and the code corresponds. */
unsigned char far redeem_a_bottle(struct Object far *obj)
{
    switch OBJ_ITEM(obj) {
    case ITEM_BOTTLE_13D:
        SET_ITEM(obj, ITEM_COIN);
    }
    return 0;
}

/* Hack 33: turns the empty bottles on x, y into coins (a bottle return). */
/* name: 45: HackTrapBottleRecycler_ovr110_37EE, target size 0x4C. */
void far redeem_all_bottles(int x, int y)
{
    union Link far *head;
    head = &Map_GetAddr(x, y)->objects;
    if (head->f.index > 0)
        Obj_Check(Obj_PtrTMem(head), redeem_a_bottle);
}

/* find_and_gronk_force_field callback: raises or lowers a force field. */
/* name: 46: ToggleForcefield_ovr110_383A, target size 0x44. Named toggle_force_field_ in FM
   Towns, at the same position among its neighbours, and the code corresponds. */
unsigned char far toggle_force_field(struct Object far *obj)
{
    if (OBJ_ITEM(obj) != ITEM_FORCE_FIELD_16D)
        return 0;
    SET_Z(obj, OBJ_Z(obj) < 0x7F ? 0x7F : 0);
    return 1;
}

/* Hack 26: toggles the force fields on x, y. */
/* name: 47: HackTrapForceField_ovr110_387E, target size 0x2F. */
void far find_and_gronk_force_field(int x, int y)
{
    Obj_Check(Obj_PtrTMem(&Map_GetAddr(x, y)->objects), toggle_force_field);
}

/* Destroys a floating skull: it becomes a plain skull, falls or vanishes, and drops what
   it carried. */
/* name: 48: SmiteUndead_ovr110_38AD, target size 0xB0. */
unsigned char far destroy_floatskull(struct Object far *obj)
{
    SET_ITEM(obj, ITEM_SKULL_C3);
    if (!can_place(ITEM_SKULL_C3, Obj_MemTPtr(obj),
                   ((obj->home & HOME_X) >> 10 << 3) + OBJ_FINEX(obj),
                   ((obj->home & HOME_Y) >> 4 << 3) + OBJ_FINEY(obj),
                   obj->pos & POS_Z, 1, 0))
        obj->qn.f.quality = 0;
    if (!OBJ_ISQUANT(obj) && obj->ol.f.link > 0)
        Obj_FreeChain(&obj->ol.link);
    return 1;
}

/* The prison tower alarm: if any non-loner of race 6 (goblins by the race strings) is
   hostile, quest 60 is set and the
   schedules notified. */
/* name: 49: PrisonTowerQuest60_ovr110_395D, target size 0xCB. */
void far prison_alarm_check(void)
{
    unsigned char far *p;
    struct Object far *npc;
    if (GET_QUEST(60) == 1)
        return;
    for (p = ActiveMob; p < LastActiveMob; p++) {
        npc = Obj_IntTMem(*p);
        if (OBJ_MAJOR(npc) != MAJOR_CREATURE)
            continue;
        if (OBJ_ATTITUDE(npc) == 0 && is_my_race(npc, 6)
            && !OBJ_LONER(npc)) {
            SET_QUEST(60, 1);
            player->xclock[XC_CHANGED]++;
        }
    }
}

/* Kills every active critter of race (0xFF: every undead, and floating skulls). */
/* name: 50: LothIsDeadKillHisLiches_ovr110_3A28, target size 0x97. */
void far genocide(int race)
{
    unsigned char far *p;
    struct Object far *obj;
    for (p = ActiveMob; p < LastActiveMob; p++) {
        obj = Obj_IntTMem(*p);
        if (OBJ_MAJOR(obj) != MAJOR_CREATURE) {
            if (race == 0xFF && OBJ_ITEM(obj) == ITEM_SKULL_13)
                destroy_floatskull(obj);
        } else if (is_my_race(obj, race) && instant_kill(obj))
            p--;
    }
}

/* Kills a critter outright: death_check both ways, its loot and corpse, and it drops what
   it carries. Returns 1 if it was removed. */
/* name: 51: KillCritter_ovr110_3ABF, target size 0xFD. */
unsigned char far instant_kill(struct Object far *obj)
{
    int oldx;
    union Link far *head;
    register struct Creature *cr;
    register int oldy;
    cr = &Creature[OBJ_INMAJOR(obj)];
    death_check(obj, 0);
    death_check(obj, 1);
    oldx = XP;
    oldy = YP;
    XP = OBJ_HOMEX(obj);
    YP = OBJ_HOMEY(obj);
    head = &Map_GetAddr(XP, YP)->objects;
    if (Obj_Rem(head, obj)) {
        generate_inventory(obj);
        build_corpse(obj, cr->corpse, cr->remains);
        drop_some_objects(obj);
        Obj_Free(obj);
        XP = oldx;
        YP = oldy;
        return 1;
    }
    XP = oldx;
    YP = oldy;
    return 0;
}

/* True when obj is of race; race 0xFF matches undead (resist bit 0x80) and floating
   skulls. */
/* name: 52: CheckIfMatchingRace_ovr110_3BBC, target size 0x7E. */
unsigned char far is_my_race(struct Object far *obj, int race)
{
    if (OBJ_MAJOR(obj) != MAJOR_CREATURE) {
        if (race == 0xFF && OBJ_ITEM(obj) == ITEM_SKULL_13)
            return 1;
        return 0;
    } else if (race == 0xFF)
        return check_res(obj, 1, 0x80) == 0;
    else
        return Creature[OBJ_INMAJOR(obj)].race == race;
}

/* Removes the Bishop (whoami 6). */
/* name: 53: PuntBishop_ovr110_3C3A, target size 0x19. FM Towns has no function between is_my_race_
   and fire_trigger_at_; it has an overlay stub entry, so it is public. The name is provisional
   (IDA's RemoveBishop_ovr110_3C3A), chosen so that its tools/bssorder.py key puts it in the
   EXE's overlay stub order. */
void far PuntBishop_ovr110_3C3A(void)
{
    gronk_whoami(6, 0, 0, (WhoamiFn)remove_whoami);
}

/* Fires the first trigger or trap on square x, y. */
/* name: 54: TriggerTrapInTile_ovr110_3C53, target size 0x87. */
void far fire_trigger_at(int x, int y)
{
    struct Object far *trap;
    union Link far *head;
    head = &Map_GetAddr(x, y)->objects;
    trap = Obj_InList(&head, 0, MAJOR_TRAP, -1, -1);
    if (trap) {
        if (OBJ_MINOR(trap) & 2)
            UseTrigger(0L, 0L, trap, -1);
        else
            SetOffTrap(0L, 0L, trap, x, y);
    }
}

/* Changes a critter into another creature (item -1 keeps it) with a new whoami,
   powerful bit and attitude (-1 keeps each), settling it on the floor. */
/* name: 55: TransformTalker_ovr110_3CDA, target size 0x131. */
unsigned char far transform_creature(struct Object far *obj, int item, int whoami,
                                     int powerful, int attitude)
{
    int x, y, height;
    register int new_item;
    register struct Creature *crit;
    x = OBJ_HOMEX(obj);
    y = OBJ_HOMEY(obj);
    height = Map_GetAddr(x, y)->height;
    if (item == -1)
        new_item = obj->id & ID_ITEM;
    else
        new_item = (((item & ID_MINOR) >> 4) << 4) + (item & ID_INCLASS) + FIRST_CREATURE;
    crit = &Creature[new_item & ID_INMAJOR];
    SET_ITEM(obj, new_item);
    if (whoami != -1)
        obj->whoami = whoami;
    if (powerful != -1)
        SET_POWERFUL(obj, powerful);
    if (attitude != -1)
        SET_TERRAIN(obj, attitude);
    if (!crit->flier
        && can_place(new_item, Obj_MemTPtr(obj), x, y, height, 0, 8))
        SET_Z(obj, height);
    return 1;
}

/* The blackrock gem: the facet the player stands at (eight around the gem at fine
   position 0xE3, 0x143) takes him to its world if that facet's gem has been used
   (QB_GEMS_USED) or it is the facet the gem currently shows (vars[6]); worlds[] gives each
   facet's level, square and facing. The world is marked visited (QB_WORLDS_VISITED, with
   facets 5 and 6 swapped for the bit). Otherwise 'That face of the gem remains opaque, and
   you are bounced back.'. */
/* name: 56: WorldGemTravel_ovr110_3E0B, target size 0x143. */
int far black_gem_trip(void)
{
    static struct { unsigned char map, x, y, flags; } worlds[8] = {
        { 0x09, 0x21, 0x20, 0x38 }, { 0x11, 0x1B, 0x22, 0x20 },
        { 0x19, 0x12, 0x27, 0x30 }, { 0x21, 0x1F, 0x1F, 0x01 },
        { 0x29, 0x04, 0x26, 0x30 }, { 0x39, 0x3B, 0x14, 0x20 },
        { 0x31, 0x20, 0x20, 0x30 }, { 0x45, 0x20, 0x16, 0x23 }
    };
    int facet_no, rel_x;
    register int world = -1;
    register int facet;
    rel_x = (OBJ_HOMEX(ThePlayer) << 3) + OBJ_FINEX(ThePlayer) - 0xE3;
    {
        register int rel_y = (OBJ_HOMEY(ThePlayer) << 3) + OBJ_FINEY(ThePlayer) - 0x143;
        facet = abs(rel_x) < abs(rel_y);
        if (rel_y > 0)
            facet = 1 - facet;
        else
            facet += 2;
    }
    if (rel_x < 0)
        facet = 7 - facet;
    facet_no = facet;
    if ((1 << facet_no) & player->quest_bytes[QB_GEMS_USED] || (player->vars[6] & 7) == facet_no)
        world = facet_no;
    if (world != -1 && worlds[world].map != 0) {
        rel_x = world;
        if (world == 5)
            rel_x = 6;
        else if (world == 6)
            rel_x = 5;
        player->quest_bytes[QB_WORLDS_VISITED] |= 1 << rel_x;
        trap_teleport_data = worlds[world].flags;
        return do_teleport(ThePlayer, worlds[world].x, worlds[world].y, worlds[world].map);
    }
    game_sprint(0x15C);  /* 'That face of the gem remains opaque, and you are bounced back.' */
    return 4;
}

/* Hack 54: the gem shows a new random facet (vars[6]), from the first 1, 3, 6 or all 8
   by the castle plot's stage, not the last one shown or a used one (8 when none is left). */
/* name: 57: RotateWorldGem_ovr110_3F4E, target size 0x9B. Named black_gem_rotate_ in FM
   Towns, at the same position among its neighbours, and the code corresponds. */
void far black_gem_rotate(void)
{
    int old_gem, gem;
    register int range;
    register int tries;
    old_gem = player->vars[6];
    range = 8;
    tries = 0;
    if (player->xclock[XC_CASTLE] < 4)
        range = 1;
    else if (player->xclock[XC_CASTLE] < 8)
        range = 3;
    else if (player->xclock[XC_CASTLE] < 0xD)
        range = 6;
    gem = rand() % range;
    while ((gem == old_gem || (1 << gem) & player->quest_bytes[QB_GEMS_USED]) && tries++ < 8)
        gem = (gem + 1) % range;
    if (tries >= 8)
        gem = 8;
    player->vars[6] = gem;
    editchng(2);
}

/* Hack 32, the q*bert floor puzzle (inferred from the name): stepping on a tile (owner
   0x3F) moves its floor colour along the sequence kept in vars[100..]; when the 5 by 5
   pyramid is all one colour the walls take it and the reward objects (fixed object indexes
   0x3CC, 0x3CD, 0x29A, 0x279) are set. */
/* name: 58: QbertTraps_ovr110_3FE9, target size 0x6C6. */
void far do_qbert(int owner)
{
    static unsigned char gate_links[7] = { 0x21, 0x7F, 0x4F, 0x5B, 0x10, 0x29, 0xC2 };
    int16 *seq;
    int16 *done;
    int colour;
    int top;
    struct Tile far *trig;
    int16 *last_tile;
    unsigned char complete;
    register int i;
    register int j;
    seq = (int16 *)(player->vars + 100);
    done = seq + 7;
    trig = 0L;
    last_tile = seq + 8;
    complete = 1;
    if (seq[0] != 6) {
        seq[0] = 6;
        for (i = 1; i < 7; i++)
            seq[i] = -1;
    }
    if (owner == 0x3F) {
        struct Object far *obj;
        if (TriggerChainTileData_dseg_67d6_1BB9 - mapdata == 0xCF1) {
            for (i = 0x1D; i <= 0x23; i++) {
                obj = Obj_FindInMapSquare(MAJOR_RECT, 2, 0xE, i, 4);
                if (obj)
                    for (j = 0; j < 7 && seq[j] != -1; j++)
                        if (seq[j] == obj->ol.f.owner)
                            obj->ol.f.owner = 5;
            }
        }
        if (mapdata + *last_tile == TriggerChainTileData_dseg_67d6_1BB9)
            return;
        *last_tile = TriggerChainTileData_dseg_67d6_1BB9 - mapdata;
        trig = TriggerChainTileData_dseg_67d6_1BB9;
        j = trig->floor;
        i = 0;
        colour = -1;
        while (seq[i] != j && seq[i] != -1)
            i++;
        if (seq[i] == j) {
            i++;
            if (seq[i] == -1)
                i = 0;
            trig->floor = seq[i];
            for (j = 0; j < 5; j++)
                for (i = 0; i > j - 5; i--) {
                    top = Map_GetAddr(i + 0x31, j + 0x33)->floor;
                    if (colour == -1)
                        colour = top;
                    else if (colour != top)
                        goto scanned;
                }
        scanned:
            complete = colour == top;
            if (complete) {
                for (j = 0; j < 6; j++)
                    for (i = 0; i > j - 6; i--)
                        TILE_WALL(Map_GetAddr(i + 0x31, j + 0x33)) = colour;
                *done = 0;
                obj = Obj_IntTMem(0x3CC);
                SET_Z(obj, 0x60);
                obj = Obj_IntTMem(0x3CD);
                if (colour == 5) {
                    obj->qn.f.quality = 0x20;
                    obj->ol.f.owner = 0x19;
                    SET_HEADING(obj, 0);
                } else {
                    obj->qn.f.quality = 4;
                    obj->ol.f.owner = colour * 6 + 4;
                    if (seq[4] == colour) {
                        obj = Obj_IntTMem(0x29A);
                        SET_INVIS(obj, 0);
                        Obj_IntTMem(0x279)->pos = Obj_IntTMem(0x279)->pos & 0xFF80 | obj->pos & POS_Z;
                    }
                }
                obj = Obj_IntTMem(0x3CE);
                obj->ol.f.link = LINK_SPECIAL | gate_links[colour];
                SET_INVIS(obj, 0);
            }
            if (!(unsigned char)*done && !complete) {
                *done = 1;
                obj = Obj_IntTMem(0x3CC);
                SET_Z(obj, 0);
                obj = Obj_IntTMem(0x3CE);
                SET_INVIS(obj, 1);
            }
        } else if (seq[0] > 0)
            trig->floor = seq[0];
    } else if (owner < 0xA) {
        for (i = 0; seq[i] != -1 && seq[i] != owner; i++)
            ;
        if (seq[i] != owner) {
            seq[i] = owner;
            if (i == 4)
                seq[i + 1] = 5;
        }
        trap_teleport_data = 0x3D;
        do_teleport(ThePlayer, 0x31, 0x33, 0x45);
    } else if (owner < 0x14) {
        int range, pick, x, y, level, tries;
        struct Tile far *from;
        struct Tile far *to;
        range = TILE_WALL(Map_GetAddr(owner + 0x2E, 1));
        level = 0x45;
        tries = 0;
        do {
            pick = rand() % (range * 3 >> 1);
            if (pick >= range)
                pick = 1;
            from = Map_GetAddr(owner + 0x2E, pick + 0x20);
            to = Map_GetAddr(owner + 0x2E, pick + 2);
            x = TILE_WALL(from);
            y = TILE_WALL(to);
        } while (++tries < 4
                 && abs(x - OBJ_HOMEX(ThePlayer)) < 3
                 && abs(y - OBJ_HOMEY(ThePlayer)) < 3);
        trap_teleport_data = (to->floor + 8) << 2;
        trap_teleport_data = trap_teleport_data + ((from->floor & 8) >> 3);
        if (from->floor & 7)
            level = level - (from->floor & 7);
        do_teleport(ThePlayer, x, y, level);
    } else {
        struct Object far *obj;
        struct Tile far *tile;
        int level;
        owner -= 0x1E;
        for (i = 0; i < 7; i++)
            if (seq[i] == owner) {
                level = 0x1D - (6 - i) * (7 - i) / 2;
                tile = Map_GetAddr(OBJ_HOMEX(ThePlayer),
                                   OBJ_HOMEY(ThePlayer));
                obj = CreateObj(ITEM_HACK_TRAP, 0);
                Obj_Add(&tile->objects, obj);
                SET_FINEX(obj, 3);
                SET_FINEY(obj, 3);
                SET_Z(obj, 0x74);
                crystal_ball(obj, 0x20, level);
                if (Obj_Rem(&tile->objects, obj))
                    Obj_Free(obj);
            }
    }
}

/* The player has left the arena mid-fight: the pit fighters stop, the first talks to him,
   and his pit record and fight are cleared. */
/* name: 59: AvatarIsACowardInThePits_ovr110_46AF, target size 0xCC. */
void far arena_player_runs(void)
{
    struct Object far *warrior;
    register int i;
    register int first = -1;
    if (player->in_pits) {
        running_away = 1;
        for (i = 0; i < 5; i++) {
            warrior = Obj_IntTMem(player->pit_fighters[i]);
            if (warrior) {
                SET_ATTITUDE(warrior, 1);
                warrior->last_hit = 0;
                SET_GOAL(warrior, 1);
                if (first == -1)
                    first = i;
                else
                    player->pit_fighters[i] = 0;
            }
        }
        if (first > -1) {
            TalkTo(Obj_IntTMem(player->pit_fighters[first]));
            player->pit_fighters[first] = 0;
        }
        player->in_pits = 0;
        player->quest_bytes[QB_PIT_RECORD] = 0;
        player->quest_bytes[QB_JOSPUR_DEBT] = 0;
    }
}

/* Hack 38 callback: a red potion with ID_FLAG11 set becomes a green potion holding a
   poison trap (1..8), or water if no trap can be made; called on the cursor object and the
   player's contents too. */
/* name: 60: TransformRedPotionToPoison_ovr110_477B, target size 0x142. */
unsigned char far morpheus_ruin_potion(struct Object far *potion)
{
    struct Object far *trap;
    register int slot;
    if (potion == ThePlayer) {
        if (GameInputMode == 1) {
            Obj_Check(CursorObjPtr, morpheus_ruin_potion);
            unforce_mouse_cursor(3);
            force_mouse_cursor(OBJ_ITEM(CursorObjPtr));
        } else
            ;   /* match: an empty else: probably a debug message compiled out. Without it -O
                   cross-jumps the force_mouse_cursor cleanup into RedisplayInvSlot's */
        return 0;
    } else {
        if (OBJ_ITEM(potion) != ITEM_RED_POTION)
            return 0;
        if (OBJ_ISQUANT(potion) && (potion->id & ID_FLAG11) && potion->ol.f.link > 0) {
            potion->ol.f.link = 0;
            trap = CreateObj(ITEM_DAMAGE_TRAP, 0);
            if (trap) {
                trap->qn.f.quality = rand() % 8 + 1;
                trap->ol.f.owner = 1;
                SET_ISQUANT(potion, 0);
                Obj_Add(&potion->ol.link, trap);
                SET_ITEM(potion, ITEM_GREEN_POTION);
            } else
                SET_ITEM(potion, ITEM_BOTTLE_OF_WATER);
            slot = FindSlot(potion);
            if (slot > 0)
                RedisplayInvSlot(slot);
        }
    }
    return 0;
}

/* Hack 38: spoils the cure potions on x, y. */
/* name: 61: HackTrapTransformPotionToPoison_ovr110_48BD, target size 0x2F. */
void far ruin_cure_potions(int x, int y)
{
    Obj_Check(Obj_PtrTMem(&Map_GetAddr(x, y)->objects), morpheus_ruin_potion);
}

/* The telekinesis wand's object index, found by find_TK_wand_check. */
/* name: FM Towns keeps it as an unnamed static (it shows as _update_vscreen+1); the name
   is ours. */
static int16 TK_wand;

/* Obj_Check callback: finds the telekinesis wand (a wand with id bit 13, spell major -1
   effect 0x27) and remembers its index. */
/* name: 62: FindAcademyWand_ovr110_48EC, target size 0x6E. */
unsigned char far find_TK_wand_check(struct Object far *obj)
{
    int16 major, effect;
    unsigned char flag;
    if (!OBJ_DOORDIR(obj) || OBJ_ITEM(obj) != ITEM_WAND_9B)
        return 0;
    if (!(unsigned char)decode_obj_spell(obj, &major, &effect, &flag))
        return 0;
    if (major != -1 || effect != 0x27)
        return 0;
    TK_wand = Obj_MemTPtr(obj);
    return 1;
}

/* Takes the telekinesis wand away from the player wherever it is (cursor, inventory or
   map) and puts it back at fine position (0xFB, 0xEB), height 0x50. */
/* name: 63: MoveAcademyWand_ovr110_495A, target size 0x1E2. */
void far remove_TK_wand(void)
{
    struct Object far *wand;
    union Link far *head;
    register int x, y;
    TK_wand = 0;
    if (GameInputMode == 1) {
        Obj_Check(CursorObjPtr, find_TK_wand_check);
        if (TK_wand != 0) {
            wand = Obj_IntTMem(TK_wand);
            if (wand != CursorObjPtr) {
                Obj_Add(&ThePlayer->ol.link, CursorObjPtr);
                InvRemoveOneObject(wand);
                Obj_Rem(&ThePlayer->ol.link, CursorObjPtr);
            } else {
                CursorObjPtr = 0L;
                unforce_mouse_cursor(3);
                GameInputMode = 0;
            }
            put_at(0xFB, 0xEB, 0x50, wand, 3, 1);
            return;
        }
    }
    if (Obj_Check(Obj_PtrTMem(&ThePlayer->ol.link), find_TK_wand_check)) {
        if (TK_wand == 0)
            return;
        wand = Obj_IntTMem(TK_wand);
        if (InvRemoveOneObject(wand))
            put_at(0xFB, 0xEB, 0x50, wand, 3, 1);
        return;
    }
    for (x = 0; x < MAP_SIZE; x++)
        for (y = 0; y < MAP_SIZE; y++) {
            head = &Map_GetAddr(x, y)->objects;
            if (Obj_Check(Obj_PtrTMem(head), find_TK_wand_check)) {
                wand = Obj_IntTMem(TK_wand);
                if (Obj_Rem(head, wand))
                    put_at(0xFB, 0xEB, 0x50, wand, 3, 1);
                return;
            }
        }
}

/* Hacks 40..42, the vending machine: 0x28 selects an item (vars[machine]), 0x29 sells it
   for half its value + 1 in coins put on x, y, 0x2A prints '<Item> is the current
   selection (N gp).'. */
/* name: 64: HackTrapVendingMachine_ovr110_4B3C, target size 0x1ED. */
void far go_vend(int which, int machine, int x, int y, int choice)
{
    static int16 vend_items[8] = {
        ITEM_FISH, ITEM_PIECE_OF_MEAT_B0, ITEM_BOTTLE_OF_ALE, ITEM_LEECHES,
        ITEM_BOTTLE_OF_WATER, ITEM_DAGGER, ITEM_LOCKPICK, ITEM_TORCH
    };
    struct Object far *item;
    unsigned char price;
    struct StaticObj sample;
    char text[80];
    register int i;
    register int id;
    switch (which) {
    case 0x28:
        player->vars[machine] = choice;
        break;
    case 0x29:
        id = vend_items[player->vars[machine]];
        price = (((int)ComObjData[id].value + 1) >> 1) + 1;
        if (!vend_check_gold(x, y, price, 1))
            break;
        vend_check_gold(x, y, price, 0);
        if ((item = CreateObj(id, 0)) == 0L)
            break;
        item->qn.f.quality = 0x3F;
        SET_Z(item, 0x76);
        Obj_Add(&Map_GetAddr(x, y)->objects, item);
        item = obj_deal(item, x, y, 0);
        break;
    case 0x2A:
        sample.ol.f.link = 0;
        SET_ITEM(&sample, vend_items[player->vars[machine]]);
        price = (((int)ComObjData[sample.id & ID_ITEM].value + 1) >> 1) + 1;
        sample.ol.f.owner = 0;
        get_name(text, (struct Object far *)&sample, 1, 0);
        if (text[0] >= 'a' && text[0] <= 'z')
            text[0] = text[0] + 'A' - 'a';
        scroll_print(text);
        game_sprint(0x15D);  /* ' is the current selection ' */
        i = 0;
        text[i++] = '(';
        if (price > 9)
            text[i++] = price / 10 + '0';
        text[i++] = price % 10 + '0';
        text[i++] = ' ';
        text[i++] = 'g';
        text[i++] = 'p';
        text[i++] = ')';
        text[i++] = 0;
        scroll_print(text);
        game_sprint(0x60);  /* '.' */
        break;
    }
}

/* With check set, only counts whether the coins in the tile cover money; otherwise
   takes them. Returns 1 when they do. */
/* name: 65: CollectMoneyInTile_ovr110_4D29, target size 0x151. */
unsigned char far vend_check_gold(char x, char y, unsigned char money, unsigned char check)
{
    struct Object far *coins;
    struct Object far *next;
    union Link far *head;
    struct Tile far *tile;
    int count;
    tile = Map_GetAddr(x, y);
    head = &tile->objects;
    for (coins = Obj_PtrTMem(head); coins && money > 0; coins = next) {
        next = Obj_PtrTMem(&coins->qn.link);
        if (OBJ_ITEM(coins) == ITEM_COIN) {
            if (OBJ_ISQUANT(coins) && !(coins->ol.f.link & LINK_SPECIAL))
                count = coins->ol.f.link;
            else
                count = 1;
            if (money >= count) {
                if (check)
                    money -= count;
                else if (Obj_Rem(head, coins)) {
                    Obj_FreeLinkChain(0L, coins);
                    money -= count;
                    head = &tile->objects;
                }
            } else {
                if (!check)
                    coins->ol.f.link = count - money;
                money = 0;
            }
        }
    }
    return money == 0;
}

/* gronk callback: stores the object's index in *result. */
/* name: 66: StoreReverseObjectLookupResultInVar_ovr110_4E7A, target size 0x1C. Named
   gronkify_find_ in FM Towns, at the same position among its neighbours, and the code
   corresponds. */
char far gronkify_find(struct Object far *obj, register int16 *result)
{
    *result = Obj_MemTPtr(obj);
    return 0;
}

/* Jail: the player wakes in the cell (0x2A, 0x26 of level 1) a little later (up to 40
   minutes pass), having lost a ninth of his experience and with his hit points set to his
   strength. The guards (race 0x1C, humans by the race strings) calm down, quest 112 is set,
   Lord British is moved outside the cell, and the cell's trigger fires. */
/* name: 67: SendAvatarToJail_ovr110_4E96, target size 0x1A8. */
void far put_player_in_jail(void)
{
    int lb_index;
    struct Object far *lb;
    int32 hour;
    char row[16];
    ThePlayer->hp = playerdat->attr[0];
    SET_SEQ(ThePlayer, 1);
    set_new_music(10);
    player_get_exp(-(int)(player->exp / 9));
    hour = player->game_clock / 0x3C00L % 0x14L;
    FixBagArea();
    pass_time((0x28 - hour) * 0x3C - (rand() % 0x5A + 0x3C));
    do_teleport(ThePlayer, 0x2A, 0x26, 1);
    row[7] = 1;
    row[8] = 0;
    gronk_race(0x1C, 1, 2, (char (far *)(struct Object far *, NEARPTR))gronkify_attitude);
    gronk_race(0x1C, 1, (NEARPTR)row, (char (far *)(struct Object far *, NEARPTR))gronkify_change_goal);
    SET_QUEST(112, 1);
    update_all_critters_whilst_player_snoozes();
    gronk_whoami(0x8E, 1, (NEARPTR)&lb_index, (WhoamiFn)gronkify_find);
    lb = Obj_IntTMem(lb_index);
    if (lb) {
        teleport_critter(lb, 0x2A, 0x22, 0);
        lb->qn.f.quality = 0x28;
        lb->ol.f.owner = 0x27;
        SET_GOAL(lb, 1);
    }
    fire_trigger_at(0x27, 0x25);
    punt_fightmode();
    gruesome_door_hack(0x2A, 0x26);
    new_player_pos();
}

/* Killorn Keep crashes: the screen shakes and fades, quest 54 is set and the schedules
   catch up (unless the player is entering the level). */
/* name: 68: KilhornIsCrashing_ovr110_503E, target size 0x54. */
void far Killorn_just_crashed(unsigned char entering)
{
    do_sfx(4, 0);
    fadeout3d(5);
    SET_QUEST(54, 1);
    player->xclock[XC_CHANGED]++;
    if (!entering)
        Sched_SetAllClocks(1);
}
