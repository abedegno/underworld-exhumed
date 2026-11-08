/* target: ovr142 */
/* opts: -mm -1 -G -O -Y -d */
/* The player record's load and save, and everything that is worked out again from the
   player's equipment and active spells.
   save_player_data and read_player_data (called from INVSAVE.C, inside a save or a
   restore) write and read struct Player as the PLAYER.DAT image: one key byte, name[0]
   xor 0xAA, then the 0x37D-byte record xor-encoded with that key (xorwrite, xorread).
   Before writing, the values that live elsewhere while playing (attributes, HP, the
   player's position, heading and level, sound and music, terrain) are copied into the
   record; after reading they are copied back out.
   FixPlayerEquips is called whenever equipment, spells or light change (14 callers): it
   recomputes armour by hit location, defence, the weapon's animation, the brightest
   carried light, the effects of active spells and of enchanted worn items
   (player_affected_by), stealth (plyNotice), the light level and the mushroom effect.
   load_dl reads the level's entry in DATA\DL.DAT, the minimum light of each level.
   Data owned: player, playerdat, ThePlayer, PlayerLevel, PlayerFacing, PlayerHeading,
   player_name_handle, plyNotice, the light globals, and the tables below.
   Name: descriptive (reading and writing player data and its spells, save_player_data). */

#include <io.h>
#include <mem.h>
#include <stdlib.h>
#ifndef __TURBOC__
#include <stdio.h>                      /* the armour probe (parse_spells) */
#endif
#include "combat.h"
#include "critter.h"
#include "file.h"
#include "gfx.h"
#include "inv.h"
#include "map.h"
#include "motion.h"
#include "object.h"
#include "player.h"
#include "sound.h"
#include "ui.h"
#include "view3d.h"

/* LIGHTING.C's set_light, declared where it is called because PLAYER.C and PHYSICS.C
   declare it taking an int. */
void far set_light(signed char n);

/* This file's _BSS, DS:8288..8297. player is the struct Player record (its storage is
   PLAYER.C's PlayerDat), playerdat the adventurer's creature type record (attributes,
   armour, defence, average HP), ThePlayer the player's object. */
/* match: laid out by name (tools/bssorder.py): player_name_handle 80, player 280,
   playerdat 440, ThePlayer 444, PlayerLevel 568, PlayerFacing 704, PlayerHeading 768. It
   follows ovr140's TxmID (988) and ends where ovr143's IsJoy (1) starts another run, and
   ovr143's run holds its own static region handles, so this run is not ovr143's. */
int16 player_name_handle;
struct Player *player;
struct Creature *playerdat;
struct Object far *ThePlayer;
int16 PlayerLevel;
int16 PlayerFacing;
int16 PlayerHeading;

/* This file's _DATA runs from DS:19AC to the end of "dl.dat" at DS:19DB. */
/* Body slot to defence index: inventory slots 0 to 4 (probably helm, chest, gloves,
   leggings, boots) to playerdat->armour[] hit locations 3, 0, 1, 2, 2. */
/* name: FM Towns keeps it as a static (_k_modes+0x10). */
static signed char defence_slot_index[6] = {3, 0, 1, 2, 2, 0};
/* [0] noise, [1] visibility; seg035 reads both as plyNotice[2]. */
unsigned char plyNotice[2] = {0x0F, 0x0F};
unsigned char UsingPole = 0;
unsigned char light_mod = 0xFF;
signed char light_act = 0xFF;
signed char loc_lght = 0xFF;
unsigned char light_hi = 0;
unsigned char last_light = 0x0C;
/* set_drugged's state: the mushroom effect in force (-1 none), and whether the first
   effect has still to be shown. */
/* name: statics in FM Towns (_last_light+1, +2); the names are descriptive. */
static char ShroomsEnabled = -1;
static char ShroomsRelated = 1;
/* Damage-protection bits for class 3 minors 5..9 (in the Guide's list missile
   protection, flameproof, poison resistance, magic protection, greater magic
   protection), or-ed into the player type's resist byte. */
/* match: FM Towns indexes it from _loc_lght as well, so the source subtracted 5 from the
   minor class. */
static unsigned char damage_protection_flags[5] = {0x40, 0x08, 0x10, 0x01, 0x02};

