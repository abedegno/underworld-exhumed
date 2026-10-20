/* target: ovr097 */
/* opts: -mm -1 -G -O -Y -d */
/* Bartering in conversations: the trade slots on the conversation screen, the
   conversation built-ins setup_to_barter, do_offer, do_demand, do_decline,
   do_judgement, give_all_stuff, set_likes_dislikes and the npc_inv_* family, and how a
   trader values an item. The whole of DOS overlay ovr097, in original order.

   Each side has six slots, two columns by three rows, the NPC's at x 0x4B and the
   player's at x 0x7C. The player drags items into their own slots and clicks to select
   them; a script calls setup_to_barter to fill the NPC's slots from its inventory, then
   do_offer or do_demand to trade the selected items. Objects in the slots are out of
   both inventories (barter_items holds their indices); end_barter drops what is left
   next to its owner.

   Entry points: barter_init and end_barter (strt_converse and free_converse in
   CONVERSE.C), the mouse handlers play_barter and npc_barter (game/PLAYER.C) and
   conv_inv_special (inv/BAGS.C), the built-ins above (bound in Converse), and the
   helpers CONVERSE.C's inventory built-ins call (player_barter_items, player_barter_give,
   npc_inv_add, npc_barter_find, npc_barter_give, npc_barter_give_id, npc_inv_create,
   npc_inv_delete, assess_value, RedisplayBarterSlots).

   The NPC's temper comes from its creature class (Creature[], critter.h) in barter_init:
   greed is the % gain it wants, patience the bad offers it takes, npc_assess how far
   off its valuations are; the player's charisma skill lowers greed and raises patience.
   A conversation can scale greed and set fudge through babl_hack (BABLHACK.C).

   Name: descriptive (map/filenames.tsv: bartering, setup_to_barter, npc_barter).

   The two sides of a trade are indexed 0 for the NPC and 1 for the player. */
/* name: Names are the originals from the FM Towns symbol table where it has them; the
   functions it lacks were static there, and have provisional names (the target table
   keeps IDA's names). */
/* match: The provisional names were chosen for their keys: Turbo C lists a file's
   publics by the tools/bssorder.py key of each name and TLINK numbers overlay stub
   entries from the last one listed, so these names reproduce the EXE's stub order. */
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

/* A trade adjustment set by a conversation (babl_hack mode 9, ovr096): do_judgement adds
   it to the value of the NPC's side. */
/* match: DS:BFE, the first byte of this file's _DATA: its string at DS:BFF follows
   ovr096's data, which ends at an even address, so this byte is ours. */
char fudge = 0;


static void far ReturnTradeObjectsToNPC_ovr097_F76(int only_unselected);

/* This file's uninitialised data. */
/* name: FM Towns names greed and npc_assess, which are public; the rest were static
   there. */
/* match: The static names were chosen so that Turbo C lays them out in _BSS where the
   EXE has them (it orders a file's _BSS by a hash of the name, see tools/bssorder.py). */
static unsigned char barter_result;     /* the last offer or demand succeeded */
static unsigned char barter_selected[2][6];
static int16 npc_wit;                   /* worked out by barter_init, never read */
static int16 far *npc_likes;            /* lists set by npc_likes_dislikes, ending in -1 */
int16 npc_assess;                       /* how far off the NPC's appraisals may be, in % */
static int16 patience;                  /* bad offers left before the NPC gives up */
static int16 far *npc_dislikes;
int16 greed;                            /* the % gain the NPC wants from a trade */
static int16 last_offer;                /* the % gain of the previous offer */
static int16 barter_vals[2][2][6];      /* the player's and the NPC's valuations, -1 unknown */
static int16 barter_saves[2][6];        /* what was on screen under each slot */
static int16 barter_items[2][6];        /* the object in each slot, 0 for none */

