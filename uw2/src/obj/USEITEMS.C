/* target: ovr138 */
/* opts: -mm -1 -G -O -Y -d */
/* Using objects: bones, the watch, crystals, gems, poles, anvils, food, oil, books, lockpicks,
   keys, containers, lights, doors, runes and the rest: the whole of DOS overlay ovr138, in
   original order. Function and global names are the originals from the FM Towns symbol
   table; the source file's own name is not known.

   OBJUSE.C's UseObj dispatches here by class: UseCont, UseLight, UseFood, UseUtil,
   UseUnique, UseMagic, UseBook, UseRect (doors, furniture, switches) and UseRune. The
   *On functions (UseBonesOn, UseKeyGem, UsePoleOn, UseAnvilOn, UseOilOn, UseLockpickOn,
   UseKeyOn, UseRockHammerOn) are the second half of a two-object use: OBJUSE.C's UseThing
   stores one as ObjectActor, and INTERACT.C calls it with the object clicked next, the
   first object being ObjectActing. Their how is nonzero for a real use, and other is set
   when the target is in the inventory rather than in the world (inferred from UseAnvilOn
   and UseRockHammerOn, which want the opposite). The door functions (OpenDoor, CloseDoor,
   changeDoor) turn a door into the moving door animation object and hand it to EFFECT.C's
   animation list. Game state touched here includes hunger, drunkenness, the key gem quest
   bytes and X clocks, and the map scraps.
   Name: descriptive (using particular objects). */

#include <string.h>
#include <stdlib.h>
#include "combat.h"
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

/* match: this file's _BSS, DS:8184 (ovr137's ends there): of the files before ovr140's TxmTerr,
   only this one uses it (seg044 does too). */
char door_type;

/* Bones or a skull used on a gravestone are buried and deleted; on anything else, "It
   seems to have no effect." */
void far UseBonesOn(struct Object far *obj, char how)
{
    unsigned char used;

    unforce_mouse_cursor(3);
    CursorObjPtr = 0;
    GameInputMode = 0;
    used = 0;
    if (OBJ_ITEM(obj) == ITEM_GRAVESTONE)
    {
        used = 1;
        game_sprint(0x94); /* "You thoughtfully give the bones a final resting place." */
    }
    if (!used)
        game_sprint(0x92); /* "It seems to have no effect." */
    else
        using_punt(ObjectActing, how, 1);
}

/* The pocketwatch prints the time from the game clock: 0x3C00 ticks a minute and 0xE1000
   an hour, on a 12-hour dial. */
void far UseWatch(void)
{
    char hours;
    char minutes;
    char text[50];
    register int i;

    hours = player->game_clock / 0xE1000L % 12;
    if (hours == 0)
        hours = 12;
    minutes = player->game_clock / 0x3C00L % 60;
    str_copy(text, get_string(0x227)); /* "The watch reads " */
    i = strlen(text);
    if (hours > 9)
        text[i++] = hours / 10 + '0';
    text[i++] = hours % 10 + '0';
    text[i++] = ':';
    text[i++] = minutes / 10 + '0';
    text[i++] = minutes % 10 + '0';
    text[i++] = 0;
    scroll_print(text);
    game_sprint(0x60);  /* '.' */
}

/* A storage crystal prints a four-character signature, letter digit letter digit, made
   from its quality. */
void far UseCrystal(int quality)
{
    char label[6];
    int i;
    int v;

    game_sprint(0x160); /* "You \"read\" the crystal's signature: " */
    v = (0x49 - quality) * 2;
    for (i = 0; i < 4; i++)
    {
        label[i] = (i & 1) ? v % 10 + '0' : v % 26 + 'A';
        v += quality + 0x1E;
    }
    label[4] = '\n';
    label[5] = 0;
    scroll_print(label);
}

/* A blackrock key gem (0x118-0x11F, gem 1 to 8 from its index) used on the large
   blackrock gem in the world, if the key gem's owner field is set (probably once it has
   been treated). It is used up, the X clock XC_GEMS counts it and picks the message
   (0x152 plus the new count), gem 4 also advances XC_CASTLE, and QB_GEMS_USED gets the
   gem's bit (gems 6 and 7 swap bits) while vars[6] records it. A different sound plays when
   all eight bits are set. Otherwise "The key gem remains inert in your hand." */
void far UseKeyGem(struct Object far *obj, char how, unsigned char other)
{
    int gem;

    gem = OBJ_INCLASS(ObjectActing) - 7;
    unforce_mouse_cursor(3);
    CursorObjPtr = 0;
    GameInputMode = 0;
    if (how != 0 && !other && OBJ_ITEM(obj) == ITEM_LARGE_BLACKROCK_GEM && OBJ_OWNER(ObjectActing))
    {
        if (gem == 4)
            player->xclock[XC_CASTLE]++;
        do_sfx(4, player->xclock[XC_GEMS] << 2);
        game_sprint(0x152); /* "The key gem fuses with the larger gem, and the face lights up." */
        player->xclock[XC_GEMS]++;
        game_sprint(player->xclock[XC_GEMS] + 0x152);
        if ((gem & 6) == 6)
            gem = 13 - gem;
        player->quest_bytes[QB_GEMS_USED] |= 1 << (gem - 1);
        player->vars[6] = gem - 1;
        using_punt(ObjectActing, how, 1);
        play_effect_here(0x12, 0x40, 0x28);
        if (player->quest_bytes[QB_GEMS_USED] != 0xFF)
            play_effect_here(0x2A, 0x40, 0x14);
        else
            play_effect_here(0x2C, 0x40, 0x14);
    }
    else
        game_sprint(0x15B); /* "The key gem remains inert in your hand." */
}