/* name: IDA left this empty function unnamed; FM Towns correspondence is unconfirmed. */
void far MaybePlayerDayLoadrelated_ovr142_0(void) {}

void far save_player_data(int fd)
{
    unsigned char key;
    key = player->name[0] ^ 0xAA;
    player->strength = playerdat->attr[0];
    player->dexterity = playerdat->attr[1];
    player->intelligence = playerdat->attr[2];
    player->health = ThePlayer->hp;
    player->maxhealth = playerdat->avghit;
    player->saved_x = PN.x;
    player->saved_y = PN.y;
    player->saved_z = PN.z;
    player->saved_facing = PlayerFacing;
    player->saved_level = PlayerLevel;
    player->sound = (unsigned)fx_is_on();
    player->music = (unsigned)music_is_on();
    player->terrain = PN.terrain;
    write(fd, &key, 1);
    xorwrite(fd, key, (unsigned char far *)player, 0x37D);
}

void far read_player_data(int fd)
{
    unsigned char key;
    read(fd, &key, 1);
    xorread(fd, key, (unsigned char far *)player, 0x37D);
    playerdat->attr[0] = player->strength;
    playerdat->attr[1] = player->dexterity;
    playerdat->attr[2] = player->intelligence;
    ThePlayer->hp = player->health;
    playerdat->avghit = player->maxhealth;
    PN.x = player->saved_x;
    PN.y = player->saved_y;
    PN.z = player->saved_z;
    PlayerFacing = player->saved_facing;
    PlayerLevel = player->saved_level;
    PN.terrain = player->terrain;
    lastDurCheck = player->game_clock >> 8;
    turn_fx(player->sound);
    turn_music(player->music);
    set_graphics_level();
    newFPS(player->fps);
}

/* Clears every spell effect before FixPlayerEquips applies them again: resistances,
   motion bits, the to-hit protection cmbModTH, poison weapon, haste, wizard eye, bless,
   time stop, valor, regeneration. Sets the base stealth from the stealth skill: noise
   13 - stealth / 3 and visibility 15 - stealth / 5 (lower is harder to notice), and the
   pick-up distance 0x90 (0x190 while UsingPole). */
void far init_spells(void)
{
    ComObjData[127].resist = 0;         /* item 127 is the player's own object type */
    plyNotice[0] = 13 - player->skills[SKILL_STEALTH] / 3;
    plyNotice[1] = 15 - player->skills[SKILL_STEALTH] / 5;
    motionbits = 0;
    memset(cmbModTH, 0, 4);
    PoisonWeap = Hasted = WizEye = Blessed = TimeStop = 0;
    Valor = 0;
    PickDist = 0x90;
    if (UsingPole) PickDist = 0x190;
    plyregen[0] = 0;
}

void far swap_tmap(void) {}

/* Turns the mushroom effect on or off. The first time ever it is set_cyb (an effect of
   the 3D view); after that one of three at random: set_cyb, a random palette of the
   first eight, or random_light. Turning it off undoes whichever was chosen. */
void far set_drugged(char on)
{
    if (on) {
        if (ShroomsEnabled < 0) {
            if (ShroomsRelated) {
                ShroomsEnabled = 0;
                ShroomsRelated = 0;
            } else
                ShroomsEnabled = rand() % 3;
            switch (ShroomsEnabled) {
            case 0: set_cyb(1); break;
            case 1: grfx_quikpal(rand() & 7); break;
            case 2: random_light(1); break;
            }
        }
    } else if (ShroomsEnabled >= 0) {
        switch (ShroomsEnabled) {
        case 0: set_cyb(0); break;
        case 1: grfx_quikpal(PAL_GAME); break;
        case 2: random_light(0); break;
        }
        ShroomsEnabled = -1;
    }
}

