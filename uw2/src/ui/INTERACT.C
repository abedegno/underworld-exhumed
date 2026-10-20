/* target: seg026_2716 */
/* opts: -mm -1 -G -O -d */
/* The player's interaction with the 3D view and the panels: the per-tick screen update,
   picking objects out of the view, reach and line-of-sight checks, getting, looking at,
   using, talking to and attacking, the inventory clicks, the interaction-mode icons and
   drawing or sheathing the weapon. The whole of DOS segment seg026_2716, in original
   order.

   What it does in the game: the left-hand icons choose what a click in the 3D view does
   (RightButtonThing: 0 the default, look and drag to get; 1 use, 2 fight, 3 look, 4 get, 5
   talk; deal_with_icons). mous_in_3d is the 3D view's mouse handler (registered by
   PLAYER.C's mous_player): it finds the object under the pointer from the renderer's pick
   buffer (pick_3d: stdat holds a colour per pixel naming the object drawn there) and
   dispatches through player_disp. GameInputMode says whether something else owns the
   pointer: 1 an object on the cursor (it is dropped or thrown), 2 a targeted spell or an
   object being used on another (ObjectActor is called on the pick), 3 a missile spell being
   aimed. mous_in_panel does the same for the right-hand panel (inventory, runes or
   statistics). display_scr runs every frame: the flasks and compass, colour cycling, death,
   and every 20 seconds of game time the timed effects (duration_check, PLAYTIME.C).

   Data owned: the spell effect flags (PoisonWeap, TimeStop, Hasted, WizEye, Blessed, Valor),
   the interaction state (RightButtonThing, GameInputMode, ObjectActor, ObjectActing,
   ObjectActorArg, newPlObj, PickMap, MapObj_X, MapObj_Y, releasePtr), PickDist (the reach,
   0x90: 12 fine units squared, inferred) and the duration check timer.
   Function and global names are the originals from the FM Towns symbol table except where
   noted.
   Name: descriptive (the player's interaction with the 3D view and panels: pick_3d,
   player_3duse). */

#include <stdlib.h>
#include "combat.h"
#include "conv.h"
#include "critter.h"
#include "event.h"
#include "gfx.h"
#include "inv.h"
#include "map.h"
#include "motion.h"
#include "object.h"
#include "player.h"
#include "sound.h"
#include "sys.h"
#include "ui.h"
#include "view3d.h"

#define TERRAIN(t)      ((TxmTerr[(t)->floor] & 0xC0) >> 6)

/* This file's data, in DS order from 0x37E. */
/* match: the six spell-effect flags lead it: they lie between seg024's data and this
   file's in link order, and FM Towns keeps them with this file's other variables
   (ObjectActor .. MapObj_X, quick_time, Valor). */
unsigned char PoisonWeap = 0;           /* DS:037E */
unsigned char TimeStop = 0;
unsigned char Hasted = 0;
unsigned char WizEye = 0;
unsigned char Blessed = 0;
unsigned char Valor = 0;                /* DS:0383 */
int16 LeftPanel = 2;
int16 PickDist = 0x90;
unsigned char quick_time = 0;
unsigned char def_mode = 0;
int32 lastDurCheck = 0;
unsigned char DurCount = 0;
unsigned char realDScheck = 1;
unsigned char releaseable = 0;


/* This file's _BSS, DS:24E4..2507. */
/* match: laid out by name (tools/bssorder.py): the keys run
   pTxtId 56, current_button 91, ObjectActor 135, ObjectActing 191, RightButtonThing 194,
   ObjectActorArg 351, PickMap 536, MapObj_X and MapObj_Y 581, newPlObj 638, CrownTmap 907,
   releasePtr 914, GameInputMode 935. CrownTmap is the FM Towns name in the same group with
   a key that fits the unreferenced byte at DS:2500 (one byte in FM Towns too; DS:2501 is
   padding, as Turbo C puts anything wider than a byte at an even offset). */