/* A pole reaches a switch out of arm's range and uses it. */
void far UsePoleOn(struct Object far *obj)
{
    UsingPole = 0;
    FixPlayerEquips();
    if (OBJ_CLASS(obj) == CLASS_SWITCH)
    {
        game_sprint(0xAB); /* "Using the pole you trigger the switch." */
        UseObj(ThePlayer, obj, 0);
    }
    else
        game_sprint(0xAC); /* "The pole cannot be used on that." */
}

/* An anvil used on an object in the inventory repairs it with the repair skill. */
void far UseAnvilOn(struct Object far *obj, unsigned char how, unsigned char other)
{
    if (!how || !other)
        return;
    unforce_mouse_cursor(3);
    CursorObjPtr = 0;
    GameInputMode = 0;
    repair_item(obj, player->skills[SKILL_REPAIR], 1);
}

/* Eating and drinking: food, potions, drinks and a few plants. Returns -2 when it cannot
   be used now, -1 for something that is not edible, 0 when the player is too full, 1 when
   eaten. A stack on the cursor, or eating outside input mode 1 (mid conversation),
   returns -2; the message for each (0x84 "You can only use those individually...", 0x16B
   "You may eat after you finish speaking.") is chosen but never printed.
   The nourishment of class 0x0B food is its byte in MISC.C's Food table: positive feeds the
   player through player_eat; negative is alcohol, adding its size to the drunk level (up
   to 0x3F) and then rolling skill_check(attribute 0, drunk): -1 passes out into sleep and
   wakes unsteady, 0 an effect for drunk / 6, 2 restores some health. The items handled by
   name each print their own line (taste is a string number counted up through the case
   fall-throughs: leeches 0xF4 to the mushroom 0xF7, water 0xFC to potions 0xFF); other food
   prints "That <name>" with a taste from 0xBB "tasted putrid" to 0xBF "tasted great", from
   (quality + random 0 to 19) / 16. The mushroom may restore a little mana and counts up
   shrooms (to 3); one plant (0x114) puts the player to sleep for 2 to 5 unless in the
   void. A spell in the food is cast with no delay, then traps fire. Meat on a stick, a
   piece of meat, a honeycomb and the three bottles leave a stick, a bone, a lump of wax or
   an empty bottle, in the same slot if it was a single item or else on the cursor. */
