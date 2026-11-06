/* target: seg028_2985 */
/* opts: -mm -1 -G -O -Y -d */
/* The player's timed updates: active spells running down, light sources burning out,
   poison, hunger, drunkenness and drowning. The whole of DOS resident segment
   seg028_2985, in original order. Seeded from UW2Decomp's src/game/PLAYTIME.C (UW2's
   overlay ovr135). Function names are UW2's (the FM Towns originals), the routines being
   the same; UW1 has no symbols of its own.

   The clock: duration_check runs once per tick of the player's slow clock (UW2's caller is
   INTERACT.C). plyregen[1] counts the calls, and the work is spread over that count:
     every call     active spells lose a step, lights burn, mushrooms wear off,
                    regeneration, drowning;
     every 3rd      poison damage, and a mana regeneration roll on the mana skill;
     every 24th     hunger, sobering up, a 1 in 4 chance of wandering monsters, the
                    critters' yearly_checkup, fatigue and two neighbouring bytes counted
                    up, an HP regeneration roll on strength (inferred: attr[ATTR_STR]), and the
                    count starts again.
   Entry points: duration_check, set_curmagic (the spells, to start an active spell),
   DegradeLights (also called with a larger amount when time passes quickly, probably by
   sleeping), sink_sink_sink, dispel_spell.

   UW1 against UW2: no Killorn countdown, sleep counter or hourly schedule (UW2's every 60th
   call); the slow work is every 24th call (UW2 30th); lights burn while time is stopped,
   go out at quality 0 and ValidLightSlots is signed; drowning flashes colour 0xC6.
   struct Player is UW1's (player.h).
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

/* Declared in each file that uses it, its own way (no header). */
extern struct Spell far spells[53];
unsigned char far player_eat(int nutrition);


/* Ends active spell *i. Levitate and fly (class 1, motion, subclasses 3 and 5, per the
   Guide's table of motion spells) do not end at once: they become slow fall (subclass
   2) for one more check, so the player is not dropped. Class 11 subclass 1 detaches the
   camera, so ending it reattaches the eye and leaves that input mode. Any other motion
   spell sets fiz_update so the physics picks up the change. The last spell is moved
   into the freed slot and *i is stepped back so the caller's loop sees it. */
char far dispel_spell(int16 *i)
{
    if (ASPELL_CLASS(player->spells[*i]) == SPELLC_MOTION
        && (ASPELL_SUB(player->spells[*i]) == 3 || ASPELL_SUB(player->spells[*i]) == 5))
    {
        player->spells[*i] = (player->spells[*i] >> 8 << 8) + 0x21;
        player->spells[*i] = (player->spells[*i] & 0xFF) + 0x100;
    }
    else
    {
        if (ASPELL_CLASS(player->spells[*i]) == SPELLC_XT && ASPELL_SUB(player->spells[*i]) == 1)
        {
            attach_eye(1);
            GameInputMode -= 8;
        }
        if (ASPELL_CLASS(player->spells[*i]) == SPELLC_MOTION)
            fiz_update = 1;
        player->active_spells--;
        if ((*i)-- < player->active_spells)
            player->spells[*i + 1] = player->spells[player->active_spells];
    }
    return 1;
}

/* One tick of the player's slow clock (see the top of the file): counts plyregen[1] and
   does that call's share of the work. An active spell whose duration is down to 1 ends
   (dispel_spell), any other loses a step; lights burn; mushrooms wear off; a change
   refits the equipment and redraws. Then regeneration, drowning, and on every 3rd and
   24th call the slower work. */
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
        stab = ASPELL_STAB(player->spells[i]);
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
    if (plyregen[1] % 3 == 0)
    {
        if (player->poison)
        {
            dmg = player->poison--;
            damage_item(ThePlayer, 0L, 0, 0, dmg, DMG_POISON);
        }
        if ((dmg = skill_check(player->skills[SKILL_MANA], 10)) > 0)
            restore_mana(ThePlayer, -dmg);
    }
    if (plyregen[1] % 24 == 0)
    {
        player_eat(-3 - (rand() & 3));
        if (player->drunk)
            player->drunk--;
        if ((rand() & 3) == 0)
            DoWanderingMonsters(1);
        yearly_checkup();
        for (i = 0; i < 3; i++)
            if ((&player->fatigue)[i] < 0xFF)  /* match: fatigue, food_heal and b3C through one field's address */
                (&player->fatigue)[i]++;
        if (skill_check(playerdat->attr[ATTR_STR], 15) > 0)
            restore_hp(ThePlayer, -1);
        plyregen[1] = 0;
    }
}

/* Burns the lit light sources in the four light slots (ValidLightSlots). A lit light
   (class CLASS_LIGHT, types 4 to 7) with burn rate r loses a point of quality when
   counter is a multiple of r, plus amount / r more when amount > 1. When it would burn
   down to nothing it goes out at quality 0: its type drops by 4 to the unlit version.
   Returns 1 if a light went out. */
char far DegradeLights(int amount, unsigned char counter)
{
    int light;
    struct Object far *obj;
    char changed;
    register int i;
    register int burn;

    changed = 0;
    for (i = 0; i < 4; i++)
    {
        if ((obj = AskInventory(ValidLightSlots[i])) == 0)
            continue;
        light = OBJ_INCLASS(obj);
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
        if (OBJ_QUALITY(obj) > burn)
            OBJ_QUALITY(obj) = OBJ_QUALITY(obj) - burn;
        else
        {
            OBJ_QUALITY(obj) = 0;
            SET_INCLASS(obj, OBJ_INCLASS(obj) - 4);
            RedisplayInvSlot(ValidLightSlots[i]);
            changed = 1;
        }
    }
    return changed;
}

/* Drowning, run while swim_count is above 0x50. The load is weight * 32 / max_weight.
   A failed swimming check against the load adds rollem(3 - result, 4) to swim_count (up
   to 0x8C). Above 0x78 the player also takes rollem(2, hurt + 2) damage, with hurt =
   2 minus a second swimming check, and the screen flashes (fill_FB(0xC6)). */
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
            fill_FB(0xC6);
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
