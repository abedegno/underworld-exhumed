/* target: ovr120 */
/* opts: -mm -1 -G -O -Y -d */
/* The player's inventory as data: what is in each slot, adding objects to it and taking
   them out (whole stacks or part of one, from the paperdoll, the backpack or the open bag),
   searching it for an object by class, damage to worn equipment, and object weight: the
   whole of DOS overlay ovr120, in original order. Seeded from UW2's INVDATA.C (ovr124);
   UW1's differences are marked "UW1:" where they are not obvious: pick_inv's panel y
   offset (0x52), Obj_Rem's result not tested in invRemoveObject and takeFromSlot, ObjWorn's
   result tested signed, the handedness bit's place in the player record, and the debris a
   destroyed item leaves (a random 0xD5 or 0xD6, not debris_type).

   What it does in the game: the inventory is the player object's contents list (the
   player's ol.link), with Inventory[] (28 link words, slot layout in inv.h) naming the
   object shown in each slot. This file keeps the two and the carried weight
   (PlayerDat.rec.weight at DS:72D2, PlayerDat being DS:7288, and each open bag's weight)
   in step. The panel (ovr121, UW2's INVPANEL.C) calls it for every move; the rest of the
   game asks it what the player carries (FindObj, AskInventory) and wears out armour and
   weapons through DamageInventory.

   Callers: nearly every subsystem that touches what the player carries: INVPANEL.C and
   BAGS.C, USEITEMS.C, COMBAT.C (weapon and armour wear), PLAYTIME.C and PLAYDATA.C,
   INTERACT.C, SKILLS.C, TRIGGER.C, WORLDEV.C, CONVERSE.C, BARTER.C, MISSILE.C.

   Data owned: none for certain (its _DATA is the string pool at DS:16F4). OpenBag and the
   slot tables are probably the panel's, as in UW2.
   Function and global names are UW2's (FM Towns originals) except FindEmptySlot, which has
   no FM Towns counterpart and a provisional name chosen for its key (see below).
   Name: descriptive (the inventory as data: AddToInventory, FindSlot). */
/* name: Turbo C lists a file's publics in descending order of the tools/bssorder.py key of each
   name, and TLINK numbers overlay stub entries from the last one listed, so the EXE's stub
   order constrains the names. FM Towns gives AskInventory and WhatsInSlot one address (the
   two functions are identical, and DOS has both), so its code cannot tell them apart; the
   stub order can: the function at +15 must have a key between 321 and 481 and the one at
   +5A4 between 630 and 702, which AskInventory (465) and WhatsInSlot (655) meet only this way
   round. FindEmptySlot (1022) must sort above takeFromSlot (1004). */

#include <stdlib.h>
#include <string.h>
#include "inv.h"
#include "map.h"
#include "object.h"
#include "player.h"
#include "ui.h"
#include "uw2.h"

/* Declared in each file that uses it, its own way (no header). */
unsigned char far EncumCheck(struct Object far *obj);
unsigned char far invRemoveObject(struct Object far *obj, int qty);

/* PLAYER.C defines the record's storage as a byte array; this file reaches it as the
   union (player.h). */
extern union PlayerStore PlayerDat;

/* Redraws the display position of Inventory[] slot slot. */
void far RedisplayInvSlot(int slot)
{
    DisplayInvObject(SlotToDisplay[slot]);
}

/* The object in Inventory[] slot slot, or 0. */
struct Object far * far AskInventory(int slot)
{
    return Obj_PtrTMem(&Inventory[slot]);
}

/* The first empty slot from 5 to 18, or -1. Not called. */
/* name: not in the FM Towns build; IDA's ovr124_2C, renamed for its key. */
int far FindEmptySlot(void)
{
    register int slot;

    for (slot = INV_SHOULDER; slot <= INV_PACK_LAST; slot++)
        if (Inventory[slot].f.index == 0)
            return slot;
    return -1;
}