int far UseFood(struct Object far *who, struct Object far *food, unsigned char how)
{
    int qty;
    int slot;
    int leftover;
    int msg;
    unsigned char is_potion;
    int sfx;
    char text[76];
    int nutrition;
    int taste;

    taste = 0;
    nutrition = 0xFF;
    is_potion = 0;
    if (OBJ_ISQUANT(food) && !(OBJ_LINK(food) & LINK_SPECIAL))
        qty = OBJ_LINK(food);
    else
        qty = 1;
    if (food == CursorObjPtr)
    {
        msg = -1;
        if (qty > 1)
            msg = 0x84;
        else if (inplist->mode != 1)
            msg = 0x16B;
        if (msg != -1)
            return -2;
    }
    else if (!how)
        return -2;
    if (OBJ_CLASS(food) == CLASS_FOOD)
        nutrition = Food[food->id & ID_INCLASS];
    switch (OBJ_ITEM(food))
    {
    case ITEM_MUSHROOM:
        if (skill_check(playerdat->attr[2], 20))
            restore_mana(ThePlayer, -(rand() * 3L / 0x8000L));
        if (player->shrooms < 3)
            player->shrooms = player->shrooms + 1;
        FixPlayerEquips();
        taste++;
    case ITEM_EYEBALL:
        taste++;
    case ITEM_CANDLE:
        taste++;
    case ITEM_LEECHES:
        taste += 0xF4;
        break;
    case ITEM_PLANT_CE:
        taste = 0xFB;
        nutrition = 4;
        break;
    case ITEM_PLANT_CF:
    case ITEM_PLANT_114:
        taste = 0xF8;
        nutrition = 0x17;
        break;
    case ITEM_BLACK_POTION:
    case ITEM_PURPLE_POTION:
    case ITEM_YELLOW_POTION:
    case ITEM_GREEN_POTION:
    case ITEM_RED_POTION:
    case ITEM_COLORLESS_POTION:
    case ITEM_BROWN_POTION:
        is_potion = 1;
        taste++;
    case ITEM_BOTTLE_OF_ALE:
        taste++;
    case ITEM_BOTTLE_OF_WINE:
        taste++;
    case ITEM_BOTTLE_OF_WATER:
        taste += 0xFC;
        break;
    default:
        if (nutrition == 0xFF)
            return -1;
    }
    if (nutrition > 0)
    {
        if (nutrition != 0xFF && !player_eat(nutrition))
        {
            game_sprint(0x8C); /* "You are too full to eat that now." */
            return 0;
        }
        if (!is_potion)
        {
            if (player->hunger >= 0xC0)
                sfx = 0x25;
            else if (player->hunger > 0x5A)
                sfx = 0x1F;
            else
                sfx = 0x21;
        }
        else
            sfx = 0x1E;
        play_effect_here(sfx, 0x40, 0);
        if (taste == 0)
        {
            strcpy(text, "That ");
            get_name(text + strlen(text), food, 0, 0);
            taste = ((int)(rand() * 20L / 0x8000L) + OBJ_QUALITY(food)) >> 4;
            if (taste > 4)
                taste = 4;
            scroll_print(text);
            taste += 0xBB;
        }
        game_sprint(taste);
        if (OBJ_ITEM(food) == ITEM_PLANT_114 && !player->in_void)
            player->sleepbits = rand() % 4 + 2;
    }
    else if (nutrition < 0)
    {
        play_effect_here(0x1E, 0x40, 0);
        game_sprint(taste);
        if (nutrition < -1 && nutrition > -0x7F)
        {
            if (player->drunk - nutrition > 0x3F)
                player->drunk = 0x3F;
            else
                player->drunk = player->drunk - nutrition;
            switch (skill_check(playerdat->attr[0], player->drunk))
            {
            case -1:
                game_sprint(0x100); /* "As the alcohol hits you, you stumble and collapse into sleep." */
                player_sleep(-2);
                if (ThePlayer->hp != 0)
                {
                    game_sprint(0x102); /* "You wake feeling somewhat unstable but better." */
                    set_effect(0x40, player->drunk / 6 + 10);
                }
                break;
            case 0:
                set_effect(0x40, player->drunk / 6);
                break;
            case 2:
                game_sprint(0x101); /* "The drink makes you feel a little better for now." */
                restore_hp(ThePlayer, -2);
                break;
            }
        }
    }
    else
        game_sprint(taste);
    switch (OBJ_ITEM(food))
    {
    case ITEM_MEAT_ON_A_STICK:
        leftover = ITEM_STICK;
        break;
    case ITEM_PIECE_OF_MEAT_B0:
        leftover = ITEM_BONE_C5;
        break;
    case ITEM_HONEYCOMB:
        leftover = ITEM_LUMP_OF_WAX;
        break;
    case ITEM_BOTTLE_OF_ALE:
    case ITEM_BOTTLE_OF_WATER:
    case ITEM_BOTTLE_OF_WINE:
        leftover = ITEM_BOTTLE_13D;
        break;
    default:
        leftover = 0;
    }
    nextSpellTime = 0;
    checkSpell(MapObj_X, MapObj_Y, who, food, 1);
    nextSpellTime = 0;
    checkTrap(who, food, 4, MapObj_X, MapObj_Y);
    if (leftover > 0 && GameInputMode != 1 && qty == 1)
    {
        slot = FindSlot(food);
        PlayerDat.rec.weight -= ComObjData[OBJ_ITEM(food)].mass;
        SET_ITEM(food, leftover);
        PlayerDat.rec.weight += ComObjData[OBJ_ITEM(food)].mass;
        displayEnc(0);
        leftover = 0;
        RedisplayInvSlot(slot);
    }
    else if ((char)using_punt(food, how, 1) && GameInputMode == 1)
        CursorObjPtr = 0;
    if (leftover > 0)
    {
        CursorObjPtr = CreateObj(leftover, 0);
        if (CursorObjPtr != 0)
        {
            unforce_mouse_cursor(3);
            force_mouse_cursor(leftover);
            GameInputMode = 1;
        }
    }
    return 1;
}

/* An oil flask used on an object in the inventory: a piece of wood becomes a torch of
   quality 40; an unlit lantern or torch gains 0x20 quality (to at most 0x3F). The flask is
   used up. Strings 0xC1-0xC3 are the lantern's and 0xC5-0xC7 the torch's (off is 4 for
   anything not a lantern): lit, refuelled, already full. */
void far UseOilOn(struct Object far *obj, unsigned char how, unsigned char other)
{
    int off;
    int id;

    id = OBJ_ITEM(obj);
    off = (id == ITEM_LANTERN || id == ITEM_LIT_LANTERN) ? 0 : 4;
    unforce_mouse_cursor(3);
    CursorObjPtr = 0;
    GameInputMode = 0;
    if (!how || !other)
        return;
    if (id >= ITEM_PIECE_OF_WOOD_CC && id <= ITEM_PIECE_OF_WOOD_CD)
    {
        if (OBJ_ISQUANT(obj) && !(OBJ_LINK(obj) & LINK_SPECIAL) && OBJ_LINK(obj) > 1)
        {
            InvRemoveOneObject(obj);
            CursorObjPtr = obj;
            force_mouse_cursor(id);
            GameInputMode = 1;
        }
        game_sprint(0xC4); /* "Dousing a cloth with oil and applying it to the wood, you make a torch." */
        using_punt(ObjectActing, how, 1);
        if (CursorObjPtr == obj)
        {
            PlayerDat.rec.weight -= ComObjData[OBJ_ITEM(obj)].mass;
            SET_ITEM(obj, ITEM_TORCH);
            PlayerDat.rec.weight += ComObjData[OBJ_ITEM(obj)].mass;
        }
        else
            SET_ITEM(obj, ITEM_TORCH);
        obj->qn.f.quality = 0x28;
        displayEnc(0);
        RedisplayInvSlot(FindSlot(obj));
    }
    else if (id == ITEM_LANTERN || id == ITEM_TORCH)
    {
        if (OBJ_QUALITY(obj) == 0x3F)
            game_sprint(off + 0xC3);
        else
        {
            if (OBJ_ISQUANT(obj) && !(OBJ_LINK(obj) & LINK_SPECIAL) && OBJ_LINK(obj) > 1)
            {
                InvRemoveOneObject(obj);
                CursorObjPtr = obj;
                force_mouse_cursor(id);
                GameInputMode = 1;
            }
            if (OBJ_QUALITY(obj) < 0x20)
                obj->qn.f.quality = OBJ_QUALITY(obj) + 0x20;
            else
                obj->qn.f.quality = 0x3F;
            game_sprint(off + 0xC2);
            using_punt(ObjectActing, how, 1);
        }
    }
    else if (id == ITEM_LIT_LANTERN || id == ITEM_LIT_TORCH)
        game_sprint(off + 0xC1);
    else
        game_sprint(0xC0); /* "You cannot use oil on that." */
}