/* Conversation built-in: fill the NPC's trade slots from its inventory (at most 0x28
   objects looked at), generating the inventory first if it has none. Once all six are
   full it goes round again, each further item replacing the slot's item (which goes
   back into the inventory) with chance 3 in 8, until it comes back to the first item it
   put back. */
void far setup_to_barter(void)
{
    struct Object far *obj;
    struct Object far *in_slot;
    struct Object far *next;
    union Link far *npc_link;
    unsigned char skipped_weapon, all_filled;
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
        if ((OBJ_CLASS(obj) == CLASS_WEAPON && !skipped_weapon) ||
            ComObjData[obj->id & ID_ITEM].value == 0 ||
            (all_filled && (rand() & 7) < 5)) {
            if (OBJ_CLASS(obj) == CLASS_WEAPON) skipped_weapon = 1;
            obj = next;
        } else {
            /* the same object is tried again if it can't be unlinked */
            if (!Obj_Rem(npc_link, obj)) continue;
            if (barter_items[0][slot]) {
                if (!in_slot) in_slot = Obj_IntTMem(barter_items[0][slot]);
                Obj_Add(&talking_to->ol.link, Obj_IntTMem(barter_items[0][slot]));
            }
            barter_items[0][slot] = Obj_MemTPtr(obj);
            drawTradeSlot_ovr097_A91(0, slot);
            slot++;
            if (slot >= 6) { slot = 0; all_filled = 1; }
            obj = next;
        }
    }
    set_workspace();
}

/* Set up the slots and the NPC's trading temper when a conversation starts. The random
   seed is the NPC's object index, so the same NPC always gets the same temper:
   greed = range(haggle * 6, -25, 25), patience = range(patience, -20, 100), npc_assess =
   range((15 - shrewd) * 6, -25, 50), each then adjusted by charisma (greed - 2 * charm,
   patience + charm / 2). npc_wit is worked out but never read. */
