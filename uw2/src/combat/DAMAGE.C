/* target: seg025_26A1 */
/* opts: -mm -1 -G -O -d */
/* Damage to objects that are not critters: resistances, wear on doors, containers and
   weapons, and what an object leaves behind when it breaks. The whole of DOS resident
   segment seg025_26A1, in original order.

   What it does in the game: every source of damage (melee in COMBAT.C, spells in SPELLS.C
   and SPELLS2.C, falls and collisions in the motion code, traps and world events in
   WORLDEV.C, eating and the void in SKILLS.C) calls damage_item. It applies the item's
   resistances from ComObjData (check_res), hands critters to damage_critter (AI.C), and
   for anything else takes the damage off the object's hit points or quality
   (damage_object). An object worn to nothing is turned into debris or removed
   (remove_object): a door opens and loses its locks, a chest or barrel spills, a weapon
   becomes the broken weapon of its skill, a bag drops its contents.

   Data owned: none; the per-item tables are ComObjData (object.h) and Weapons (combat.h).
   Function and global names are the originals from the FM Towns symbol table.
   Name: original (damage_object is in System Shock's DAMAGE.C, the damage to objects in both). */

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
            if (Obj_Rem(head, lock))
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
   object turns into: -2 means work it out with debris_type, -1 means leave the object as
   it is. Doors of minor class 0..7 swing open and every door loses its locks; a chest or
   barrel spills its contents unless its id bit 13 is set; a weapon becomes its broken
   form unless the damage type is exactly 8 (pure fire); a bag drops its contents if its
   fate says so. Returns 1 when the object has left the map (removed by try_remove, or
   obj_deal failed to replace it), else 0; the final debris < -1 test is always false,
   because debris_type never returns a negative value.

   Breaking bottle 0x116 is the djinn quest: the bottle holds the air daemon. Before the
   djinn has been captured (xclock[XC_DJINN] < 5) breaking it kills the player; broken at
   squares (0x15..0x16, 0x34..0x35) of level 0x45 once it is captured, it sets
   xclock[XC_DJINN] to 6, advances the castle plot from 0xD to 0xE and sets bit 1 of
   quest variable 26 (the player has absorbed the djinn; inferred from the string). */