/* Adds obj to the inventory, in slot (checked by ItemFitsSlot; -1 adds it to the
   player's contents without a slot). Slots above 18 are the open bag: the object goes into
   that bag and every enclosing open bag's weight grows. Returns 1 when added; with
   ItemFitsSlot's -1 (the object was eaten from the head slot, inferred) it returns 0. */
unsigned char far AddToInventory(struct Object far *obj, int slot)
{
    char done;
    struct Object far *owner;
    struct Bag far *bag;
    register int mass;
    register int fits;

    done = 0;
    owner = ThePlayer;
    if (slot == -1)
        fits = 1;
    else
        fits = ItemFitsSlot(obj, slot);
    if (fits > 0) {
        mass = ItemWeight(obj);
        if (slot >= 0) {
            if (slot > INV_PACK_LAST) {
                owner = Obj_PtrTMem(&OpenBag->obj);
                for (bag = OpenBag; bag; bag = bag->prev)
                    bag->weight += mass;
            }
            Inventory[slot].f.index = Obj_MemTPtr(obj);
        }
        Obj_AddEnd(&owner->ol.link, obj);
        PlayerDat.rec.weight += mass;
        done = 1;
    } else if (fits == -1)
        done = 0;
    FixPlayerEquips();
    return done;
}

/* The slot holding obj, minus the slot of a container holding it, or -1. */
int far FindSlot(struct Object far *obj)
{
    int index;
    struct Object far *inslot;
    register int slot;
    register int i;

    index = Obj_MemTPtr(obj);
    for (i = 0; i < DISP_BAG; i++) {
        slot = DisplayToSlot[i];
        if (Inventory[slot].f.index != 0) {
            if (Inventory[slot].f.index == index)
                return slot;
            inslot = Obj_PtrTMem(&Inventory[slot]);
            if (!OBJ_ISQUANT(inslot) && Inventory[slot].f.index != FARNULLREC(OpenBag, "ffww")->obj.f.index
                && inslot != 0 && Obj_Find(&inslot->ol.link, 1, index) != 0)
                return -slot;
        }
    }
    return -1;
}

/* Finds an object by major, minor and class (-1 for any) in the inventory and sets *where
   to its slot. how limits the search: 1 the paperdoll and hands (slots 0..10) only, 2 and
   3 the backpack too (to 18), anything else also inside the containers carried. */
struct Object far * far FindObj(int major, int minor, int cls, int how, register int16 *where)
{
    struct Object far *contents;
    struct Object far *objs[INV_PACK_LAST + 1];
    register int i;

    for (i = 0; i < INV_PACK; i++) {
        objs[i] = Obj_PtrTMem(&Inventory[i]);
        if (objs[i] != 0 && (major < 0 || OBJ_MAJOR(objs[i]) == major)
            && (minor < 0 || OBJ_MINOR(objs[i]) == minor)
            && (cls < 0 || OBJ_INCLASS(objs[i]) == cls)) {
            *where = i;
            return objs[i];
        }
    }
    if (how == 1)
        return 0;
    for (; i <= INV_PACK_LAST; i++) {
        objs[i] = Obj_PtrTMem(&Inventory[i]);
        if (objs[i] != 0 && (major < 0 || OBJ_MAJOR(objs[i]) == major)
            && (minor < 0 || OBJ_MINOR(objs[i]) == minor)
            && (cls < 0 || OBJ_INCLASS(objs[i]) == cls)) {
            *where = i;
            return objs[i];
        }
    }
    if (how == 2)
        return 0;
    if (how == 3)
        return 0;
    for (i = 0; i <= INV_PACK_LAST; i++) {
        if (objs[i] != 0 && !OBJ_ISQUANT(objs[i])) {
            contents = Obj_PtrTMem(&objs[i]->ol.link);
            objs[i] = find_obj(major, minor, cls, &contents);
            if (objs[i] != 0) {
                *where = i;
                return objs[i];
            }
        }
    }
    return 0;
}

/* Depth-first search of an object list and its containers for the first object matching
   major, minor and class (-1 for any). On a match *list is set to 0, or after a match
   inside a container to the rest of that container's list. */