/* Reading from the inventory. The map opens the automap (in mode 1); a bit of a map is
   copied to the player's map (update_map_scraps), its link set to LINK_SPECIAL once
   copied, or, already copied, opens the automap at that scrap (player->map_scrap). A book
   or scroll with id bit 10 plays cutscene 0x100 plus its link; otherwise its link below
   0x100 is a string in block 3 (STR_BOOKS), printed after "You read the <name>...".
   Reading book text 6 sets bit 2 of quests[26] (quests 104 to 107). A link of 0x100 or more
   calls the empty make_stew. An enchanted scroll casts its spell and is used up. */
void far UseBook(struct Object far *obj, unsigned char how)
{
    char far *str;
    int16 where;
    int owner;
    char link;
    char text[100];
    int scrap;

    if (!how)
        return;
    if (OBJ_INCLASS(obj) > 0xA)
        return;
    if (OBJ_ITEM(obj) == ITEM_MAP)
    {
        game_sprint(0x8E); /* "You unroll your map." */
        if (inplist->mode == 1)
            newscr(2);
    }
    else if (OBJ_ITEM(obj) == ITEM_BIT_OF_A_MAP)
    {
        if (FindObj(MAJOR_SPEC, 3, 0xA, 4, &where))
        {
            scrap = OBJ_QUALITY(obj) + 0x47;
            owner = OBJ_OWNER(obj);
            link = OBJ_LINK(obj) & 0xFF;
            if (link == 0)
            {
                if (inplist->mode == 1)
                {
                    player->map_scrap = ~owner;
                    newscr(2);
                }
            }
            else
            {
                obj->ol.f.link = LINK_SPECIAL;
                game_sprint(9); /* "You copy the map scrap to your map." */
                update_map_scraps(scrap, owner, link);
            }
        }
        else
            game_sprint(0xA); /* "You do not have your map." */
    }
    else if (!(obj->id & ID_ENCHANT) || OBJ_MAJOR(obj) == MAJOR_RECT)
    {
        if (obj->id & ID_FLAG10)
            show_cutscene((OBJ_LINK(obj) & 0x1FF) + 0x100);
        else if ((OBJ_LINK(obj) & 0x1FF) < 0x100)
        {
            strcpy(text, "You read the ");
            get_name(text + strlen(text), obj, 0, 0);
            strcat(text, "...\n");
            scroll_print(text);
            if ((OBJ_LINK(obj) & 0x1FF) == 6)
                player->quests[26] = (player->quests[26] & 0xFFFFFFFBL) + 4;
            str = get_string((OBJ_LINK(obj) & 0x1FF) | STR_BOOKS);
            scroll_print(str);
            scroll_print("\n");
        }
        else
            make_stew();
    }
    else
    {
        checkTrap(ThePlayer, obj, 4, MapObj_X, MapObj_Y);
        if (checkSpell(MapObj_X, MapObj_Y, ThePlayer, obj, how))
            using_punt(obj, how, 0);
    }
}

/* Picking a lock with checkLock (OBJUSE.C), passing minus (Picklock skill + 1). A fumble
   breaks the pick unless a dexterity check against 20 succeeds; with no Picklock skill an
   unlocked lock reports the attempt failed rather than "not locked". */
void far UseLockpickOn(struct Object far *obj, unsigned char how)
{
    int result;
    int skill;

    if (!how)
        return;
    unforce_mouse_cursor(3);
    CursorObjPtr = 0;
    GameInputMode = 0;
    skill = -(player->skills[SKILL_PICKLOCK] + 1);
    result = checkLock(ThePlayer, obj, skill);
    if (result == 5 && skill_check(player->dexterity, 20) > 0 || result == 4 && skill == -1)
        result = 0;
    switch (result)
    {
    case 1:
        game_sprint(3); /* "There is no lock on that." */
        break;
    case 5:
        using_punt(ObjectActing, 1, 1);
        game_sprint(0x86); /* "You broke your pick." */
        break;
    case 0:
        game_sprint(0x85); /* "Your lockpicking attempt failed." */
        break;
    case 4:
        game_sprint(0x88); /* "That is not locked." */
        break;
    default:
        play_effect_here(0x13, 0x40, 0);
        game_sprint(0x87); /* "You succeed in picking the lock." */
        break;
    }
}

/* A key used on a lock: the key's owner field is the lock number it fits. The message is
   checkLock's result plus 2: "The key does not fit.", "There is no lock on that.", "The
   key locks the lock.", "The key unlocks the lock.", "That is already open." */
#ifndef __TURBOC__
/* port only: --enhance key-ring (docs/ENHANCEMENTS.md). A key that does not fit is answered
   "The key does not fit."; with key-ring the player's other keys are tried, as a key ring
   would be. A key opens the lock whose number is its owner field, and a lock holds its
   number in its link (checkLock), so the key that fits is the one carried whose owner field
   is the lock's number: found without trying each in checkLock, which would change the lock
   or set off its traps. Then checkLock is called with it, as UseKeyOn calls it, so locking,
   unlocking and their traps are the game's own. */

