/* target: ovr095 */
/* opts: -mm -1 -G -O -Y -d */
/* Bartering in conversations: the trade slots on the conversation screen, the
   conversation built-ins setup_to_barter, do_offer, do_demand, do_decline,
   do_judgement, set_likes_dislikes and the npc_inv_* family, and how a trader values an
   item. The whole of UW1's DOS overlay ovr095 (UW2's ovr097), in original order.

   Each side has four slots, two columns by two rows, placed by the tables below. The
   player drags items into their own slots and clicks to select them; a script calls
   setup_to_barter to fill the NPC's slots from its inventory, then do_offer or
   do_demand to trade the selected items. Objects in the slots are out of both
   inventories (barter_npc_ids and barter_ply_ids hold their indices); end_barter drops
   what is left next to its owner.

   Entry points: barter_init and end_barter (the conversation's start and end), the
   mouse handlers play_barter and npc_barter and conv_inv_special, the built-ins above
   (bound in Converse), and the helpers the conversation's inventory built-ins call
   (player_barter_items, player_barter_give, npc_inv_add, npc_barter_find,
   npc_barter_give, npc_barter_give_id, npc_inv_create, npc_inv_delete, assess_value).

   The NPC's temper comes from its creature class (Creature[], critter.h) in barter_init:
   greed is the % gain it wants, patience the bad offers it takes, npc_assess how far
   off its valuations are; the player's charisma skill lowers greed and raises patience.

   UW1 against UW2: four slots a side rather than six, each side's state in arrays of
   its own, and the slot positions in tables; no fudge, give_all_stuff or
   RedisplayBarterSlots; the selection mark is a five-pixel cross; do_demand has no
   "nothing selected" case and no dungeon-level factor, an ally always gives in, and
   attitude can fall to 0; a first offer (last_offer 0) of half of greed or more is
   not tested further; npc_inv_create adds the new item to a like stack; npc_inv_delete unlinks and
   frees one object; assess_value computes in 16 bits and has no coin rule; barter_init
   reseeds the generator from the clock.

   Data owned: the slot tables and the trade state below (the objects in the slots, the
   selections, the valuations, greed, patience, npc_assess); the screen saves under the
   slots.
   Neighbours: CONVERSE.C binds the built-ins and calls barter_init and end_barter;
   INVPANEL.C's DoInventoryMouse and BAGS.C's DoSpecialActions hand clicks in the trade
   area here; TREASURE.C generates an NPC's inventory before it trades; BABL.C gives the
   script memory (getmem) and the npc_attitude variable.

   Name: UW2's (map/filenames.tsv: bartering, setup_to_barter, npc_barter).

   The two sides of a trade are indexed 0 for the NPC and 1 for the player. */
/* name: The function names are UW2's (the FM Towns originals, or UW2Decomp's
   provisional names for FM Towns statics), the routines being the same; ovr095_58B is
   the listing's name for UW2's static ovr097_5F0. The target table's
   RedisplayBarterSlots (ovr095_F07) is a kin false hit: it is the static
   probablyTradeObjects; ovr095_A06 is drawTradeSlot, and ovr095_1D97 npc_inv_create. */
/* match: UW2's provisional names reproduce UW1's stub order too (checked against the
   EXE's 32 stub entries). */
#include <stdlib.h>
#include <time.h>
#include "conv.h"
#include "critter.h"
#include "gfx.h"
#include "inv.h"
#include "map.h"
#include "object.h"
#include "player.h"
#include "sys.h"
#include "ui.h"
#include "uw2.h"

/* Declared as this file uses them (in no header). */
char far EncumCheck(struct Object far *obj);
void far grfx_load_font(char *name);              /* UW2: grfx_quikfont(int) */

/* A point on the screen. */
struct BarterXY {
    int16 x, y;
};

/* UW1 has four trade slots a side, two columns by two rows, placed by these tables: the
   player's slots, the marks showing which are selected, the NPC's slots and their marks. */
static struct BarterXY play_slot_xy[NUM_TRADE_SLOTS] = { { 0x94, 0xBC }, { 0xA9, 0xBC }, { 0x94, 0xAA }, { 0xA9, 0xAA } };
static struct BarterXY play_mark_xy[NUM_TRADE_SLOTS] = { { 0x91, 0xB5 }, { 0xBB, 0xB5 }, { 0x91, 0xA3 }, { 0xBB, 0xA3 } };
static struct BarterXY npc_slot_xy[NUM_TRADE_SLOTS] = { { 0x5B, 0xBC }, { 0x70, 0xBC }, { 0x5B, 0xAA }, { 0x70, 0xAA } };
static struct BarterXY npc_mark_xy[NUM_TRADE_SLOTS] = { { 0x58, 0xB5 }, { 0x82, 0xB5 }, { 0x58, 0xA3 }, { 0x82, 0xA3 } };

static void far ReturnTradeObjectsToNPC_ovr097_F76(int only_unselected);

/* This file's uninitialised data, DS:485E..48BD. */
/* name: greed and npc_assess are UW2's public FM Towns names; the rest are statics. */
/* match: The static names were chosen so that Turbo C lays them out in _BSS where the
   EXE has them (it orders a file's _BSS by a hash of the name, see tools/bssorder.py);
   UW1 keeps each side in arrays of its own. */