int16 pTxtId;
int16 current_button;
ActorFn ObjectActor;
struct Object far *ObjectActing;
int16 RightButtonThing;
int16 ObjectActorArg;
struct Tile far *PickMap;
int16 MapObj_X, MapObj_Y;
struct Object far *newPlObj;
unsigned char CrownTmap;
union Link far *releasePtr;
int16 GameInputMode;


/* Called every frame: drives the player's attack, redraws the health and mana flasks and
   the compass (no compass in the Ethereal Void), cycles the palette, starts death when the
   player's hit points are 0, and once per second of game time counts down paralysis and
   maybe changes the music; every 20 seconds it runs duration_check. */
void far display_scr(void)
{
    int v;

    player_attack(0);
    v = ThePlayer->hp;
    set_screen_frame(0, v);
    ThePlayer->b11 = 0;
    v = player->play_mana;
    set_screen_frame(1, v);
    if ((PlayerLevel - 1) / LEVELS_PER_WORLD != 8) {
        v = (OBJ_HEADING(ThePlayer) << 5) + OBJ_FINEHEAD(ThePlayer);
        v = (v + 8 & 0xFF) >> 4;
        set_screen_frame(2, v);
    }
    cycle_colors(GAME_TIME() & 0xFF);
    if (ThePlayer->hp == 0)
        player_is_dead();
    v = (player->game_clock >> 8) - lastDurCheck;
    if (v != 0 && realDScheck != 0) {
        lastDurCheck = player->game_clock >> 8;
        DurCount += v;
        change_music_maybe();
        if (DurCount > 0x14) {
            DurCount -= 0x14;
            duration_check();
        }
        if (player->paralyzed != 0)
            player->paralyzed--;
    }
}

void far RedispInv(void)
{
    if (RightPanel == 0 || inplist->mode == 4) {
        load_inventory_pix();
        DisplayInvSpecial();
        DisplayInventory();
    }
}

/* Whether obj on tile is within reach: the squared fine distance at most dist, and the
   height difference within 12 below or 24 above (doubled with a pole; a swimming player
   reaches lower). dist 0 means any distance. Sets MapObj_X, MapObj_Y to the tile. */
unsigned char far InPickRange(int dist, struct Object far *obj, struct Tile far *tile)
{
    int px, py, dz;
    int x, y;

    MapObj_X = (tile - mlowptr) & MAP_MASK;
    MapObj_Y = (int)(tile - mlowptr) >> 6;
    if (dist != 0) {
        x = (MapObj_X << 3) + OBJ_FINEX(obj);
        y = (MapObj_Y << 3) + OBJ_FINEY(obj);
        px = OBJ_HOMEX(ThePlayer);
        py = OBJ_HOMEY(ThePlayer);
        x = x - ((px << 3) + OBJ_FINEX(ThePlayer));
        y = y - ((py << 3) + OBJ_FINEY(ThePlayer));
        x = abs(x);
        y = abs(y);
        if (x * x + y * y > dist)
            return 0;
        dz = OBJ_Z(ThePlayer) - OBJ_Z(obj);
        if (player->swim_count > 0x50)
            dz -= player->swim_count >> 3;
        if ((UsingPole + 1) * 12 < dz || (UsingPole + 1) * -24 > dz)
            return 0;
    }
    return 1;
}

/* The height of the highest bridge on tile, or -1. */
int far BridgeHeight(struct Tile far *tile)
{
    struct Object far *obj;
    int h = -1;

    for (obj = Obj_PtrTMem(&tile->objects); obj; obj = Obj_PtrTMem(&obj->qn.link))
        if (OBJ_ITEM(obj) == ITEM_BRIDGE && (int)OBJ_Z(obj) > h)
            h = OBJ_Z(obj);
    return h;
}

/* Whether the terrain stops the player reaching obj: it is too high or low for him, or a
   tile on the straight line to it rises above him (for the normal reach), or the line
   crosses another terrain class (water, lava, ice) than the one he stands on, unless a
   bridge carries it. dist 0 means never blocked. */