/* a real key: 0x102 to 0x10E, not the curious implement (0x100), which UseKey also sends
   here, the lockpick or the lock */
static int key_ring_key(struct Object far *obj)
{
    int item = OBJ_ITEM(obj);

    return item >= ITEM_KEY_102 && item <= ITEM_KEY_10E;
}

/* the first real key for lock number lock in list and the containers in it, depth first, as
   INVDATA.C's find_obj walks them */
static struct Object far *key_ring_find(struct Object far *list, int lock)
{
    struct Object far *next;
    struct Object far *found;

    while (list != 0) {
        if (key_ring_key(list) && OBJ_OWNER(list) == lock)
            return list;
        if (!OBJ_ISQUANT(list) && (next = Obj_PtrTMem(&list->ol.link)) != 0
            && (found = key_ring_find(next, lock)) != 0)
            return found;
        list = Obj_PtrTMem(&list->qn.link);
    }
    return 0;
}

/* After the key used did not fit door (checkLock's 0): the lock's number, from its lock as
   checkLock finds it, and a key for it anywhere in the inventory: the paperdoll, the hands,
   the rings and the backpack (slots 0 to 18, as FindObj searches them) and every container
   in them. Returns 0, changing nothing, when the door has no lock with a number a key can
   hold (0, or above 63: a key's owner field is 6 bits), so no key opens it and the original
   message stands; else prints what happened, the game's own message but for the two that
   say "The key": "Another key ..." when another key locks or unlocks it, "No key fits." when
   none does. */
static int key_ring(struct Object far *door)
{
    struct Object far *lock;
    struct Object far *key;
    struct Object far *obj;
    union Link far *link;
    int number;
    int result;
    int slot;

    if (OBJ_ISQUANT(door) || door->ol.f.link <= 0)
        return 0;
    link = &door->ol.link;
    if ((lock = Obj_InList(&link, 0, MAJOR_SPEC, 0, 0xF)) == 0 || (number = lock->ol.f.link & 0x1FF) == 0
        || number > 0x3F)                /* no key's owner field (6 bits) holds it */
        return 0;
    key = 0;
    for (slot = 0; slot <= 18 && key == 0; slot++) {
        if ((obj = Obj_PtrTMem(&Inventory[slot])) == 0)
            continue;
        if (key_ring_key(obj) && OBJ_OWNER(obj) == number)
            key = obj;
        else if (!OBJ_ISQUANT(obj) && Obj_PtrTMem(&obj->ol.link) != 0)
            key = key_ring_find(Obj_PtrTMem(&obj->ol.link), number);
    }
    if (key == 0) {
        scroll_print("No key fits.\n");
        return 1;
    }
    result = checkLock(ThePlayer, door, OBJ_OWNER(key));
    if (result == 2)
        scroll_print("Another key locks the lock.\n");
    else if (result == 3)
        scroll_print("Another key unlocks the lock.\n");
    else
        game_sprint(result + 2);
    return 1;
}
#endif

void far UseKeyOn(struct Object far *obj, unsigned char how)
{
    int result;

    if (!how)
        return;
    unforce_mouse_cursor(3);
    CursorObjPtr = 0;
    GameInputMode = 0;
    result = checkLock(ThePlayer, obj, OBJ_OWNER(ObjectActing));
#ifndef __TURBOC__
    /* port only: --enhance key-ring, the other keys carried tried when this one does not
       fit (key_ring above) */
    if (result == 0 && ENHANCED(ENH_KEY_RING) && key_ring_key(ObjectActing) && key_ring(obj))
        return;
#endif
    game_sprint(result + 2);
}

/* A container: locked ones say so; in the inventory it opens in the panel (OpenTheBag),
   in the world its contents are dumped onto the floor (DumpTheBag). */
void far UseCont(struct Object far *who, struct Object far *obj, char how)
{
    char name[20];

    if (checkLock(who, obj, 0) == 0)
    {
        get_name(name, obj, 0, 0);
        scroll_print("The ");
        scroll_print(name);
        game_sprint(0x170);  /* ' is locked.' */
    }
    else if (how)
        OpenTheBag(FindSlot(obj));
    else
        DumpTheBag(obj, who == ThePlayer);
}

/* Lights a light or puts it out (unlit items 0x90-0x93, lit 0x94-0x97). A light must be a
   single item in the inventory, and a used-up one (quality 1 or less) will not light.
   When it is not in one of the four light slots (ValidLightSlots, 5 to 8: the shoulders and
   hands), a torch, candle, lantern or light sphere is moved to the first free one, or
   "Your hands are full." */