static int16 barter_npc_ids[NUM_TRADE_SLOTS];         /* the object in each of the NPC's slots, 0 for none */
static int16 barter_unused1;            /* cleared by barter_init, never read */
static int16 barter_unused2;            /* cleared by barter_init, never read */
static int16 barter_ply_ids[NUM_TRADE_SLOTS];         /* the object in each of the player's slots */
static unsigned char barter_result;     /* the last offer or demand succeeded */
static int16 npc_wit;                   /* worked out by barter_init, never read */
static int16 far *npc_likes;            /* lists set by npc_likes_dislikes, ending in -1 */
static char npc_select[NUM_TRADE_SLOTS];              /* which of the NPC's slots are selected */
static char play_select[NUM_TRADE_SLOTS];             /* which of the player's */
int16 npc_assess;                       /* how far off the NPC's appraisals may be, in % */
static int16 patience;                  /* bad offers left before the NPC gives up */
static int16 far *npc_dislikes;
int16 greed;                            /* the % gain the NPC wants from a trade */
static int16 last_offer;                /* the % gain of the previous offer */
static int16 npc_judgement[NUM_TRADE_SLOTS];          /* the player's valuations of the NPC's items, -1 unknown */
static int16 npc_appraisals[NUM_TRADE_SLOTS];         /* the NPC's valuations of its own */
static int16 npc_undersave[NUM_TRADE_SLOTS];          /* what was on screen under each NPC slot */
static int16 play_judgement[NUM_TRADE_SLOTS];         /* the player's valuations of the player's items */
static int16 play_appraisals[NUM_TRADE_SLOTS];        /* the NPC's valuations of the player's items */
static int16 play_undersave[NUM_TRADE_SLOTS];         /* what was on screen under each player slot */

/* Conversation built-in: fill the NPC's trade slots from its inventory (at most 0x28
   objects looked at), generating the inventory first if it has none. Once all four are
   full it goes round again, each further item replacing the slot's item (which goes
   back into the inventory) with chance 3 in 8, until it comes back to the first item it
   put back. */
void far setup_to_barter(void)
{
    struct Object far *obj;
    struct Object far *in_slot;
    struct Object far *next;
    union Link far *npc_link;
    char skipped_weapon, all_filled;
    register int slot;
    register int count;

    count = 0;
    skipped_weapon = 0;
    all_filled = 0;
    if (OBJ_HAS_INV(talking_to) == 0)
        generate_inventory(talking_to);
    npc_link = &talking_to->ol.link;
    obj = Obj_PtrTMem(&talking_to->ol.link);
    slot = 0;
    in_slot = 0;
    while (obj && obj != in_slot && count++ < 0x28) {
        next = Obj_PtrTMem(&obj->qn.link);
        /* Skip the first weapon, anything worthless and, once every slot has been
           filled, a random five in eight of the rest. */
        if ((OBJ_MINOR(obj) == 0 && !skipped_weapon) ||
            ComObjData[OBJ_ITEM(obj)].value == 0 ||
            (all_filled && (rand() & 7) < 5)) {
            if (OBJ_MINOR(obj) == 0) skipped_weapon = 1;
            obj = next;
        } else {
            Obj_Rem(npc_link, obj);
            if (barter_npc_ids[slot]) {
                if (!in_slot) in_slot = Obj_IntTMem(barter_npc_ids[slot]);
                Obj_Add(&talking_to->ol.link, Obj_IntTMem(barter_npc_ids[slot]));
            }
            barter_npc_ids[slot] = Obj_MemTPtr(obj);
            drawTradeSlot_ovr097_A91(0, slot);
            slot++;
            if (slot > 3) { slot = 0; all_filled = 1; }
            obj = next;
        }
    }
    set_workspace();
}

/* Set up the slots and the NPC's trading temper when a conversation starts. The random
   seed is the NPC's object index, so the same NPC always gets the same temper:
   greed = range(haggle * 6, -25, 25), patience = range(patience, -20, 100), npc_assess =
   range((15 - shrewd) * 6, -25, 50), each then adjusted by charisma (greed - 2 * charm,
   patience + charm / 2). npc_wit is worked out but never read. The generator is reseeded
   from the clock afterwards. */
void far barter_init(void)
{
    int charm;
    register int slot;
    register struct Creature *crit;

    crit = &Creature[OBJ_INMAJOR(talking_to)];
    Transparency = 1;
    for (slot = 0; slot < NUM_TRADE_SLOTS; slot++) {
        play_undersave[slot] = valloc(16, 16);
        npc_undersave[slot] = valloc(16, 16);
    }
    for (slot = 0; slot < NUM_TRADE_SLOTS; slot++) {
        save_rect(play_undersave[slot], play_slot_xy[slot].x, play_slot_xy[slot].y, 16, 16);
        save_rect(npc_undersave[slot], npc_slot_xy[slot].x, npc_slot_xy[slot].y, 16, 16);
    }
    barter_unused2 = 0;
    barter_unused1 = 0;
    for (slot = 0; slot < NUM_TRADE_SLOTS; slot++) {
        barter_ply_ids[slot] = 0;
        barter_npc_ids[slot] = 0;
        play_judgement[slot] = -1;
        npc_judgement[slot] = -1;
        play_appraisals[slot] = -1;
        npc_appraisals[slot] = -1;
        play_select[slot] = 0;
        npc_select[slot] = 0;
        showSelection_ovr097_E83(1, slot);
        showSelection_ovr097_E83(0, slot);
    }
    barter_result = 0;
    SRAND(Obj_MemTPtr(talking_to));
    greed = range(crit->haggle * 6, -25, 25);
    patience = range(crit->patience, -20, 100);
    npc_assess = range((15 - crit->shrewd) * 6, -25, 50);
    npc_wit = range(crit->level, -20, 20);
    last_offer = 0;
    charm = player->skills[SKILL_CHARISMA];
    greed = greed - charm * 2;
    patience += charm >> 1;
    npc_wit -= charm / 6;
    if (npc_wit < 1) npc_wit = 1;
    npc_likes = 0;
    npc_dislikes = 0;
    SRAND(WALL_TIME(0));
}