unsigned char far BlockingTerrain(int dist, struct Object far *obj)
{
    int px, py, xdir, ydir, pz, oz, terr, bridge;
    struct Tile far *tile;
    unsigned char found;
    int reach, high;
    int x, y;

    if (dist != 0) {
        px = OBJ_HOMEX(ThePlayer);
        py = OBJ_HOMEY(ThePlayer);
        tile = Map_GetAddr(px, py);
        terr = TERRAIN(tile);
        if (MapObj_X != px || MapObj_Y != py) {
            if (MapObj_X < px)
                xdir = -1;
            else if (MapObj_X > px)
                xdir = 1;
            else
                xdir = 0;
            if (MapObj_Y < py)
                ydir = -1;
            else if (MapObj_Y > py)
                ydir = 1;
            else
                ydir = 0;
            reach = (UsingPole + 1) << 3;
            high = (UsingPole + 1) << 5;
            if ((pz = OBJ_Z(ThePlayer)) > reach)
                pz -= reach;
            else
                pz = 0;
            if ((oz = OBJ_Z(obj)) < pz || pz + high < oz)
                return 1;
            y = py;
            x = px;
            for (;;) {
                x += xdir;
                if (xdir < 0 && x < MapObj_X || xdir > 0 && x > MapObj_X)
                    x = MapObj_X;
                y += ydir;
                if (ydir < 0 && y < MapObj_Y || ydir > 0 && y > MapObj_Y)
                    y = MapObj_Y;
                tile = Map_GetAddr(x, y);
                bridge = BridgeHeight(tile);
                found = 0;
                if (bridge >= 0 && bridge <= oz && bridge >= pz)
                    found = 1;
                if (!found && (pz >> 3) > tile->height && dist == 0x90)
                    return 1;
                if (x == MapObj_X && y == MapObj_Y)
                    break;
                if (dist <= 0x90 && !found && TERRAIN(tile) != 0 && TERRAIN(tile) != terr)
                    return 1;
            }
        }
    }
    return 0;
}

/* Searches outward from x, y in the pick buffer, a growing square spiral up to radius 9,
   for a pixel that names an object; returns its colour or 0. Lets a click near a small
   object pick it. */
unsigned char far check_around(unsigned char far *map, int x, int y)
{
    unsigned char c;
    int side, k, r;
    int16 d[2] = { 0, 1 };

    for (r = 1; r < 10; r++) {
        for (side = 0; side < 1; side++) {
            for (k = 0; k < r; k++) {
                x += d[0];
                y += d[1];
                if (x >= 1 && x <= xwid + 1 && y >= 0 && y <= xhgt) {
                    c = (map + y * (xwid + 2))[x + 2];
                    /* match: this file read PickUp as a word */
                    if (c >= 1 && c < *(int16 *)&PickUp)
                        return c;
                }
            }
            if (d[0] != 0) {
                d[1] = -d[0];
                d[0] = 0;
            } else {
                d[0] = d[1];
                d[1] = 0;
            }
        }
    }
    return 0;
}

/* The object drawn at the pointer in the 3D view, or 0: the renderer's pick buffer gives
   each object a colour (1..PickUp-1, mapped to the object by color_to_obj and its tile by
   color_to_map); a click on a wall or floor (colours 0xAC..0xFC) looks around for a nearby
   object, else leaves the texture in pTxtId for look_nothing. Sets PickMap, releasePtr and
   whether the object could be picked up (releaseable). */
struct Object far * far pick_3d(int how)
{
    unsigned char far *p;
    struct Object far *obj;
    int idx;
    struct ComObj *co;

    do_3d_grab();
    p = stdat;
    p += inplist->y * (xwid + 2) + inplist->x + 2;
    idx = 0;
    pTxtId = 0;
    if (*p >= 1 && *p < *(int16 *)&PickUp) {
        idx = color_to_obj[*p - 1];
        PickMap = mlowptr + color_to_map[*p - 1];
    } else if (*p >= 0xAC && *p <= 0xFC || *p == 0) {
        if ((idx = check_around(stdat, inplist->x, inplist->y)) == 0)
            pTxtId = *p - 0xAB;
        else {
            PickMap = mlowptr + color_to_map[idx - 1];
            idx = color_to_obj[idx - 1];
        }
    }
    if (idx == 0)
        return 0;
    obj = Obj_IntTMem(idx);
    co = &ComObjData[OBJ_ITEM(obj)];
    releasePtr = &PickMap->objects;
    releaseable = co->pickable && !IsMobElem(obj);
    return obj;
}

