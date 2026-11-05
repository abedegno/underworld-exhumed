/* target: ovr135 */
/* opts: -mm -1 -G -O -Y -d */
/* The player's timed updates: active spells running down, light sources burning out,
   poison, hunger, drunkenness, drowning, and the hourly schedule. The whole of DOS
   overlay ovr135, in original order. Function and global names are the originals from
   the FM Towns symbol table.

   The clock: INTERACT.C calls duration_check once every 20 steps of game_clock >> 8.
   plyregen[1] counts the calls, and the work is spread over that count:
     every call     active spells lose a step, lights burn, mushrooms wear off,
                    regeneration, drowning, Killorn's countdown, the sleep counter;
     every 3rd      poison damage, and a mana regeneration roll on the mana skill;
     every 30th     hunger, sobering up, a 1 in 4 chance of wandering monsters, the
                    critters' yearly_checkup, fatigue and two neighbouring bytes counted
                    up, and an HP regeneration roll on strength (inferred: attr[0]);
     every 60th     the time of day, X clock 0, moves on one of its 72 steps and the
                    schedules in SCD.ARK block 0 are run up to the new time.
   Entry points: duration_check (INTERACT.C), set_curmagic (the spells, to start an active
   spell), DegradeLights (also called with a larger amount when time passes quickly,
   probably by sleeping), sink_sink_sink, dispel_spell.
   Data: none of its own; plyregen and the light tables are elsewhere.
   Name: descriptive (the player's timed updates: duration_check, DegradeLights). */

#include <stdlib.h>
#include "combat.h"
#include "critter.h"
#include "event.h"
#include "gfx.h"
#include "inv.h"
#include "object.h"
#include "player.h"
#include "sys.h"
#include "ui.h"

/* An active spell word, struct Player's spells[]: the class in bits 0 to 3, the
   subclass in bits 4 to 7, the duration left (in duration checks) in the high byte. */
#define SPELL_CLASS(s)  ((s) & 0x0F)
#define SPELL_SUB(s)    (((s) & 0xF0) >> 4)
#define SPELL_STAB(s)   ((s) >> 8)

/* Ends active spell *i. Levitate and fly (class 1, motion, subclasses 3 and 5, per the
   Guide's table of motion spells) do not end at once: they become slow fall (subclass
   2) for one more check, so the player is not dropped. Class 11 subclass 1 detaches the
   camera, so ending it reattaches the eye and leaves that input mode. Any other motion
   spell sets fiz_update so the physics picks up the change. The last spell is moved
   into the freed slot and *i is stepped back so the caller's loop sees it. */
char far dispel_spell(int16 *i)
{
    if (SPELL_CLASS(player->spells[*i]) == SPELLC_MOTION
        && (SPELL_SUB(player->spells[*i]) == 3 || SPELL_SUB(player->spells[*i]) == 5))
    {
        player->spells[*i] = (player->spells[*i] >> 8 << 8) + 0x21;
        player->spells[*i] = (player->spells[*i] & 0xFF) + 0x100;
    }
    else
    {
        if (SPELL_CLASS(player->spells[*i]) == SPELLC_XT && SPELL_SUB(player->spells[*i]) == 1)
        {
            attach_eye(1);
            GameInputMode -= 8;
        }
        if (SPELL_CLASS(player->spells[*i]) == SPELLC_MOTION)
            fiz_update = 1;
        player->active_spells--;
        if ((*i)-- < player->active_spells)
            player->spells[*i + 1] = player->spells[player->active_spells];
    }
    return 1;
}

void far duration_check(void)
{
    int16 i;
    char changed;
    int dmg;
    int stab;

    changed = 0;
    plyregen[1]++;
    for (i = 0; i < player->active_spells; i++)
    {
        stab = SPELL_STAB(player->spells[i]);
        if (stab == 1)
            changed = dispel_spell(&i);
        else
            player->spells[i] = (player->spells[i] & 0xFF) + ((stab - 1) << 8);
    }
    if (DegradeLights(1, plyregen[1]))
        changed = 1;
    if (player->shrooms)
    {
        if (--player->shrooms == 0)
            changed = 1;
    }
    if (changed)
    {
        FixPlayerEquips();
        editchng(2);
    }
    if (plyregen[0] != 0)
    {
        if (plyregen[0] & 1)
            restore_hp(ThePlayer, -1);
        if (plyregen[0] & 2)
            restore_mana(ThePlayer, -1);
    }
    if (player->swim_count > 0x50)
        sink_sink_sink();
    /* Quest 50 (the keep is going to crash) set and quest 54 not yet: Killorn's
       countdown, quest_bytes[6], runs down. At zero Killorn crashes and the player takes
       0xFF damage. do_sfx(4, 0x2C) is played every check meanwhile. */
    if ((int)((player->quests[12] & 4) >> 2) && !(int)((player->quests[13] & 4) >> 2))
    {
        do_sfx(4, 0x2C);
        if (!TimeStop && player->quest_bytes[6]-- == 0)
        {
            Killorn_just_crashed(0);
            damage_item(ThePlayer, 0L, 0, 0, 0xFF, 0);
        }
    }
    if (player->sleepbits)
    {
        player->sleepbits--;
        if (!player->sleepbits && player->in_void)
            punt_void();
    }
    if (plyregen[1] % 3 == 0)
    {
        if (player->poison)
        {
            dmg = player->poison--;
            damage_item(ThePlayer, 0L, 0, 0, dmg, 0x10);
        }
        if ((dmg = skill_check(player->skills[SKILL_MANA], 10)) > 0)
            restore_mana(ThePlayer, -dmg);
    }
    if (plyregen[1] % 30 == 0)
    {
        player_eat(-3 - (rand() & 3));
        if (player->drunk)
            player->drunk--;
        if ((rand() & 3) == 0)
            DoWanderingMonsters(1);
        yearly_checkup();
        for (i = 0; i < 3; i++)
            if ((&player->fatigue)[i] < 0xFF)
                (&player->fatigue)[i]++;
        if (skill_check(playerdat->attr[0], 15) > 0)
            restore_hp(ThePlayer, -1);
    }
    if (plyregen[1] % 60 == 0)
    {
        /* The hour: X clock 0 steps on, and the schedules in SCD.ARK block 0 are
           loaded into the workspace, run up to the new time and saved back. */
        player->xclock[XC_TIME]++;
        player->xclock[XC_TIME] = player->xclock[XC_TIME] % DAY_STEPS;
        if (get_workspace())
        {
            Sched_SetBuf(0, set_workspace());
            Sched_Load(0);
            Sched_WrapTime(player->xclock[XC_TIME], DAY_STEPS, 1);
            Sched_Save(0);
            release_workspace();
        }
        plyregen[1] = 0;
    }
}