/* Give back whatever is still in the slots when the conversation ends. */
void far end_barter(void)
{
    register int slot;

    mouse_hide();
    for (slot = 0; slot < NUM_TRADE_SLOTS; slot++) {
        if (barter_ply_ids[slot] > 0) {
            near_mob_put_at(ThePlayer, Obj_IntTMem(barter_ply_ids[slot]), 5, 0);
            restore_rect(play_undersave[slot]);
        }
        if (barter_npc_ids[slot] > 0) {
            near_mob_put_at(talking_to, Obj_IntTMem(barter_npc_ids[slot]), 5, 0);
            restore_rect(npc_undersave[slot]);
        }
    }
    mouse_show();
    for (slot = 0; slot < NUM_TRADE_SLOTS; slot++) {
        vfree(play_undersave[slot]);
        vfree(npc_undersave[slot]);
    }
    set_workspace();
}

/* Mouse handlers for the player's and the NPC's slot areas. */
void far play_barter(void)
{
    int x;
    register int y;
    register int slot;

    x = inplist->x + 0x8B;
    y = inplist->y + 0x98;
    if ((slot = play_slot_hit_abs(x, y)) >= 0) {
        UseTradeSlot_ovr097_6E8(1, slot, barter_ply_ids, play_select);
        set_workspace();
    }
}

/* Which of the player's slots is at x, y, or -1. */
int far play_slot_hit_abs(int x, int y)
{
    register int i;
    register int found;
    found = -1;
    for (i = 0; i < NUM_TRADE_SLOTS; i++) {
        if (play_slot_xy[i].x <= x &&
            play_slot_xy[i].x + 0x10 >= x &&
            play_slot_xy[i].y >= y &&
            play_slot_xy[i].y - 0x10 <= y) {
            found = i;
            break;
        }
    }
    return found;
}

/* Which of the NPC's slots is at x, y, or -1. */
int far npc_slot_hit_abs(int x, int y)
{
    register int i;
    register int found;
    found = -1;
    for (i = 0; i < NUM_TRADE_SLOTS; i++) {
        if (npc_slot_xy[i].x <= x &&
            npc_slot_xy[i].x + 0x10 >= x &&
            npc_slot_xy[i].y >= y &&
            npc_slot_xy[i].y - 0x10 <= y) {
            found = i;
            break;
        }
    }
    return found;
}

/* Which slot of either side is at x, y: 1 with the side, slot and that side's arrays
   filled in, or 0. */
static int16 far ovr095_58B(int x, int y, int16 *side, int16 *slot,
                   int16 **content, register char **active)
{
    register int i;
    if ((i = play_slot_hit_abs(x, y)) > -1) {
        *side = 1;
        *slot = i;
        *content = barter_ply_ids;
        *active = play_select;
        return 1;
    }
    if ((i = npc_slot_hit_abs(x, y)) > -1) {
        *side = 0;
        *slot = i;
        *content = barter_npc_ids;
        *active = npc_select;
        return 1;
    }
    return 0;
}

/* A click on the player's slots during a conversation, at the mouse position. */
void far conv_inv_special(void)
{
    int16 x, y;
    register int slot;
    mouse_getxy(&x, &y);
    slot = play_slot_hit_abs(x, y);
    if (slot > -1)
        UseTradeSlot_ovr097_6E8(1, slot, barter_ply_ids, play_select);
    set_workspace();
}

/* The mouse handler for the NPC's slot area: a click there (UseTradeSlot). */
void far npc_barter(void)
{
    int x;
    register int y;
    register int slot;
    x = inplist->x + 0x52;
    y = inplist->y + 0x98;
    if ((slot = npc_slot_hit_abs(x, y)) >= 0) {
        UseTradeSlot_ovr097_6E8(0, slot, barter_npc_ids, npc_select);
        set_workspace();
    }
}

/* A click on a trade slot: pick up what is there (asking how many of a stack), put
   down what is held, toggle the slot's selection, or look at the item. The NPC's items
   can only be picked up after a successful offer or demand (barter_result), and
   nothing can be put down in the NPC's slots. Looking uses the lore skill: an NPC item
   gets detail 2 on a lore check against 20, the player's own 1 plus the result of a
   check against 15. */
void far UseTradeSlot_ovr097_6E8(int16 side, int16 slot, int16 *content,
                                                 char *active)
{
    struct Object far *found;
    struct Object far *moved;
    int16 x;
    int16 y;
    unsigned char had_cursor;
    struct Object far *obj;
    register int lore;

    moved = 0;
    had_cursor = CursorObjPtr != 0;
    if (CursorObjPtr == 0 && content[slot] == 0) return;
    if (CursorObjPtr == 0 && mouse_dragged(1)) {
        if (side == 0 && barter_result == 0) return;
        found = Obj_IntTMem(content[slot]);
        if (OBJ_ISQUANT(found) && !(found->ol.f.link & LINK_SPECIAL) && found->ol.f.link != 1) {
            if ((moved = AskHowMany(found)) == 0) return;
        }
        if (!EncumCheck(found)) {
            if (moved && moved != found) {
                found->ol.f.link += moved->ol.f.link;
                Obj_Rem(&found->qn.link, moved);
                Obj_Free(moved);
            }
            game_sprint(0xFC);   /* "That is too heavy to take.\n" */
            return;
        }
        if (moved && moved != found) Obj_Add(&found->qn.link, moved);
        had_cursor = 1;
        PickUpFromSlot_ovr097_C39(slot, content, moved && moved != found);
        drawTradeSlot_ovr097_A91(side, slot);
        active[slot] = 0;
        play_judgement[slot] = -1;
        play_appraisals[slot] = -1;
        showSelection_ovr097_E83(side, slot);
        if (CursorObjPtr == 0) return;
        if (moved) {
            GameInputMode = 1;
            return;
        }
        mouse_release(1);
        mouse_getxy(&x, &y);
        if (!ovr095_58B(x, y, &side, &slot, &content, &active)) {
            GameInputMode = 1;
            DoInventoryMouse(-1);
            return;
        }
    }
    if (CursorObjPtr) {
        mouse_release(1);
        GameInputMode = 1;
        if (side == 0) return;
        ovr097_CDB(side, slot, content);
        drawTradeSlot_ovr097_A91(side, slot);
        active[slot] = 1;
        showSelection_ovr097_E83(side, slot);
        play_judgement[slot] = -1;
        play_appraisals[slot] = -1;
    } else if (inplist->cmd & 1) {
        active[slot] = !active[slot];
        showSelection_ovr097_E83(side, slot);
    } else {
        lore = 1;
        obj = Obj_IntTMem(content[slot]);
        if (side == 0) {
            if (skill_check(player->skills[SKILL_LORE], 20) > 0) lore++;
        } else lore += skill_check(player->skills[SKILL_LORE], 15);
        seg024_24DC_D0A(obj, lore);
    }
    if (had_cursor && CursorObjPtr == 0) {
        unforce_mouse_cursor(3);
        GameInputMode = 0;
    }
}

