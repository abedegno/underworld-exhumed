/* target: seg023 */
/* opts: -mm -1 -G -O -d */
/* Damage to objects that are not critters: resistances, wear on doors and containers,
   and what an object leaves behind when it breaks. The whole of DOS resident segment
   seg023, in original order. Seeded from UW2Decomp's src/combat/DAMAGE.C (UW2's
   seg025_26A1). Function names are UW2's (the FM Towns originals), the routines being the
   same; UW1 has no symbols of its own.

   What it does in the game: every source of damage (melee in COMBAT.C, spells in
   SPELLS.C, falls and collisions in the motion code, traps) calls damage_item. It applies
   the item's resistances from ComObjData (check_res), hands critters to damage_critter,
   and for anything else takes the damage off the object's hit points or quality
   (damage_object). An object worn to nothing is turned into debris or removed
   (remove_object): a door opens and loses its locks, a chest or barrel spills, a bag
   drops its contents.

   UW1 against UW2: no debris_type (broken objects become one of the two piles of debris,
   0xD5 or 0xD6, at random), no broken weapons, no djinn bottle, no test of a chest's id bit
   13, no fire against cold doubling in check_res, debris is not given fresh hit points or
   quality, every object with id bit 13 set is indestructible (UW2 excepts doors), and
   damaging an owned object does not anger its owner.

   Callers: COMBAT.C, SPELLS.C, WORLDEV.C (traps), INVDATA.C (worn equipment), and the
   motion and game code for falls, collisions, drowning, poison and hunger (OBJPHYS.C,
   PLAYMOVE.C, PHYSICS.C, PATHFIND.C, SKILLS.C, PLAYTIME.C). Critters go on to AI.C's
   damage_critter; broken doors and containers to USEITEMS.C.

   Data owned: none; the per-item table is ComObjData (object.h).
   Name: UW2Decomp's (damage_object is in System Shock's DAMAGE.C, the damage to objects in
   both). */

#include <stdlib.h>
#include "combat.h"
#include "critter.h"
#include "event.h"
#include "gfx.h"
#include "map.h"
#include "motion.h"
#include "object.h"
#include "player.h"
#include "sys.h"
#include "ui.h"

/* Declared in each file that uses it, its own way (no header). */
unsigned char far IsMobElem(struct Object far *obj);

/* Removes the lock object (a MAJOR_SPEC object of minor class 0) from obj's contents:
   the first one, or every one if all is set. Returns 1 only when all was set and a lock
   was found; with all clear it returns 0 even after removing one. */
char far remove_lock(struct Object far *obj, char all)
{
    union Link far *head;
    struct Object far *lock;
    char found = 0;

    if (!OBJ_ISQUANT(obj) && OBJ_LINK(obj) > 0)
    {
        for (head = &obj->ol.link; (lock = Obj_InList(&head, 1, MAJOR_SPEC, 0, 0xF)) != 0; )
        {
            Obj_Rem(head, lock);        /* UW1: Obj_Rem returns nothing */
            Obj_Free(lock);
            if (all == 0)
                break;
            found = 1;
        }
    }
    else
        found = 0;
    return found;
}

/* Takes a dying object off the map list head. Objects below objdata (the player and
   other fixed records, inferred) cannot be freed: their hit points are zeroed instead and
   0 is returned. Otherwise returns 1 when Obj_Punt removed it. */
char far try_remove(union Link far *head, struct Object far *obj)
{
    if (obj >= (struct Object far *)objdata)
        return Obj_Punt(head, obj, 0) == 0;
    obj->hp = 0;
    return 0;
}

/* What happens when an object is destroyed by damage of the given type at map square
   x, y (x < 0: not on the map, nothing is done and 1 is returned). debris is the item the
   object turns into: -2 means a random pile of debris (0xD5 or 0xD6), -1 means leave the
   object as it is. Doors of minor class 0..7 swing open and every door loses its locks; a
   chest or barrel spills its contents; a bag drops its contents if its fate says so; fire
   (type bit 3) removes a pile of debris and turns anything else into one, one time in
   four, with effect 8. Returns 1 when the object has left the map (removed by
   try_remove, or obj_deal failed to replace it), else 0; the final debris < -1 test is
   always false. */