char far remove_object(struct Object far *obj, struct Object far *who, char type, int x, int y)
{
    int debris = -2;
    int item;
    union Link far *head;

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
        if (OBJ_DOORDIR(obj))
            debris = -1;
        else
        {
            MapObj_X = x;
            MapObj_Y = y;
            remove_lock(obj, 0);
            UseCont(0L, obj, 0);
        }
    }
    else if (ITEM_CLASS(item) == CLASS_WEAPON && type != 8)
    {
        if (item == ITEM_DAGGER || item == ITEM_JEWELLED_DAGGER)
            debris = ITEM_BROKEN_DAGGER;
        else
            debris = Weapons[item & 0xF].skill + 0xC5;
    }
    else if (ITEM_CLASS(item) == CLASS_CONTAINER)
    {
        if (!Obj_Elem_Fate(10, obj))
            debris = -1;
        else
            DumpTheBag(obj, 0);
    }
    else if (item == ITEM_BOTTLE_116)
    {
        if (player->xclock[XC_DJINN] < 5)
        {
            game_sprint(0x171);  /* 'The air daemon is released, and then rends you.' */
            damage_item(ThePlayer, 0L, OBJ_HOMEX(ThePlayer),
                        OBJ_HOMEY(ThePlayer), 0xFF, 0);
        }
        else if (PlayerLevel == 0x45 && x >= 0x15 && x <= 0x16 && y >= 0x34 && y <= 0x35)
        {
            fill_FB(2);
            player->xclock[XC_DJINN] = 6;
            game_sprint(0x150);  /* 'The air-daemon is absorbed into your body, ...' */
            if (player->xclock[XC_CASTLE] == 0xD)
                player->xclock[XC_CASTLE] = 0xE;
            player->quests[26] = (player->quests[26] & 0xFFFFFFFDL) + 2;
            if (try_remove(&Map_GetAddr(x, y)->objects, obj))
                return 1;
            debris = -1;
        }
        else
            fill_FB(2);
    }
    else
    {
        if (type & 8)
        {
            if (OBJ_ITEM(obj) == ITEM_PILE_OF_DEBRIS_D6)
            {
                if (try_remove(&Map_GetAddr(x, y)->objects, obj))
                    return 1;
                debris = -1;
            }
            else if (!(rand() & 3))
            {
                put_effect(obj, 8, rollem(6, 10), 0, 0, x, y);
                debris = ITEM_PILE_OF_DEBRIS_D6;
            }
        }
        if (!OBJ_ISQUANT(obj) && OBJ_LINK(obj) > 0)
        {
            head = &obj->ol.link;
            Obj_FreeChain(head);
        }
    }
    if (debris < -1)
        debris = debris_type(item, type);
    if (debris >= 0)
    {
        SET_ITEM(obj, debris);
#ifndef __TURBOC__
        /* port only: --enhance plain-debris (docs/ENHANCEMENTS.md). SET_ITEM above changes only
           the item number, so a broken object keeps the spell it carried, and decode_obj_spell
           (obj/OBJUSE.C) still finds it on the debris: an enchanted scroll thrown at a wall
           until it breaks leaves debris that casts its spell each time it is used, for
           nothing, where reading the scroll would have used it up. A spell is carried in one
           of two ways, and this takes away both, so the debris carries none:
           - in the object itself (an is_quant object with ID_ENCHANT; its link less
             LINK_SPECIAL names the spell): remove_spell clears ID_ENCHANT, as the game does
             itself when a worn item's enchantment ends (game/PLAYDATA.C). The link is left
             as it is, which a pile of debris never reads as a quantity (LINK_SPECIAL is set);
           - as a spell object (MAJOR_SPEC minor 2) in its contents. Most objects have had
             their contents freed above (Obj_FreeChain), but a weapon, which breaks into its
             broken form, keeps them, and a chest, barrel or bag has had them spilled
             instead, so one may be left.
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
        if (IsMobElem(obj))
            obj->hp = 0x28;
        obj->qn.f.quality = 0x28;
        if (obj >= (struct Object far *)objdata && !obj_deal(obj, x, y, 1))
            return 1;
    }
    return debris < -1;
}

/* The item a broken object becomes: a weapon its skill's broken weapon (Weapons[].skill
   + 0xC5, so SKILL_SWORD gives 0xC8 'a broken sword'; a dagger gives the broken dagger),
   furniture wood chips, anything else a pile of debris (0xD6). */
int far debris_type(int item, char type)
{
    if (ITEM_CLASS(item) == CLASS_WEAPON && type != 8)
    {
        if (item == ITEM_DAGGER)
            return ITEM_BROKEN_DAGGER;
        return Weapons[item & 0xF].skill + 0xC5;
    }
    if (ITEM_CLASS(item) == CLASS_FURNITURE)
        return ITEM_PILE_OF_WOOD_CHIPS;
    return ITEM_PILE_OF_DEBRIS_D6;
}

/* Applies the item's resist byte (ComObjData[].resist) to damage of the given type bits
   and returns the damage left. The bits, named by Study Monster's list (SPELLS2.C's dtypes
   and strings 0x146..0x14B): 3 magic, 4 physical attacks, 8 fire, 0x10 poison, 0x20 cold,
   0x40 missiles; 0x80 marks the undead (SPELLS.C tests it). Magic resistance is a chance:
   the item ignores the blow when rand() % 3 < (resist & 3). Any other type bit the item
   resists cancels the damage. Fire against a cold-resistant item, or cold against a
   fire-resistant one that does not also resist cold, does double damage (capped at 0xFF). */
unsigned char far check_res(struct Object far *obj, unsigned char damage, unsigned char type)
{
    unsigned char res = ComObjData[OBJ_ITEM(obj)].resist;

    if (type & res)
    {
        if (type & 3)
        {
            if (rand() % 3 < (res & 3))
                return 0;
            type = type & 0xFC;
        }
        if (res & type)
            return 0;
    }
    if ((type & 8 && res & 0x20) || (type & 0x20 && res & 8 && (res & 0x28) != 0x28))
    {
        if (damage < 0x7F)
            damage = damage << 1;
        else
            damage = 0xFF;
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
   class 3 objects are indestructible, as are objects other than doors with id bit 13 set.
   Mobile objects lose hit points. Doors (0x140..0x147) whose owner field has bit 0 set keep
   their strength in the rest of the owner field and are worn down but never destroyed
   here. Everything else loses quality; damaging an ownable object counts against the
   player with its owner (player_did_bad). A destroyed static object on the map sets off
   any trap attached to it (checkTrap with event 4, the use triggers' mode). */
char far damage_object(struct Object far *obj, struct Object far *who, int damage, int x, int y)
{
    char destroyed = 0;
    unsigned char mobile;
    struct ComObj *co = &ComObjData[OBJ_ITEM(obj)];
    int scale;
    int hp;

    if ((OBJ_DOORDIR(obj) && OBJ_CLASS(obj) != CLASS_DOOR) || (scale = co->qualclass) == 3)
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
        if (ComObjData[OBJ_ITEM(obj)].can_own)
            player_did_bad(OBJ_OWNER(obj));
    }
    if (destroyed && !mobile && x > -1)
        checkTrap(who, obj, 4, x, y);
    return destroyed;
}
