/* target: seg038_3307 */
/* opts: -mm -1 -G -O -Y -d */
/* Casting spells: the spell table, mana and health, missiles, area spells, spells on a
   target and on an object, summoning, detecting monsters, the extra spells, and the damage
   an area spell does to a tile: the whole of DOS resident segment seg038_3307, in original
   order. Seeded from UW2Decomp's src/combat/SPELLS.C (UW2's overlay ovr156), with the
   routines UW2 moved to SPELLS2.C (ovr157): sp_true_sight, creat_spell, tremor_area,
   print_monster, mdetect and xt_spells. Function and global names are UW2's (the FM Towns
   originals) where the routine is the same one; UW1 has no symbols of its own.

   What it does in the game: do_spell is the one dispatcher for every spell, whoever casts
   it: the player from runes (RUNES.C's player_cast), wands, scrolls and enchanted objects
   (cast, which takes a spells[] index), critters and traps. It refuses on an anti-magic
   square and on level 9, and sends the spell by class (combat.h's SPELLC_*): classes 0..3
   become a timed active spell (set_curmagic, PLAYTIME.C), 4 heals the target, 5 fires a
   missile (for the player, after aiming: GameInputMode 3), 6 hits an area in front of the
   caster, 7 a critter in front of the player, 8 creates things, 9 is a backfire, 10
   restores mana, 11 is the extra spells (xt_spells), 13 two special effects and 14 plays a
   cutscene.

   UW1 against UW2: 53 spells; no spend_mana, special_spells, target_spells, wound_foe or
   the spells built on it (shockwave, bleeding, smite foe, frost, repel undead), study
   monster, enchantment, mending or the Map Area spell. Area spells (class 6) act 3d4 times
   four squares ahead, radius 2, from area_spells; class 7 spells hit a critter in front of
   the player at once (area1_spells) instead of waiting for a click. The object spells
   (Remove Trap, Name Enchantment, Open) are class 11 minors 2..5 and wait for the click
   (obj_spells). hit_critter_goal has no gtarg (always 1), sp_charm (UW1's Ally) makes the
   critter an ally, Flame Wind (sp_meteor) blasts a cross of five squares, Detect Monster
   names no creature, summoning has only food, the runes of warding (a trap, cast_trap_spell)
   and Summon Monster. struct Player differs (player.h has UW1's record).

   Callers: RUNES.C (player_cast), USEITEMS.C and WORLDEV.C (spells objects carry,
   inanimate_spell), AI.C and CRITTIME.C (critters casting), TRIGGER.C (spell traps),
   SKILLS.C, PLAYTIME.C, PLAYDATA.C, BABLHACK.C, EFFECT.C. It reaches into most of the
   game: PLAYTIME.C (set_curmagic), MISSILE.C, DAMAGE.C, EFFECT.C, TRIGGER.C
   (cast_trap_spell), WORLDEV.C, AI.C and CRITTIME.C (critter goals), CUTS.C.

   Data owned: spells[] (far), mspell_mused, the mana a missile spell will cost;
   area_spells and area1_spells, the per-square handlers; the dice of damage_square; and
   inanmMapX, inanmMapY, the square of an object that casts.
   Name: descriptive (casting spells: cast, do_spell). */

#include <stdlib.h>
#include <string.h>
#include "combat.h"
#include "critter.h"
#include "event.h"
#include "file.h"
#include "gfx.h"
#include "map.h"
#include "motion.h"
#include "object.h"
#include "player.h"
#include "sound.h"
#include "sys.h"
#include "ui.h"

/* Declared in each file that uses it, its own way (no header). */
unsigned char far IsMobElem(struct Object far *obj);
void far player_setup(int x, int y, int how);

/* PLAYER.C defines the record's storage as a byte array; this file reaches it as the
   union (player.h). */
extern union PlayerStore PlayerDat;

extern struct Spell far spells[NUM_SPELLS];
/* UW1's own routines, at the addresses of their stubs (the listing's names). */

/* This file's _BSS, DS:3636..3637: the map square of an object (not a critter) that casts. */
unsigned char inanmMapX, inanmMapY;

/* This file's _DATA, DS:09AA..09D8. */
unsigned char mspell_mused = 0;

/* True on a square whose tile has bit 0 of its door byte set: no magic works there. */
unsigned char far anti_magic_p(int x, int y)
{
    return Map_GetAddr(x, y)->door & 1;
}

/* Casts spell by number, an index into spells[] (0..0x34; anything higher does nothing). */
void far cast(unsigned char spell, struct Object far *who, struct Object far *target)
{
    if (spell < NUM_SPELLS)
        do_spell(SPELL_CLASS(spells[spell]), spells[spell].sub, who, target);
}