char far remove_object(struct Object far *obj, struct Object far *who, char type, int x, int y)
{
    union Link far *head;
    register int debris = -2;
    register int item;

    if (x < 0)
        return 1;
    if (ITEM_CLASS(item = OBJ_ITEM(obj)) == CLASS_DOOR)
    {
        if ((item & ID_INCLASS) <= 7)
        {
            MapObj_X = x;
            MapObj_Y = y;
            OpenDoor(who, obj);
        }
        remove_lock(obj, 1);
        debris = -1;
    }
    else if (item == ITEM_CHEST || item == ITEM_BARREL)
    {
        MapObj_X = x;
        MapObj_Y = y;
        remove_lock(obj, 0);
        UseCont(0L, obj, 0);
    }
    else if (ITEM_CLASS(item) == CLASS_CONTAINER)
    {
        /* UW1: this file saw Obj_Elem_Fate returning a char (cbw); object.h says unsigned */
        if (!(char)Obj_Elem_Fate(10, obj))
            debris = -1;
        else
            DumpTheBag(obj, 0);
    }
    else
    {
        if (type & DMG_FIRE)
        {
            /* UW1: 0xD5 and 0xD6 are both piles of debris */
            if (OBJ_ITEM(obj) == ITEM_PILE_OF_DEBRIS_D5 || OBJ_ITEM(obj) == ITEM_PILE_OF_DEBRIS_D6)
            {
                if (try_remove(&Map_GetAddr(x, y)->objects, obj))
                    return 1;
                debris = -1;
            }
            else if (!(rand() & 3))
            {
                put_effect(obj, 8, rollem(6, 10), 0, 0, x, y);
                debris = (int)(rand() * 2L / 0x8000L) + ITEM_PILE_OF_DEBRIS_D5;
            }
        }
        if (!OBJ_ISQUANT(obj) && OBJ_LINK(obj) > 0)
        {
            head = &obj->ol.link;
            Obj_FreeChain(head);
        }
    }
    if (debris < -1)
        debris = (int)(rand() * 2L / 0x8000L) + ITEM_PILE_OF_DEBRIS_D5;
    if (debris >= 0)
    {
        SET_ITEM(obj, debris);
#ifndef __TURBOC__
        /* port only: --enhance plain-debris (docs/ENHANCEMENTS.md). SET_ITEM above changes only
           the item number, so a broken object keeps the spell it carried, and decode_obj_spell
           (obj/USEITEMS.C) still finds it on the debris: an enchanted scroll thrown at a wall
           until it breaks leaves debris that casts its spell each time it is used, for
           nothing, where reading the scroll would have used it up. A spell is carried in one
           of two ways, and this takes away both, so the debris carries none:
           - in the object itself (an is_quant object with ID_ENCHANT; its link less
             LINK_SPECIAL names the spell): remove_spell clears ID_ENCHANT, as the game does
             itself when a worn item's enchantment ends (game/PLAYDATA.C). The link is left
             as it is, which a pile of debris never reads as a quantity (LINK_SPECIAL is set);
           - as a spell object (MAJOR_SPEC minor 2) in its contents. Most objects have had
             their contents freed above (Obj_FreeChain); a chest, barrel or bag has had
             them spilled instead, so one may be left.
             Obj_InList finds it as decode_obj_spell does; Obj_Rem unlinks it and Obj_Free
             frees it, as UW2's charge_object (combat/SPELLS2.C) does with a spell it
             destroys. Not Obj_Punt, which would also free a chain from the spell's link
             word, where a spell object keeps its spell, not contents.
           Done before obj_deal places the debris, so nothing sees the enchanted debris */
        if (ENHANCED(ENH_PLAIN_DEBRIS))
        {
            struct Object far *spell;

            if (OBJ_ISQUANT(obj))
                remove_spell(obj);
            else if (OBJ_LINK(obj) > 0)
            {
                head = &obj->ol.link;
                if ((spell = Obj_InList(&head, 0, MAJOR_SPEC, 2, 0)) != 0)
                {
                    Obj_Rem(head, spell);
                    Obj_Free(spell);
                }
            }
        }
#endif
        if (obj >= (struct Object far *)objdata && !obj_deal(obj, x, y, 1))
            return 1;
    }
    return debris < -1;
}