/* Draw what is in one trade slot, with a count for a stack of more than one. */
void far drawTradeSlot_ovr097_A91(int side, register int slot)
{
    int item;
    int qty;
    struct Object far *obj;
    char num[6];
    register int index;

    mouse_hide();
    Transparency = 1;
    if (side) index = barter_ply_ids[slot];
    else index = barter_npc_ids[slot];
    if (index) {
        obj = Obj_IntTMem(index);
        item = OBJ_ITEM(obj);
    }
    if (side) {
        restore_rect(play_undersave[slot]);
        if (index) pic_to_screen(item, play_slot_xy[slot].x, play_slot_xy[slot].y, 16, 16);
    } else {
        restore_rect(npc_undersave[slot]);
        if (index) pic_to_screen(item, npc_slot_xy[slot].x, npc_slot_xy[slot].y, 16, 16);
    }
    if (index) {
        if (OBJ_ISQUANT(obj) && !(obj->ol.f.link & LINK_SPECIAL)) qty = obj->ol.f.link;
        else qty = 0;
        Transparency = 0;
        if (qty > 1) {
            grfx_load_font("font4x5p.sys");
            *foreground_color = 0x60;
            if (side)
                string_to_screen(itoa(qty, num, 10), play_slot_xy[slot].x + 3, play_slot_xy[slot].y - 1);
            else
                string_to_screen(itoa(qty, num, 10), npc_slot_xy[slot].x + 3, npc_slot_xy[slot].y - 1);
            grfx_load_font("font5x6p.sys");
        }
    }
    mouse_show();
    set_workspace();
}

/* Pick up what is in a slot; with split, the rest of a divided stack stays there. */
void far PickUpFromSlot_ovr097_C39(register int slot, register int16 *content, unsigned char split)
{
    unsigned char had_cursor;

    had_cursor = CursorObjPtr != 0;
    CursorObjPtr = Obj_IntTMem(content[slot]);
    content[slot] = 0;
    if (CursorObjPtr) {
        if (split) content[slot] = Obj_MemTPtr(Obj_PtrTMem(&CursorObjPtr->qn.link));
        mouse_hide();
        if (had_cursor) unforce_mouse_cursor(0);
        force_mouse_cursor(OBJ_ITEM(CursorObjPtr));
        mouse_show();
        set_workspace();
    }
}

/* Put what is held into a slot, or onto a stack already there. */
void far ovr097_CDB(int side, register int slot, register int16 *content)
{
    if (content[slot] == 0) {
        content[slot] = Obj_MemTPtr(CursorObjPtr);
        CursorObjPtr = 0;
    } else if (CombineToSlot_ovr097_D30(CursorObjPtr, side, slot, content))
        CursorObjPtr = 0;
}

/* Add a held stack to the like stack in a slot, or else swap the two. Returns 1 when
   the held object was merged and freed. */
unsigned char far CombineToSlot_ovr097_D30(struct Object far *obj, int side,
                                                 register int slot, int16 *content)
{
    struct Object far *found;
    unsigned char merged;
    register int held;

    merged = 0;
    found = Obj_IntTMem(content[slot]);
    if (OBJ_MAJOR(found) == MAJOR_MISC && OBJ_MINOR(found) == MINOR_CONTAINER)
        return 0;
    if (OBJ_ISQUANT(obj) && OBJ_ISQUANT(found) &&
        !(obj->ol.f.link & LINK_SPECIAL) && !(found->ol.f.link & LINK_SPECIAL) &&
        OBJ_ITEM(obj) == OBJ_ITEM(found) &&
        obj->ol.f.link + found->ol.f.link < STACK_LIMIT) {
        found->ol.f.link += obj->ol.f.link;
        Obj_Free(obj);
        merged = 1;
    } else {
        held = Obj_MemTPtr(CursorObjPtr);
        PickUpFromSlot_ovr097_C39(slot, content, 0);
        content[slot] = held;
    }
    drawTradeSlot_ovr097_A91(side, slot);
    set_workspace();
    return merged;
}

/* Draw the small cross beside a slot that shows whether it is selected: colour 0x60 when
   it is, 0xF1 when not. */
void far showSelection_ovr097_E83(int side, int slot)
{
    register struct BarterXY *mark;
    register int color;

    if (side) {
        mark = &play_mark_xy[slot];
        color = play_select[slot] == 1 ? 0x60 : 0xF1;
    } else {
        mark = &npc_mark_xy[slot];
        color = npc_select[slot] == 1 ? 0x60 : 0xF1;
    }
    mouse_hide();
    seg015_1F9B_25A(mark->x, mark->y, color);
    seg015_1F9B_25A(mark->x - 1, mark->y, color);
    seg015_1F9B_25A(mark->x + 1, mark->y, color);
    seg015_1F9B_25A(mark->x, mark->y - 1, color);
    seg015_1F9B_25A(mark->x, mark->y + 1, color);
    mouse_show();
    set_workspace();
}