/* The spell dispatcher (see the file comment). An object caster (who at or above
   objdata) is tested for anti-magic at inanmMapX, inanmMapY, a critter at its own square,
   and nothing works on level 9. Bits 6 and 7 of sub are flags for the active spell classes
   and the area spell's target mode. Levitate and Fly (class 1, minors 3 and 5) give a
   little upward bounce. Class 13 minor 3 is the bullfrog (work_bullfrog_tiles), minor 5
   makes the player hallucinate (string 0xE4). Returns 0 when the spell did not take:
   anti-magic, an active spell the player could not start, or a heal with no target. */
char far do_spell(unsigned char cls, unsigned char sub, struct Object far *who,
                  struct Object far *target)
{
    if (who >= (struct Object far *)objdata && cls <= SPELLC_XT) {
        if (anti_magic_p(inanmMapX, inanmMapY))
            return 0;
    } else if (anti_magic_p(OBJ_HOMEX(who), OBJ_HOMEY(who)) || PlayerLevel == 9)
        return 0;
    switch (cls) {
    case SPELLC_MOTION:
        if ((sub & ~SPELL_FLAGS) == 3 || (sub & ~SPELL_FLAGS) == 5)
            phys_bounce_up(who);
    case SPELLC_LIGHT:
    case SPELLC_ARMOUR:
    case SPELLC_PROTECT:
        if (who == ThePlayer && set_curmagic(cls, sub & SPELL_MINOR, sub & SPELL_FLAGS))
            break;
        return 0;
    case SPELLC_HEAL:
        if (target) {
            healing(target, sub);
            break;
        }
        return 0;
    case SPELLC_MISSILE:
        if (who == ThePlayer) {
            GameInputMode = 3;
            ObjectActing = ThePlayer;
            ObjectActorArg = sub;
            force_mouse_cursor(ICON_CURSORS + 9);
        } else
            release_missile(who, sub);
        break;
    case SPELLC_AREA:
        nail_area(who, sub);
        break;
    case SPELLC_1AREA:
        nail_1area(who, sub);
        break;
    case SPELLC_CREATE:
        creat_spell(who, sub);
        break;
    case SPELLC_BACKFIRE:
        backfire(who, sub);
        break;
    case SPELLC_MANA:
        restore_mana(who, sub);
        break;
    case SPELLC_XT:
        xt_spells(who, sub & SPELL_FLAGS, sub & SPELL_MINOR);
        break;
    case SPELLC_CUTSCENE:
        runcutscene(sub);
        update_animobj(4);
        break;
    case SPELLC_SPECIAL:
        switch (sub) {
        case 3:
            work_bullfrog_tiles(4, 0, 0);
            break;
        case 5:
            game_sprint(0xE4);  /* 'Your vision distorts and you feel light headed.' */
            player->shrooms = 3;
            FixPlayerEquips();
            break;
        }
        break;
    }
    return 1;
}

/* Restores the player's mana: amount > 0 gives max_mana * (amount + 0..3) / 16 + 1, a
   negative amount adds -amount. Capped at max_mana. */
#ifndef __TURBOC__
#define restore_mana shared_restore_mana
#endif
void far restore_mana(struct Object far *who, char amount)
{
    register int gain;

    if (who == ThePlayer) {
        if (amount > 0) {
            gain = player->max_mana * (amount + (rand() & 3));
            player->play_mana += (gain >> 4) + 1;
        } else
            player->play_mana -= amount;
        if (player->play_mana > player->max_mana)
            player->play_mana = player->max_mana;
        panel_check_hpmp();
    }
}

#ifndef __TURBOC__
#undef restore_mana
void far restore_mana(struct Object far *who,char amount)
{
    int old=port_shared_context(who ? Obj_MemTPtr(who) : 0);
    shared_restore_mana(who,amount);port_shared_context_end(old);
}
#endif

/* Heals the player like restore_mana: amount > 0 gives avghit * (amount + 0..3) / 16 + 1,
   a negative amount adds -amount, capped at the player's maximum (playerdat->avghit). */
#ifndef __TURBOC__
#define restore_hp shared_restore_hp
#endif
void far restore_hp(struct Object far *who, char amount)
{
    int hp;

    if (who == ThePlayer) {
        if (amount > 0) {
            hp = playerdat->avghit * (amount + (rand() & 3));
            hp >>= 4;
            hp += ThePlayer->hp + 1;
        } else
            hp = ThePlayer->hp - amount;
        if (hp > playerdat->avghit)
            ThePlayer->hp = playerdat->avghit;
        else
            ThePlayer->hp = hp;
        panel_check_hpmp();
    }
}