/* Burns the lit light sources in the four light slots (ValidLightSlots). A lit light
   (class CLASS_LIGHT, types 4 to 7) with burn rate r loses a point of quality when
   counter is a multiple of r, plus amount / r more when amount > 1. At quality 1 it
   goes out: its type drops by 4 to the unlit version. Nothing burns while time is
   stopped. Returns 1 if a light went out. */
char far DegradeLights(int amount, unsigned char counter)
{
    int light;
    struct Object far *obj;
    char changed;
    register int i;
    register int burn;

    changed = 0;
    if (TimeStop == 0)
    {
        for (i = 0; i < 4; i++)
        {
            if ((obj = AskInventory(ValidLightSlots[i])) == 0)
                continue;
            light = obj->id & ID_INCLASS;
            if (OBJ_CLASS(obj) != CLASS_LIGHT || light < 4 || light >= 8)
                continue;
            if ((light = Lights[light].duration) == 0)
                continue;
            burn = 0;
            if (counter % light == 0)
                burn = 1;
            if (amount > 1)
                burn += amount / light;
            if (burn == 0)
                continue;
            if ((uint16)(OBJ_QUALITY(obj) - 1) > burn)
                OBJ_QUALITY(obj) = OBJ_QUALITY(obj) - burn;
            else
            {
                OBJ_QUALITY(obj) = 1;
                SET_INCLASS(obj, OBJ_INCLASS(obj) - 4);
                RedisplayInvSlot(ValidLightSlots[i]);
                changed = 1;
            }
        }
    }
    return changed;
}

/* Drowning, run while swim_count is above 0x50. The load is weight * 32 / max_weight.
   A failed swimming check against the load adds rollem(3 - result, 4) to swim_count (up
   to 0x8C). Above 0x78 the player also takes rollem(2, hurt + 2) damage, with hurt =
   2 minus a second swimming check, and the screen flashes (fill_FB(0x50)). */
void far sink_sink_sink(void)
{
    unsigned char load;
    register int skill;
    register int hurt;

    load = 0;
    if (player->max_weight != 0)
        load += (uint16)(player->weight << 5) / player->max_weight;   /* 16 bits, as Turbo C's */
    if ((skill = skill_check(player->skills[SKILL_SWIMMING], load)) < 1 && player->swim_count < 0x8C)
        player->swim_count += rollem(3 - skill, 4);
    if (player->swim_count > 0x78)
    {
        hurt = 2 - skill_check(player->skills[SKILL_SWIMMING], load);
        if (hurt)
        {
            fill_FB(0x50);
            editchng(2);
            damage_item(ThePlayer, 0L, 0, 0, rollem(2, hurt + 2), 0);
        }
    }
}

/* Starts an active spell of class cls and subclass sub on the player. At most three
   are active; returns 0 if all three slots are taken. The duration, in duration checks,
   comes from stability: 1 gives 1, 0 gives 2d3, 0x40 gives 2d8 + 6, 0x80 gives 3d20 + 24
   (rollem(dice, sides)). */
char far set_curmagic(unsigned char cls, unsigned char sub, unsigned char stability)
{
    unsigned char stab;

    if (player->active_spells == 3)
        return 0;
    player->spells[player->active_spells] =
        (player->spells[player->active_spells] >> 8 << 8) + cls + (sub << 4);
    switch (stability)
    {
    case 1:
        stab = 1;
        break;
    case 0:
        stab = rollem(2, 3);
        break;
    case 0x40:
        stab = rollem(2, 8) + 6;
        break;
    case 0x80:
        stab = rollem(3, 20) + 24;
        break;
    }
    player->spells[player->active_spells] =
        (player->spells[player->active_spells] & 0xFF) + (stab << 8);
    player->active_spells++;
    FixPlayerEquips();
    return 1;
}