/* Put the NPC's slot items back in its inventory: all of them, or with only_unselected
   just those not selected. */
static void far ReturnTradeObjectsToNPC_ovr097_F76(int only_unselected)
{
    struct Object far *obj;
    register int slot;
    register int flag;
    flag = only_unselected;
    mouse_hide();
    for (slot = 0; slot < NUM_TRADE_SLOTS; slot++) {
        if (barter_npc_ids[slot] > 0) {
            if (!flag || npc_select[slot] == 0) {
                obj = Obj_IntTMem(barter_npc_ids[slot]);
                Obj_Add(&talking_to->ol.link, obj);
                restore_rect(npc_undersave[slot]);
                npc_select[slot] = 0;
                barter_npc_ids[slot] = 0;
                showSelection_ovr097_E83(0, slot);
            }
        }
    }
    mouse_show();
    set_workspace();
}

/* Hand the NPC every selected item of the player's that it doesn't dislike, adding
   coins to a pile it already has. */
static void far probablyTradeObjects_ovr097_100A(void)
{
    struct Object far *obj;
    struct Object far *other;
    register int slot;

    mouse_hide();
    for (slot = 0; slot < NUM_TRADE_SLOTS; slot++) {
        if (barter_ply_ids[slot] > 0 && play_select[slot] &&
            does_npc_like(barter_ply_ids[slot]) != -1) {
            obj = Obj_IntTMem(barter_ply_ids[slot]);
            other = Obj_PtrTMem(&talking_to->ol.link);
            if (OBJ_ITEM(obj) == ITEM_GOLD_COIN)
                for (; other; other = Obj_PtrTMem(&other->qn.link)) {
                    if (OBJ_ISQUANT(obj) && OBJ_ISQUANT(other) &&
                        !(obj->ol.f.link & LINK_SPECIAL) && !(other->ol.f.link & LINK_SPECIAL) &&
                        OBJ_ITEM(obj) == OBJ_ITEM(other) &&
                        obj->ol.f.link + other->ol.f.link < STACK_LIMIT) {
                        other->ol.f.link += obj->ol.f.link;
                        Obj_Free(obj);
                        obj = 0;
                        break;
                    }
                }
            if (obj) Obj_Add(&talking_to->ol.link, obj);
            restore_rect(play_undersave[slot]);
            play_select[slot] = 0;
            barter_ply_ids[slot] = 0;
            showSelection_ovr097_E83(1, slot);
        }
    }
    mouse_show();
    set_workspace();
}

/* 1 when no slot of a side is both filled and selected. */
char far nothing_there(int16 *content, char *selected)
{
    register int i;
    for (i = 0; i < NUM_TRADE_SLOTS; i++)
        if (selected[i] > 0 && content[i] > 0) return 0;
    return 1;
}

/* The player offers the selected items for the NPC's selected items. The five
   arguments, arg5 to arg1, are the strings to say: accepted, not good enough, worse than
   before, out of patience and nothing offered. Returns 1 when the trade is made.

   Both sides are valued by the NPC (with its likes and dislikes, blurred by npc_assess),
   and the offer's gain is (player's - NPC's) * 100 / NPC's, in %. At or above greed the
   NPC accepts: its unselected items go back to its inventory, the player's selected
   items it does not dislike go to it, and its selected items stay in the slots for the
   player to take. Otherwise: a first offer under half of greed costs 1 patience; an
   offer worse than the last costs 2; and later an offer that closed less than a third
   of the gap left by the last one (inferred from (greed - last) * 3 / 2 > greed - eval)
   costs 1. An offer that costs nothing says nothing. */
int far do_offer(int16 far *args)
{
    int player_value;
    int yes_str, no_str, worse_str, tired_str, none_str;
    register int eval;
    register int npc_value;

    set_workspace();
    yes_str = getmem(args[-5]);
    no_str = getmem(args[-4]);
    worse_str = getmem(args[-3]);
    tired_str = getmem(args[-2]);
    none_str = getmem(args[-1]);
    if (patience < 0) {
        npc_say(get_string(tired_str));
        return 0;
    }
    if (nothing_there(barter_ply_ids, play_select) ||
        nothing_there(barter_npc_ids, npc_select)) {
        npc_say(get_string(none_str));
        barter_result = 0;
        return 0;
    }
    player_value = total_offering_ovr097_17CB(1, barter_ply_ids, play_select,
                                                       play_appraisals, npc_assess);
    npc_value = total_offering_ovr097_17CB(0, barter_npc_ids, npc_select,
                                                    npc_appraisals, npc_assess);
    /* 16-bit ints, as Turbo C's: the gain's product (imul, then cwd before the idiv) wraps
       once the two sides differ by more than 327, so a gross overpayment comes out a loss
       and is refused, where the host's int kept 32 bits and took it; the tests below are
       16-bit too (shl ax,1; imul, cwd and idiv; a 16-bit sub) */
    if (npc_value > 0) eval = (int16)((player_value - npc_value) * 100) / npc_value;
    else eval = 100;
    if (eval >= greed) {
        npc_say(get_string(yes_str));
        ReturnTradeObjectsToNPC_ovr097_F76(1);
        probablyTradeObjects_ovr097_100A();
        barter_result = 1;
        return 1;
    }
    if (last_offer == 0) {
        if ((int16)(eval * 2) < greed) {
            npc_say(get_string(no_str));
            patience--;
        }
    } else if (eval < last_offer) {
        npc_say(get_string(worse_str));
        patience -= 2;
    } else if ((int16)((greed - last_offer) * 3) / 2 > (int16)(greed - eval)) {
        npc_say(get_string(no_str));
        patience--;
    }
    last_offer = eval;
    return 0;
}

