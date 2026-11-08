/* target: ovr133 */
/* opts: -mm -1 -G -O -Y -d */
/* The player record's load and save, and everything that is worked out again from the
   player's equipment and active spells: the whole of UW1's DOS overlay ovr133, in
   original order. Seeded from UW2Decomp's src/game/PLAYDATA.C (UW2's ovr142).
   save_player_data and read_player_data (called from INVSAVE.C, inside a save or a
   restore) write and read the player record as the PLAYER.DAT image: one key byte,
   name[0] xor 0xAA, then the 0xD2-byte record xor-encoded with that key (xorwrite,
   xorread). Before writing, the values that live elsewhere while playing (attributes, HP,
   the player's position, heading and level, sound and music, terrain) are copied into
   the record; after reading they are copied back out.
   FixPlayerEquips is called whenever equipment, spells or light change: it recomputes
   armour by hit location, defence, the weapon's animation, the brightest carried light,
   the effects of active spells and of enchanted worn items (player_affected_by),
   stealth (plyNotice), the light level and the mushroom effect.

   UW1 against UW2: the record is 0xD2 bytes with its own layout (struct Player, player.h); no
   DL.DAT (load_dl) and no per-level minimum light, no poison weapon or valor; two
   functions of UW1's own, swap_tmap (a body here, empty in UW2) and set_maze, the maze
   navigation spell (class 13 minor 4) that swaps the floor textures for the maze level,
   level 7; FixPlayerEquips notes worn dragon skin boots, and does not test the void's
   sleep.

   Data owned: player, playerdat, ThePlayer, PlayerLevel, PlayerFacing, PlayerHeading,
   player_name_handle, plyNotice, UsingPole, the dragon skin boots and maze flags, and the
   tables below.
   UW1 has no symbol-bearing build: names are UW2's (the FM Towns symbol table) where the
   routine is the same.
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

/* Declared in each file that uses it, its own way (no header). */
extern struct Spell far spells[53];
extern unsigned char UsingPole;

/* UW1: LIGHTING.C's set_light (ovr142_0). */
void far set_light(signed char n);
/* UW1: decode_obj_spell's flag is tested signed; fx_is_on and music_is_on return char. */
/* UW1: ovr131's floor texture data (the listing's names). */
extern int16 floor_num[];
/* floor_IDs[4] (DS:7184), the floor texture the maze spell swaps (TEXTMAPS.C's array: the
   link proved it is not a variable of its own) */

/* This file's _BSS, in UW1 DS:726E..727D (UW2 DS:8288..8297). player is the player
   record (its storage is PLAYER.C's PlayerDat), playerdat the adventurer's creature type
   record (attributes, armour, defence, average HP), ThePlayer the player's object. */
/* match: laid out by name (tools/bssorder.py): player_name_handle 80, player 280,
   playerdat 440, ThePlayer 444, PlayerLevel 568, PlayerFacing 704, PlayerHeading 768. */
int16 player_name_handle;
struct Player *player;
struct Creature *playerdat;
struct Object far *ThePlayer;
int16 PlayerLevel;
int16 PlayerFacing;
int16 PlayerHeading;

/* This file's _DATA, in UW1 DS:1AF8..1B35. */
/* Body slot to defence index: inventory slots 0 to 4 (probably helm, chest, gloves,
   leggings, boots) to playerdat->armour[] hit locations 3, 0, 1, 2, 2. */
/* name: FM Towns keeps it as a static (_k_modes+0x10). */
static signed char defence_slot_index[6] = {3, 0, 1, 2, 2, 0};
/* [0] noise, [1] visibility. */
unsigned char plyNotice[2] = {0x0F, 0x0F};
unsigned char UsingPole = 0;
/* UW1: set while the player wears the dragon skin boots (PLAYMOVE.C keeps lava from
   hurting); name: the listing's (DS:1B01), as symbols.tsv has it. */
char DragonSkinBoots_dseg_5c99_1B01 = 0;
/* UW1: set while the maze navigation spell is in force; set_maze applies it. name:
   descriptive. */
char MazeSpell = 0;
/* set_drugged's state: the mushroom effect in force (-1 none), and whether the first
   effect has still to be shown. */
/* name: statics in FM Towns (UW2's _last_light+1, +2); the names are descriptive. */
static char ShroomsEnabled = -1;
static char ShroomsRelated = 1;
/* Damage-protection bits for class 3 minors 5..9 (in the Guide's list missile
   protection, flameproof, poison resistance, magic protection, greater magic
   protection), or-ed into the player type's resist byte. */
static unsigned char damage_protection_flags[5] = {0x40, 0x08, 0x10, 0x01, 0x02};

/* name: IDA left this empty function unnamed; UW2's name for the empty function in the
   same place. */
void far MaybePlayerDayLoadrelated_ovr142_0(void) {}