#ifndef __TURBOC__
#undef restore_hp
void far restore_hp(struct Object far *who,char amount)
{
    int old=port_shared_context(who ? Obj_MemTPtr(who) : 0);
    shared_restore_hp(who,amount);port_shared_context_end(old);
}
#endif

/* Adds amount hit points to a critter, capped at its Creature avghit (the player's
   record too). */
#ifndef __TURBOC__
#define get_hp_back shared_get_hp_back
#endif
void far get_hp_back(struct Object far *who, unsigned char amount)
{
    who->hp = who->hp + amount > Creature[OBJ_INMAJOR(who)].avghit
        ? Creature[OBJ_INMAJOR(who)].avghit : who->hp + amount;
    if (who == ThePlayer)
        panel_check_hpmp();
}

#ifndef __TURBOC__
#undef get_hp_back
void far get_hp_back(struct Object far *who,unsigned char amount)
{
    int old=port_shared_context(who ? Obj_MemTPtr(who) : 0);
    shared_get_hp_back(who,amount);port_shared_context_end(old);
}
#endif

/* The heal spells (class 4): minor d8 hit points, and minor 15 all of them. */
void far healing(struct Object far *who, char sub)
{
    unsigned char amount;

    if (OBJ_MAJOR(who) == MAJOR_CREATURE) {
        if (sub == 15)
            amount = 0xFF;
        else
            amount = rollem(sub, 8);
        get_hp_back(who, amount);
    }
}

/* A backfired spell (class 9): (sub)d8 damage to the caster, but never below 3 hit
   points. The player sees the screen flash. */
void far backfire(struct Object far *who, char sub)
{
    char damage;

    if (OBJ_MAJOR(who) != MAJOR_CREATURE)
        return;
    damage = rollem(sub, 8);
    if (who->hp <= 3)
        return;
    if (who->hp - damage <= 3)
        who->hp = 3;
    else
        who->hp = who->hp - damage;
    if (who == ThePlayer)
        fill_FB(0xA8);
}

/* Fires missile spell sub (1..4: items 0x10 + items[sub - 1]). The player pays the mana
   held in mspell_mused only if it was launched (string 0xFF when there is no room). */
void far release_missile(struct Object far *who, char sub)
{
    unsigned char items[4] = { 7, 5, 4, 6 };
    char ok;

    ok = spell_fire(who, items[sub - 1]);
    if (who == ThePlayer) {
        if (!ok)
            game_sprint(0xFF);  /* 'There is not enough room to release that spell.' */
        else if (mspell_mused)
            player->play_mana -= mspell_mused;
        mspell_mused = 0;
    }
}

/* Creates an effect object of the given item for a tile, at a random height between the
   floor and the ceiling. */
struct Object far * far build_new_obj(int item, struct Tile far *tile)
{
    struct Object far *obj;
    int z;

    obj = CreateObj(item, 0);
    z = tile->height * 8;
    if (z < 0x80)
        z += rand() % (0x80 - z);
    SET_Z(obj, z);
    return obj;
}

/* Reveal, one object: a container or object with a hidden trap trigger (MAJOR_TRAP minor 2
   class 3) has it set off with the player's Search skill raised to 45 for the call, so
   the hidden thing shows itself (inferred from UseTrigger's how 5). */
char far sp_true_sight(struct Object far *caster, struct Object far *target)
{
    union Link far *head;
    struct Object far *trig;
    int search;

    if (OBJ_ISQUANT(target) || target->ol.f.link == 0)
        return 0;
    if (OBJ_MAJOR(target) == MAJOR_TRAP)
        return 0;
    head = &target->ol.link;
    trig = Obj_InList(&head, 0, MAJOR_TRAP, 2, 3);
    if (trig) {
        search = player->skills[SKILL_SEARCH];
        player->skills[SKILL_SEARCH] = 0x2D;
        UseTrigger(ThePlayer, target, trig, 5);
        player->skills[SKILL_SEARCH] = search;
        return 1;
    }
    return 0;
}

/* Sheet Lightning, one square: a lightning effect and damage_square kind 2. */
char far sp_sheet_light(int x, int y, struct Object far *target, struct Tile far *tile,
                        unsigned char src)
{
    struct Object far *obj;

    obj = build_new_obj(ITEM_SPLASH_1C5, tile);
    damage_square(x, y, 2, src);
    if (add_animobj(Obj_MemTPtr(obj), 4, rand() % 4, x, y) == -1)
        Obj_Free(obj);
    else
        Obj_Add(&tile->objects, obj);
    return 1;
}

/* Flame Wind, one square: an explosion, and damage_square kind 1 on the square and the
   four next to it. */