/* Applies one spell effect (from an active spell, or from an enchanted worn item when
   slot >= 0) to the player:
     class 0   light: the brightest level wins (player->light high nibble);
     class 1   motion: sets bit minor - 1 of motionbits (leap, slow fall, levitate, water
               walk, fly, bounce in the Guide's order);
     class 2   armour: the best level is kept in bits 4 to 7 of *bonuses, meant to be
               added to every hit location by parse_spells (which loses it);
     class 3   minor 1 adds 3 to cmbModTH at every hit location (COMBAT.C takes it off
               the attacker's skill); minors 2 to 4 set stealth bits in *bonuses
               (parse_spells), 4 setting the bit of 2 too; minors 5 to 9 add resistances;
               10 is valor (10 + casting / 5); 11 poisons the weapon;
     class 9   backfire;
     class 11  0 time stop, 1 wizard eye, 2 haste, 3 PickDist 0, 14 and 15 regenerate
               health and mana (plyregen bits read by PLAYTIME.C);
     class 12  an item's own protection (minor bits 0 to 2 plus one) or, with the 8 bit set,
               toughness, for the item's hit location; slots above 4 count for
               locations 0 and 1. The toughness is always added to armour[slots[0]],
               not armour[slots[i]], so the second location gets the first's.
   Always returns 0, so FixPlayerEquips never calls remove_spell. */
/* name: FM Towns player_affected_by_ is between set_drugged_ and parse_aspells_ and
   applies the same spell classes; IDA left its DOS name descriptive. */
unsigned char far player_affected_by(unsigned char major, unsigned char minor,
                                    register uint16 *bonuses, int slot)
{
    register int i;
    switch (major) {
    case SPELLC_ARMOUR:
        if ((*bonuses >> 4) < minor)
            *bonuses = (*bonuses & 0xF) + (minor << 4);
        break;
    case SPELLC_PROTECT:
        switch (minor - 1) {
        case 0:
            cmbModTH[0] += 3;
            cmbModTH[1] += 3;
            cmbModTH[2] += 3;
            cmbModTH[3] += 3;
            break;
        case 1: case 2: case 3:
            *bonuses |= 1 << (minor - 1);
            if (minor == 4) *bonuses |= 2;
            break;
        case 4: case 5: case 6: case 7: case 8:
            ComObjData[127].resist |= damage_protection_flags[minor - 5];
            break;
        case 9:
            Valor = 10 + player->skills[SKILL_CASTING] / 5;
            break;
        case 10:
            PoisonWeap = 1;
            break;
        }
        break;
    case SPELLC_MOTION:
        motionbits = motionbits | (1 << (minor - 1));
        break;
    case SPELLC_LIGHT:
        if (((player->light & 0xF0) >> 4) < minor)
            player->light = minor << 4;
        break;
    case SPELLC_XT:
        switch (minor) {
        case 0: TimeStop = 1; break;
        case 1: WizEye = 1; break;
        case 2: Hasted = 1; break;
        case 3: PickDist = 0; break;
        case 14: plyregen[0] |= 1; break;
        case 15: plyregen[0] |= 2; break;
        }
        break;
    case SPELLC_BACKFIRE:
        backfire(ThePlayer, minor);
        break;
    case 12:
        if (slot < 0) break;
        {
        int16 slots[2] = {-1, -1};
        i = 0;
        if (slot > 4) {
            slots[0] = 0;
            slots[1] = 1;
        } else
            slots[0] = defence_slot_index[slot];
        for (; slots[i] != -1 && i < 2; i++) {
            register int toughness = 0;
            if (minor & 8)
                toughness = (minor & 7) + 1;
            else
                cmbModTH[slots[i]] = (minor & 7) + cmbModTH[slots[i]] + 1;
            playerdat->armour[slots[0]] = playerdat->armour[slots[0]] + toughness;
        }
        }
        break;
    }
    return 0;
}

/* Spell icon base for each major class (the subclass is added; 0x1E is no icon). */
/* name: a static in FM Towns (_last_light+8). */
static unsigned char spell_class_values[16] = {
    0x14, 0xFF, 0x13, 0x05, 0x80, 0x80, 0x80, 0x80,
    0x80, 0x80, 0x80, 0x11, 0x80, 0x80, 0x80, 0x80
};

/* Fills out[3] with the icon of each active spell for the active spell display. */
void far parse_aspells(unsigned char *out)
{
    unsigned char i;
    memset(out, 0x1E, 3);
    for (i = 0; i < player->active_spells; i++) {
        out[i] = spell_class_values[player->spells[i] & 0xF];
        out[i] = out[i] + ((player->spells[i] & 0xF0) >> 4);
    }
}

