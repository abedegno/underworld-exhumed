/* target: ovr107 */
/* opts: -mm -1 -G -O -Y -d */
/* World events: teleports, terrain changes, traps, plot deaths and item repair: the
   whole of UW1's DOS overlay ovr107 (UW2's ovr110, of which it has the general part), in
   original order.

   What it does in the game: the actions behind the traps and some of the hack traps,
   and the deaths that matter to the plot. The general part: do_teleport (and
   find_good_x_and_y, which finds room near the destination), changing tiles with
   everything standing on them (change_terrain), damage and spell traps, fishing
   (go_fish), deaths that matter to the plot (death_check, TyballDeath_ovr107_13D1),
   repairing items (repair_item, from the Repair skill and from conversations) and the
   Lore skill's retries (clear_loretry, clear_all_loretries). The hack traps here are the
   bullfrog puzzle (work_bullfrog_tiles), the emerald puzzle (emerald_trap), the trespass
   trap, the eight-position switch, the exploding book, the talking door and Arial's
   scene.

   UW1 against UW2: do_teleport has no jail, sleep or Pits rules; change_terrain moves the
   objects on the tile itself (no raise_up, put_down or filter, no neighbours) and takes
   heights up to 13; go_fish ignores the Track skill; the poison and quest fields are in
   UW1's player record (player.h); repair_item shows the anvil, adds the time to
   the game clock itself (there is no pass_time) and has no default name. UW1 has none of
   UW2's later quest hacks.

   Callers: TRIGGER.C (the trap actions and hack traps), USEITEMS.C (fishing, the anvil,
   the exploding book), SPELLS.C (gate travel, the bullfrog), SKILLS.C, AI.C (death_check),
   UWEDIT.C. It calls back into SPELLS.C, DAMAGE.C, OBJPHYS.C and COLLIDE.C (placing
   objects), CONVERSE.C (TalkTo) and CUTS.C (cutscenes).

   Data owned: tyball_allies and the exploding book's message.
   Function names are UW2's (the FM Towns originals) where the routine is the same; the
   UW1 functions UW2 lacks have names chosen for the overlay's stub order (Turbo C lists a
   file's publics by the tools/bssorder.py key of each name, and TLINK numbers the stub
   entries from the last one listed) or, where the listing's name fits, the listing's.
   Each function's first comment gives its explanation; the second, tagged name:, its
   number in the file and IDA's name.
   Name: UW2's (world events: do_teleport, change_terrain, death_check). */
/* match: work_bullfrog_tiles is symbols.tsv's name for 59C8:007A, so verify requires
   it; its key (10) lists it before remove_whoami, so the EXE's stub order (where it is
   the 19th entry) is not reproduced. A name with a key from 859 to 876 (such as
   work_bullfrog_tiles, 863) gives the EXE's order. */
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

/* Declared in each file that uses it, its own way (no header). */
unsigned char far IsMobElem(struct Object far *obj);

/* name: 1: ovr107_0 (UW2's find_good_x_and_y, the same bytes). A breadth-first search
   outward from (x, y), at most 20 tiles a ring, for a tile within the 9x9 box around it
   where the object fits. */
#define VISITED(tx, ty) (visited[(tx) - xmin] & (1 << ((ty) - ymin)))
#define VISIT(tx, ty) \
    if (!VISITED(tx, ty) && nnext < 20 \
        && (tx) >= xmin && (tx) <= xmax && (ty) >= ymin && (ty) <= ymax) { \
        next[nnext][0] = (tx); \
        next[nnext][1] = (ty); \
        nnext++; \
        visited[(tx) - xmin] |= 1 << ((ty) - ymin); \
    }
/* Finds where obj fits near (x, y), searching outward (the comment and macros above);
   the tile found goes to *nx, *ny. Returns 0 when there is none. */