/* Writes the player record to fd as PLAYER.DAT has it: first the values kept elsewhere
   while playing are copied in (attributes and vitality from playerdat, HP from the player
   object, PN's position, the facing and level, sound and music on, the footing), then a
   key byte (name[0] xor 0xAA) and the whole record xor-encoded with it (xorwrite). */
void far save_player_data(int fd)
{
    unsigned char key;
    key = player->name[0] ^ 0xAA;
    player->strength = playerdat->attr[ATTR_STR];
    player->dexterity = playerdat->attr[ATTR_DEX];
    player->intelligence = playerdat->attr[ATTR_INT];
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
    xorwrite(fd, key, (unsigned char far *)player, sizeof(struct Player));
}

/* The reverse of save_player_data: reads the key and the record, and copies the values
   kept elsewhere back out to playerdat, the player object, PN, the facing and level and
   the sound settings; then the detail level (set_graphics_level) and the movement state
   (newFPS). */
void far read_player_data(int fd)
{
    unsigned char key;
    read(fd, &key, 1);
    xorread(fd, key, (unsigned char far *)player, sizeof(struct Player));
    playerdat->attr[ATTR_STR] = player->strength;
    playerdat->attr[ATTR_DEX] = player->dexterity;
    playerdat->attr[ATTR_INT] = player->intelligence;
    ThePlayer->hp = player->health;
    playerdat->avghit = player->maxhealth;
    PN.x = player->saved_x;
    PN.y = player->saved_y;
    PN.z = player->saved_z;
    PlayerFacing = player->saved_facing;
    PlayerLevel = player->saved_level;
    PN.terrain = player->terrain;
    turn_fx(player->sound);
    turn_music(player->music);
    set_graphics_level();
    newFPS(player->fps);
}

/* Clears every spell effect before FixPlayerEquips applies them again: resistances,
   motion bits, the to-hit protection cmbModTH, haste, wizard eye, bless, time stop, the
   dragon skin boots and the maze spell, regeneration. Sets the base stealth from the
   stealth skill: noise 13 - stealth / 3 and visibility 15 - stealth / 5 (lower is harder
   to notice), and the pick-up distance 0x90 (0x190 while UsingPole). */
void far init_spells(void)
{
    ComObjData[ITEM_ADVENTURER].resist = 0;         /* item 127 is the player's own object type */
    plyNotice[0] = 13 - player->skills[SKILL_STEALTH] / 3;
    plyNotice[1] = 15 - player->skills[SKILL_STEALTH] / 5;
    motionbits = 0;
    memset(cmbModTH, 0, 4);
    /* name: DS:0285 is UW2's Blessed by its place in the chain. */
    Hasted = WizEye = Blessed = TimeStop = DragonSkinBoots_dseg_5c99_1B01 = MazeSpell = 0;
    PickDist = 0x90;
    if (UsingPole) PickDist = 0x190;
    plyregen[0] = 0;
}

/* UW1: loads the floor textures DATA\F32.TR and F16.TR, first choosing texture tex
   (0xFF keeps the current one). Empty in UW2. */
void far swap_tmap(unsigned char tex)
{
    if (tex != 0xFF)
        floor_IDs[4] = tex;
    load_tr_mem("DATA\\f32.tr", floor_IDs,
                               floor_num, f32_buf);
    load_tr_mem("DATA\\f16.tr", floor_IDs,
                               floor_num, f16p);
}

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
     class 0   light: the brightest level wins (light high nibble);
     class 1   motion: sets bit minor - 1 of motionbits;
     class 2   armour: the best level is kept in bits 4 to 7 of *bonuses, meant to be
               added to every hit location by parse_spells (which loses it);
     class 3   minor 1 adds 3 to cmbModTH at every hit location; minors 2 to 4 set
               stealth bits in *bonuses (parse_spells); minors 5 to 9 add resistances;
     class 9   backfire;
     class 11  0 time stop, 1 wizard eye, 2 haste, 3 PickDist 0, 14 and 15 regenerate
               health and mana;
     class 12  an item's own protection (minor bits 0 to 2 plus one) or, with the 8 bit set,
               toughness, for the item's hit location; slots above 4 count for
               locations 0 and 1. The toughness is always added to armour[slots[0]];
     class 13  (UW1) minor 4, maze navigation.
   Always returns 0, so FixPlayerEquips never calls remove_spell. */
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
        if (minor == 1) {
            cmbModTH[0] += 3;
            cmbModTH[1] += 3;
            cmbModTH[2] += 3;
            cmbModTH[3] += 3;
        } else if (minor <= 4)
            *bonuses |= 1 << (minor - 1);
        else if (minor <= 9)
            ComObjData[ITEM_ADVENTURER].resist |= damage_protection_flags[minor - 5];
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
    case SPELLC_SPECIAL:
        switch (minor) {
        case 4: MazeSpell = 1; break;
        }
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