/* Applies the bits player_affected_by gathered: bit 1 cuts noise by 16, bit 2
   visibility by 5, bit 3 visibility by 16 (none below 0); the high nibble was meant to be
   added to all four armour values, but the first loop has shifted bonuses right four
   times by then, so the second reads the original bits 8 to 11, which nothing sets: the
   armour spells (Resist Blows, Thick Skin, Iron Flesh) and class 2 enchantments add
   nothing (UW2.EXE the same, ovr142). Then redraws the active spell icons. */
void far parse_spells(uint16 bonuses)
{
    unsigned char i;
    unsigned char spells[3];
#ifndef __TURBOC__
    /* port only: --enhance armour-spells (docs/ENHANCEMENTS.md), the armour level as
       player_affected_by left it, before the loop below shifts it away */
    unsigned char spell_armour = (unsigned char)((bonuses >> 4) & 0xF);
    static int armour_probe = -1;       /* UW2PORT_ARMOUR_PROBE, read once */
#endif
    for (i = 0; i < 4; i++, bonuses = bonuses >> 1) {
        if (bonuses & 1) {
            switch (i) {
            case 1:
                plyNotice[0] -= plyNotice[0] > 0x10 ? 0x10 : plyNotice[0];
                break;
            case 2:
                plyNotice[1] -= plyNotice[1] > 5 ? 5 : plyNotice[1];
                break;
            case 3:
                plyNotice[1] -= plyNotice[1] > 0x10 ? 0x10 : plyNotice[1];
                break;
            }
        }
    }
    for (i = 0; i < 4; i++)
        playerdat->armour[i] = playerdat->armour[i] + ((bonuses >> 4) & 0xF);
#ifndef __TURBOC__
    /* port only: with the fix the armour level is added to every hit location, as the
       loop above was meant to; FixPlayerEquips has worked the armour out afresh, so it is
       added once. UW2PORT_ARMOUR_PROBE (a test, tools/armourcheck.py)
       reports the four values and the level on stderr after each recomputation */
    if (ENHANCED(ENH_ARMOUR_SPELLS))
        for (i = 0; i < 4; i++)
            playerdat->armour[i] = playerdat->armour[i] + spell_armour;
    if (armour_probe < 0) armour_probe = getenv("UW2PORT_ARMOUR_PROBE") != NULL;
    if (armour_probe)
        fprintf(stderr, "armour-probe: armour %u %u %u %u spell %u\n", playerdat->armour[0],
                playerdat->armour[1], playerdat->armour[2], playerdat->armour[3], spell_armour);
#endif
    parse_aspells(spells);
    active_spells(spells);
}

/* An armour item's protection: its table protection scaled by quality / 64, plus one.
   Weapons (hack major, minors 0 and 1) give none. */
/* name: FM Towns armor_val_ occupies this position and performs the same calculation. */
int far armor_val(struct Object far *obj)
{
    register int armour;
    register int protection;
    if (OBJ_MAJOR(obj) == MAJOR_HACK && OBJ_MINOR(obj) < MINOR_ARMOR)
        return 0;
    armour = Armor[OBJ_ITEM(obj) - FIRST_ARMOR].protection;
    protection = ((unsigned)(obj->qn.f.quality * armour)) >> 6;
    protection++;
    return protection;
}


/* Works out the player's derived state from scratch:
   - armour per hit location from slots 0 to 4, plus a shield (hack major, minor 3,
     types 11 to 15) in the off hand (slot 7 + lefty) on locations 0 and 1;
   - defence: the defence skill plus half the skill of the weapon in hand (slot 8 -
     lefty): the weapon's own skill clamped to sword, axe or mace for a hand weapon,
     barehand otherwise; load_weapon picks the weapon animation to match;
   - the light: the brightest lit light in the light slots or on the cursor (bits 4 to
     7 of player->light, the slot in the low bits);
   - the effects of active spells, then of worn enchanted items (slots 0 to 10, ObjWorn);
   - the light level: the larger of the carried light (6 under wizard eye) and the
     level's minimum from DL.DAT;
   - mushrooms, the void's sleep motion bit 0x10, the physics and the frame rate. */