void far UseLight(struct Object far *obj, unsigned char how)
{
    int slot;
    int qty;
    int id;
    int newslot;
    struct Object far *inslot;
    int i;
    int j;

    if (OBJ_ISQUANT(obj) && !(OBJ_LINK(obj) & LINK_SPECIAL))
        qty = OBJ_LINK(obj);
    else
        qty = 1;
    if (!how)
    {
        game_sprint(0x89); /* "Lights may only be used if equipped." */
        return;
    }
    if (OBJ_QUALITY(obj) <= 1)
    {
        game_sprint(0x8A); /* "That light is already used up." */
        return;
    }
    slot = FindSlot(obj);
    for (j = 0; j < 4; j++)
    {
        if (ValidLightSlots[j] == slot && qty == 1)
            break;
    }
    if (j == 4)
    {
        newslot = 0;
        id = OBJ_ITEM(obj);
        if (id == ITEM_TORCH || id == ITEM_CANDLE || id == ITEM_LANTERN || id == ITEM_LIGHT_SPHERE)
        {
            for (i = 5; i <= 8; i++)
            {
                inslot = AskInventory(i);
                if (inslot == 0 && newslot == 0)
                    newslot = i;
                else if (inslot == obj && qty == 1)
                {
                    newslot = i;
                    break;
                }
            }
            if (newslot == 0)
            {
                game_sprint(0x105); /* "Your hands are full." */
                return;
            }
            InvRemoveOneObject(obj);
            AddToInventory(obj, newslot);
            DisplayInventory();
            slot = newslot;
        }
    }
    if (OBJ_INCLASS(obj) >= 4)
        SET_INCLASS(obj, OBJ_INCLASS(obj) - 4);
    else
    {
        SET_INCLASS(obj, OBJ_INCLASS(obj) + 4);
        play_effect_here(0x20, 0x40, 0);
    }
    FixPlayerEquips();
    RedisplayInvSlot(slot);
}

/* Quest items: the stoppered bottle cannot be opened; the horn sounds properly (an
   instrument tune, and the second message) only once the X clock XC_DJINN reaches 6; plant
   0x114 is eaten; a blackrock key gem asks what to use it on. */
void far UseUnique(struct Object far *who, struct Object far *obj, unsigned char how)
{
    int n = 0;

    switch (OBJ_ITEM(obj))
    {
    case ITEM_BOTTLE_116:
        game_sprint(0x8D); /* "You are unable to remove the stopper." */
        break;
    case ITEM_HORN:
        if (player->xclock[XC_DJINN] >= 6)
            n++;
        if (n)
            play_instrument(2);
        game_sprint(n + 0x161);
        break;
    case ITEM_PLANT_114:
        UseFood(ThePlayer, obj, how);
        break;
    default:
        if (OBJ_ITEM(obj) >= FIRST_BLACKROCK_GEM && how)
            UseThing(obj, (void (far *)())UseKeyGem);
        break;
    }
}

/* Starts a door moving: it becomes the moving door (0x1CF) with its own index within the
   major class (0-7 closed, 8-15 open) kept in its owner field, and joins the animation
   list for 5 frames (a portcullis, index 6, for 4). EFFECT.C turns it back into a door
   when the animation ends. */
void far moveDoor(struct Object far *door)
{
    int len = 5;

    if ((OBJ_INCLASS(door) & 7) == 6)
        len = 4;
    door->ol.f.owner = OBJ_INMAJOR(door);
    SET_ITEM(door, ITEM_MOVING_DOOR);
    add_animobj(Obj_MemTPtr(door), len, 0, MapObj_X, MapObj_Y);
}

/* Reverses a door that is moving: flips bit 3 of its id flags, fires its traps with how 9
   (now closing) or 8 (now opening), and turns the frames left into frames done. */
void far changeDoor(struct Object far *door)
{
    int cur;
    int len = 5;

    if (OBJ_MAJOR(door) == MAJOR_RECT && (OBJ_INCLASS(door) & 7) == 6
        || OBJ_MAJOR(door) == MAJOR_ANIMOBJ && (OBJ_OWNER(door) & 7) == 6)
        len = 4;
    if (OBJ_FLAGS(door) & 8)
        SET_FLAGS(door, OBJ_FLAGS(door) & 7);
    else
        SET_FLAGS(door, (OBJ_FLAGS(door) & 7) + 8);
    checkTrap(0L, door, (OBJ_FLAGS(door) & 8) ? 9 : 8, XP, YP);
    cur = get_animlen(door);
    if (cur >= 0)
        set_animlen(door, len - cur);
}

/* Opens a closed door (index below 8), or reverses a moving door that is closing (owner 8
   and up). A closed door other than a portcullis is first raised 0x18 in z (inferred: the
   z an open door is drawn at), its owner bit 0 cleared, and its traps fire with how 8.
   Plays the door sound (0x14 for a portcullis, 0xB for a door) at the door. */
void far OpenDoor(struct Object far *who, struct Object far *door)
{
    unsigned char type;
    int state;

    type = OBJ_INCLASS(door) & 7;
    if (OBJ_ITEM(door) == ITEM_MOVING_DOOR)
    {
        state = (OBJ_OWNER(door) >> 0) & 0xF;
        if (state < 8)
            return;
        door->ol.f.owner = state - 8;
        changeDoor(door);
    }
    else if (OBJ_INCLASS(door) < 8)
    {
        door->ol.f.owner = OBJ_OWNER(door) & 0xFFFE;
        if (OBJ_INCLASS(door) != 6)
            SET_Z(door, OBJ_Z(door) + 0x18);
        checkTrap(who, door, 8, MapObj_X, MapObj_Y);
        moveDoor(door);
    }
    else
        return;
    if (type == 6)
        type = 0x14;
    else
        type = 0xB;
    play_effect(type, (MapObj_X << 3) + OBJ_FINEX(door),
                (MapObj_Y << 3) + OBJ_FINEY(door), 0);
}

/* Closes an open door, or reverses a moving door that is opening; traps fire with how 9.
   No sound when quick_time is set. door_type keeps the door's type for the sound. */