/* Applies the item's resist byte (ComObjData[].resist) to damage of the given type bits
   and returns the damage left. The bits, named by Study Monster's list (UW2's SPELLS2.C dtypes
   and strings 0x146..0x14B): 3 magic, 4 physical attacks, 8 fire, 0x10 poison, 0x20 cold,
   0x40 missiles; 0x80 marks the undead (SPELLS.C tests it). Magic resistance is a chance:
   the item ignores the blow when rand() % 3 < (resist & 3). Any other type bit the item
   resists cancels the damage. */
unsigned char far check_res(struct Object far *obj, unsigned char damage, unsigned char type)
{
    unsigned char res = ComObjData[OBJ_ITEM(obj)].resist;

    if (type & res)
    {
        if (type & DMG_MAGIC)
        {
            if (rand() % 3 < (res & 3))
                return 0;
            type = type & 0xFC;
        }
        if (res & type)
            return 0;
    }
    return damage;
}

/* The entry point for damaging any object: resistances first, then critters go to
   damage_critter (AI.C) and other objects to damage_object, which removes them through
   remove_object once they are destroyed. Returns nonzero when the object is gone. */
char far damage_item(struct Object far *obj, struct Object far *who, int x, int y,
                     unsigned char damage, unsigned char type)
{
    damage = check_res(obj, damage, type);
    if (OBJ_MAJOR(obj) == MAJOR_CREATURE)
        return damage_critter(obj, damage, who);
    if (damage_object(obj, who, damage, x, y))
        return remove_object(obj, who, type, x, y);
    return 0;
}

/* Takes damage off an object that is not a critter and returns 1 when it is destroyed.
   ComObjData's qualclass is the object's toughness: damage is shifted right by it, and
   class 3 objects are indestructible, as are objects with id bit 13 set.
   Mobile objects lose hit points. Doors (0x140..0x147) whose owner field has bit 0 set keep
   their strength in the rest of the owner field and are worn down but never destroyed
   here. Everything else loses quality. A destroyed static object on the map sets off
   any trap attached to it (checkTrap with event 4, the use triggers' mode). */
char far damage_object(struct Object far *obj, struct Object far *who, int damage, int x, int y)
{
    char destroyed = 0;
    char mobile;
    struct ComObj *co = &ComObjData[OBJ_ITEM(obj)];
    int scale;
    int hp;

    if (OBJ_DOORDIR(obj) || (scale = co->qualclass) == 3)
        return 0;
    damage >>= scale;
    if (damage <= 0)
        return 0;
    mobile = IsMobElem(obj);
    if (mobile)
    {
        hp = obj->hp - damage;
        if (hp <= 0)
        {
            hp = 0;
            destroyed = 1;
        }
        obj->hp = hp;
    }
    else if (OBJ_ITEM(obj) >= FIRST_RECT && OBJ_ITEM(obj) <= ITEM_SECRET_DOOR_147 && (OBJ_OWNER(obj) & 1)
             && OBJ_OWNER(obj) >> 1 > 0)
    {
        hp = (OBJ_OWNER(obj) >> 1) - damage;
        if (hp <= 0)
            hp = 0;
        obj->ol.f.owner = OBJ_OWNER(obj) & 1 | hp << 1;
    }
    else
    {
        hp = OBJ_QUALITY(obj) - damage;
        if (hp <= 0)
        {
            destroyed = 1;
            hp = 0;
        }
        obj->qn.f.quality = hp;
    }
    if (destroyed && !mobile && x > -1)
        checkTrap(who, obj, 4, x, y);
    return destroyed;
}