char far find_good_x_and_y(struct Object far *obj, int x, int y,
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
                    if (ComObjData[OBJ_ITEM(o)].height || IsMobElem(o))
                        Obj_Punt(&tile->objects, o, 0);
                }
            }
            if (can_place(OBJ_ITEM(obj), Obj_MemTPtr(obj), tx * 8 + 3, ty * 8 + 3,
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

/* Teleports who to square x, y of level (0 or this level: a nearby free square is found).
   Only the player can change level. The player's move is only recorded (NewPlayerLevel,
   NewPlayerX, NewPlayerY) for the main loop to carry out. Returns 0x10 on success, 2
   otherwise. */
/* name: 2: Teleport_ovr107_949. */
int far do_teleport(struct Object far *who, int x, register int y, int level)
{
    int16 new_x, new_y;
    unsigned char ok;
    register int dest = level;
    ok = 1;
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
        if (who == ThePlayer) {
            NewPlayerLevel = dest;
            NewPlayerX = x;
            NewPlayerY = y;
            editchng(0x20);
        }
        return 0x10;
    }
    return 2;
}

/* Changes the tiles from x, y to x + dx, y + dy: wall and floor textures (0x30 and 0xB or
   more mean unchanged), type (10 or more unchanged) and height (at most 13; adjust 1 and 3
   raise or lower by one instead). Objects on a tile that rises are lifted to the new floor
   if below it; those resting on a tile's old floor go down with it (mobiles keep their fine
   height in step; the player's physics are updated). Returns 2. */
/* name: 3: ChangeTile_ovr107_9F8. */
int far change_terrain(int x, int y, int wall, int floor, register int height,
                       int type, int dx, int dy, int adjust)
{
    int endx, endy, cx, cy;
    struct Tile far *tile;
    union Link far *head;
    struct Object far *obj;
    register int old_height;
    obj = 0;
    endx = x + dx;
    endy = y + dy;
    for (cx = x; cx <= endx; cx++) {
        for (cy = y; cy <= endy; cy++) {
            tile = Map_GetAddr(cx, cy);
            old_height = tile->height;
            if (adjust == 1 || adjust == 3) {
                height = old_height + 2 - adjust;
                if (height >= 0 && height <= 0xD)
                    tile->height = height;
            } else if (height <= 0xD)
                tile->height = height;
            if (tile->height > old_height) {
                for (head = &tile->objects; head->f.index != 0; head = &obj->qn.link) {
                    obj = Obj_PtrTMem(head);
                    if (OBJ_MAJOR(obj) == MAJOR_TRAP) continue;
                    if (OBJ_Z(obj) < (height << 3)) {
                        SET_Z(obj, height << 3);
                        if (IsMobElem(obj) && OBJ_MAJOR(obj) != MAJOR_CREATURE)
                            obj->b0F = height << 6;
#ifndef __TURBOC__
                        else if (port_shared_avatar(obj))
                            port_shared_terrain(obj, height, 1);
#endif
                        else if (obj == ThePlayer)
                            PN.z = height << 6;
                    }
                }
            } else if (tile->height < old_height) {
                for (head = &tile->objects; head->f.index != 0; head = &obj->qn.link) {
                    obj = Obj_PtrTMem(head);
                    if (OBJ_MAJOR(obj) == MAJOR_TRAP) continue;
                    if (OBJ_Z(obj) == (old_height << 3)) {
                        SET_Z(obj, height << 3);
                        if (IsMobElem(obj) && OBJ_MAJOR(obj) != MAJOR_CREATURE)
                            obj->b0F = height << 6;
#ifndef __TURBOC__
                        else if (port_shared_avatar(obj))
                            port_shared_terrain(obj, height, 0);
#endif
                        else if (obj == ThePlayer)
                            parse_player_terr(0x10, 1);
                    }
                }
            }
            if (floor < 0xB)
                tile->floor = floor;
            if (wall < 0x30)
                TILE_WALL(tile) = wall;
            if (type < 10)
                tile->type = type;
        }
    }
    editchng(6);
    return 2;
}

/* The damage trap: damage to object index with damage type 4 (physical). Negative damage
   poisons the player at that strength instead (unless he resists poison) or, for others,
   is plain damage. Returns 0x10 when the object was destroyed, else 2. */
/* name: 4: DamageTrap_ovr107_CB2. */
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
/* name: 5: SpellTrap_ovr107_D74. */
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

/* Fishing: the player must face water (a non-solid tile of terrain class 1 whose floor
   is below him) about a square ahead. Then a fish bites one time in five, and is caught if
   he can carry it. Returns 1 for a catch. */
/* name: 6: Fishing_ovr107_DC4. */
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
        if ((TxmTerr[tile->floor] >> 4) != TERRAIN_WATER)
            goto bad_place;
        if ((OBJ_Z(ThePlayer) >> 3) <= (uint16)(tile->height - 1))
            goto bad_place;
    } else
        goto bad_place;
    if (rand() % 5 != 0)
        goto no_luck;
    if (player->weight + ComObjData[ITEM_FISH].mass >= player->max_weight)
        goto no_room;
    game_sprint(0x63);  /* 'You catch a lovely fish.' */
    return 1;