/* The player demands the NPC's selected items for nothing. The two arguments, arg2 and
   arg1, are the strings to say: given up and refused. Whether the NPC gives in weighs
   the player's health, level, readiness to fight and charm against the NPC's health,
   temper and mood and the value demanded. Returns 1 when it gives in.

   player = level + drawn + health + charisma / 6, where health is 2 - 2 * damage taken
   / average hit points; npc = class level + mood (-1 an ally, 1 attitude below 2, else
   0) + its health likewise + value demanded / 10. The player must score more, or the
   NPC be an ally. Giving in lowers npc_attitude by one (not below 0); refusing sets the
   NPC to attack the player (goal 5, target 1). */
int far do_demand(int16 far *args)
{
    int player_score;
    int npc_score;
    int armed;
    int16 attitude;
    int insist_str;
    int wont_str;
    struct Creature *crit;
    int demanded;
    register int health;
    register int mood;

    insist_str = getmem(args[-2]);
    wont_str = getmem(args[-1]);
    crit = &Creature[OBJ_INMAJOR(talking_to)];
    if (playerdat->avghit > 0)
        health = 2 - (player->maxhealth - ThePlayer->hp) * 2 / playerdat->avghit;
    else health = 1;
    armed = player->drawn;
    player_score = player->level + armed + health + player->skills[SKILL_CHARISMA] / 6;
    demanded = total_offering_ovr097_17CB(0, barter_npc_ids, npc_select,
                                                   npc_appraisals, npc_assess);
    if (Creature[OBJ_INMAJOR(talking_to)].avghit > 0)
        health = 2 - (Creature[OBJ_INMAJOR(talking_to)].avghit - talking_to->hp) * 2 /
                     Creature[OBJ_INMAJOR(talking_to)].avghit;
    else health = 1;
    bab_var_out("npc_attitude", &attitude, 1);
    if (OBJ_ALLY(talking_to)) mood = -1;
    else if (attitude < 2) mood = 1;
    else mood = 0;
    npc_score = crit->level + mood + health + demanded / 10;
    if (player_score > npc_score || OBJ_ALLY(talking_to)) {
        npc_say(get_string(insist_str));
        ReturnTradeObjectsToNPC_ovr097_F76(1);
        barter_result = 1;
        if (attitude > 0) {
            attitude--;
            bab_var("npc_attitude", &attitude, 1);
        }
        return 1;
    }
    npc_say(get_string(wont_str));
    ReturnTradeObjectsToNPC_ovr097_F76(0);
    change_critter_goal(talking_to, GOAL_ATTACK, 1);
    return 0;
}

/* Conversation built-ins: decline (all the NPC's slot items go back), and the player's
   own appraisal of the offer. */
void far do_decline(void)
{
    ReturnTradeObjectsToNPC_ovr097_F76(0);
}

/* The appraisal: both sides valued without likes, blurred by 50 - appraise * 1.5 %, and
   the gain in % mapped to a verdict from "a terrible deal" (above 50, the player gives
   far more) to "an excellent deal" (-50 or less), prefixed by a certainty from "...I
   guess" (appraise below 6) to "...I know" (24 and up). Spoken as the player's line. */
void far do_judgement(void)
{
    int skill, player_value, npc_value, accuracy, certainty;
    char appraisal[80];
    register int evaluation;
    register int result;

    skill = player->skills[SKILL_APPRAISE];
    accuracy = 50 - skill * 45 / 30;
    player_value = total_offering_ovr097_17CB(0, barter_ply_ids,
                    play_select, play_judgement, accuracy);
    npc_value = total_offering_ovr097_17CB(0, barter_npc_ids,
                    npc_select, npc_judgement, accuracy);
    /* 16-bit, as do_offer's: an offer far above the NPC's can wrap to "an excellent deal" */
    if (npc_value > 0) evaluation = (int16)((player_value - npc_value) * 100) / npc_value;
    else evaluation = 100;
    if (skill < 6) certainty = 0;
    else if (skill < 12) certainty = 1;
    else if (skill < 18) certainty = 2;
    else if (skill < 24) certainty = 3;
    else certainty = 4;
    if (evaluation > 50) result = 0;
    else if (evaluation > 35) result = 1;
    else if (evaluation > 25) result = 2;
    else if (evaluation > 10) result = 3;
    else if (evaluation > -10) result = 4;
    else if (evaluation > -25) result = 5;
    else if (evaluation > -35) result = 6;
    else if (evaluation > -50) result = 7;
    else result = 8;
    str_copy(appraisal, get_string((certainty + 3) | STR_CONV)); /* "...I guess" .. "...I know" */
    str_cat(appraisal, get_string(2 | STR_CONV));  /* " that I am getting " */
    str_cat(appraisal, get_string((result + 8) | STR_CONV)); /* "a terrible deal.." .. "an excellent deal.." */
    play_say(appraisal);
}

/* The total value of a side's selected items, valuing each once and caching it in
   values. With use_likes the NPC's likes and dislikes count. */
int far total_offering_ovr097_17CB(int use_likes, int16 *items,
                                            char *selected,
                                            int16 *values, int accuracy)
{
    register int i;
    register int total;
    total = 0;
    for (i = 0; i < NUM_TRADE_SLOTS; i++) {
        if (selected[i] > 0 && items[i] > 0) {
            if (values[i] == -1)
                values[i] = assess_value(use_likes, items[i], accuracy);
            total += values[i];
        }
    }
    return (int16)total;                /* a 16-bit sum (add di), which wraps as DOS's does */
}