char far sp_meteor(int x, int y, struct Object far *target, struct Tile far *tile,
                   unsigned char src)
{
    struct Object far *obj;

    obj = build_new_obj(ITEM_EXPLOSION_1C2, tile);
    damage_square(x, y, 1, src);
    damage_square(x + 1, y, 1, src);
    damage_square(x - 1, y, 1, src);
    damage_square(x, y + 1, 1, src);
    damage_square(x, y - 1, 1, src);
    if (add_animobj(Obj_MemTPtr(obj), 4, 0, x, y) == -1)
        Obj_Free(obj);
    else {
        Obj_Add(&tile->objects, obj);
        fireball_effect(obj, x, y);
    }
    return 1;
}

/* Smite Undead: a critter whose resist byte has bit 0x80 (check_res returns 0: the
   undead, inferred) takes 255 damage of type 3. */
char far sp_ward_undead(int x, int y, struct Object far *target, struct Tile far *tile,
                        unsigned char src)
{
    if (check_res(target, 1, RES_UNDEAD) == 0) {
        damage_item(target, Obj_IntTMem(src), x, y, 0xFF, DMG_MAGIC);
        return 1;
    }
    return 0;
}

/* Poison: 5d4 damage of type 0x13 (magic and poison). */
char far sp_poison(int x, int y, struct Object far *target, struct Tile far *tile,
                   unsigned char src)
{
    put_effect(target, 7, 4, 0, 7, x, y);
    damage_item(target, Obj_IntTMem(src), x, y, rollem(5, 4), DMG_MAGIC | DMG_POISON);
    return 1;
}

/* A mind spell lands: unless the critter shrugs it off (check_res type 3: a chance of
   (resist & 3) in 3) it gets goal for gtarg 1 and, if attitude is not -1, that attitude.
   Always returns 1. */
char far hit_critter_goal(char goal, char attitude, struct Object far *npc, int x, int y)
{
    if (check_res(npc, 1, DMG_MAGIC)) {
        put_effect(npc, 7, 4, 0, 7, x, y);
        change_critter_goal(npc, goal, 1);
        if (attitude != -1)
            SET_ATTITUDE(npc, attitude);
    }
    return 1;
}

/* Ally (UW2's Charm): unless resisted the critter becomes the player's ally and friendly
   (attitude 3), and one that was not an ally already gets goal 2. */
char far sp_charm(int x, int y, struct Object far *target)
{
    if (check_res(target, 1, DMG_MAGIC)) {
        put_effect(target, 7, 4, 0, 7, x, y);
        if (!OBJ_ALLY(target))
            change_critter_goal(target, GOAL_WANDER, 0);
        SET_ALLY(target, 1);
        SET_ATTITUDE(target, ATT_FRIENDLY);
    }
    return 1;
}

/* Confusion: goal 2 and attitude 1 unless resisted. */
char far sp_confusion(int x, int y, struct Object far *target, struct Tile far *tile,
                      unsigned char src)
{
    return hit_critter_goal(GOAL_WANDER, ATT_UPSET, target, x, y);
}

/* Cause Fear: goal 6 (flee, inferred) unless resisted. */
char far sp_fear(int x, int y, struct Object far *target, struct Tile far *tile,
                 unsigned char src)
{
    return hit_critter_goal(GOAL_FLEE, -1, target, x, y);
}

/* Paralyze: goal 7 and attitude 1 unless resisted. */
char far sp_hold(int x, int y, struct Object far *target, struct Tile far *tile,
                 unsigned char src)
{
    return hit_critter_goal(GOAL_STAND_7, ATT_UPSET, target, x, y);
}

/* Calls fn on the squares of a w + 1 by h + 1 rectangle (clipped to the map), at most
   count times that return true. type is the target mode, sub's top bits: 0 every critter
   other than the caster; 0x40 random open squares (each with chance count in w * h + 3,
   up to five passes until count is used); 0x80 every object; 0xC0 every object. */