/* No object under the pointer: describes the texture clicked when looking ('You see' and
   the texture's description from block 10: a wall or floor texture, a terrain, or
   nothing), else prints string 0xA6 + how ('You cannot use that.', ...). */
void far look_nothing(unsigned char how, int txt)
{
    int t;

    if (txt > 0 && how == 2) {
        txt--;
        if (txt < 0x40)
            t = TxmID[txt];
        else if (txt < 0x50)  /* past TxmTerr's 64 entries: in DOS, TxmID, which follows it */
            t = 0x1FE - (uint16)TABLE_NEXT(TxmTerr, txt, 0x40, TxmID);
        else
            t = 0x1FF;
        scroll_print("You see ");
        scroll_print(get_string(t | STR_TEXTURES));
        game_sprint(0x60);  /* '.' */
    } else
        game_sprint(how + 0xA6);  /* 'You cannot use that.', 'Why is this being printed?', 'You see nothing.' */
}

/* Takes a picked-up object off its tile, firing its pick-up triggers (mode 2). */
void far release_3d(struct Object far *obj)
{
    if (releaseable) {
        checkTrap(ThePlayer, obj, 2, MapObj_X, MapObj_Y);
        Obj_Rem(releasePtr, obj);
        editchng(2);
        releaseable = 0;
    }
}

/* Picks up newPlObj into the cursor if it can be taken, is in reach and not blocked
   (a stack asks how many; too heavy: 'That is too heavy for you to pick up.'). Taking book
   0x138 with id bit 13 set (a book that belongs to someone, inferred) sets quest 26 bit 2
   and is a crime against the humans (race 0x1C); taking a moonstone frees its
   player->moonstones entry; owners nearby notice (player_grabbed), and pressure plates are
   updated. Otherwise it explains why, or in the default mode uses or talks instead. */
void far player_3dget(void)
{
    unsigned char inrange, clear;
    struct Object far *split;
    int i;

    split = 0;
    inrange = InPickRange(PickDist, newPlObj, PickMap);
    clear = !BlockingTerrain(PickDist, newPlObj);
    if (releaseable && inrange && clear) {
        if (OBJ_ISQUANT(newPlObj) && !(OBJ_LINK(newPlObj) & LINK_SPECIAL) && OBJ_LINK(newPlObj) != 1) {
            if ((split = AskHowMany(newPlObj)) == 0)
                return;
            if (split != newPlObj)
                Obj_Add(&newPlObj->qn.link, split);
        }
        if (!EncumCheck(newPlObj)) {
            if (split && split != newPlObj) {
                newPlObj->ol.f.link += split->ol.f.link;
                if (Obj_Rem(&newPlObj->qn.link, split))
                    Obj_Free(split);
            }
            game_sprint(0x6C);  /* 'That is too heavy for you to pick up.' */
        } else {
            if (OBJ_ITEM(newPlObj) == ITEM_BOOK_138 && OBJ_DOORDIR(newPlObj)) {
                player->quests[26] = (player->quests[26] & 0xFFFFFFFBL) + 4;
                player_did_bad(0x1C);
            }
            if (HasOrIsObj(newPlObj, ITEM_MOONSTONE))
                for (i = 0; i < 2; i++)
                    if (player->moonstones[i] == PlayerLevel) {
                        player->moonstones[i] = 0;
                        break;
                    }
            player_grabbed(newPlObj, 0);
            release_3d(newPlObj);
            check_pplate(newPlObj, PickMap, OBJ_Z(newPlObj), 0xF);
            GameInputMode = 1;
            DoInventoryDrag(newPlObj);
        }
    } else if (releaseable) {
        game_sprint(inrange + 0x6A);  /* 0x6A 'That is too far away to take.', 0x6B 'You cannot reach that.' */
        mouse_release(1);
    } else if (def_mode) {
        if (IsMobElem(newPlObj) && OBJ_MAJOR(newPlObj) == MAJOR_CREATURE || OBJ_ITEM(newPlObj) == ITEM_WISP)
            player_3dtalk();
        else
            player_3duse();
    } else {
        if (OBJ_ITEM(newPlObj) == ITEM_FROST) {
            if (inrange && clear)
                UseObj(ThePlayer, newPlObj, 0);
        } else
            game_sprint(0x6D);  /* 'You cannot pick that up.' */
        mouse_release(1);
    }
}