/* An object's value, scaled by quantity and quality and blurred by up to accuracy %,
   the same each time for the same object. With use_likes a disliked item is worth 0
   and a liked one 1.5 times as much. Value is the class value (ComObjData) times the
   quantity times quality / 64, at least 1 if the quality is not 0, in 16 bits. The blur
   seeds rand with the object index, then reseeds from the clock. */
int far assess_value(int use_likes, int item, int accuracy)
{
    struct Object far *obj;
    int quality;
    int quantity;
    register int value;
    register int disposition;

    obj = Obj_IntTMem(item);
    if (use_likes) {
        disposition = does_npc_like(item);
        if (disposition == -1) return 0;
        value = ComObjData[OBJ_ITEM(obj)].value;
        if (disposition) value = (int16)(value * 3) >> 1;
    } else value = ComObjData[OBJ_ITEM(obj)].value;
    if (OBJ_ISQUANT(obj) && !(obj->ol.f.link & LINK_SPECIAL))
        quantity = obj->ol.f.link;
    else quantity = 1;
    /* 16-bit ints, as Turbo C's (imul, the low word kept, and imul then sar ax,6): a stack
       or a valuable item passes 7FFFh, and then DOS values it below 0 or at a fraction, where
       the host's int kept 32 bits until the store into values[] */
    value = (int16)(value * quantity);
    quality = obj->qn.f.quality;
    if (value > 0) {
        if (quality > 0) {
            value = (int16)(value * quality) >> 6;
            if (value == 0) value = 1;
        } else value = 0;
    }
    SRAND(item);
    value = range(value, -accuracy, accuracy);
    SRAND(WALL_TIME(0));
    return value;
}

/* base plus a random min..max % of it (max itself excluded). */
int far range(int base, int min, int max)
{
    /* the product is a 16-bit int (imul, then cwd before the idiv), which wraps once base
       times the percentage passes 7FFFh; the callers store the sum in 16 bits */
    return base + (int16)(base * (min + (int)(((int32)rand() * (max - min)) / 0x8000L))) / 100;
}

/* The item types and indexes of the player's selected items; returns how many. */
int far player_barter_items(int16 *items, int16 *indices)
{
    struct Object far *obj;
    register int slot;
    register int count;

    slot = 0;
    count = 0;
    for (; slot < NUM_TRADE_SLOTS; slot++) {
        if (play_select[slot]) {
            obj = Obj_IntTMem(barter_ply_ids[slot]);
            indices[count] = barter_ply_ids[slot];
            items[count] = OBJ_ITEM(obj);
            count++;
        }
    }
    return count;
}

/* Give an object to the NPC, adding coins to a pile it already has. */
void far npc_inv_add(struct Object far *obj)
{
    struct Object far *other;
    if (OBJ_ITEM(obj) == ITEM_GOLD_COIN) {
        for (other = Obj_PtrTMem(&talking_to->ol.link); other;
             other = Obj_PtrTMem(&other->qn.link)) {
            if (OBJ_ISQUANT(obj) &&
                OBJ_ISQUANT(other) &&
                !(obj->ol.f.link & LINK_SPECIAL) &&
                !(other->ol.f.link & LINK_SPECIAL) &&
                OBJ_ITEM(obj) == OBJ_ITEM(other) &&
                obj->ol.f.link + other->ol.f.link < STACK_LIMIT) {
                other->ol.f.link += obj->ol.f.link;
                Obj_Free(obj);
                obj = 0;
                break;
            }
        }
    }
    if (obj) Obj_Add(&talking_to->ol.link, obj);
}

/* The player gives an object from the slots to the NPC (for give_to_npc and
   give_ptr_npc, CONVERSE.C). */
void far player_barter_give(int index)
{
    register int slot;
    register int objindex;
    objindex = index;
    npc_inv_add(Obj_IntTMem(objindex));
    mouse_hide();
    for (slot = 0; slot < NUM_TRADE_SLOTS; slot++) {
        if (barter_ply_ids[slot] == objindex) {
            restore_rect(play_undersave[slot]);
            barter_ply_ids[slot] = 0;
            play_select[slot] = 0;
            showSelection_ovr097_E83(1, slot);
        }
    }
    mouse_show();
    set_workspace();
}

/* For find_inv: find an item (or from 1000 a major and minor, (item - 1000) >> 2 and & 3)
   in the NPC's or the player's inventory, searching containers too; returns its index or
   0. */
int far npc_barter_find(int item, int from_player)
{
    struct Object far *found;
    union Link far *head;
    int major, minor;
    register int id;
    register int cls;
    id = item;
    if (from_player) head = &ThePlayer->ol.link;
    else {
        if (OBJ_HAS_INV(talking_to) == 0)
            generate_inventory(talking_to);
        head = &talking_to->ol.link;
    }
    if (id > BARTER_CLASS - 1) {
        major = (id - BARTER_CLASS) >> 2;
        minor = (id - BARTER_CLASS) & 3;
        cls = -1;
    } else {
        major = id >> 6;
        minor = (id & ID_MINOR) >> 4;
        cls = id & 0xF;
    }
    found = Obj_InList(&head, 1, major, minor, cls);
    return Obj_MemTPtr(found);
}

/* For take_from_npc: the NPC hands over an item of the given type (or, from 1000, of
   class item - 1000, that is item types (item - 1000) * 16 onwards): into the player's hand, else into a free trade slot,
   else dropped at the NPC's feet. Returns 1, 2 when dropped, 3 when even that fails,
   or 0 when there is none or the player's hand is full. */