void far process_area(char count, unsigned char src, SpellFn fn, unsigned char type,
                      char x0, char y0, char w, char h)
{
    struct Object far *obj;
    struct Tile far *tile;
    struct Tile far *start;
    int tries = 0;
    union Link far *link;
    int next;
    int x;
    int y;

    if (x0 >= MAP_SIZE)
        return;
    if (x0 + w < 0)
        return;
    if (y0 >= MAP_SIZE)
        return;
    if (y0 + h < 0)
        return;
    if (x0 < 0) {
        w -= -x0;
        x0 = 0;
    } else if (x0 + w >= MAP_SIZE)
        w -= x0 + w - MAP_SIZE;
    if (y0 < 0) {
        h -= -y0;
        y0 = 0;
    } else if (y0 + h > MAP_SIZE)
        h -= y0 + h - MAP_SIZE;
    if (w <= 0)
        return;
    if (h <= 0)
        return;
    start = Map_GetAddr(x0, y0);
    do {
        for (x = x0; x <= x0 + w; x++)
            for (y = y0; y <= y0 + h; y++) {
                if (x < 0)
                    continue;
                if (x >= MAP_SIZE)
                    continue;
                if (y < 0)
                    continue;
                if (y >= MAP_SIZE)
                    continue;
                tile = start + (x - x0) + ((y - y0) << 6);
                if (type == AREA_RANDOM) {
                    obj = 0;
                    if (tile->type > TILE_SOLID && rand() % (w * h + 3) < count)
                        if (fn(x, y, obj, tile, src) && --count == 0)
                            return;
                    continue;
                }
                link = &tile->objects;
                while ((obj = Obj_PtrTMem(link)) != 0) {
                    next = link->word >> 6 & 0x3FF;
                    if (type == AREA_OBJECTS
                        || type == AREA_CRITTERS && OBJ_MAJOR(obj) == MAJOR_CREATURE && Obj_MemTPtr(obj) != src
                        || type == AREA_ALL)
                        if (fn(x, y, obj, tile, src) && --count <= 0)
                            return;
                    if ((link->word >> 6 & 0x3FF) == next)
                        link = &obj->qn.link;
                }
            }
    } while (type == AREA_RANDOM && count > 0 && tries++ < 4);
}

/* An area spell centred dist squares ahead of who (along the caster's heading; an object
   caster uses inanmMapX, inanmMapY and the coarse heading), radius squares around. */
void far gronk_area(struct Object far *who, char count, SpellFn fn, unsigned char type,
                    unsigned char dist, unsigned char radius)
{
    int16 x;
    int16 y;
    int index;
    unsigned char src;
    int heading;

    index = Obj_MemTPtr(who);
    if (index < NUM_MOBILE) {
        src = index;
        heading = (OBJ_HEADING(who) << 5) + OBJ_FINEHEAD(who);
        x = OBJ_HOMEX(who);
        y = OBJ_HOMEY(who);
    } else {
        src = 0;
        heading = OBJ_HEADING(who) << 5;
        x = inanmMapX;
        y = inanmMapY;
    }
    move_along(heading, dist, &x, &y);
    process_area(count, src, fn, type, (char)x - radius, (char)y - radius, radius * 2 + 1,
                 radius * 2 + 1);
    src = 0;                            /* match: a dead store, but the DOS bytes have it */
}

/* Calls fn(npc, arg) for the first (or with all, every) active critter whose whoami
   matches. fn returns true when it removed the critter from the active list. */
void far gronk_whoami(int whoami, char all, NEARPTR arg,
                      char (far *fn)(struct Object far *npc, NEARPTR arg))
{
    unsigned char far *p;
    struct Object far *npc;

    for (p = ActiveMob; p < LastActiveMob; p++) {
        npc = Obj_IntTMem(*p);
        if (npc->whoami == whoami) {
            if (fn(npc, arg))
                p--;
            if (!all)
                return;
        }
    }
}

/* The per-square handlers of the area spells (class 6, minors 1..4) and of the spells on
   a critter in front of the player (class 7, minors 1..5). */
SpellFn area_spells[4] = {
    (SpellFn)sp_true_sight, sp_sheet_light, sp_confusion, sp_meteor
};

SpellFn area1_spells[5] = {
    sp_fear, sp_ward_undead, (SpellFn)sp_charm, sp_poison, sp_hold
};

/* Area spells (class 6): the handler acts 3d4 times on the squares within 2 of the square
   four ahead of the caster. */
void far nail_area(struct Object far *who, unsigned char sub)
{
    char count;

    count = rollem(3, 4);
    gronk_area(who, count, area_spells[(sub & ~SPELL_FLAGS) - 1], sub & SPELL_FLAGS, 4, 2);
}

/* Spells on a critter (class 7), the player's only: the handler acts once within 2 of the
   square four ahead. */
void far nail_1area(struct Object far *who, unsigned char sub)
{
    if (who == ThePlayer)
        gronk_area(who, 1, area1_spells[(sub & ~SPELL_FLAGS) - 1], sub & SPELL_FLAGS, 4, 2);
}

/* Creates something about a square in front of the caster, a little to either side at
   random: which 1 food (one of seven), 3 a rune of warding (cast_trap_spell; the player
   reads string 0x114 when it returns nonzero, else 0x115), 4 Summon Monster twelve fine
   units ahead (a random creature
   number from lvl to 2 * lvl - 1, lvl being the player's Casting skill or a critter
   caster's dungeon level * 4, at least 2; rerolled for swimmers, for creatures with no hit
   points or Creature bit bA_1, and for items 0x7B and 0x7C). A monster the player summons
   is his ally; one a critter summons is hostile and heads for the player. With no room
   the player reads string 0x115. */