struct Object far * far find_obj(int major, int minor, register int cls, register struct Object far **list)
{
    struct Object far *next;
    struct Object far *found;

    while (*list != 0) {
        if ((major < 0 || OBJ_MAJOR(*list) == major)
            && (minor < 0 || OBJ_MINOR(*list) == minor)
            && (cls < 0 || OBJ_INCLASS(*list) == cls)) {
            found = *list;
            *list = 0;
            return found;
        }
        if (!OBJ_ISQUANT(*list) && (next = Obj_PtrTMem(&(*list)->ol.link)) != 0
            && (found = find_obj(major, minor, cls, &next)) != 0) {
            if (next != 0)
                *list = next;
            return found;
        }
        *list = Obj_PtrTMem(&(*list)->qn.link);
    }
    return 0;
}

/* The object under the pointer in the inventory panel: with how 2 just looked at, else
   taken out of its slot. */
struct Object far * far pick_inv(int how)
{
    int x;
    register int hit;
    register int y;

    x = inplist->x + 0xF0;
    y = inplist->y + 0x52;
    hit = FindInventoryHit(x, y);
    if (hit < 0 || hit >= DISP_BAG)
        return 0;
    if (how == 2)
        return WhatsInSlot(DisplayToSlot[hit]);
    return RemoveAllFromSlot(-1, -1, -1, DisplayToSlot[hit]);
}

/* The same as AskInventory. */
/* name: the FM Towns map gives both names one address. */
struct Object far * far WhatsInSlot(int slot)
{
    return Obj_PtrTMem(&Inventory[slot]);
}

/* Takes all of obj out of the inventory. */
char far InvRemoveObject(struct Object far *obj)
{
    return invRemoveObject(obj, -1);
}

/* Takes one of obj (one of a stack) out of the inventory. */
char far InvRemoveOneObject(struct Object far *obj)
{
    return invRemoveObject(obj, 1);
}

/* Removes obj, or qty of a stack (-1 for all), from the inventory: through its slot if it
   has one (redrawing the slot or the open bag), else out of the contents lists, splitting
   a stack when only part is taken. Returns 0 when it is not carried. */
unsigned char far invRemoveObject(struct Object far *obj, int qty)
{
    int index;
    int have;
    struct Object far *copy;
    struct Bag far *bag;
    register int i;
    register int mass;

    mass = ItemWeight(obj);
    index = Obj_MemTPtr(obj);
    for (i = 0; i < NUM_INV_SLOTS; i++)
        if (Inventory[i].f.index == index)
            break;
    if (i < NUM_INV_SLOTS) {
        removeFromSlot(-1, -1, -1, i, qty);
        if (i >= INV_BAG) {
            FixOpenBag();
            DisplayOpenBag();
            for (bag = OpenBag; bag; bag = bag->prev)
                bag->weight -= mass;
        } else
            DisplayInvObject(SlotToDisplay[i]);
    } else {
        obj = Obj_Find(&ThePlayer->ol.link, 1, index);
        if (obj == 0)
            return 0;
        if (qty > 0 && OBJ_ISQUANT(obj) && !(obj->ol.f.link & LINK_SPECIAL)
            && (have = obj->ol.f.link) > 1 && qty < have) {
            copy = Obj_Alloc(0);
            *(struct StaticObj far *)copy = *(struct StaticObj far *)obj;
            copy->ol.f.link = have - qty;
            obj->ol.f.link = qty;
            Obj_Add(&obj->qn.link, copy);
        }
        Obj_Rem(Obj_Find_Head, obj);
        PlayerDat.rec.weight -= mass;
        DisplayInvObject(0x13);
        FixPlayerEquips();
    }
    return 1;
}

/* Takes the whole object in slot (or the first match inside it, by major, minor and class)
   out of the inventory, and redraws the slot, or the open bag when one is open. */
struct Object far * far RemoveAllFromSlot(int major, int minor, int cls, register int slot)
{
    struct Object far *taken;
    struct Object far *inslot;