/* Spell icon base for each major class (the subclass is added; 0x15 is no icon). */
/* name: a static in FM Towns (UW2's _last_light+8). */
static unsigned char spell_class_values[16] = {
    0x0E, 0xFF, 0x0D, 0x04, 0x80, 0x80, 0x80, 0x80,
    0x80, 0x80, 0x80, 0x0B, 0x80, 0x80, 0x80, 0x80
};

/* Fills out[3] with the icon of each active spell for the active spell display. */
void far parse_aspells(unsigned char *out)
{
    unsigned char i;
    memset(out, 0x15, 3);
    for (i = 0; i < player->active_spells; i++) {
        out[i] = spell_class_values[ASPELL_CLASS(player->spells[i])];
        out[i] = out[i] + ASPELL_SUB(player->spells[i]);
    }
}

/* UW1: turns the maze navigation spell's view on or off. On level 7 (the maze), turning
   it on swaps in floor texture 0x0C unless it is already that, and off restores 0x0E
   when it was 0x0C; the record keeps the state (player->maze, bit 4 of byte 0x62). */
/* name: descriptive, chosen to fit the stub order (bssorder key 835, between
   MaybePlayerDayLoadrelated_ovr142_0 749 and set_drugged 859). */
void far set_maze(char on)
{
    char tex;
    if (PlayerLevel == LEVEL_TYBAL) {
        tex = 0xFF;
        if (on && floor_IDs[4] != 0x0C)
            tex = 0x0C;
        if (!on && floor_IDs[4] == 0x0C)
            tex = 0x0E;
        if (tex >= 0)
            swap_tmap(tex);
    }
    player->maze = on;
}

/* Applies the bits player_affected_by gathered: bit 1 cuts noise by 16, bit 2
   visibility by 5, bit 3 visibility by 16 (none below 0); the high nibble was meant to be
   added to all four armour values, but the first loop has shifted bonuses right four
   times by then, so the second reads the original bits 8 to 11, which nothing sets: the
   armour spells (Resist Blows, Thick Skin, Iron Flesh) and class 2 enchantments add
   nothing (UW.EXE the same, ovr133). Then the maze spell, and redraws the active spell
   icons. */
void far parse_spells(uint16 bonuses)
{
    unsigned char i;
    unsigned char spells[3];
#ifndef __TURBOC__
    /* port only: --enhance armour-spells (docs/ENHANCEMENTS.md), the armour level as
       player_affected_by left it, before the loop below shifts it away */
    unsigned char spell_armour = (unsigned char)((bonuses >> 4) & 0xF);
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
       added once. UW1PORT_ARMOUR_PROBE (a test, tools/armourcheck.py)
       reports the four values and the level on stderr after each recomputation */
    if (ENHANCED(ENH_ARMOUR_SPELLS))
        for (i = 0; i < 4; i++)
            playerdat->armour[i] = playerdat->armour[i] + spell_armour;
    if (getenv("UW1PORT_ARMOUR_PROBE"))
        fprintf(stderr, "armour-probe: armour %u %u %u %u spell %u\n", playerdat->armour[0],
                playerdat->armour[1], playerdat->armour[2], playerdat->armour[3], spell_armour);
#endif
    set_maze(MazeSpell);
    parse_aspells(spells);
    active_spells(spells);
}

/* An armour item's protection: its table protection scaled by quality / 64, plus one.
   Weapons (hack major, minors 0 and 1) give none. */
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
     7 of the record's light, the slot in the low bits);
   - the effects of active spells, then of worn enchanted items (slots 0 to 10, ObjWorn);
     worn dragon skin boots (item 0x2F) are noted;
   - the light level (6 under wizard eye), mushrooms, the physics and the frame rate. */
void far FixPlayerEquips(void)
{
    int brightness, best_slot, armour;
    uint16 bonuses;
    char flag;
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
            armour = Weapons[OBJ_INCLASS(ActiveObj)].skill;
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
        player_affected_by(ASPELL_CLASS(player->spells[slot]),
                           ASPELL_SUB(player->spells[slot]), &bonuses, -1);
    for (slot = 0; slot <= 10; slot++) {
        ActiveObj = AskInventory(slot);
        if (ActiveObj && ObjWorn(OBJ_ITEM(ActiveObj), slot)) {
            if (decode_obj_spell(ActiveObj, &major, &effect, &flag) && !flag) {
                if (player_affected_by(major, effect, &bonuses, slot))
                    remove_spell(ActiveObj);
            } else if (OBJ_ITEM(ActiveObj) == ITEM_DRAGON_SKIN_BOOTS)
                DragonSkinBoots_dseg_5c99_1B01 = 1;
        }
    }
    parse_spells(bonuses);
    if (WizEye) set_light(6);
    else set_light((player->light & 0xF0) >> 4);
    set_drugged(player->shrooms > 0);
    fizix_update();
    newFPS(-1);
}