void far creat_spell(struct Object far *caster, char which)
{
    struct Object far *obj;
    struct Object far *save;
    int heading;
    int16 x;
    int16 y;
    int z;
    int homex;
    int homey;
    unsigned char lvl;
    struct Tile far *tile;
    register int item;
    register int msg;

    msg = 0x115;                        /* 'There is no room to create that.' */
    heading = ((OBJ_HEADING(caster) << 5) + OBJ_FINEHEAD(caster) + (rand() % 0x1B - 0xD)) % 0xFF;
    x = (OBJ_HOMEX(caster) << 3) + OBJ_FINEX(caster);
    y = (OBJ_HOMEY(caster) << 3) + OBJ_FINEY(caster);
    move_along(heading, which == 4 ? 0xC : 9, &x, &y);
    homex = x >> 3;
    homey = y >> 3;
    if (which == 3) {
        if (cast_trap_spell(homex, homey, 9))
            msg--;
        if (caster == ThePlayer)
            game_sprint(msg);
        return;
    }
    tile = Map_GetAddr(homex, homey);
    z = tile->height << 3;
    switch (which) {
    case 1:
        item = rand() % 7 + FIRST_FOOD;
        break;
    case 4:
        lvl = caster == ThePlayer ? player->skills[SKILL_CASTING] : PlayerLevel << 2;
        if (lvl < 2)
            lvl = 2;
        do
            item = lvl + rand() % lvl + FIRST_CREATURE;
        while (Creature[item & ~0x1C0].avghit == 0 || Creature[item & ~0x1C0].bA_1
               || item == 0x7B || item == 0x7C || Creature[item & ~0x1C0].swims);
        break;
    }
    if (can_place(item, 0, x, y, z, 1, 8)) {
        obj = CreateObj(item, which == 4);
        SET_FINEX_UNSIGNED(obj, x & 7);
        SET_FINEY(obj, y);
        if (which == 4) {
            save = ActiveObj;
            ActiveObj = obj;
            creature_obj_init();
            ActiveObj = save;
            SET_HOMEX(obj, homex);
            SET_HOMEY(obj, homey);
            if (Creature[item & ~0x1C0].flier)
                z = (z + 0x80) / 2;
            if (caster == ThePlayer) {
                SET_ALLY(obj, 1);
            } else {
                SET_ATTITUDE(obj, ATT_HOSTILE);
                SET_B19_0(obj, 1);
                SET_DESTX(obj, OBJ_HOMEX(ThePlayer));
                SET_DESTY(obj, OBJ_HOMEY(ThePlayer));
            }
        } else
            obj->qn.f.quality = 0x3F;
        SET_Z(obj, z);
        Obj_Add(&tile->objects, obj);
        if (which != 4)
            obj_deal(obj, homex, homey, 1);
    } else if (caster == ThePlayer)
        game_sprint(msg);
}

/* Tremor, one square: drops a boulder (one of three sizes) from high up, which falls and
   can hit what is below. */
char far tremor_area(int x, int y, struct Object far *target, struct Tile far *tile,
                     unsigned char src)
{
    struct Object far *boulder;

    boulder = CreateObj(rand() % 3 + ITEM_LARGE_BOULDER_154, 0);
    SET_Z(boulder, 0x6E);
    if (put_at(x * 8 + 3, y * 8 + 3, 0x6E, boulder, 0, 0) && IsMobElem(boulder)) {
        SET_SPEED(boulder, (rand() & 3) + 2);
        boulder->heading = rand() & 0xFF;
        SET_BIN(boulder, curBin + (rand() & 3));
        /* match: open-coded, as SET_RATE neither masks nor shifts by 0 */
        boulder->b14 = boulder->b14 & 0xF8 | (rand() % 3 + 1 & 7) << 0;
    }
    return 1;
}

/* Prints 'You detect a creature / a few creatures / the activity of many creatures' (one,
   2..4, 5 and more) followed by the direction dir. */
void far print_monster(unsigned char dir, unsigned char n)
{
    print_path_to(get_string((n > 1) + (n > 4) + 0x3B | STR_GAME), 0, 0, 0, 0, 0, 0, -(dir + 1));
}

/* Detect Monster: for active critters within dist squares, a skill check against
   15 - the critter's noise counts it in one of eight directions. The direction with most
   is reported, and one other direction with more than a few; 'You detect no monster
   activity.' when none. */