void far player_3dtalk(void)
{
#ifndef __TURBOC__
    if (port_shared_avatar(newPlObj)) { scroll_print("Your companion.\n"); return; }
#endif
    mouse_release(1);
    TalkTo(newPlObj);
}

/* Looks at newPlObj, with detail only in close reach. In look mode a found trap may be
   disarmed (asks first, then RemoveTrap with the Traps skill); otherwise dragging after
   the look picks the object up. Fires the object's look triggers (mode 5). */
void far player_3dlook(void)
{
    char yes;
    int r;

    if (InPickRange(0x48, newPlObj, PickMap) && !releaseable)
        LookAt(newPlObj, 1);
    else
        LookAt(newPlObj, 0);
    if (RightButtonThing == 3) {
        if (DetectedTrap(newPlObj, player->skills[SKILL_SEARCH]) > 0) {
            yes = 1;
            r = wyorn(0, 0x103, &yes);  /* 'You found a trap! Do you wish to try to disarm it? ' */
            if (r != 0 && r < 4)
                wd_bool(yes = r == 2);
            scroll_print("\n");
            if (yes)
                RemoveTrap(newPlObj, player->skills[SKILL_TRAPS]);
        }
    } else
        def_mode = 1;
    checkTrap(ThePlayer, newPlObj, 5, MapObj_X, MapObj_Y);
    if (RightButtonThing != 3) {
        if (mouse_dragged(1))
            player_3dget();
    } else
        mouse_release(1);
    def_mode = 0;
}

/* Uses newPlObj if it is in reach, else 'You are unable to use that from here.' (not for
   the texture map objects). */
void far player_3duse(void)
{
    mouse_release(1);
    if (InPickRange(PickDist, newPlObj, PickMap) && !BlockingTerrain(PickDist, newPlObj))
        UseObj(ThePlayer, newPlObj, 0);
    else if ((newPlObj->id & 0x1FE) != ITEM_TMAP_C)
        game_sprint(0xC8);  /* 'You are unable to use that from here.' */
}

/* Fight mode click: the swing is the ninth of the view clicked (1..9, left to right
   and top to bottom). */
/* name: FM Towns calls it player_3dattack; the DOS name is not known otherwise. */
void far player_3dattack(void)
{
    int n;

    n = inplist->x * 3 / (PWid + 2) + inplist->y * 3 / (PHgt + 2) * 3;
    player_attack(n + 1);
}

void (far *player_disp[])(void) = {
    player_3duse, player_3dattack, player_3dlook, player_3dget, player_3dtalk
};

/* The 3D view's mouse handler (see the file comment). A paralysed player can do nothing;
   the left button moves him (player_mous_move). */