void far FixPlayerEquips(void)
{
    int brightness, best_slot, armour;
    uint16 bonuses;
    unsigned char flag;
    int16 major, effect;
    struct Object far *item;
    register int slot;
    unsigned char *data;
    bonuses = 0;
    for (slot = 0; slot < 4; slot++) playerdat->armour[slot] = 0;
    for (slot = 0; slot <= 4; slot++) {
        if (item = AskInventory(slot))
            playerdat->armour[defence_slot_index[slot]] += armor_val(item);
    }
    item = AskInventory(player->lefty + 7);
    if (item && OBJ_MAJOR(item) == MAJOR_HACK &&
        OBJ_MINOR(item) == MINOR_ARMOR2 &&
        OBJ_INCLASS(item) >= 11 && OBJ_INCLASS(item) <= 15) {
        armour = armor_val(item);
        playerdat->armour[0] += armour;
        playerdat->armour[1] += armour;
    }
    playerdat->defence = player->skills[SKILL_DEFENSE];
    ActiveObj = AskInventory(8 - player->lefty);
    armour = 2;
    if (ActiveObj && OBJ_MAJOR(ActiveObj) == MAJOR_HACK &&
        OBJ_MINOR(ActiveObj) < MINOR_ARMOR) {
        if (OBJ_MINOR(ActiveObj) == MINOR_WEAPON) {
            armour = Weapons[ActiveObj->id & ID_INCLASS].skill;
            if (armour < 3) armour = 3;
            else if (armour > 5) armour = 5;
            load_weapon((unsigned char)armour + 0xFD);
        } else if (OBJ_INCLASS(ActiveObj) <= 7)
            load_weapon(3);
        else
            load_weapon(-1);
    } else load_weapon(3);
    playerdat->defence = playerdat->defence + (player->skills[armour] >> 1);
    init_spells();
    best_slot = 0;
    brightness = 0;
    for (slot = 0; slot <= 4; slot++) {
        if (slot == 4) ActiveObj = CursorObjPtr;
        else ActiveObj = AskInventory(ValidLightSlots[slot]);
        if (ActiveObj && OBJ_CLASS(ActiveObj) == CLASS_LIGHT &&
            OBJ_INCLASS(ActiveObj) >= 4 && OBJ_INCLASS(ActiveObj) < 8) {
            data = get_class_data();
            if (data[1] > brightness) {
                brightness = data[1];
                best_slot = slot;
            }
        }
    }
    player->light = (brightness << 4) + best_slot;
    for (slot = 0; slot < player->active_spells; slot++)
        player_affected_by(player->spells[slot] & 0xF,
                           (player->spells[slot] & 0xF0) >> 4, &bonuses, -1);
    for (slot = 0; slot <= 10; slot++) {
        ActiveObj = AskInventory(slot);
        if (ActiveObj && ObjWorn(ActiveObj->id & ID_ITEM, slot) &&
            decode_obj_spell(ActiveObj, &major, &effect, &flag) && !flag &&
            player_affected_by(major, effect, &bonuses, slot))
            remove_spell(ActiveObj);
    }
    parse_spells(bonuses);
    if (WizEye) light_act = 6;
    else light_act = (player->light & 0xF0) >> 4;
    if (light_act > loc_lght) set_light(light_act);
    else set_light(loc_lght);
    set_drugged(player->shrooms > 0);
    if (player->sleepbits && player->in_void)
        motionbits |= 0x10;
    fizix_update();
    newFPS(-1);
}

/* Reads DL.DAT's byte for the current level (one byte per level, 80 in all). The
   value mod 10 is a light level; 10 or more means it is the level's minimum light
   (loc_lght), otherwise the level has none (0xFF). For example level 1, Lord British's
   castle, has 14: light 4 at least. */
void far load_dl(void)
{
    unsigned char value;
    register int fd;
    fd = our_open("dl.dat", 1, 0);
    if (fd >= 0) {
        lseek(fd, (int32)(PlayerLevel - 1), 0);
        read(fd, &value, 1);
        close(fd);
        light_mod = value % 10;
        light_hi = value >= 10;
        if (light_hi)
            loc_lght = light_mod;
        else
            loc_lght = 0xFF;
        last_light = 0x0C;
    }
}