void far CloseDoor(struct Object far *who, struct Object far *door)
{
    unsigned char type;
    int state;

    door_type = OBJ_INCLASS(door) & 7;
    if (OBJ_ITEM(door) == ITEM_MOVING_DOOR)
    {
        state = (OBJ_OWNER(door) >> 0) & 0xF;
        if (state >= 8)
            return;
        door->ol.f.owner = state + 8;
        changeDoor(door);
    }
    else
    {
        if (OBJ_INCLASS(door) < 8)
            return;
        checkTrap(who, door, 9, MapObj_X, MapObj_Y);
        moveDoor(door);
    }
    if (!quick_time)
    {
        type = door_type;
        if (type == 6)
            type = 0x14;
        else
            type = 0xB;
        play_effect(type, (MapObj_X << 3) + OBJ_FINEX(door),
                    (MapObj_Y << 3) + OBJ_FINEY(door), 0);
    }
}

void far ToggleDoor(struct Object far *who, struct Object far *door)
{
    if (OBJ_INCLASS(door) < 8)
        OpenDoor(who, door);
    else
        CloseDoor(who, door);
}

/* Spills a container's contents onto the floor (TREASURE.C's drop_link_chain), counting it
   as theft from its owner if the item can be owned (player_did_bad), and tells the player
   "You empty the <name>." or "The <name> is empty." */
void far DumpTheBag(struct Object far *bag, char to_player)
{
    char dumped;
    char text[80];
    int owner = 0;

    if (ComObjData[OBJ_ITEM(bag)].can_own)
        owner = OBJ_OWNER(bag);
    dumped = drop_link_chain(bag, owner);
    player_did_bad(owner);
    if (to_player)
    {
        if (dumped)
            strcpy(text, "You empty the ");
        else
            strcpy(text, "The ");
        get_name(text + strlen(text), bag, 0, 0);
        if (dumped)
            strcat(text, ".\n");
        else
            strcat(text, " is empty.\n");
        scroll_print(text);
    }
    editchng(2);
}

/* The flam and tym rune traps, set off by whoever touches them. A flam rune does 3d4 + 4
   damage of type 8 (probably fire) and becomes an explosion animation with a fireball
   effect. A tym rune paralyses the player for 4 to 19 (and stops him) or gives a critter
   goal 0xF (probably paralysed) for a random time, then disappears. */
void far UseRune(struct Object far *who, struct Object far *rune)
{
    if (OBJ_ITEM(rune) == ITEM_FLAM_RUNE)
    {
        damage_item(who, rune, MapObj_X, MapObj_Y, rollem(3, 4) + 4, 8);
        SET_ITEM(rune, ITEM_EXPLOSION_1C2);
        if (add_animobj(Obj_MemTPtr(rune), 4, 0, MapObj_X, MapObj_Y) == -1)
            return;
        fireball_effect(rune, MapObj_X, MapObj_Y);
    }
    else
    {
        if (who == ThePlayer)
        {
            player->paralyzed = (rand() & 0xF) + 4;
            PN.vel[0] = PN.vel[1] = PN.speed = 0;
            game_sprint(0x163); /* "You feel your limbs stiffen.  You are unable to move." */
        }
        else
            hit_critter_goal(0xF, 1, ((rand() & 0x3F) << 2) + 0x40, who, MapObj_X, MapObj_Y);
        using_punt(rune, 0, 0);
    }
}

/* Major class 5 by minor class. 0, doors: a closed door opens unless locked (the player
   hears "The <name> is locked." and a rattle); a critter cannot open a door with owner
   bit 0 set. An open door closes. 2, decals: the lever (0x161) and switch (0x162) step
   their id flags through 0 to 7; the bed (0x167) puts the player to sleep; anything else
   is looked at. 1, furniture: a barrel or chest (0x15B, 0x15D) not marked is_quant is
   dumped out like a container, as theft if owned. 3, switches: flip_switch. */
void far UseRect(struct Object far *who, struct Object far *obj)
{
    char name[20];
    register int state;

    switch (OBJ_MINOR(obj))
    {
    case 0:
        if (OBJ_INCLASS(obj) < 8)
        {
            if (checkLock(who, obj, 0) == 0)
            {
                if (OBJ_ITEM(who) == ITEM_ADVENTURER)
                {
                    get_name(name, obj, 0, 0);
                    scroll_print("The ");
                    scroll_print(name);
                    game_sprint(0x170);  /* ' is locked.' */
                    play_effect(0x2D, (MapObj_X << 3) + OBJ_FINEX(obj),
                                (MapObj_Y << 3) + OBJ_FINEY(obj), 0);
                }
            }
            else if (who == ThePlayer || !(OBJ_OWNER(obj) & 1))
                OpenDoor(who, obj);
        }
        else
            CloseDoor(who, obj);
        break;
    case 2:
        if (OBJ_INCLASS(obj) == 1 || OBJ_INCLASS(obj) == 2)
        {
            SET_FLAGS(obj, state = OBJ_FLAGS(obj) + 1 & 7);
            editchng(2);
        }
        else if (OBJ_INCLASS(obj) == 7)
            player_sleep(2);
        else
            RectLook(obj, -1);
        break;
    case 1:
        if ((OBJ_INCLASS(obj) == 0xB || OBJ_INCLASS(obj) == 0xD) && !OBJ_ISQUANT(obj))
        {
            UseCont(who, obj, 0);
            if (ComObjData[OBJ_ITEM(obj)].can_own)
                player_did_bad(OBJ_OWNER(obj));
        }
        break;
    case 3:
        flip_switch(obj, 3);
        break;
    }
}