void far mdetect(int dist, int skill)
{
    struct Object far *obj;
    unsigned char far *p;
    unsigned char counts[8];
    unsigned char px;
    unsigned char py;
    signed char xv;
    signed char yv;
    unsigned char i;
    unsigned char max;
    unsigned char best;
    unsigned char tries;

    memset(counts, 0, 8);
    px = OBJ_HOMEX(ThePlayer);
    py = OBJ_HOMEY(ThePlayer);
    for (p = ActiveMob; p < LastActiveMob; p++) {
        obj = &critdata[*p];
        if (OBJ_MAJOR(obj) != MAJOR_CREATURE)
            continue;
        xv = OBJ_HOMEX(obj) - px;
        yv = OBJ_HOMEY(obj) - py;
        if (abs(xv) < dist && abs(yv) < dist
            && skill_check(skill, 0xF - Creature[OBJ_INMAJOR(obj)].noise) > 0)
            counts[mpos(xv, yv)]++;
    }
    for (max = i = 0; i < 8; i++)
        if (counts[i] > max)
            max = counts[i];
    if (max == 0) {
        game_sprint(0x3E);  /* 'You detect no monster activity.' */
        return;
    }
    if (max >= 4)
        best = 3;
    else
        best = max;
    for (i = 0; i < 8; i++) {
        if (counts[i] == max) {
            print_monster(i, counts[i]);
            max = best;
            best = i;
            break;
        }
    }
    i = rand() & 7;
    for (tries = 0; tries < 8; tries++, i++) {
        if ((i & 7) != best && counts[i & 7] > max) {
            print_monster(i & 7, counts[i & 7]);
            return;
        }
    }
}

/* The player has clicked an object for an object spell (ObjectActorArg, class 11 minor):
   3 Remove Trap, 4 Name Enchantment (look with identify), 5 Open (checkLock with skill
   -45). */
void far obj_spells(struct Object far *target, int how, unsigned char b)
{
    switch (ObjectActorArg) {
    case 3:
        if (DetectedTrap(target, 0x2D))
            RemoveTrap(target, 0x2D);
        break;
    case 4:
        LookAt(target, 3);
        if (OBJ_MAJOR(target) != MAJOR_RECT && OBJ_MAJOR(target) != MAJOR_CREATURE
            && ComObjData[OBJ_ITEM(target)].render != 2)
            SET_HEADING(target, 7);
        break;
    case 5:
        if (checkLock(ThePlayer, target, -45) == 3)
            game_sprint(0x10E);  /* 'The spell unlocks the lock.' */
        else
            game_sprint(0x10F);  /* 'The spell has no discernable effect.' */
        break;
    }
    unforce_mouse_cursor(3);
    GameInputMode = 0;
    mouse_release(1);
}

/* Class 11 spells (SPELLC_XT), the player's only. stab is the active spell flag passed on
   to set_curmagic. 0 speed, 1 Detect Monster, 2..5 the object spells (the targeting
   cursor, then obj_spells), 6 cure poison, 7 Roaming Sight (detaches the camera,
   GameInputMode + 8), 8 telekinesis, 9 tremor, 10 gate travel to the level of the
   moonstone, 11 freeze time, 12 Armageddon (destroys the player's inventory and every
   object on the level, empties the rune bag and shelf). */
void far xt_spells(struct Object far *caster, char stab, char sub)
{
    if (caster == ThePlayer)
        switch (sub) {
        case 0:                         /* speed */
            set_curmagic(SPELLC_XT, 2, stab);
            break;
        case 1:                         /* detect monster */
            mdetect(10, 0x2D);
            break;
        case 2:
        case 3:
        case 4:
        case 5:                         /* the object spells */
            GameInputMode = 2;
            ObjectActorArg = sub;
            ObjectActing = ThePlayer;
            ObjectActor = (ActorFn)obj_spells;
            force_mouse_cursor(ICON_CURSORS + 10);
            break;
        case 6:                         /* cure poison */
            player->poison = 0;
            break;
        case 7:                         /* roaming sight */
            set_curmagic(SPELLC_XT, 1, stab);
            home_cam(0);
            attach_eye(-1);
            GameInputMode += 8;
            break;
        case 8:                         /* telekinesis */
            set_curmagic(SPELLC_XT, 3, stab);
            break;
        case 9:                         /* tremor */
            gronk_area(caster, rollem(8, 3), tremor_area, AREA_RANDOM, 5, 3);
            set_effect(0x40, 0x28);
            play_effect_on_mobile(0x12, caster, 0);
            break;
        case 10:                        /* gate travel */
            if (player->moonstone) {
                npp_func = do_mstone;
                do_teleport(ThePlayer, 0x3F, 0x3F, player->moonstone);
                player_setup(0, 0, 0);
                editchng(0x7FFE);
            } else
                game_sprint(0x111);  /* 'The moonstone is not available.' */
            break;
        case 11:                        /* freeze time */
            set_curmagic(SPELLC_XT, 0, stab);
            break;
        case 12:                        /* armageddon */
            FreePlayerInv(&ThePlayer->ol.link);
            clearobj(0);
            clear_runes();
            clear_shelf();
            player->armageddon = 1;
            player->tree = 0;
            PlayerDat.rec.weight = 0;
            FixPlayerEquips();
            pretty_panelagain();
            break;
        }
}