no_room:
    game_sprint(0x66);  /* 'You feel a nibble, but the fish gets away.' */
    return 0;
no_luck:
    game_sprint(0x64);  /* 'No luck this time.' */
    return 0;
bad_place:
    game_sprint(0x65);  /* 'You cannot fish there. Perhaps somewhere else.' */
    return 0;
}

/* The bullfrog puzzle on level 4 (a hack trap; owner is the mode): game variables 24 and
   25 select a square of an 8 by 8 area at 0x30, 0x30, variable 26 counts the moves left.
   Modes 0 and 1 lower or raise the selected square and, one step less, its neighbours
   within the area (a move costs one; with none left only a click is heard); 2 and 3 step
   the selection along y and x; 4 resets the area to height 4 and the count to 63.
   Anywhere but level 4 it only prints a message. */
/* name: 7: work_bullfrog_tiles. */
void far work_bullfrog_tiles(int mode, int x, int y)
{
    int dx;
    int dy;
    register int tx;
    register int ty;
    if (PlayerLevel != 4)
        game_sprint(0xBF);
    else switch (mode) {
    case 0:
    case 1:
        x = player->game_vars[0x18] + 0x30;
        y = player->game_vars[0x19] + 0x30;
        if (player->game_vars[0x1A] > 1) {
            player->game_vars[0x1A] = player->game_vars[0x1A] - 1;
            tx = x - 1;
            ty = y - 1;
            dx = 2;
            dy = 2;
            if (player->game_vars[0x18] == 0) tx = x;
            if (player->game_vars[0x18] == 0 || player->game_vars[0x18] == 7) dx = 1;
            if (player->game_vars[0x19] == 0) ty = y;
            if (player->game_vars[0x19] == 0 || player->game_vars[0x19] == 7) dy = 1;
            change_terrain(tx, ty, 0x3F, 0xF, 0xF, 0xF, dx, dy, mode * 2 + 1);
            change_terrain(x, y, 0x3F, 0xF, 0xF, 0xF, 0, 0, mode * 2 + 1);
        } else {
            game_sprint(0xC0);  /* 'There is an empty clicking sound.' */
            player->game_vars[0x1A] = 1;
        }
        break;
    case 2:
        player->game_vars[0x19] = (player->game_vars[0x19] + 1) & 7;
        break;
    case 3:
        player->game_vars[0x18] = (player->game_vars[0x18] + 1) & 7;
        break;
    case 4:
        player->game_vars[0x1A] = 0x3F;
        change_terrain(0x30, 0x30, 0x3F, 0xF, 4, 0xF, 7, 7, 0);
        game_sprint(0xC1);  /* 'A voice utters the words "Reset Activated."' */
        break;
    }
}