int far npc_barter_give(register int item)
{
    struct Object far *obj;
    union Link far *head;
    register int slot;

    if (CursorObjPtr) return 0;
    if (OBJ_HAS_INV(talking_to) == 0)
        generate_inventory(talking_to);
    head = &talking_to->ol.link;
    for (obj = Obj_PtrTMem(&talking_to->ol.link); obj; obj = Obj_PtrTMem(&obj->qn.link)) {
        if (item > BARTER_CLASS - 1) {
            if (OBJ_CLASS(obj) != item - BARTER_CLASS) continue;
        } else if (OBJ_ITEM(obj) != item) continue;
        Obj_Rem(head, obj);
        if (EncumCheck(obj)) {
            CursorObjPtr = obj;
            mouse_hide();
            force_mouse_cursor(OBJ_ITEM(obj));
            GameInputMode = 1;
            mouse_show();
            set_workspace();
            return 1;
        }
        for (slot = 0; slot < NUM_TRADE_SLOTS; slot++)
            if (barter_ply_ids[slot] == 0) {
                barter_ply_ids[slot] = Obj_MemTPtr(obj);
                play_select[slot] = 0;
                play_judgement[slot] = npc_appraisals[slot] = -1;
                showSelection_ovr097_E83(0, slot);
                drawTradeSlot_ovr097_A91(1, slot);
                return 1;
            }
        if (near_mob_put_at(talking_to, obj, 5, 0)) return 2;
        return 3;
    }
    return 0;
}

/* The same for one particular object, by its index. */
int far npc_barter_give_id(register int index)
{
    struct Object far *obj;
    union Link far *head;
    register int slot;

    if (CursorObjPtr) return 0;
    head = &talking_to->ol.link;
    for (obj = Obj_PtrTMem(&talking_to->ol.link); obj; obj = Obj_PtrTMem(&obj->qn.link)) {
        if (Obj_MemTPtr(obj) != index) continue;
        Obj_Rem(head, obj);
        if (EncumCheck(obj)) {
            CursorObjPtr = obj;
            mouse_hide();
            force_mouse_cursor(OBJ_ITEM(obj));
            GameInputMode = 1;
            mouse_show();
            set_workspace();
            return 1;
        }
        for (slot = 0; slot < NUM_TRADE_SLOTS; slot++)
            if (barter_ply_ids[slot] == 0) {
                barter_ply_ids[slot] = Obj_MemTPtr(obj);
                play_select[slot] = 0;
                play_judgement[slot] = npc_appraisals[slot] = -1;
                showSelection_ovr097_E83(0, slot);
                drawTradeSlot_ovr097_A91(1, slot);
                return 1;
            }
        if (near_mob_put_at(talking_to, obj, 5, 0)) return 2;
        return 3;
    }
    return 0;
}

/* For do_inv_create and do_inv_delete: create an item (quality 63) in the NPC's
   inventory, adding it to a like stack there, and return its index; or remove and free
   the first of a type, returning 1 if found. */
int far npc_inv_create(int item)
{
    struct Object far *obj;
    struct Object far *other;
    obj = CreateObj(item, 0);
    if (!obj) return 0;
    obj->qn.f.quality = 0x3F;
    for (other = Obj_PtrTMem(&talking_to->ol.link); other;
         other = Obj_PtrTMem(&other->qn.link)) {
        if (OBJ_ISQUANT(obj) &&
            OBJ_ISQUANT(other) &&
            !(obj->ol.f.link & LINK_SPECIAL) &&
            !(other->ol.f.link & LINK_SPECIAL) &&
            OBJ_ITEM(obj) == OBJ_ITEM(other) &&
            obj->ol.f.link + other->ol.f.link < STACK_LIMIT) {
            other->ol.f.link += obj->ol.f.link;
            Obj_Free(obj);
            obj = 0;
            break;
        }
    }
    if (obj) Obj_Add(&talking_to->ol.link, obj);
    return Obj_MemTPtr(obj);
}

/* Removes and frees the first object of item in the NPC's inventory; 1 if there was one. */
int far npc_inv_delete(int item)
{
    struct Object far *obj;
    union Link far *head;
    register int id;
    id = item;
    head = &talking_to->ol.link;
    for (obj = Obj_PtrTMem(&talking_to->ol.link); obj; obj = Obj_PtrTMem(&obj->qn.link)) {
        if (OBJ_ITEM(obj) == id) {
            Obj_Rem(head, obj);
            Obj_Free(obj);
            return 1;
        }
    }
    return 0;
}

/* set_likes_dislikes(arg2 likes, arg1 dislikes): point at the NPC's lists of liked and
   disliked items, two script arrays ending in -1. */
int far npc_likes_dislikes(int16 far *args)
{
    npc_likes = getmem_addr(args[-2]);
    npc_dislikes = getmem_addr(args[-1]);
    return 1;
}

/* 1 when the NPC likes an object, -1 when it dislikes it or it is worthless, else 0.
   List entries of 1000 and up name a class, (item >> 4) + 1000. */
int far does_npc_like(int index)
{
    struct Object far *obj;
    int liked;
    int cls;
    register int id;
    register int i;

    obj = Obj_IntTMem(index);
    if (ComObjData[OBJ_ITEM(obj)].value == 0) return -1;
    id = OBJ_ITEM(obj);
    cls = (id >> 4) + BARTER_CLASS;
    liked = 0;
    if (npc_likes)
        for (i = 0; npc_likes[i] > -1; i++) {
            if (npc_likes[i] < BARTER_CLASS) {
                if (npc_likes[i] == id) return 1;
            } else if (npc_likes[i] == cls) liked = 1;
        }
    if (npc_dislikes)
        for (i = 0; npc_dislikes[i] > -1; i++) {
            if (npc_dislikes[i] < BARTER_CLASS) {
                if (npc_dislikes[i] == id) return -1;
            } else if (npc_dislikes[i] == cls) liked = -1;
        }
    return liked;
}