static unsigned char sq_dice[2] = { 10, 6 };
static unsigned char sq_sides[2] = { 6, 5 };
static unsigned char sq_type[2] = { DMG_MAGIC | DMG_FIRE, DMG_MAGIC };

/* The 53 spells: cls (class << 3), the three runes packed five bits each (0x18 none)
   and the minor. */
/* match: far, so its own segment (the listing's seg064). The far data segments follow
   the link order of the files that define them, and seg064 lies between seg019's and
   seg039's, so it was defined by a resident file in between: this one (RUNES.C, the other
   user, is an overlay). */
struct Spell far spells[NUM_SPELLS] = {
    { 0x00, 0x2178, 0x83 }, { 0x10, 0x0512, 0x02 }, { 0x29, 0x3938, 0x01 },
    { 0x40, 0x2197, 0x01 }, { 0x18, 0x48F8, 0x02 }, { 0x08, 0x51F8, 0x01 },
    { 0x18, 0x0258, 0x01 }, { 0x08, 0x446F, 0x02 }, { 0x20, 0x202C, 0x02 },
    { 0x58, 0x5998, 0x01 }, { 0x38, 0x4058, 0x01 }, { 0x40, 0x2138, 0x03 },
    { 0x58, 0x466F, 0x00 }, { 0x18, 0x064B, 0x03 }, { 0x00, 0x4178, 0x85 },
    { 0x29, 0x38D8, 0x02 }, { 0x5A, 0x4938, 0x02 }, { 0x10, 0x2258, 0x43 },
    { 0x08, 0x5DF8, 0x44 }, { 0x20, 0x2198, 0x04 }, { 0x08, 0x1DF8, 0x03 },
    { 0x38, 0x3598, 0x04 }, { 0x18, 0x48B8, 0x46 }, { 0x5A, 0x0138, 0x03 },
    { 0x29, 0x3CB8, 0x03 }, { 0x38, 0x004C, 0x02 }, { 0x5A, 0x3AD7, 0x04 },
    { 0x18, 0x1A4F, 0x05 }, { 0x5A, 0x12F8, 0x05 }, { 0x58, 0x01B8, 0x06 },
    { 0x20, 0x550C, 0x0F }, { 0x30, 0x55C6, 0x42 }, { 0x58, 0x562F, 0x0A },
    { 0x38, 0x008F, 0x05 }, { 0x00, 0x550B, 0x86 }, { 0x5A, 0x39F7, 0x08 },
    { 0x08, 0x54EF, 0x05 }, { 0x38, 0x2191, 0x03 }, { 0x40, 0x2998, 0x04 },
    { 0x18, 0x564B, 0x44 }, { 0x30, 0x5416, 0x03 }, { 0x30, 0x3810, 0x81 },
    { 0x10, 0x22B2, 0x45 }, { 0x58, 0x55F7, 0x09 }, { 0x58, 0x39F6, 0x07 },
    { 0x30, 0x14F8, 0x44 }, { 0x58, 0x0278, 0x0B }, { 0x58, 0x5542, 0x0C },
    { 0x30, 0x6318, 0x05 }, { 0x28, 0x6318, 0x04 }, { 0x58, 0x6318, 0x0D },
    { 0x50, 0x6318, 0x03 }, { 0x50, 0x6318, 0x09 }
};

/* Damages every object on a square: kind 1 is 10d6 of type 11 (magic and fire), kind 2
   6d5 of type 3 (magic). */
void far damage_square(int x, int y, unsigned char kind, unsigned char src)
{
    struct Object far *obj;
    struct Object far *next;

    if (kind-- == 0)
        return;
    obj = Obj_PtrTMem(&Map_GetAddr(x, y)->objects);
    while (obj) {
        next = Obj_PtrTMem(&obj->qn.link);
        damage_item(obj, Obj_IntTMem(src), x, y, rollem(sq_dice[kind], sq_sides[kind]),
                    sq_type[kind]);
        obj = next;
    }
}