/* The emerald puzzle (a hack trap): when each corner of the 9 by 9 square around x, y
   (four squares off each way) holds an emerald (class 2, 2, 7), a new object 0xFD is put
   at x, y + 1 and the four emeralds are destroyed. */
/* name: 8: EmeraldDoTrap_ovr107_100B. */
void far emerald_trap(struct Object far *trap, int x, int y)
{
    struct Tile far *tile;
    union Link far *head;
    char dx, dy, count;
    char xs[4], ys[4];
    struct Object far *obj;
    struct Tile far *tiles[4];
    struct Object far *found[4];
    count = 0;
    for (dx = -4; dx < 5; dx += 8) {
        for (dy = -4; dy < 5; dy += 8) {
            tiles[count] = Map_GetAddr(x + dx, y + dy);
            head = &tiles[count]->objects;
            if ((found[count] = Obj_InList(&head, 0, 2, 2, 7)) != 0) {
                xs[count] = x + dx;
                ys[count] = y + dy;
                count++;
            }
        }
    }
    if (count == 4) {
        obj = CreateObj(ITEM_VAS_STONE, 0);
        tile = Map_GetAddr(x, y + 1);
        SET_Z(obj, 0x40);
        SET_FINEX(obj, 3);
        SET_FINEY(obj, 3);
        Obj_Add(&tile->objects, obj);
        obj_deal(obj, x, y + 1, 1);
        for (dx = 0; dx < 4; dx++)
            Obj_Punt(&tiles[dx]->objects, found[dx], 1);
    }
}

/* The trespass trap: the owner race is told the player has done something it would
   object to (player_grabbed). */
/* name: 9: TrespassTrap_ovr107_11B9. */
void far player_did_bad(int owner)
{
    player_grabbed(ThePlayer, (unsigned char)owner);
}

/* Hack 3 and 4, the eight-position switch: with the switch's position (flags) times 8
   added to the trap's height, hack 3 sets the height of the tile, hack 4 the height of the
   trap's linked object. */
/* name: 10: do_trap_platform_ovr107_11D2. */
void far eight_pos_switch(int flags, struct Object far *trap, int x, int y)
{
    struct Object far *obj;
    register int height;
    height = OBJ_Z(trap) + (flags << 3);
    if (trap->qn.f.quality == 3) {
        if (height < 0x68)
            change_terrain(x, y, 0xFF, 0xFF, height >> 3, 0xFF, 0, 0, 0);
    } else {
        obj = Obj_IntTMem(trap->ol.f.link & 0x1FF);
        SET_Z(obj, height);
    }
}

/* A hack trap: if the player's square holds an exploding book (class 4, 1, 4), it blows
   up in his face: quest 8 is set, he suffers a backfire, and the book is destroyed. */
/* name: 11: ExplodingBook_ovr107_1259. */
void far ExplodingBook_ovr107_1259(struct Object far *trap, int x, int y)
{
    struct Object far *book;
    struct Tile far *tile;
    union Link far *head;
    register int tx;
    register int ty;
    tx = OBJ_HOMEX(ThePlayer);
    ty = OBJ_HOMEY(ThePlayer);
    tile = Map_GetAddr(tx, ty);
    head = &tile->objects;
    book = Obj_InList(&head, 1, 4, 1, 4);
    if (book) {
        scroll_print("The book explodes in your face!\n");
        player->quests |= QUEST_BIT(QUEST_BRONUS_BOOK);
        backfire(ThePlayer, 3);
        InvRemoveOneObject(book);
        Obj_Punt(0L, book, 1);
        DisplayInventory();
        FixPlayerEquips();
    }
}

/* A hack trap: a conversation with no body, whoami 0x19 (a talking door): a creature
   (item 0x40) is made, talked to and freed again. */