void far mous_in_3d(void)
{
    unsigned char how;

    if (player->paralyzed > 0)
        return;
    if (inplist->cmd & 1)
        player_mous_move();
    newPlObj = 0;
    if (inplist->cmd & 2) {
        switch (GameInputMode) {
        case 0:
            if (RightButtonThing == 0)
                how = 2;
            else
                how = RightButtonThing - 1;
            if (how != 1) {
                if (inplist->cmd & 1)
                    break;
                newPlObj = pick_3d(2);
                if (newPlObj == 0) {
                    look_nothing(how, pTxtId);
                    mouse_release(1);
                    break;
                }
            }
            (*player_disp[how])();
            break;
        case 1:
            DoSpecialActions(0x17);
            mouse_release(1);
            break;
        case 2:
            if ((newPlObj = pick_3d(2)) != 0) {
                if (InPickRange(PickDist, newPlObj, PickMap) && !BlockingTerrain(PickDist, newPlObj))
                    (*ObjectActor)(newPlObj, 1, 0);
                else
                    game_sprint(0x6B);  /* 'You cannot reach that.' */
            }
            if (CursorObjPtr) {
                unforce_mouse_cursor(3);
                CursorObjPtr = 0;
                GameInputMode = 0;
            }
            mouse_release(1);
            break;
        case 3:
            BlastFunction();
            break;
        }
    }
}

/* Looks at an object in the inventory, identifying it with the Lore skill: the first
   look rolls skill_check(Lore, 8) + 1 (at least 1) and keeps the best result in the
   object's heading (bit 2 marks it examined, bits 0..1 the lore level), so it is not
   rerolled until a better Lore clears the marks (WORLDEV.C's clear_all_loretries). */
void far inv_look(void)
{
    unsigned char ident;
    int lore;
    int head;

    checkTrap(ThePlayer, newPlObj, 5, MapObj_X, MapObj_Y);
    if (newPlObj == 0)
        newPlObj = pick_inv(2);
    ident = OBJ_MAJOR(FARNULLTRAP(newPlObj)) != MAJOR_RECT && OBJ_MAJOR(FARNULLTRAP(newPlObj)) != MAJOR_TRAP
        && ComObjData[OBJ_ITEM(FARNULLTRAP(newPlObj))].render != 2;
    if (ident == 1) {
        head = OBJ_HEADING(FARNULLTRAP(newPlObj));
        if (head & 4)
            lore = head & 3;
        else {
            lore = skill_check(player->skills[SKILL_LORE], 8) + 1;
            if (lore == 0)
                lore = 1;
            if ((head & 3) > lore)
                lore = head & 3;
            SET_HEADING(FARNULLTRAP(newPlObj), lore | 4);
        }
    } else
        lore = 1;
    LookAt(newPlObj, lore);
    DoInventoryMouse(-1);
}

/* name: not in the FM Towns build and never called in DOS. */
static void far seg026_2716_F8A(void)
{
    DoInventoryMouse(1);
}

/* Use the object under the cursor. */
/* name: not in the FM Towns build and never called in DOS. */
void far seg026_2716_F98(void)
{
    if (newPlObj == 0)
        newPlObj = pick_inv(2);
    mouse_release(1);
    UseObj(ThePlayer, newPlObj, 1);
}

/* Inventory panel clicks by GameInputMode: 0 pick up or put down (looking in look mode or
   on the right button), 1 put down the cursor object, 2 use the cursor object or spell on
   the object clicked (ObjectActor). */
void far mous_in_inv(void)
{
    int x, y;
    int n;
    int hit;

    newPlObj = 0;
    switch (GameInputMode) {
    case 0:
        n = 0;
        if (CursorObjPtr == 0) {
            if (inplist->cmd & 1) {
                if (RightButtonThing == 3)
                    n = -2;
            } else if (RightButtonThing != 1 || inplist->mode != 1)
                n = -2;
        }
        DoInventoryMouse(n);
        break;
    case 1:
        DoInventoryMouse(4);
        break;
    case 2:
        x = inplist->x + 0xF0;
        y = inplist->y + 0x51;
        hit = FindInventoryHit(x, y);
        if (hit == 0x15 || hit == 0x16)
            DoInventoryMouse(1);
        else if ((newPlObj = pick_inv(2)) != 0) {
            (*ObjectActor)(newPlObj, 1, 1);
            mouse_release(1);
        } else {
            unforce_mouse_cursor(3);
            CursorObjPtr = 0;
            GameInputMode = 0;
        }
        break;
    }
}

/* The right panel's mouse handler: inventory, rune bag or statistics by RightPanel. Not
   while asleep in the void or paralysed. */