    taken = removeFromSlot(major, minor, cls, slot, 0);
    if ((inslot = Obj_PtrTMem(&Inventory[slot])) != 0 && OBJ_MAJOR(inslot) == MAJOR_MISC
        && OBJ_MINOR(inslot) == MINOR_CONTAINER && OpenBag != 0) {
        FixOpenBag();
        DisplayOpenBag();
    } else
        DisplayInvObject(SlotToDisplay[slot]);
    return taken;
}

/* As RemoveAllFromSlot, taking one of a stack. */
struct Object far * far RemoveOneFromSlot(int major, int minor, int cls, register int slot)
{
    struct Object far *taken;
    struct Object far *inslot;

    taken = removeFromSlot(major, minor, cls, slot, 1);
    if ((inslot = Obj_PtrTMem(&Inventory[slot])) != 0 && OBJ_MAJOR(inslot) == MAJOR_MISC
        && OBJ_MINOR(inslot) == MINOR_CONTAINER && OpenBag != 0) {
        FixOpenBag();
        DisplayOpenBag();
    } else
        DisplayInvObject(SlotToDisplay[slot]);
    return taken;
}

/* takeFromSlot, then the equipment effects are worked out again (FixPlayerEquips). */
struct Object far * far removeFromSlot(int major, int minor, int cls, int slot, int qty)
{
    struct Object far *obj;

    obj = takeFromSlot(major, minor, cls, slot, qty);
    FixPlayerEquips();
    return obj;
}

/* Takes the object in slot, or with major, minor or class given the first matching object
   inside it, out of the inventory. qty splits a stack (0 takes it all); the rest of a split
   stack stays in the slot. Keeps the carried weight and the open bags' weights right.
   Returns the object taken or 0. */
struct Object far * far takeFromSlot(int major, int minor, int cls, int slot, int qty)
{
    struct Object far *obj;
    struct Object far *copy;
    struct Object far *owner;
    int have;
    int index;
    struct Bag far *bag;
    register int i;
    register int mass;

    copy = 0;
    obj = Obj_PtrTMem(&Inventory[slot]);
    if (obj == 0)
        return 0;
    if (slot <= INV_PACK_LAST)
        owner = ThePlayer;
    else
        owner = Obj_PtrTMem(&OpenBag->obj);
    if (major >= 0 || minor >= 0 || cls >= 0) {
        if ((major >= 0 && OBJ_MAJOR(obj) != major) || (minor >= 0 && OBJ_MINOR(obj) != minor)
            || (cls >= 0 && OBJ_INCLASS(obj) != cls)) {
            if ((obj = find_obj(major, minor, cls, &owner)) == 0)
                return 0;
        }
    }
    if (qty != 0 && OBJ_ISQUANT(obj) && !(obj->ol.f.link & LINK_SPECIAL)
        && (have = obj->ol.f.link) > 1 && qty < have) {
        copy = Obj_Alloc(0);
        *(struct StaticObj far *)copy = *(struct StaticObj far *)obj;
        copy->ol.f.link = have - qty;
        obj->ol.f.link = qty;
        Obj_Add(&obj->qn.link, copy);
    }
    if (owner == ThePlayer || slot >= INV_BAG_ITEMS) {
        if (copy != 0)
            Inventory[slot].f.index = Obj_MemTPtr(copy);
        else
            Inventory[slot].f.index = 0;
    }
    Obj_Rem(&owner->ol.link, obj);
    mass = ItemWeight(obj);
    PlayerDat.rec.weight -= mass;
    if (OpenBag != 0 && Obj_MemTPtr(owner) == OpenBag->obj.f.index) {
        index = Obj_MemTPtr(obj);
        for (i = INV_BAG_ITEMS; i <= INV_BAG_LAST; i++) {
            if (Inventory[i].f.index == index) {
                Inventory[i].f.index = copy == 0 ? 0 : Obj_MemTPtr(copy);
                for (bag = OpenBag; bag; bag = bag->prev)
                    bag->weight -= mass;
                break;
            }
        }
    }
    return obj;
}