void far barter_init(void)
{
    int charm;
    struct Creature *crit;
    register int side;
    register int slot;

    crit = &Creature[OBJ_INMAJOR(talking_to)];
    Transparency = 1;
    for (slot = 0; slot < 6; slot++)
        for (side = 0; side < 2; side++)
            barter_saves[side][slot] = valloc(16, 16);
    for (slot = 0; slot < 6; slot++) {
        save_rect(barter_saves[1][slot], (slot & 1) * 0x13 + 0x7C, 0xBA - slot / 2 * 0x12, 16, 16);
        save_rect(barter_saves[0][slot], (slot & 1) * 0x13 + 0x4B, 0xBA - slot / 2 * 0x12, 16, 16);
    }
    for (slot = 0; slot < 6; slot++)
        for (side = 0; side < 2; side++) {
            barter_items[side][slot] = 0;
            barter_vals[side][0][slot] = -1;
            barter_vals[side][1][slot] = -1;
            barter_selected[side][slot] = 0;
            showSelection_ovr097_E83(side, slot);
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
}

/* Give back whatever is still in the slots when the conversation ends. */
void far end_barter(void)
{
    register int side;
    register int slot;

    mouse_hide();
    for (slot = 0; slot < 6; slot++)
        for (side = 0; side < 2; side++) {
            if (barter_items[side][slot] > 0) {
                near_mob_put_at(side == 1 ? ThePlayer : talking_to,
                                Obj_IntTMem(barter_items[side][slot]), 5, 0);
                restore_rect(barter_saves[side][slot]);
                if (barter_selected[side][slot]) {
                    barter_selected[side][slot] = 0;
                    showSelection_ovr097_E83(side, slot);
                }
            }
            vfree(barter_saves[side][slot]);
        }
    mouse_show();
    set_workspace();
}

/* Mouse handlers for the player's and the NPC's slot areas. */
void far play_barter(void)
{
    int x;
    register int y;
    register int slot;

    x = inplist->x + 0x77;
    y = inplist->y + 0x87;
    if ((slot = play_slot_hit_abs(x, y)) >= 0) {
        UseTradeSlot_ovr097_6E8(1, slot, barter_items[1], barter_selected[1]);
        set_workspace();
    }
}

/* Which of the player's slots is at x, y, or -1. */
int far play_slot_hit_abs(int x, int y)
{
    register int i;
    register int found;
    found = -1;
    for (i = 0; i < 6; i++) {
        if ((i & 1) * 0x13 + 0x7C <= x &&
            (i & 1) * 0x13 + 0x8C >= x &&
            0xBA - (i / 2) * 0x12 >= y &&
            0xBA - (i / 2) * 0x12 - 0x10 <= y) {
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
    for (i = 0; i < 6; i++) {
        if ((i & 1) * 0x13 + 0x4B <= x &&
            (i & 1) * 0x13 + 0x5B >= x &&
            0xBA - (i / 2) * 0x12 >= y &&
            0xBA - (i / 2) * 0x12 - 0x10 <= y) {
            found = i;
            break;
        }
    }
    return found;
}

/* Which slot of either side is at x, y: 1 with the side, slot and that side's arrays
   filled in, or 0. */
/* name: Unnamed (static) in the FM Towns build, which has it straight after
   npc_slot_hit_abs. */
static int16 far ovr097_5F0(int x, int y, int16 *side, int16 *slot,
                   int16 **content, register unsigned char **active)
{
    register int i;
    if ((i = play_slot_hit_abs(x, y)) > -1) {
        *side = 1;
        *slot = i;
        *content = barter_items[1];
        *active = barter_selected[1];
        return 1;
    }
    if ((i = npc_slot_hit_abs(x, y)) > -1) {
        *side = 0;
        *slot = i;
        *content = barter_items[0];
        *active = barter_selected[0];
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
        UseTradeSlot_ovr097_6E8(1, slot, barter_items[1], barter_selected[1]);
    set_workspace();
}

void far npc_barter(void)
{
    int x;
    register int y;
    register int slot;
    x = inplist->x + 0x46;
    y = inplist->y + 0x87;
    if ((slot = npc_slot_hit_abs(x, y)) >= 0) {
        UseTradeSlot_ovr097_6E8(0, slot, barter_items[0], barter_selected[0]);
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
                                                 unsigned char *active)
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
        if (moved && moved != found) Obj_Add(&found->qn.link, moved);
        if (!EncumCheck(found)) {
            if (moved && moved != found) {
                found->ol.f.link += moved->ol.f.link;
                if (Obj_Rem(&found->qn.link, moved)) Obj_Free(moved);
            }
            game_sprint(0x10C);   /* "That is too heavy to take.\n" */
            return;
        }
        had_cursor = 1;
        PickUpFromSlot_ovr097_C39(slot, content, moved && moved != found);
        drawTradeSlot_ovr097_A91(side, slot);
        active[slot] = 0;
        barter_vals[1][0][slot] = -1;
        barter_vals[1][1][slot] = -1;
        showSelection_ovr097_E83(side, slot);
        if (CursorObjPtr == 0) return;
        if (moved) {
            GameInputMode = 1;
            return;
        }
        mouse_release(1);
        mouse_getxy(&x, &y);
        if (!ovr097_5F0(x, y, &side, &slot, &content, &active)) {
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
        barter_vals[1][0][slot] = -1;
        barter_vals[1][1][slot] = -1;
    } else if (inplist->cmd & 1) {
        active[slot] = !active[slot];
        showSelection_ovr097_E83(side, slot);
    } else {
        lore = 1;
        obj = Obj_IntTMem(content[slot]);
        if (side == 0) {
            if (skill_check(player->skills[SKILL_LORE], 20) > 0) lore++;
        } else lore += skill_check(player->skills[SKILL_LORE], 15);
        LookAt(obj, lore);
    }
    if (had_cursor && CursorObjPtr == 0) {
        unforce_mouse_cursor(3);
        GameInputMode = 0;
    }
}

void far RedisplayBarterSlots(int side)
{
    register int i;
    for (i = 0; i < 6; i++) drawTradeSlot_ovr097_A91(side, i);
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
    index = barter_items[side][slot];
    if (index) {
        obj = Obj_IntTMem(index);
        item = obj->id & ID_ITEM;
    }
    restore_rect(barter_saves[side][slot]);
    if (side) {
        if (index) pic_to_screen(item, (slot & 1) * 0x13 + 0x7C, 0xBA - slot / 2 * 0x12, 16, 16);
    } else if (index) pic_to_screen(item, (slot & 1) * 0x13 + 0x4B, 0xBA - slot / 2 * 0x12, 16, 16);
    if (index) {
        if (OBJ_ISQUANT(obj) && !(obj->ol.f.link & LINK_SPECIAL)) qty = obj->ol.f.link;
        else qty = 0;
        Transparency = 0;
        if (qty > 1) {
            grfx_quikfont(FONT_4X5P);
            *foreground_color = 2;
            if (side)
                string_to_screen(itoa(qty, num, 10), (slot & 1) * 0x13 + 0x7F, 0xBA - slot / 2 * 0x12 - 1);
            else
                string_to_screen(itoa(qty, num, 10), (slot & 1) * 0x13 + 0x4E, 0xBA - slot / 2 * 0x12 - 1);
            grfx_quikfont(FONT_5X6P);
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
        obj->ol.f.link + found->ol.f.link < 0x3E7) {
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

/* Draw the small selection mark beside a slot: lit when it is selected. */
void far showSelection_ovr097_E83(int side, int slot)
{
    int color;
    register int x, y;

    y = 0xBC - slot / 2 * 0x12 - 10;
    if (slot & 1) x = 0x71;
    else x = 0x47;
    if (side) x += 0x31;
    mouse_hide();
    color = barter_selected[side][slot] ? 0x23 : 1;
    set_the_color(color);
    uhline(x - 1, y + 1, x + 1);
    if (color != 1) color++;
    *pixel_color = color;
    plot_pixel(x, y + 2);
    color = barter_selected[side][slot] ? 0x21 : 1;
    set_the_color(color);
    uhline(x - 1, y, x + 1);
    uvline(x, y + 1, y - 1);
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
    for (slot = 0; slot < 6; slot++) {
        if (barter_items[0][slot] > 0) {
            if (!flag || barter_selected[0][slot] == 0) {
                obj = Obj_IntTMem(barter_items[0][slot]);
                Obj_Add(&talking_to->ol.link, obj);
                restore_rect(barter_saves[0][slot]);
                barter_selected[0][slot] = 0;
                barter_items[0][slot] = 0;
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
    for (slot = 0; slot < 6; slot++) {
        if (barter_items[1][slot] > 0 && barter_selected[1][slot] &&
            does_npc_like(barter_items[1][slot]) != -1) {
            obj = Obj_IntTMem(barter_items[1][slot]);
            other = Obj_PtrTMem(&talking_to->ol.link);
            if (OBJ_ITEM(obj) == ITEM_COIN)
                for (; other; other = Obj_PtrTMem(&other->qn.link)) {
                    if (OBJ_ISQUANT(obj) && OBJ_ISQUANT(other) &&
                        !(obj->ol.f.link & LINK_SPECIAL) && !(other->ol.f.link & LINK_SPECIAL) &&
                        OBJ_ITEM(obj) == OBJ_ITEM(other) &&
                        obj->ol.f.link + other->ol.f.link < 0x3E7) {
                        other->ol.f.link += obj->ol.f.link;
                        Obj_Free(obj);
                        obj = 0;
                        break;
                    }
                }
            if (obj) Obj_AddEnd(&talking_to->ol.link, obj);
            restore_rect(barter_saves[1][slot]);
            barter_selected[1][slot] = 0;
            barter_items[1][slot] = 0;
            showSelection_ovr097_E83(1, slot);
        }
    }
    mouse_show();
    set_workspace();
}

/* 1 when no slot of a side is both filled and selected. */
unsigned char far nothing_there(int16 *content, unsigned char *selected)
{
    register int i;
    for (i = 0; i < 6; i++)
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
    if (nothing_there(barter_items[1], barter_selected[1]) ||
        nothing_there(barter_items[0], barter_selected[0])) {
        npc_say(get_string(none_str));
        barter_result = 0;
        return 0;
    }
    player_value = total_offering_ovr097_17CB(1, barter_items[1], barter_selected[1],
                                                       barter_vals[1][1], npc_assess);
    npc_value = total_offering_ovr097_17CB(0, barter_items[0], barter_selected[0],
                                                    barter_vals[0][1], npc_assess);
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
    if (last_offer == 0 && (int16)(eval * 2) < greed) {
        npc_say(get_string(no_str));
        patience--;
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

/* The player demands the NPC's selected items for nothing. The three arguments, arg3
   to arg1, are the strings to say: nothing selected, given up and refused. Whether the
   NPC gives in weighs the player's health, level, readiness to fight and charm against
   the NPC's health, temper and mood and the value demanded. Returns 1 when it gives in.

   player = level + drawn + health + charisma / 6, where health is 2 - 2 * damage taken
   / average hit points; npc = class level + mood (-1 an ally, 1 attitude below 2, else
   0) + its health likewise + value demanded / 10, times 1.5 on dungeon levels 9 and
   0x11 (PlayerLevel). The player must score more. Giving in lowers npc_attitude by one
   (not below 1), as does demanding nothing; refusing sets the NPC to attack the player
   (goal 5, target 1). */
int far do_demand(int16 far *args)
{
    int player_score;
    int npc_score;
    int armed;
    int16 attitude;
    int insist_str;
    int wont_str;
    int what_str;
    struct Creature *crit;
    int demanded;
    register int health;
    register int mood;

    insist_str = getmem(args[-2]);
    wont_str = getmem(args[-1]);
    what_str = getmem(args[-3]);
    if (nothing_there(barter_items[0], barter_selected[0])) {
        bab_var_out("npc_attitude", &attitude, 1);
        if (attitude > 1) {
            attitude--;
            bab_var("npc_attitude", &attitude, 1);
        }
        npc_say(get_string(what_str));
        ReturnTradeObjectsToNPC_ovr097_F76(0);
        barter_result = 0;
        return 0;
    }
    crit = &Creature[OBJ_INMAJOR(talking_to)];
    if (playerdat->avghit > 0)
        health = 2 - (player->maxhealth - ThePlayer->hp) * 2 / playerdat->avghit;
    else health = 1;
    armed = player->drawn;
    player_score = player->level + armed + health + player->skills[SKILL_CHARISMA] / 6;
    demanded = total_offering_ovr097_17CB(0, barter_items[0], barter_selected[0],
                                                   barter_vals[0][1], npc_assess);
    if (Creature[OBJ_INMAJOR(talking_to)].avghit > 0)
        health = 2 - (Creature[OBJ_INMAJOR(talking_to)].avghit - talking_to->hp) * 2 /
                     Creature[OBJ_INMAJOR(talking_to)].avghit;
    else health = 1;
    bab_var_out("npc_attitude", &attitude, 1);
    if OBJ_ALLY(talking_to) mood = -1;
    else if (attitude < 2) mood = 1;
    else mood = 0;
    npc_score = crit->level + mood + health + demanded / 10;
    if (PlayerLevel == 9 || PlayerLevel == 0x11)
        npc_score = npc_score * 3 / 2;
    if (player_score > npc_score) {
        npc_say(get_string(insist_str));
        ReturnTradeObjectsToNPC_ovr097_F76(1);
        barter_result = 1;
        if (attitude > 1) {
            attitude--;
            bab_var("npc_attitude", &attitude, 1);
        }
        return 1;
    }
    npc_say(get_string(wont_str));
    ReturnTradeObjectsToNPC_ovr097_F76(0);
    change_critter_goal(talking_to, 5, 1);
    return 0;
}

/* Conversation built-ins: decline (all the NPC's slot items go back), and the player's
   own appraisal of the offer. */
void far do_decline(void)
{
    ReturnTradeObjectsToNPC_ovr097_F76(0);
}

/* The appraisal: both sides valued without likes, blurred by 50 - appraise * 1.5 %,
   fudge added to the NPC's side, and the gain in % mapped to a verdict from "a terrible
   deal" (above 50, the player gives far more) to "an excellent deal" (-50 or less),
   prefixed by a certainty from "...I guess" (appraise below 6) to "...I know" (24 and
   up). Spoken as the player's line. */
void far do_judgement(void)
{
    int skill, player_value, npc_value, accuracy, certainty;
    char appraisal[80];
    register int evaluation;
    register int result;

    skill = player->skills[SKILL_APPRAISE];
    accuracy = 50 - skill * 45 / 30;
    player_value = total_offering_ovr097_17CB(0, barter_items[1],
                    barter_selected[1], barter_vals[1][0], accuracy);
    npc_value = total_offering_ovr097_17CB(0, barter_items[0],
                    barter_selected[0], barter_vals[0][0], accuracy);
    npc_value += fudge;
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
    str_cat(appraisal, get_string(0xE02));  /* " that I am getting " */
    str_cat(appraisal, get_string((result + 8) | STR_CONV)); /* "a terrible deal.." .. "an excellent deal.." */
    play_say(appraisal);
}

/* The total value of a side's selected items, valuing each once and caching it in
   values. With use_likes the NPC's likes and dislikes count. */
int far total_offering_ovr097_17CB(int use_likes, int16 *items,
                                            unsigned char *selected,
                                            int16 *values, int accuracy)
{
    register int i;
    register int total;
    total = 0;
    for (i = 0; i < 6; i++) {
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
   quantity times quality / 64 (coins count as quality 63), at least 1 if the quality is
   not 0. The blur seeds rand with the object index, then reseeds from the clock. */
int far assess_value(int use_likes, int item, int accuracy)
{
    struct Object far *obj;
    int quantity;
    int disposition;
    register int value;
    register int quality;

    obj = Obj_IntTMem(item);
    if (use_likes) {
        disposition = does_npc_like(item);
        if (disposition == -1) return 0;
        value = ComObjData[obj->id & ID_ITEM].value;
        if (disposition) value = (int16)(value * 3) >> 1;
    } else value = ComObjData[obj->id & ID_ITEM].value;
    if (OBJ_ISQUANT(obj) && !(obj->ol.f.link & LINK_SPECIAL))
        quantity = obj->ol.f.link;
    else quantity = 1;
    /* a 16-bit int, as Turbo C's (imul, the low word kept): a stack passes 7FFFh and DOS
       then values it below 0 or at a fraction, where the host's int kept 32 bits (the quality
       product below is a long in UW2, from a 16-bit value) */
    value = (int16)(value * quantity);
    quality = obj->qn.f.quality;
    if (OBJ_ITEM(obj) == ITEM_COIN) quality = 0x3F;
    if (value > 0) {
        if (quality > 0) {
            value = (int)(((int32)value * quality) >> 6);
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
    for (; slot < 6; slot++) {
        if (barter_selected[1][slot]) {
            obj = Obj_IntTMem(barter_items[1][slot]);
            indices[count] = barter_items[1][slot];
            items[count] = obj->id & ID_ITEM;
            count++;
        }
    }
    return count;
}

/* Give an object to the NPC, adding coins to a pile it already has. */
void far npc_inv_add(struct Object far *obj)
{
    struct Object far *other;
    if (OBJ_ITEM(obj) == ITEM_COIN) {
        for (other = Obj_PtrTMem(&talking_to->ol.link); other;
             other = Obj_PtrTMem(&other->qn.link)) {
            if (OBJ_ISQUANT(obj) &&
                OBJ_ISQUANT(other) &&
                !(obj->ol.f.link & LINK_SPECIAL) &&
                !(other->ol.f.link & LINK_SPECIAL) &&
                OBJ_ITEM(obj) == OBJ_ITEM(other) &&
                obj->ol.f.link + other->ol.f.link < 0x3E7) {
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
    for (slot = 0; slot < 6; slot++) {
        if (barter_items[1][slot] == objindex) {
            restore_rect(barter_saves[1][slot]);
            barter_items[1][slot] = 0;
            barter_selected[1][slot] = 0;
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
    if (id > 0x3E7) {
        major = (id - 0x3E8) >> 2;
        minor = (id - 0x3E8) & 3;
        cls = -1;
    } else {
        major = id >> 6;
        minor = (id & 0x30) >> 4;
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
        if (item > 0x3E7) {
            if (OBJ_CLASS(obj) != item - 1000) continue;
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
        for (slot = 0; slot < 6; slot++)
            if (barter_items[1][slot] == 0) {
                barter_items[1][slot] = Obj_MemTPtr(obj);
                barter_selected[1][slot] = 0;
                barter_vals[1][0][slot] = barter_vals[0][1][slot] = -1;
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
        for (slot = 0; slot < 6; slot++)
            if (barter_items[1][slot] == 0) {
                barter_items[1][slot] = Obj_MemTPtr(obj);
                barter_selected[1][slot] = 0;
                barter_vals[1][0][slot] = barter_vals[0][1][slot] = -1;
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
   inventory, returning its index, or delete the first of a type (with its contents,
   inferred from Obj_FreeLinkChain), returning 1 if found. */
int far npc_inv_create(int item)
{
    struct Object far *obj;
    obj = CreateObj(item, 0);
    if (!obj) return 0;
    obj->qn.f.quality = 0x3F;
    if (obj) Obj_Add(&talking_to->ol.link, obj);
    return Obj_MemTPtr(obj);
}

int far npc_inv_delete(int item)
{
    struct Object far *obj;
    union Link far *head;
    register int id;
    id = item;
    head = &talking_to->ol.link;
    for (obj = Obj_PtrTMem(&talking_to->ol.link); obj; obj = Obj_PtrTMem(&obj->qn.link)) {
        if (OBJ_ITEM(obj) == id) {
            Obj_FreeLinkChain(head, obj);
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
    if (ComObjData[obj->id & ID_ITEM].value == 0) return -1;
    id = obj->id & ID_ITEM;
    cls = (id >> 4) + 0x3E8;
    liked = 0;
    if (npc_likes)
        for (i = 0; npc_likes[i] > -1; i++) {
            if (npc_likes[i] < 0x3E8) {
                if (npc_likes[i] == id) return 1;
            } else if (npc_likes[i] == cls) liked = 1;
        }
    if (npc_dislikes)
        for (i = 0; npc_dislikes[i] > -1; i++) {
            if (npc_dislikes[i] < 0x3E8) {
                if (npc_dislikes[i] == id) return -1;
            } else if (npc_dislikes[i] == cls) liked = -1;
        }
    return liked;
}

/* Conversation built-in: the NPC gives the player everything in its slots: all are
   selected and left in the slots for the player to take, as after an accepted offer. */
int far give_all_stuff(void)
{
    register int i;
    for (i = 0; i < 6; i++)
        if (barter_items[0][i]) {
            barter_selected[0][i] = 1;
            showSelection_ovr097_E83(0, i);
        }
    if (nothing_there(barter_items[0], barter_selected[0])) return 0;
    ReturnTradeObjectsToNPC_ovr097_F76(1);
    barter_result = 1;
    return 1;
}