/* name: 12: TalkingDoor_ovr107_1319. */
void far talking_door_trap(struct Object far *trap, int x, int y)
{
    struct Object far *door;
    door = CreateObj(FIRST_CREATURE, 1);
    door->whoami = 0x19;
    SET_ATTITUDE(door, ATT_FRIENDLY);
    SET_GOAL(door, GOAL_TALK);
    TalkTo(door);
    Obj_Free(door);
}

/* A hack trap: four frames of animation, then cutscene 3. */
/* name: 13: ArialTalking_ovr107_1373. */
void far do_arial_talking(struct Object far *trap, int x, int y)
{
    update_animobj(4);
    runcutscene(3);
}

/* Removes an NPC and its contents from the map (a gronk callback). */
/* name: 14: RemoveObject_ovr107_138A. */
unsigned char far remove_whoami(struct Object far *obj)
{
    union Link far *head;
    head = &Map_GetAddr(OBJ_HOMEX(obj), OBJ_HOMEY(obj))->objects;
    Obj_FreeLinkChain(head, obj);
    return 1;
}

/* Tyball's death: cutscene 2, bit 2 of the player's word 0x6E, his allies (the whoamis
   in tyball_allies[1..9]) removed, and the move triggers (item 0x1A0) on square 0x17,
   0x38 destroyed. */
/* name: 15: TyballDeath_ovr107_13D1. */
static unsigned char tyball_allies[10] = {
    0xDE, 0xD1, 0xDB, 0xD2, 0xDC, 0xD5, 0xD8, 0xD4, 0xD3, 0xDD
};
/* Tyball's death (see above). */
void far TyballDeath_ovr107_13D1(void)
{
    char i;
    union Link far *head;
    struct Object far *obj;
    struct Object far *next;
    runcutscene(2);
    player->dreams |= 4;
    for (i = 9; i > 0; i--)
        gronk_whoami(tyball_allies[i], 0, 0, (WhoamiFn)remove_whoami);
    head = &Map_GetAddr(0x17, 0x38)->objects;
    for (obj = Obj_PtrTMem(head); obj; obj = next) {
        next = Obj_PtrTMem(&obj->qn.link);
        if (OBJ_ITEM(obj) == ITEM_MOVE_TRIGGER) {
            Obj_Rem(head, obj);
            Obj_Free(obj);
        }
    }
}

/* Deaths that matter to the plot (mode set: the death has happened; clear: the killing
   blow is about to land). Returns 0 when the creature is spared. 0x1B dying clears bit 2
   of the player's byte 0x62; 0xE7 is Tyball (TyballDeath); 0x16 is never killed: it
   gives up (once worth 500 experience), stops fighting and talks; 0x0B talks and is
   left with 60 hit points; 0x6E, 0x8E and 0x18 set quests 4, 11 and 6. */
/* name: 16: SpecialDeathCases_ovr107_149F. */
char far death_check(struct Object far *obj, char mode)
{
    switch (obj->whoami) {
    case 0x1B:
        if (mode) player->talisman_ok = 0;
        break;
    case 0xE7:
        if (mode) TyballDeath_ovr107_13D1();
        break;
    case 0x16:
        if (!mode) {
            if (!OBJ_TERRAIN(obj)) {
                SET_TERRAIN(obj, 1);
                player_get_exp(0x1F4);
            }
            obj->last_hit = 0;
            clear_fight_state();
            SET_SEQ(obj, SEQ_STAND);
            SET_FRAME(obj, 0);
            TalkTo(obj);
            return 0;
        }
        break;
    case 0x0B:
        if (!mode) {
            TalkTo(obj);
            obj->hp = 0x3C;
            return 0;
        }
        break;
    case 0x6E:
        if (mode) player->quests |= QUEST_BIT(QUEST_GAZER);
        break;
    case 0x8E:
        if (mode) player->quests |= QUEST_BIT(QUEST_RODRICK);
        break;
    case 0x18:
        if (mode) player->quests |= QUEST_BIT(QUEST_LIZARDMEN);
        break;
    }
    return 1;
}