void far mous_in_panel(void)
{
    if (player->sleepbits && player->in_void || player->paralyzed > 0)
        return;
    switch (RightPanel) {
    case 0:
        mous_in_inv();
        break;
    case 1:
        mous_in_rune();
        break;
    case 2:
        mous_in_stat();
        break;
    }
}

/* A click on one of the interaction icons (mode -1: find which from the pointer). Mode 5
   is the options screen. Otherwise it sheathes the weapon, then selects the mode or, if it
   was already selected, returns to the default. Selecting fight draws the weapon (not
   while asleep in the void, paralysed or with motion_state bit 0 set) and
   starts the combat music; use, look and get change the pointer. */
void far deal_with_icons(int mode)
{
    unsigned char released = 0;

    if (GameInputMode != 0)
        return;
    if (mode == -1) {
        current_button = get_iconreg_button();
        mode = button_to_mode[current_button];
        mouse_release(1);
        released = 1;
    } else
        current_button = TABLE_PREV(mode_to_button, mode - 1, button_to_mode, 6);
    if (mode == 5)
        busywaiting_new_options(&gameopts_buttongroup);
    else {
        set_screen_frame(8, 6);
        if (player->drawn) {
            player->drawn = 0;
            clear_fight_state();
            player->drawn = 0;
        }
        player->drawn = 0;
        if (RightButtonThing == 1 || RightButtonThing == 3 || RightButtonThing == 4)
            unforce_mouse_cursor(3);
        if (++mode == RightButtonThing) {
            new_IconUnselect(mode_to_button[RightButtonThing - 1]);
            RightButtonThing = 0;
        } else {
            if (RightButtonThing != 0)
                new_IconUnselect(mode_to_button[RightButtonThing - 1]);
            RightButtonThing = mode;
            if (RightButtonThing == 2) {
                if (player->sleepbits && player->in_void || player->paralyzed > 0 || player->motion_state & 1)
                    RightButtonThing = 0;
                else {
                    player->drawn = 1;
                    set_screen_frame(8, 4);
                    new_IconSelect(mode_to_button[RightButtonThing - 1]);
                    if (get_current_music() < MUSIC_FOE_HURT || get_current_music() > MUSIC_DANGER)
                        set_new_music(MUSIC_ARMED);
                }
            } else
                new_IconSelect(mode_to_button[RightButtonThing - 1]);
        }
        if (!player->drawn && get_current_music() == 5)
            set_random_walking_music(-1);
        if (released)
            mouse_release(1);
        if (RightButtonThing == 1 || RightButtonThing == 3 || RightButtonThing == 4)
            force_mouse_cursor(0x1077);
    }
}

/* Draws the weapon: fight mode, the combat music, the weapon picture. */
void far pick_fightmode(void)
{
    if (player->drawn == 1)
        return;
    if (player->motion_state & 1)
        return;
    if (RightButtonThing == 1 || RightButtonThing == 3 || RightButtonThing == 4)
        unforce_mouse_cursor(3);
    if (RightButtonThing != 0)
        new_IconUnselect(mode_to_button[RightButtonThing - 1]);
    RightButtonThing = 2;
    player->drawn = 1;
    set_screen_frame(8, 4);
    new_IconSelect(mode_to_button[RightButtonThing - 1]);
    if (get_current_music() < MUSIC_FOE_HURT || get_current_music() > MUSIC_DANGER)
        set_new_music(MUSIC_ARMED);
}

/* Sheathes the weapon and leaves fight mode, back to walking music. */
void far punt_fightmode(void)
{
    if (player->drawn) {
        set_screen_frame(8, 6);
        player->drawn = 0;
        if (LeftPanel == 0)
            new_IconUnselect(mode_to_button[1]);
        RightButtonThing = 0;
        clear_fight_state();
        set_random_walking_music(-1);
    }
}

/* Draws or sheathes the weapon (from a click on the weapon hand), only in the 3D view mode. */
void far toggle_fightmode(void)
{
    if (inplist->mode == 1) {
        if (player->drawn)
            punt_fightmode();
        else
            pick_fightmode();
    }
}