/* Class 0x12 items. From the inventory: the bedroll sleeps (in mode 1), the mandolin and
   flute play, leeches cure poison at the cost of a backfire and are used up, the rock
   hammer and oil flask ask for a target, and the fishing pole fishes (go_fish), putting a
   fish of quality 0x3F on the cursor on success. In the world: the fountain casts the
   spell it holds (the strength message for spell 4), or is dry at quality 0. */
void far UseMagic(struct Object far *who, struct Object far *obj, char how)
{
    struct Object far *fish;
    int16 spell;
    int16 power;
    char flag;

    if (how)
    {
        switch (OBJ_ITEM(obj))
        {
        case ITEM_BEDROLL:
            if (inplist->mode == 1)
                player_sleep(1);
            break;
        case ITEM_MANDOLIN:
        case ITEM_FLUTE:
            play_instrument(OBJ_ITEM(obj) - ITEM_MANDOLIN);
            break;
        case ITEM_LEECHES:
            if (player->poison > 0)
                game_sprint(0xEF); /* "The leeches remove the poison as well as..." */
            player->poison = 0;
            backfire(ThePlayer, 2);
            using_punt(obj, how, 1);
            break;
        case ITEM_ROCK_HAMMER:
            UseThing(obj, (void (far *)())UseRockHammerOn);
            break;
        case ITEM_FISHING_POLE:
            if (go_fish())
            {
                fish = place_new(0L, ITEM_FISH);
                fish->qn.f.quality = 0x3F;
            }
            mouse_release(1);
            break;
        case ITEM_OIL_FLASK:
            UseThing(obj, (void (far *)())UseOilOn);
            break;
        }
    }
    else
    {
        switch (OBJ_ITEM(obj))
        {
        case ITEM_FOUNTAIN_12E:
            if (OBJ_QUALITY(obj) == 0)
                game_sprint(0x93); /* "The fountain is dry." */
            else if (decode_obj_spell(obj, &spell, &power, &flag))
            {
                inanimate_spell(MapObj_X, MapObj_Y, obj, who, spell, power);
                if (spell == 4)
                    game_sprint(0x108); /* "The waters of the fountain renew your strength." */
                else
                    game_sprint(0xFC);  /* 'The water refreshes you.' */
            }
            else
                game_sprint(0xFC);  /* 'The water refreshes you.' */
            break;
        }
    }
}

/* A rock hammer used on a boulder in the world (not one the player carries) breaks it into
   1 or 2 pieces plus one for each size it is above a boulder (0x155): each piece is one or
   two sizes smaller, and anything smaller than a small boulder becomes 3 to 8 sling
   stones. The pieces are placed where the boulder was. */
void far UseRockHammerOn(struct Object far *obj, unsigned char how, char other)
{
    int z;
    int xoff;
    int yoff;
    int id;
    struct Tile far *tile;
    struct Object far *rock;
    int n;
    int count;

    unforce_mouse_cursor(3);
    CursorObjPtr = 0;
    GameInputMode = 0;
    if (!how || other)
        return;
    if (Obj_Find(&ThePlayer->ol.link, 1, Obj_MemTPtr(obj)) != 0)
        return;
    id = OBJ_ITEM(obj);
    if (id >= ITEM_LARGE_BOULDER_153 && id <= ITEM_SMALL_BOULDER)
    {
        game_sprint(0x95); /* "The rock breaks into smaller pieces." */
        tile = Map_GetAddr(MapObj_X, MapObj_Y);
        xoff = OBJ_FINEX(obj);
        yoff = OBJ_FINEY(obj);
        z = obj->pos & POS_Z;
        Obj_Punt(&tile->objects, obj, 1);
        for (count = (int)(rand() * 2L / 0x8000L) + (ITEM_BOULDER - id) + 1; count > 0; count--)
        {
            rock = CreateObj(1, 0);
            if (rock == 0)
                break;
            n = id + (int)(rand() * 2L / 0x8000L) + 1;
            if (n > ITEM_SMALL_BOULDER)
                n = ITEM_SLING_STONE;
            SET_ITEM(rock, n);
            if (n == ITEM_SLING_STONE)
            {
                SET_ISQUANT(rock, 1);
                rock->ol.f.link = rand() % 6 + 3;
            }
            if (!put_at((MapObj_X << 3) + xoff, (MapObj_Y << 3) + yoff, z, rock, 6, 0))
                ;
        }
        editchng(2);
    }
    else
        game_sprint(0x92); /* "It seems to have no effect." */
}

/* Class 0x0C and 0x0D scenery: bones and skulls (0xC2-0xC6), the anvil and the pole ask
   for a target (the pole also sets UsingPole, which extends reach, inferred); two plants
   are eaten. */
void far UseUtil(struct Object far *obj, char how)
{
    if (OBJ_ITEM(obj) >= ITEM_SKULL_C2 && OBJ_ITEM(obj) <= ITEM_PILE_OF_BONES_C6)
    {
        if (how)
            UseThing(obj, (void (far *)())UseBonesOn);
    }
    else if (OBJ_ITEM(obj) == ITEM_ANVIL)
        UseThing(obj, (void (far *)())UseAnvilOn);
    else if (OBJ_ITEM(obj) == ITEM_POLE)
    {
        UsingPole = 1;
        FixPlayerEquips();
        UseThing(obj, (void (far *)())UsePoleOn);
    }
    else if (how && (OBJ_ITEM(obj) == ITEM_PLANT_CE || OBJ_ITEM(obj) == ITEM_PLANT_CF))
        UseFood(ThePlayer, obj, how);
}