/* An item's durability for repair: from the Weapons or Armour tables; -1 for anything
   else (missiles included), which cannot be repaired. */
/* name: 17: GetItemDurability_ovr107_15CF. */
int far get_rep_diff(struct Object far *obj)
{
    register int item;
    switch (OBJ_MAJOR(obj)) {
    case MAJOR_HACK:
        item = OBJ_INCLASS(obj);
        switch (OBJ_MINOR(obj)) {
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
/* name: 18: ovr107_163C. */
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
   possible, hard, very difficult, and asked to confirm. The anvil is shown (cutscene
   0x104); the attempt is noisy and its minutes are added to the game clock; a destroyed
   item may survive by its fate. The result is printed from string 0x8E + result. */
/* name: 19: ItemRepair_ovr107_1754. */
void far repair_item(struct Object far *obj, int skill, char who)
{
    int16 time;
    int durability;
    int reply;
    char answer;
    char name[80];
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
            game_sprint(0xD8);  /* 'You think it will be ' */
            game_sprint(estimate + 0xDB);  /* 'trivial', 'simple', 'possible', 'hard', 'very difficult' */
            game_sprint(0xD9);  /* ' to repair the ' */
            scroll_print(name);
            reply = wyorn(0, 0xDA, &answer);  /* 'Make an attempt? ' */
            if (reply != 0 && reply < 4)
                wd_bool(answer = reply == 2);
            scroll_print("\n");
            if (!answer)
                return;
        } else {
            game_sprint(0x8E);  /* 'You cannot repair that.' */
            return;
        }
    }
    runcutscene(0x104);
    result = do_repair(obj, skill, &time);
    if (who) {
        playerdat->noise = 0xF;
        player->game_clock += ((int32)time << 8) * 60;
        if (result == -2) {
            if (Obj_Elem_Fate(10, obj)) {
                InvRemoveOneObject(obj);
                Obj_Punt(0L, obj, 1);
            } else
                result = 0;
        }
        game_sprint(result + 0x8E);  /* -2 'You destroy the ' .. 0 'You cannot repair that.' .. 3 'You have fully repaired the ' */
        if (result) {
            scroll_print(name);
            game_sprint(0x53);  /* '.' */
        }
        FixPlayerEquips();
        editchng(0x200);
    } else if (result == -2)
        Obj_Punt(&Map_GetAddr(MapObj_X, MapObj_Y)->objects, obj, 0);
}

/* Clears an object's "already examined" bit (heading bit 2), so a better Lore skill can
   identify it again. Not for mobiles, MAJOR_RECT, traps or objects drawn as 3D models
   (the Guide, on the lore skill). */
/* name: 20: ResetObjectIdentification_ovr107_1946 (UW2: FM Towns clear_loretry_). */
unsigned char far clear_loretry(struct Object far *obj)
{
    if (IsMobElem(obj)) return 0;
    if (OBJ_MAJOR(obj) == MAJOR_RECT || OBJ_MAJOR(obj) == MAJOR_TRAP
        || ComObjData[OBJ_ITEM(obj)].render == 2) return 0;
    SET_HEADING(obj, OBJ_HEADING(obj) & 3);
    return 0;
}

/* Clears the examined bit on every object on the map (after the Lore skill rises). */
/* name: 21: ResetObjectIdentifcation_ovr107_19BC. */
void far clear_all_loretries(void)
{
    struct Tile far *tile;
    union Link far *head;
    register int x, y;
    tile = mapdata;
    for (y = 0; y < 64; y++) {
        for (x = 0; x < 64; x++, tile++) {
            head = &tile->objects;
            if (head->f.index > 0)
                Obj_Check(Obj_PtrTMem(head), clear_loretry);
        }
    }
}