/* Whether an object of type id is being worn or wielded in slot: anything in the armour
   slots 0..4 and the ring slots 9 and 10, and in the shield hand (lefty + 7) a shield
   (MAJOR_HACK, minor 2 or 3, class 0xB..0xF). */
unsigned char far ObjWorn(register int id, register int slot)
{
    if (slot >= 0 && slot <= INV_BOOTS)
        return 1;
    if (slot == INV_RING + 1 || slot == INV_RING)
        return 1;
    if (player->lefty + INV_HAND != slot)
        return 0;
    if ((id >> 6) == MAJOR_HACK && (id & ID_MINOR) >> 4 >= 2 && (id & ID_INCLASS) >= 0xB && (id & ID_INCLASS) <= 0xF)
        return 1;
    return 0;
}

/* Damage the object in slot: -2 if there is none or it does not take this damage, -1 if
   it was unharmed, otherwise 1 if it was destroyed and 0 if only damaged. how 0 damages
   only a weapon, 1 only something worn there, 2 anything. With debris set a destroyed
   object leaves debris by the player (UW1: item 0xD5 or 0xD6 at random). Prints 'Your <item> was/were damaged.' or
   '... destroyed.' (were when the name ends in s). */
int far DamageInventory(int slot, unsigned char damage, unsigned char type, int how, char debris)
{
    struct Object far *obj;
    int quality;
    struct Object far *junk;
    char text[50];
    register char *msg;
    register int result;

    if ((obj = AskInventory(slot)) == 0)
        return -2;
    if (how != 2) {
        if (how == 0) {
            if (OBJ_CLASS(obj) != CLASS_WEAPON)
                return -2;
        } else if (!(char)ObjWorn(OBJ_ITEM(obj), slot))  /* UW1: tested signed (cbw) */
            return -2;
    }
    quality = obj->qn.f.quality;
    if (damage_item(obj, 0L, -1, -1, damage, type)) {
        if (debris) {
            junk = CreateObj((int)(rand() * 2L / 0x8000L) + ITEM_PILE_OF_DEBRIS_D5, 0);
            near_mob_put_at(ThePlayer, junk, 6, 0);
        }
        InvRemoveOneObject(obj);
        Obj_Punt(0L, obj, 1);
        FixPlayerEquips();
        msg = " destroyed.\n";
        result = 1;
    } else if (obj->qn.f.quality != quality) {
        msg = " damaged.\n";
        result = 0;
    } else
        return -1;
    strcpy(text, "Your ");
    if (obj == ThePlayer)
        SET_ITEM(obj, ITEM_FIST);
    get_name(text + strlen(text), obj, 0, 0);
    if (text[strlen(text) - 1] == 's')
        strcat(text, " were");
    else
        strcat(text, " was");
    strcat(text, msg);
    scroll_print(text);
    RedisplayInvSlot(slot);
    return result;
}

/* An object's weight in tenths of a stone (inferred from the panel's weight / 10): mass
   times quantity for a stack, or its own mass plus its contents for a container. */
int far ItemWeight(struct Object far *obj)
{
    int16 mass;
    register struct ComObj *com;

    com = &ComObjData[OBJ_ITEM(obj)];
    if (OBJ_ISQUANT(obj) && !(obj->ol.f.link & LINK_SPECIAL))
        mass = obj->ol.f.link * com->mass;
    else {
        mass = com->mass;
        if (!OBJ_ISQUANT(obj) && obj->ol.f.link > 0)
            BagWeight(&obj->ol.link, &mass);
    }
    return mass;
}

/* True if the player can carry obj as well without going over max_weight. */
unsigned char far EncumCheck(struct Object far *obj)
{
    /* an unsigned int, as Turbo C's (int + unsigned): a sum past FFFFh wraps, where the
       host's int kept it */
    if ((uint16)(ItemWeight(obj) + PlayerDat.rec.weight) > PlayerDat.rec.max_weight)
        return 0;
    return 1;
}
